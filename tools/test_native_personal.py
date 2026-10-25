#!/usr/bin/env python3
"""Synthetic CFGU transaction checks and original personal-export ARM ABI proof.

Neither host nor ARM11 fixture reads real personal settings. Native code never
maps DS firmware RAM. Only the separate ROM oracle gets synthetic firmware.
This tests a bounded SDK leaf and the menu's original language-selection tail,
not a running menu, an emulator application, console performance or gameplay.
"""
import hashlib
import json
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import zlib

from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-personal'
OUT.mkdir(exist_ok=True)
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
CTRU = '/opt/devkitpro/libctru/include'
SOURCES = ('source/game_port/native_personal.cpp', 'tests/test_native_personal.cpp')
compiler = shutil.which('clang++') or shutil.which('c++')
host = OUT / 'test-native-personal'
subprocess.run([compiler, '-std=c++17', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra',
    '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
    '-DSM64DS_NATIVE_PERSONAL_HOST_TEST', '-I', str(ROOT/'include'), '-I', CTRU,
    *(str(ROOT/path) for path in SOURCES), '-o', str(host)], check=True)
host_cases = 0
for seed, scenario in [(value, 0) for value in range(66)] + [(10, value) for value in range(1,14)]:
    result = json.loads(subprocess.check_output([str(host), str(seed), str(scenario)], text=True))
    assert result['passed']
    host_cases += 1

flags = ['-std=c++17', '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard',
    '-mtp=soft', '-Os', '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
    '-fdata-sections', '-Wa,--noexecstack', '-Werror=trampolines',
    '-I', str(ROOT/'include'), '-I', CTRU]
objects=[]
for source in SOURCES:
    obj=OUT/(Path(source).stem+'.o')
    subprocess.run([TOOL+'g++', *flags, '-c', str(ROOT/source), '-o', str(obj)], check=True)
    objects.append(obj)
elf_path = OUT/'native-personal.elf'
subprocess.run([TOOL+'g++', *flags, '-nostartfiles',
    '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,func_02059eb0',
    '-Wl,-u,test_personal_initialize', '-Wl,-u,test_personal_copy', '-Wl,-u,test_personal_stat',
    *map(str, objects), '-o', str(elf_path)], check=True)
symbols = {parts[2]: int(parts[0],16) for line in subprocess.check_output(
    [TOOL+'nm', '--defined-only', str(elf_path)], text=True).splitlines()
    if len(parts:=line.split()) == 3}
elf = elf_path.read_bytes()
phoff=struct.unpack_from('<I',elf,28)[0]
phsize,phnum=struct.unpack_from('<HH',elf,42)
segments=[]
for index in range(phnum):
    kind,offset,address,_,filesz,memsz,perms,_=struct.unpack_from('<8I',elf,phoff+index*phsize)
    if kind==1 and memsz: segments.append((address,memsz,elf[offset:offset+filesz],perms))
sys.path.insert(0,str(ROOT/'build/game-data/python'))
import ndspy.rom
import ndspy.codeCompression
raw=(ROOT/'build/roms/europe.nds').read_bytes()
assert zlib.crc32(raw)==0x29715dec, 'Requires the verified ASMP0 source ROM'
rom=ndspy.rom.NintendoDSRom(raw)
arm9=ndspy.codeCompression.decompress(rom.arm9)
ov7=rom.loadArm9Overlays([7])[7]
ORIGINAL=0x02059eb0
FIRMWARE=0x027ffc80
BUFFER,STACK,STOP=0x500000,0x600000,0x700000
ARGUMENTS=(UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)
SAVED=(UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
       UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11)


