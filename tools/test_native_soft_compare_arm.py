#!/usr/bin/env python3
"""Audit GP soft-compare and the exact enrolled int-to-float ASM against ASMP0.

No MMIO is mapped. A separate ARM9 ROM oracle supplies the original integer
register ABI. Enabled FP traps stop at the real dispatcher boundary; there is
no invented dispatcher result and no claimed successful caller continuation.
"""
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import zlib

from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE,UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_PC
import test_native_matrix_submit_arm as base

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/native-soft-compare'
sys.path.insert(0,str(ROOT/'build/game-data/python'))
import ndspy.rom,ndspy.code

COMPARE=0x01ff98f4
CONVERT=0x01ffa4bc
STATE=0x020aa3f4
GETTER=0x0207322c
DISPATCH=0x0206dd30
UNARY=0x0206ddcc
FLAGS=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
       '-O2','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-Wa,--noexecstack']

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def build():
    corpus=json.loads((ROOT/'build/native-core/report.json').read_text())
    owner=next(e for e in corpus['entries'] if e['source']=='src/func_01ffa4bc.c')
    assert owner['compiled'] and owner['adaptations']==['mwcc-to-gas-soft-float-r0-bits']
    assert owner['sha256_original']==sha(ROOT/'upstream/src/func_01ffa4bc.c')
    objects=[]
    for index,path in enumerate(['source/game_port/native_soft_compare.cpp',
                                  'tests/native_soft_compare_arm.cpp','upstream/src/func_0207322c.c']):
        obj=OUT/f'fixture-{index}.o'
        c=path.endswith('.c')
        subprocess.run([base.TOOL+('gcc' if c else 'g++'),*FLAGS,
                        '-std=gnu11' if c else '-std=gnu++17','-c',path,'-o',str(obj)],cwd=ROOT,check=True)
        objects.append(str(obj))
    objects.append(str(ROOT/owner['object']))
    elf=OUT/'soft-math.elf'
    subprocess.run([base.TOOL+'g++',*FLAGS,'-nostartfiles',
        '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,func_01ff98f4,-u,func_01ffa4bc',
        *objects,'-o',str(elf)],cwd=ROOT,check=True)
    nm=subprocess.check_output([base.TOOL+'nm','-g','--defined-only',str(elf)],text=True)
    names={v[2]:int(v[0],16) for line in nm.splitlines() if len(v:=line.split())==3}
    disasm=subprocess.check_output([base.TOOL+'objdump','-d',str(elf)],text=True)
    (OUT/'native.disasm.txt').write_text(disasm)
    owner={**owner,'object_sha256':sha(ROOT/owner['object'])}
    # This minimal fixture has no generic fail-fast provider. Load its exact
    # ELF PT_LOAD regions without the matrix fixture's fatal-symbol hook.
    native=base.Cpu()
    raw=elf.read_bytes()
    phoff=struct.unpack_from('<I',raw,28)[0]
    phsize,phnum=struct.unpack_from('<HH',raw,42)
    for i in range(phnum):
        kind,offset,va,_,filesz,memsz,_,_=struct.unpack_from('<8I',raw,phoff+i*phsize)
        if kind==1 and memsz:
            native.map(va,memsz)
            native.write(va,raw[offset:offset+filesz])
    return native,names,owner

