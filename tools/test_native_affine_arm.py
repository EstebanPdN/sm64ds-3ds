#!/usr/bin/env python3
"""Exact ARM11 Fix12 kernel / production dispatch / C / original ARM9 oracle.

Instruction counts describe executed instructions, not CPU cycles or game FPS.
All four paths execute without a DS register mapping.
"""
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import zlib
import ndspy.rom
import ndspy.codeCompression
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_SP,
                              UC_ARM_REG_LR,UC_ARM_REG_CPSR)
from test_native_len_vec3_arm import Cpu,BUFFER,STACK,STOP,ARGS,SAVED,signed32

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/native-renderer/affine-arm';OUT.mkdir(parents=True,exist_ok=True)
TOOL=str(Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-')
SOURCES=('source/native_renderer/game_model_pose.cpp','source/native_renderer/affine_compose_arm11.S',
         'tests/native_renderer/affine_arm_fixture.cpp')
flags=['-marm','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-O2',
       '-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-Wall','-Wextra','-Werror',
       '-DSM64DS_NATIVE_AFFINE_ASM=1','-I'+str(ROOT/'source/native_renderer')]
objects=[]
for source in SOURCES:
    obj=OUT/(Path(source).stem+'.o');objects.append(obj)
    subprocess.run([TOOL+'g++',*flags,'-c',str(ROOT/source),'-o',str(obj)],check=True)
elf=OUT/'affine.elf'
subprocess.run([TOOL+'g++',*flags,'-nostartfiles',
    '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,affine_production',
    *map(str,objects),'-o',str(elf)],check=True)
symbols={p[2]:int(p[0],16) for line in subprocess.check_output(
    [TOOL+'nm','--defined-only',str(elf)],text=True).splitlines() if len(p:=line.split())==3}
raw=(ROOT/'build/roms/europe.nds').read_bytes();assert zlib.crc32(raw)==0x29715dec
rom=ndspy.rom.NintendoDSRom(raw);arm9=ndspy.codeCompression.decompress(rom.arm9)
original_address=0x02052914;original_code=bytes(arm9[original_address-rom.arm9RamAddress:original_address-rom.arm9RamAddress+0x220])
native,original=Cpu(),Cpu();native.elf(elf.read_bytes())
original.map(original_address,len(original_code));original.uc.mem_write(original_address,original_code)
# Keep the hook installed before any translation blocks are cached. Installing
# and removing it per call gives incomplete counts on reused Unicorn blocks.
executed=[0]
def instruction(*_):executed[0]+=1
native.uc.hook_add(UC_HOOK_CODE,instruction)

def reference(a,b):
    return tuple(signed32((sum(a[col*3+k]*b[k*3+row] for k in range(3))>>12)+
                         (b[9+row] if col==3 else 0)) for col in range(4) for row in range(3))

instructions={};counts={}
def invoke(cpu,address,a,b,mode,alias,alignment,label,count_instructions=False):
    a_at=32+alignment;b_at=a_at if alias=='inputs' else 128+alignment
    out_at=a_at if alias=='local' else b_at if alias=='parent' else 224+alignment
    image=bytearray(b'\xa5'*320)
    struct.pack_into('<12i',image,a_at,*a);struct.pack_into('<12i',image,b_at,*b)
    expected=bytearray(image);struct.pack_into('<12i',expected,out_at,*reference(a,b))
    uc=cpu.uc;uc.mem_write(BUFFER,bytes(image));uc.reg_write(UC_ARM_REG_CPSR,0x10)
    for reg,value in zip(ARGS,(BUFFER+a_at,BUFFER+b_at,BUFFER+out_at,mode)):uc.reg_write(reg,value)
    for i,reg in enumerate(SAVED):uc.reg_write(reg,0x76540000+i)
    uc.reg_write(UC_ARM_REG_SP,STACK+0x8000);uc.reg_write(UC_ARM_REG_LR,STOP)
    cpu.reads.clear();cpu.writes.clear();cpu.invalid.clear()
    before=executed[0]
    uc.emu_start(address,STOP,count=20000)
    if count_instructions:
        total=executed[0]-before;assert total>0
        instructions.setdefault(label,[]).append(total)
    assert uc.reg_read(UC_ARM_REG_PC)==STOP and uc.reg_read(UC_ARM_REG_SP)==STACK+0x8000
    assert all(uc.reg_read(reg)==0x76540000+i for i,reg in enumerate(SAVED)),label
    actual=bytes(uc.mem_read(BUFFER,len(image)))
    assert actual==expected,(label,alias,a,b,struct.unpack_from('<12i',actual,out_at),reference(a,b))
    assert not cpu.invalid and 0x04000000 not in cpu.pages
    counts[label]=counts.get(label,0)+1

rng=random.Random(0x41464631)
edge=(0,1,-1,4095,4096,-4096,0x7fffffff,-0x80000000)
cases=[((a,)*12,(b,)*12) for a in edge for b in edge]
identity=(4096,0,0,0,4096,0,0,0,4096,0,0,0)
cases.extend([(identity,identity),((4096,0,0,0,4096,0,0,0,4096,12288,-16384,20480),identity)])
for _ in range(4096):cases.append((tuple(rng.randint(-2**31,2**31-1) for _ in range(12)),tuple(rng.randint(-2**31,2**31-1) for _ in range(12))))
for index,(a,b) in enumerate(cases):
    alignment=(index%4)*4
    for alias in ('disjoint','inputs','local','parent'):
        current_b=a if alias=='inputs' else b
        for label,mode in [('production_c',0),('production_asm',1)]:
            invoke(native,symbols['affine_production'],a,current_b,mode,alias,alignment,label,index<8 and alias=='disjoint')
        invoke(original,original_address,a,current_b,0,alias,alignment,'original_rom')
        if alias in ('disjoint','inputs'):
            invoke(native,symbols['sm64ds_affine_compose_arm11'],a,current_b,0,alias,alignment,'raw_asm')
report={'scope':'ARM instruction correctness/ABI; instruction counts are not hardware cycle or FPS measurements',
        'cases':counts,'sample_instruction_counts':instructions,'rom_leaf_sha256':hashlib.sha256(original_code).hexdigest(),
        'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),
        'source_sha256':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in SOURCES}}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