class Cpu:
    def __init__(self,native):
        self.uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
        self.pages=set();self.failed=False;self.writes=[];self.tail_reads=[]
        for address,size,perms in ((BUFFER,4096,3),(STACK,4096,3),(STOP,4096,5)):
            self.map(address,size,perms)
        if native:
            for address,size,data,_ in segments:
                self.map(address,size,7);self.write(address,data)
            def missing(uc,address,size,user):
                self.failed=True;uc.emu_stop()
            self.uc.hook_add(UC_HOOK_CODE,missing,begin=symbols['sm64ds_native_missing'],
                             end=symbols['sm64ds_native_missing'])
        else:
            for address,size in ((ORIGINAL,0x7c),(0x0205a460,0x1c)):
                self.map(address,size,5)
                self.write(address,bytes(arm9[address-rom.arm9RamAddress:address-rom.arm9RamAddress+size]))
            self.map(FIRMWARE,0x70,3)
        def writes(uc,access,address,size,value,user):
            if BUFFER<=address<BUFFER+4096:self.writes.append((address,size))
        self.uc.hook_add(UC_HOOK_MEM_WRITE,writes)

    def map(self,address,size,perms):
        for page in range(address&~4095,(address+size+4095)&~4095,4096):
            if page not in self.pages:self.uc.mem_map(page,4096,perms);self.pages.add(page)

    def write(self,address,data):self.uc.mem_write(address,bytes(data))
    def read(self,address,size):return bytes(self.uc.mem_read(address,size))
    def word(self,name,value):self.write(symbols[name],struct.pack('<I',value&0xffffffff))
    def reg(self,register):return self.uc.reg_read(register)
    def invoke(self,address,args=(),failure=False):
        self.failed=False;self.writes=[]
        self.uc.reg_write(UC_ARM_REG_CPSR,0x10)
        for index,register in enumerate(ARGUMENTS):
            self.uc.reg_write(register,(args[index] if index<len(args) else 0)&0xffffffff)
        for index,register in enumerate(SAVED):self.uc.reg_write(register,0x12340000+index)
        self.uc.reg_write(UC_ARM_REG_SP,STACK+0x800)
        self.uc.reg_write(UC_ARM_REG_LR,STOP)
        self.uc.emu_start(address,STOP,count=30000)
        assert self.failed==failure
        if not failure:
            assert self.reg(UC_ARM_REG_PC)==STOP
            assert self.reg(UC_ARM_REG_SP)==STACK+0x800
            assert all(self.reg(register)==0x12340000+index for index,register in enumerate(SAVED))
        return self.reg(UC_ARM_REG_R0)
    def call(self,name,*args,**kw):return self.invoke(symbols[name],args,**kw)
    def stat(self,index):return self.call('test_personal_stat',index)
    def counters(self):return struct.unpack('<5I',self.read(symbols['test_cfg_calls'],20))


def inputs(cpu,language,name,birthday,fail_stage=0,success=0):
    cpu.write(symbols['test_cfg_language'],bytes([language]))
    cpu.write(symbols['test_cfg_name'],name)
    cpu.write(symbols['test_cfg_birthday'],bytes(birthday))
    cpu.word('test_cfg_failure_stage',fail_stage)
    cpu.word('test_cfg_success_result',success)


def expected(language,name,birthday):
    effective=language if 1<=language<=5 else 1
    units=struct.unpack('<10H',name[:20])
    length=next((index for index,unit in enumerate(units) if unit==0),10)
    return bytes([effective,0,*birthday])+name[:20]+struct.pack('<H',length)+bytes(54)


oracle=Cpu(False)
rng=random.Random(0x2059eb0)
fixtures=[]
days=(31,29,31,30,31,30,31,31,30,31,30,31)
for language in range(12):
    for length in range(11):
        units=[rng.randint(1,0xd7ff) for _ in range(14)]
        if length<10:units[length]=0
        month=1+length%12
        fixtures.append((language,struct.pack('<14H',*units),(month,days[month-1])))
for _ in range(64):
    language=rng.randrange(12);length=rng.randrange(11)
    units=[rng.choice((rng.randint(1,0xd7ff),rng.randint(0xe000,0xffff))) for _ in range(14)]
    if length<10:units[length]=0
    month=rng.randrange(1,13)
    fixtures.append((language,struct.pack('<14H',*units),(month,rng.randint(1,days[month-1]))))
for length in (2,4,6,8,10):
    units=[0xd834,0xdd1e]+[0x41]*(length-2)+[0]*(14-length)
    fixtures.append((5,struct.pack('<14H',*units),(2,29)))