class Observe:
    def __init__(self,cpu,state,getter,dispatch,unary):
        self.machine=cpu;self.state=state;self.getter=getter
        self.dispatch=dispatch;self.unary=unary;self.calls=[];self.reads=[];self.writes=[]
        self.invalid=[];self.getters=0
        cpu.cpu.hook_add(UC_HOOK_CODE,self.code)
        cpu.cpu.hook_add(UC_HOOK_MEM_READ,self.read)
        cpu.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write)
        cpu.cpu.hook_add(UC_HOOK_MEM_INVALID,self.bad)
    def code(self,uc,address,size,user):
        if address==self.getter:self.getters+=1
        if address in (self.dispatch,self.unary):
            event={'entry':'binary' if address==self.dispatch else 'unary',
                   'r0':uc.reg_read(UC_ARM_REG_R0),'r1':uc.reg_read(UC_ARM_REG_R1),
                   'r2':uc.reg_read(UC_ARM_REG_R2),
                   'status':struct.unpack('<I',self.machine.read(self.state,4))[0]}
            self.calls.append(event);self.machine.failed=event;uc.emu_stop()
    def read(self,uc,access,address,size,value,user):
        if self.state<=address<self.state+4:self.reads.append((address-self.state,size))
    def write(self,uc,access,address,size,value,user):
        if self.state-16<=address<self.state+20:self.writes.append((address-self.state,size,value&0xffffffff))
    def bad(self,uc,access,address,size,value,user):
        self.invalid.append({'address':address,'size':size,'pc':uc.reg_read(UC_ARM_REG_PC)})
        return False
    def invoke(self,entry,args,state,trap):
        self.calls=[];self.reads=[];self.writes=[];self.getters=0
        self.machine.write(self.state-16,bytes([0xa5])*36)
        self.machine.write(self.state,struct.pack('<I',state))
        result=self.machine.invoke(entry,args,fail=trap,instructions=1000)
        got=struct.unpack('<I',self.machine.read(self.state,4))[0]
        assert self.machine.read(self.state-16,16)==bytes([0xa5])*16
        assert self.machine.read(self.state+4,16)==bytes([0xa5])*16
        assert not self.invalid
        return {'returned':not trap,'result':None if trap else result,'status':got,
                'edge':self.calls[0] if trap else None,'getters':self.getters,
                'status_reads':len(self.reads),'status_writes':len(self.writes)}

def original():
    raw=(ROOT/'build/roms/europe.nds').read_bytes()
    assert zlib.crc32(raw)==0x29715dec
    rom=ndspy.rom.NintendoDSRom(raw)
    assert rom.idCode==b'ASMP' and rom.version==0
    original=base.Cpu()
    code=ndspy.code.MainCodeFile(rom.arm9,rom.arm9RamAddress)
    regions=[]
    for section in code.sections:
        if not section.data:continue
        original.map(section.ramAddress,len(section.data))
        original.write(section.ramAddress,section.data)
        regions.append({'address':section.ramAddress,'bytes':len(section.data),
                        'sha256':hashlib.sha256(section.data).hexdigest()})
    original.map(STATE-16,36,3)
    return original,sha(ROOT/'build/roms/europe.nds'),regions

