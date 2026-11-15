#!/usr/bin/env python3
"""ARM11 normal-basis normalisation: BUILD131 (exact VFP divide/square root)
against the BUILD130 integer loops, in Unicorn (ARM11 MPCore, VFP enabled).

Both paths must return the same nine words for every bone matrix. Instruction
counts describe executed instructions, not CPU cycles or game FPS: a VFP
double divide or square root is one instruction of about 30 cycles.
"""
import hashlib,json,os,random,struct,subprocess
from pathlib import Path
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_CPSR
from test_native_len_vec3_arm import Cpu,BUFFER,STACK,STOP,ARGS,SAVED

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/native-renderer/normalize-arm';OUT.mkdir(parents=True,exist_ok=True)
TOOL=str(Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-')
SOURCES=('source/native_renderer/game_model_pose.cpp','source/native_renderer/affine_compose_arm11.S',
         'source/native_renderer/contracts.cpp','tests/native_renderer/normalize_arm_fixture.cpp')
flags=['-marm','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-O2','-ffunction-sections','-fdata-sections',
       '-fno-strict-aliasing','-fno-exceptions','-fno-rtti','-Wall','-Wextra','-Werror','-DSM64DS_NATIVE_AFFINE_ASM=1','-I'+str(ROOT/'source/native_renderer')]
objects=[]
for source in SOURCES:
    obj=OUT/(Path(source).stem+'.o');objects.append(obj)
    subprocess.run([TOOL+'g++',*flags,'-c',str(ROOT/source),'-o',str(obj)],check=True)
elf=OUT/'normalize.elf'
subprocess.run([TOOL+'g++',*flags,'-nostartfiles','-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,normalize_production,--undefined=normalize_build130',
    *map(str,objects),'-lm','-o',str(elf)],check=True)
symbols={p[2]:int(p[0],16) for line in subprocess.check_output([TOOL+'nm','--defined-only',str(elf)],text=True).splitlines() if len(p:=line.split())==3}
cpu=Cpu();cpu.elf(elf.read_bytes())
executed=[0]
cpu.uc.hook_add(UC_HOOK_CODE,lambda *_:executed.__setitem__(0,executed[0]+1))  # before any block is translated

def call(name,bone):
    uc=cpu.uc;image=bytearray(b'\xa5'*256);struct.pack_into('<12i',image,32,*bone)
    uc.mem_write(BUFFER,bytes(image));uc.reg_write(UC_ARM_REG_CPSR,0x10)
    for reg,value in zip(ARGS,(BUFFER+32,BUFFER+128,0,0)):uc.reg_write(reg,value)
    for i,reg in enumerate(SAVED):uc.reg_write(reg,0x76540000+i)
    uc.reg_write(UC_ARM_REG_SP,STACK+0x8000);uc.reg_write(UC_ARM_REG_LR,STOP)
    cpu.invalid.clear();before=executed[0]
    uc.emu_start(symbols[name],STOP,count=200000)
    assert uc.reg_read(UC_ARM_REG_PC)==STOP and uc.reg_read(UC_ARM_REG_SP)==STACK+0x8000 and not cpu.invalid,(name,bone)
    assert all(uc.reg_read(reg)==0x76540000+i for i,reg in enumerate(SAVED)),name
    after=bytes(uc.mem_read(BUFFER,256))
    assert after[:128]==bytes(image[:128]) and after[164:]==bytes(image[164:]),name  # input and canaries intact
    return uc.reg_read(UC_ARM_REG_R0),struct.unpack_from('<9i',after,128),executed[0]-before

rng=random.Random(0x4e4f524d)
edges=(0,1,-1,2,3,7,-7,64,-64,4095,4096,-4096,65535,-65536,0xffffff,-0xffffff,0x1000000,-0x1000000)
cases=[tuple(edges[(a+k*step)%len(edges)] for k in range(9))+(0,0,0) for a in range(len(edges)) for step in (1,2,5,7)]
cases+=[tuple(rng.randint(-r,r) for _ in range(9))+(0,0,0) for r in (3,7,300,4096,70000,0x1000000) for _ in range(2500)]
cases+=[tuple(rng.randint(-0x1000001,0x1000001) for _ in range(9))+(0,0,0) for _ in range(500)]  # some out of the accepted domain
cases+=[(0x1000001,0,0,0,4096,0,0,0,4096,0,0,0),(4096,0,0,0,-0x1000001,0,0,0,4096,0,0,0)]  # outside the accepted domain
# What the game feeds it: rotations with 20.12 columns, scaled 0.25x to 4x.
import math
def rotation(scale):
    a,b,c=(rng.uniform(0,2*math.pi) for _ in range(3));ca,sa,cb,sb,cc,sc=math.cos(a),math.sin(a),math.cos(b),math.sin(b),math.cos(c),math.sin(c)
    m=((cb*cc,cb*sc,-sb),(sa*sb*cc-ca*sc,sa*sb*sc+ca*cc,sa*cb),(ca*sb*cc+sa*sc,ca*sb*sc-sa*cc,ca*cb))
    return tuple(int(round(v*4096*scale)) for column in m for v in column)+(0,0,0)
poses=[rotation(s) for s in (0.25,0.5,1,1,1,1,2,4) for _ in range(400)]
def run(group):
    totals={'build131':0,'build130':0};accepted=refused=0
    for bone in group:
        new=call('normalize_production',bone);old=call('normalize_build130',bone)
        assert new[0]==old[0],(bone,new[0],old[0])
        if new[0]:
            assert new[1]==old[1],(bone,new[1],old[1]);accepted+=1
            totals['build131']+=new[2];totals['build130']+=old[2]
        else:refused+=1
    return {'cases':len(group),'identical_bases':accepted,'refused_by_both':refused,
            'instructions_per_basis':{k:round(v/max(1,accepted),1) for k,v in totals.items()},
            'instruction_ratio_build130_over_build131':round(totals['build130']/max(1,totals['build131']),2)}
report={'scope':'ARM11 instruction correctness; instruction counts are not hardware cycles or FPS',
        'edge_and_random_columns':run(cases),'rotations_scaled_quarter_to_four':run(poses),
        'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),
        'source_sha256':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in SOURCES}}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