exports=0;caller_cases=0;caller_profile_reads=set()
for case,(language,name,birthday) in enumerate(fixtures):
    cpu=Cpu(True)
    inputs(cpu,language,name,birthday,success=0x08000001 if case&1 else 0)
    assert cpu.call('test_personal_initialize')==1
    assert cpu.stat(0)==cpu.stat(1)==1
    assert cpu.stat(2)==int(not 1<=language<=5)
    assert cpu.counters()==(1,1,1,1,1)
    assert struct.unpack('<I',cpu.read(symbols['test_cfg_protocol_errors'],4))[0]==0
    profile=expected(language,name,birthday)
    firmware=bytearray(rng.randbytes(0x70))
    firmware[2]=0;firmware[3:5]=bytes(birthday);firmware[6:26]=name[:20]
    firmware[0x1a]=profile[0x18];firmware[0x1c:0x50]=bytes(52);firmware[0x50]=0
    # The SDK uses only the low length byte, even if synthetic high bytes differ.
    struct.pack_into('<H',firmware,0x64,profile[0]|(rng.randrange(1<<13)<<3))
    oracle.write(FIRMWARE,firmware)
    for offset in (8,10,16,30,64):
        image=bytes([0xa5])*256
        target=bytearray(image);target[offset:offset+80]=profile
        cpu.write(BUFFER,image);oracle.write(BUFFER,image)
        cpu.call('func_02059eb0',BUFFER+offset)
        oracle.invoke(ORIGINAL,(BUFFER+offset,))
        assert cpu.read(BUFFER,256)==oracle.read(BUFFER,256)==bytes(target)
        for engine in (cpu,oracle):
            covered=[byte for address,size in engine.writes for byte in range(address,address+size)]
            assert sorted(covered)==list(range(BUFFER+offset,BUFFER+offset+80)), 'Output store footprint changed'
        exports+=1
    assert cpu.stat(3)==5 and cpu.stat(4)==0
    # Calling cached init after changing service fixtures must not change export.
    inputs(cpu,3,bytes(28),(1,1),fail_stage=1)
    assert cpu.call('test_personal_initialize')==1 and cpu.counters()==(1,1,1,1,1)
    cpu.write(BUFFER,bytes([0xa5])*256)
    assert cpu.call('test_personal_copy',BUFFER+16,84)==1
    assert cpu.read(BUFFER+16,80)==profile
    assert 0x027ff000 not in cpu.pages and 0x04000000 not in cpu.pages

    # Bounded original caller tail: constructors before the profile export are
    # outside this proof. The CPU executes 020b6b78 through the original return,
    # with its real jump-table language selector and 84-byte stack reservation.
    if case<132:
        address=0x020b6a84;size=0x1d0
        cpu.map(address,size,5)
        cpu.write(address,bytes(ov7.data[address-ov7.ramAddress:address-ov7.ramAddress+size]))
        cpu.map(ORIGINAL,4,5)
        cpu.map(0x02103000,4096,3)
        cpu.write(0x0210342c,struct.pack('<I',BUFFER+0x400))
        manager=bytearray(bytes([0xa5])*0x100)
        struct.pack_into('<I',manager,0x28,BUFFER+0x600)
        cpu.write(BUFFER+0x400,bytes(manager));cpu.write(BUFFER+0x600,bytes([0xa5])*0x100)
        sp=STACK+0x800
        cpu.write(sp,bytes([0xa5])*84+struct.pack('<I',STOP))
        def redirect(uc,address,size,user):uc.reg_write(UC_ARM_REG_PC,symbols['func_02059eb0'])
        cpu.uc.hook_add(UC_HOOK_CODE,redirect,begin=ORIGINAL,end=ORIGINAL)
        def caller_read(uc,access,address,size,value,user):
            if 0x020b6b80<=uc.reg_read(UC_ARM_REG_PC)<0x020b6c44 and sp<=address<sp+84:
                caller_profile_reads.add((address-sp,size))
        cpu.uc.hook_add(UC_HOOK_MEM_READ,caller_read)
        cpu.uc.reg_write(UC_ARM_REG_CPSR,0x10)
        cpu.uc.reg_write(UC_ARM_REG_SP,sp);cpu.uc.reg_write(UC_ARM_REG_LR,STOP)
        for index,register in enumerate(SAVED):cpu.uc.reg_write(register,0x12340000+index)
        cpu.uc.emu_start(0x020b6b78,STOP,count=30000)
        assert cpu.reg(UC_ARM_REG_PC)==STOP and cpu.reg(UC_ARM_REG_SP)==sp+88
        assert all(cpu.reg(register)==0x12340000+index for index,register in enumerate(SAVED))
        assert cpu.read(sp,84)==profile+bytes([0xa5])*4
        selected={1:1,2:2,3:3,4:5,5:4}[profile[0]]
        assert cpu.read(BUFFER+0x600+0x3a,1)==bytes([selected])
        manager[0xe8:0xec]=bytes(4)
        assert cpu.read(BUFFER+0x400,0x100)==bytes(manager)
        output=bytearray(bytes([0xa5])*0x100);output[0x3a]=selected
        assert cpu.read(BUFFER+0x600,0x100)==bytes(output)
        caller_cases+=1