def float32(bits):return struct.unpack('<f',struct.pack('<I',bits))[0]
def signed(bits):return bits-(1<<32) if bits&0x80000000 else bits
def isnan(bits):return (bits&0x7fffffff)>0x7f800000

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    native,names,owner=build()
    rom,rom_sha,regions=original()
    no=Observe(native,names['data_020aa3f4'],names['func_0207322c'],names['func_0206dd30'],names['func_0206ddcc'])
    ro=Observe(rom,STATE,GETTER,DISPATCH,UNARY)
    bits=[0,0x80000000,1,0x80000001,0x7fffff,0x807fffff,0x800000,0x80800000,
          0x3f000000,0xbf000000,0x3f800000,0xbf800000,0x3f800001,0xbf800001,
          0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7f800001,0xff800001,
          0x7fbfffff,0xffbfffff,0x7fc00000,0xffc00000,0x7fffffff,0xffffffff]
    statuses=[0,1,0x100,0x101,0x400000,0x800000,0xc00000,0xffffffff]
    rng=random.Random(0x98f4)
    cases=[(a,b,s) for a,b in itertools.product(bits,bits) for s in statuses]
    cases.extend((rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(32)) for _ in range(8192))
    compare_traps=compare_nan=0;digest=hashlib.sha256();samples=[]
    for a,b,s in cases:
        nan=isnan(a) or isnan(b);trap=nan and bool(s&0x100)
        expected_status=s if not nan or trap else s|1
        nr=no.invoke(names['func_01ff98f4'],(a,b),s,trap)
        rr=ro.invoke(COMPARE,(a,b),s,trap)
        assert nr==rr,(hex(a),hex(b),hex(s),nr,rr)
        assert nr['status']==expected_status
        if trap:
            assert nr['edge']=={'entry':'binary','r0':a,'r1':b,'r2':0x04000049,'status':s}
            compare_traps+=1
        else:
            assert nr['result']==int(float32(a)<float32(b))
        if not nan:assert nr['getters']==0 and nr['status_reads']==0 and nr['status_writes']==0
        else:compare_nan+=1
        digest.update(struct.pack('<3I',a,b,s));digest.update(json.dumps(nr,sort_keys=True).encode())
        if len(samples)<24 and (trap or nan or (a&0x7fffffff)==0):samples.append({'a':a,'b':b,'state':s,'result':nr})
    ints=[0,1,-1,127,-127,128,-128,255,-255,256,-256,65535,-65535,
          0x7fffffff,-0x80000000,0x1000000,-0x1000000,0x1000001,-0x1000001,
          0x1000003,-0x1000003,0x1ffffff,-0x1ffffff]
    cv_status=[0,0x10,0x1000,0x1010,0x400000,0x800000,0xc00000,
               0x401000,0x801000,0xc01000,0xffffffff]
    conversion=[(i&0xffffffff,s) for i,s in itertools.product(ints,cv_status)]
    conversion.extend((rng.getrandbits(32),rng.getrandbits(32)) for _ in range(8192))
    convert_traps=convert_inexact=0
    for a,s in conversion:
        # Determine exactness independently from Python's binary32 conversion.
        nearest_bits=struct.unpack('<I',struct.pack('<f',signed(a)))[0]
        inexact=float32(nearest_bits)!=signed(a)
        trap=inexact and bool(s&0x1000)
        nr=no.invoke(names['func_01ffa4bc'],(a,),s,trap)
        rr=ro.invoke(CONVERT,(a,),s,trap)
        assert nr==rr,(hex(a),hex(s),nr,rr)
        assert nr['status']==(s|0x10 if inexact and not trap else s)
        if not inexact:assert nr['getters']==0 and nr['status_reads']==0 and nr['status_writes']==0
        if not trap and not s&0xc00000:assert nr['result']==nearest_bits
        if trap:
            assert nr['edge']['entry']=='unary' and nr['edge']['r1']==0x40000208
            convert_traps+=1
        if inexact:convert_inexact+=1
        digest.update(struct.pack('<2I',a,s));digest.update(json.dumps(nr,sort_keys=True).encode())
    result={'status':'pass','scope':'ARM11 GP arithmetic vs actual ASMP0 ARM9 instructions; enabled dispatcher continuation is not executed',
            'compare_cases':len(cases),'compare_nan_cases':compare_nan,'compare_enabled_invalid_edges':compare_traps,
            'convert_cases':len(conversion),'convert_inexact_cases':convert_inexact,'convert_enabled_inexact_edges':convert_traps,
            'all_results_fp_status_and_edge_arguments_equal':True,'finite_compare_independent_python_ieee_results':True,
            'conversion_nearest_independent_python_ieee_results':True,'rounding_modes_checked':[0,0x400000,0x800000,0xc00000],
            'callee_saved_and_sp_preserved_for_returning_cases':True,'status_canaries_preserved':True,'invalid_memory_accesses':[],
            'mmio_mapped_native':False,'mmio_mapped_oracle':False,'vfp_abi_used':False,
            'enabled_traps':'Observed and stopped before func0206dd30/func0206ddcc; no fabricated result or caller success',
            'original_convert_owner':owner,'rom_sha256':rom_sha,'original_sections':regions,'result_digest':digest.hexdigest(),
            'samples':samples,'elf_sha256':sha(OUT/'soft-math.elf'),
            'source_sha256':{p:sha(ROOT/p) for p in ['source/game_port/native_soft_compare.cpp','tests/native_soft_compare_arm.cpp',
                'tools/test_native_soft_compare_arm.py','upstream/src/func_0207322c.c','upstream/src/func_01ffa4bc.c']}}
    (OUT/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('samples','original_sections')},indent=2))

if __name__=='__main__':main()
