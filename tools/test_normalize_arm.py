#!/usr/bin/env python3
"""Compare ARM11 NormalizeVec3 with its ROM routine and modeled math registers.

Only the original leaf oracle gets DS arithmetic registers; the game diagnostic
keeps all DS I/O unmapped. This does not run a DS or 3DS game.
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
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ
from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R4,
    UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,
    UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_CPSR)

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'build/game-data/python'))
import ndspy.rom
import ndspy.codeCompression
OUT=ROOT/'build/native-normalize';OUT.mkdir(exist_ok=True)
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
objects=[ROOT/'build/3ds/source/ctr/CMakeFiles/sm64ds_game_math.dir/game_math.o',
         ROOT/'build/3ds/CMakeFiles/sm64ds_math.dir/source/core/fixed_math.o']
elf_path=OUT/'normalize.elf'
subprocess.run([TOOL+'g++','-march=armv6k','-mfloat-abi=hard','-mtp=soft',
    '-nostartfiles','-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,NormalizeVec3,-u,func_02049018',
    *map(str,objects),'-o',str(elf_path)],check=True)
symbols={line.split()[2]:int(line.split()[0],16) for line in
    subprocess.check_output([TOOL+'nm','--defined-only',str(elf_path)],text=True).splitlines() if len(line.split())==3}
cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
elf=elf_path.read_bytes();phoff=struct.unpack_from('<I',elf,28)[0]
phsize,phnum=struct.unpack_from('<HH',elf,42);pages=set()
for i in range(phnum):
    typ,off,va,pa,filesz,memsz,flags,align=struct.unpack_from('<8I',elf,phoff+i*phsize)
    if typ!=1 or not memsz: continue
    for page in range(va&~4095,(va+memsz+4095)&~4095,4096):
        if page not in pages: cpu.mem_map(page,4096,7);pages.add(page)
    cpu.mem_write(va,elf[off:off+filesz])
raw=(ROOT/'build/roms/europe.nds').read_bytes()
assert zlib.crc32(raw)==0x29715dec
rom=ndspy.rom.NintendoDSRom(raw);arm9=ndspy.codeCompression.decompress(rom.arm9)
original=0x020534ec
cpu.mem_map(original&~4095,4096,5)
cpu.mem_write(original,bytes(arm9[original-rom.arm9RamAddress:original-rom.arm9RamAddress+0x158]))
audio_original=0x02049018
cpu.mem_map(audio_original&~4095,4096,5)
cpu.mem_write(audio_original,bytes(arm9[audio_original-rom.arm9RamAddress:audio_original-rom.arm9RamAddress+0x98]))
cpu.mem_map(0x04000000,4096,3)
def math_read(uc,access,address,size,value,user):
    if address in (0x040002a0,0x040002a4):
        a,b=struct.unpack('<qq',uc.mem_read(0x04000290,16))
        q=(-1 if a>=0 else 1) if b==0 else (abs(a)//abs(b))*(-1 if (a<0)!=(b<0) else 1)
        uc.mem_write(0x040002a0,struct.pack('<Q',q&0xffffffffffffffff))
    elif address==0x040002b4:
        value=struct.unpack('<Q',uc.mem_read(0x040002b8,8))[0]
        if not struct.unpack('<H',uc.mem_read(0x040002b0,2))[0]&1: value &= 0xffffffff
        uc.mem_write(address,struct.pack('<I',math.isqrt(value)))
cpu.hook_add(UC_HOOK_MEM_READ,math_read,begin=0x040002a0,end=0x040002b7)
buffer=0x500000;stack=0x600000;stop=0x700000
cpu.mem_map(buffer,4096,3);cpu.mem_map(stack,4096,3);cpu.mem_map(stop,4096,5)
saved=(UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11)
def invoke(address,vector,in_place,scalar=False):
    cpu.mem_write(buffer,bytes([0xa5])*64)
    source=buffer+8;destination=source if in_place else buffer+40
    cpu.mem_write(source,struct.pack('<3i',*vector));cpu.reg_write(UC_ARM_REG_CPSR,0x10)
    for reg,value in ((UC_ARM_REG_R0,source),(UC_ARM_REG_R1,destination),
                      (UC_ARM_REG_SP,stack+0xf00),(UC_ARM_REG_LR,stop)):
        cpu.reg_write(reg,value)
    for i,reg in enumerate(saved): cpu.reg_write(reg,0x12340000+i)
    cpu.emu_start(address,stop,count=3000)
    assert cpu.reg_read(UC_ARM_REG_PC)==stop
    assert cpu.reg_read(UC_ARM_REG_SP)==stack+0xf00
    assert all(cpu.reg_read(reg)==0x12340000+i for i,reg in enumerate(saved))
    contents=bytes(cpu.mem_read(buffer,64))
    assert contents[:8]==bytes([0xa5])*8 and contents[20:40]==bytes([0xa5])*20 and contents[52:]==bytes([0xa5])*12
    return cpu.reg_read(UC_ARM_REG_R0) if scalar else bytes(cpu.mem_read(destination,12))
cases=list(itertools.product((0,1,-1,4096,-4096,-2147483648,2147483647),repeat=3))
rng=random.Random(0x534ec)
cases.extend(tuple(rng.randint(-2147483648,2147483647) for _ in range(3)) for _ in range(1000))
cases.extend(tuple(rng.randint(-100000,100000) for _ in range(3)) for _ in range(1000))
for v in cases:
    for in_place in (False,True):
        actual=invoke(symbols['NormalizeVec3'],v,in_place)
        expected=invoke(original,v,in_place)
        assert actual==expected,(v,in_place,actual.hex(),expected.hex())
    actual=invoke(symbols['func_02049018'],v,False,True)
    expected=invoke(audio_original,v,False,True)
    assert actual==expected,('sound-distance',v,actual,expected)
report={'status':'passed','scope':'CPU oracle only; DS arithmetic modeled for original leaf, no gameplay or FPS proof',
    'comparisons':len(cases)*2,'audio_distance_comparisons':len(cases),
    'in_place_and_separate_output':True,'canaries_and_registers':'passed',
    'objects':{str(o.relative_to(ROOT)):hashlib.sha256(o.read_bytes()).hexdigest() for o in objects},
    'elf_sha256':hashlib.sha256(elf).hexdigest(),'rom_sha256':hashlib.sha256(raw).hexdigest()}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