assert caller_profile_reads=={(0,1)}, 'Menu consumed more profile fields than its language byte'

failures=0
invalid=[]
valid_name=struct.pack('<14H',0x41,0,*([0xbeef]*12))
for stage in range(1,5):invalid.append((1,valid_name,(1,1),stage))
for language in range(12,256):invalid.append((language,valid_name,(1,1),0))
for month,day in ((0,1),(13,1),(255,1),(1,0),(1,32),(4,31),(2,30),(255,255)):
    invalid.append((1,valid_name,(month,day),0))
for units in ((0xd800,0),(0xdc00,0),(0xd800,0x41,0),(*([0x41]*9),0xd800)):
    invalid.append((1,struct.pack('<14H',*units,*([0]*(14-len(units)))),(1,1),0))
for language,name,birthday,stage in invalid:
    cpu=Cpu(True);inputs(cpu,language,name,birthday,stage)
    assert cpu.call('test_personal_initialize')==0
    assert cpu.stat(0)==1 and cpu.stat(1)==0 and cpu.stat(6)==1
    assert cpu.stat(4)==1
    calls=cpu.counters()
    assert calls[0]==1 and calls[4]==int(stage!=1)
    assert cpu.call('test_personal_initialize')==0 and cpu.counters()==calls
    cpu.write(BUFFER,bytes([0xa5])*256)
    assert cpu.call('test_personal_copy',BUFFER+16,80)==0
    cpu.call('func_02059eb0',BUFFER+16,failure=True)
    assert cpu.read(BUFFER,256)==bytes([0xa5])*256
    if stage:assert cpu.stat(5)==((-0x1234567)&0xffffffff)
    failures+=1

cpu=Cpu(True)
cpu.write(BUFFER,bytes([0xa5])*256)
assert cpu.call('test_personal_copy',BUFFER+16,80)==0
cpu.call('func_02059eb0',BUFFER+16,failure=True)
assert cpu.counters()==(0,0,0,0,0) and cpu.read(BUFFER,256)==bytes([0xa5])*256
failures+=1
cpu=Cpu(True);inputs(cpu,1,valid_name,(1,1));assert cpu.call('test_personal_initialize')==1
for destination,capacity in ((0,80),(BUFFER+17,80),(BUFFER+16,79),
                              (0x027ffc80,80),(0x04000000,80),(0xfffffffe,80)):
    cpu.write(BUFFER,bytes([0xa5])*256)
    assert cpu.call('test_personal_copy',destination,capacity)==0
    assert cpu.read(BUFFER,256)==bytes([0xa5])*256
    failures+=1
for destination in (0,BUFFER+17,0x027ffc80,0x04000000,0xfffffffe):
    cpu.call('func_02059eb0',destination,failure=True)
    failures+=1

report={
    'scope':'host ASan/UBSan and bounded ARM11 CPU/ABI proof only; synthetic CFGU and ROM-oracle data',
    'real_personal_data_read':False,'host_cases':host_cases,'host_sanitizers':'ASan/UBSan passed',
    'ARM11_valid_profiles':len(fixtures),'ROM_export_comparisons':exports,
    'original_menu_language_tail_cases':caller_cases,
    'original_menu_profile_reads':sorted(caller_profile_reads),
    'explicit_failure_cases':failures,'native_DS_firmware_or_IO_mapping':False,
    'CFGU_transaction':'init -> system language -> name 0xa0000/28 bytes -> birthday 0xa0001/2 bytes -> exit, once',
    'ROM_crc32':f'{zlib.crc32(raw):08x}','ROM_sha256':hashlib.sha256(raw).hexdigest(),
    'original_export_sha256':hashlib.sha256(arm9[ORIGINAL-rom.arm9RamAddress:ORIGINAL-rom.arm9RamAddress+0x7c]).hexdigest(),
    'ARM11_ELF_sha256':hashlib.sha256(elf).hexdigest(),
    'sources_sha256':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        for name in (*SOURCES,'include/sm64ds/native_personal.hpp')},
    'unverified':['real console CFGU reads','full game menu execution','emulator application','physical Old/New 3DS','FPS'],
}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
