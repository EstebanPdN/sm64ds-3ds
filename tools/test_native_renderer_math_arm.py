#!/usr/bin/env python3
"""Compare the linked renderer math wrappers against ROM ARM9 leaves.

Native ARM11 execution has no DS page. Only the separate ROM oracle gets
sqrt/IME registers. Checks returns, ABI, input canaries and disabled fallback;
does not measure game FPS or physical 3DS performance.
"""
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import zlib

from test_native_len_vec3_arm import Cpu, BUFFER, STACK, STOP, ARGS, SAVED, signed32
from unicorn import UC_HOOK_MEM_READ
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_SP,
                              UC_ARM_REG_LR, UC_ARM_REG_CPSR)
import ndspy.rom
import ndspy.codeCompression

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/native-renderer/math-arm';OUT.mkdir(parents=True,exist_ok=True)
TOOL=str(Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-')
SOURCES=('source/integrated_native/renderer/game_native_math.cpp',
         'source/core/fixed_math.cpp','tests/native_renderer/math_boundary_stub.cpp')
ENTRIES={'LenVec3':(0x020536e4,0x8c),'func_02053274':(0x02053274,0xac),
         '_ZN4cstd4sqrtEy':(0x0203d744,0x74),'func_02053644':(0x02053644,0xa0),
         'func_02049018':(0x02049018,0x98)}
flags=['-std=c++17','-marm','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard',
       '-mtp=soft','-O2','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror',
       '-DSM64DS_FRESH_NATIVE_GAME=1','-I'+str(ROOT/'include'),'-I'+str(ROOT/'source/native_renderer')]
objects=[]
for source in SOURCES:
    obj=OUT/(Path(source).stem+'.o')
    subprocess.run([TOOL+'g++',*flags,'-c',str(ROOT/source),'-o',str(obj)],check=True)
    objects.append(obj)
elf=OUT/'renderer-math.elf'
subprocess.run([TOOL+'g++',*flags,'-nostartfiles',
    '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,__wrap_LenVec3',
    *('-Wl,-u,__wrap_'+name for name in ENTRIES),*map(str,objects),'-o',str(elf)],check=True)
symbols={parts[2]:int(parts[0],16) for line in subprocess.check_output(
    [TOOL+'nm','--defined-only',str(elf)],text=True).splitlines()
    if len(parts:=line.split())==3}
raw=(ROOT/'build/roms/europe.nds').read_bytes()
assert zlib.crc32(raw)==0x29715dec
rom=ndspy.rom.NintendoDSRom(raw);arm9=ndspy.codeCompression.decompress(rom.arm9)
native,original=Cpu(),Cpu();native.elf(elf.read_bytes())
rom_hashes={}
for name,(address,size) in ENTRIES.items():
    code=bytes(arm9[address-rom.arm9RamAddress:address-rom.arm9RamAddress+size])
    original.map(address,size);original.uc.mem_write(address,code)
    rom_hashes[name]=hashlib.sha256(code).hexdigest()
original.map(0x04000000,4096,3)
root_reads=0
def arithmetic(uc,access,address,size,value,user):
    global root_reads
    mode=struct.unpack('<H',uc.mem_read(0x040002b0,2))[0]
    assert mode in (0,1)
    radicand=struct.unpack('<Q' if mode else '<I',uc.mem_read(0x040002b8,8 if mode else 4))[0]
    uc.mem_write(address,struct.pack('<I',math.isqrt(radicand)));root_reads+=1
original.uc.hook_add(UC_HOOK_MEM_READ,arithmetic,begin=0x040002b4,end=0x040002b4)

def invoke(cpu,address,args,image):
    uc=cpu.uc;uc.mem_write(BUFFER,image)
    uc.reg_write(UC_ARM_REG_CPSR,0x10)
    for i,reg in enumerate(ARGS):uc.reg_write(reg,args[i] if i<len(args) else 0)
    for i,reg in enumerate(SAVED):uc.reg_write(reg,0x76540000+i)
    uc.reg_write(UC_ARM_REG_SP,STACK+0x8000);uc.reg_write(UC_ARM_REG_LR,STOP)
    cpu.reads.clear();cpu.writes.clear();cpu.invalid.clear()
    uc.emu_start(address,STOP,count=20000)
    assert uc.reg_read(UC_ARM_REG_PC)==STOP and uc.reg_read(UC_ARM_REG_SP)==STACK+0x8000
    assert all(uc.reg_read(reg)==0x76540000+i for i,reg in enumerate(SAVED))
    assert bytes(uc.mem_read(BUFFER,len(image)))==image
    assert not cpu.invalid
    return signed32(uc.reg_read(UC_ARM_REG_R0))

def root(value):return signed32(math.isqrt((value<<2)&0xffffffffffffffff)+1)//2
rng=random.Random(0x534d3939)
edges=(0,1,-1,4096,-4096,0x7fffffff,-0x80000000)
vectors=list(itertools.product(edges,repeat=3))+[tuple(rng.randint(-2**31,2**31-1) for _ in range(3)) for _ in range(700)]
counts={name:0 for name in ENTRIES}
for name in ENTRIES:
    for vector in vectors:
        image=bytearray(b'\xa5'*128)
        struct.pack_into('<3i',image,32,*vector)
        args=(BUFFER+32,)
        if name=='LenVec3':expected=root(sum(v*v for v in vector))
        elif name=='func_02053274':
            other=tuple(rng.randint(-2**31,2**31-1) for _ in range(3))
            struct.pack_into('<3i',image,64,*other);args=(BUFFER+32,BUFFER+64)
            expected=root(sum(signed32(a-b)**2 for a,b in zip(vector,other)))
        elif name=='_ZN4cstd4sqrtEy':
            value=((vector[0]&0xffffffff)<<32)|(vector[1]&0xffffffff)
            args=(value&0xffffffff,value>>32);expected=root(value)
        elif name=='func_02053644':
            short=tuple(((v+32768)&65535)-32768 for v in vector)
            struct.pack_into('<3h',image,32,*short);expected=root(sum(v*v for v in short))
        else:
            x,y,z=(v>>12 for v in vector);expected=math.isqrt((x*x+4*y*y+z*z)&0xffffffff)
        image=bytes(image)
        left=invoke(native,symbols['__wrap_'+name],args,image)
        right=invoke(original,ENTRIES[name][0],args,image)
        assert left==right==expected,(name,vector,left,right,expected)
        counts[name]+=1
    # Comparison renderer retains its existing owner, observable per leaf.
    native.uc.mem_write(symbols['native_math_test_enabled'],struct.pack('<I',0))
    assert invoke(native,symbols['__wrap_'+name],args,image)==-777-list(ENTRIES).index(name)
    native.uc.mem_write(symbols['native_math_test_enabled'],struct.pack('<I',1))
assert 0x04000000 not in native.pages
report={'scope':'ARM CPU/ABI comparison; not FPS or hardware validation','cases':counts,
        'native_ds_mapping':False,'original_sqrt_reads':root_reads,'fallback_cases':5,
        'source_sha256':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in SOURCES},
        'rom_leaf_sha256':rom_hashes,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest()}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
