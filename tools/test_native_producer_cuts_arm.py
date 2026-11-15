#!/usr/bin/env python3
"""BUILD134 producer cost cuts under the console compiler, in Unicorn (ARM,
VFP enabled): tests/native_renderer/producer_cuts.cpp built with devkitARM's
g++ and the console's flags.

1. Equality: the cuts, the BUILD 133 paths kept behind the console switch and
   the reference copies of the BUILD 133 routines publish the same bits. Run
   under several FPSCR values (round to nearest, the three directed modes,
   flush-to-zero with default NaN): the cuts keep every element's operations
   and their order, so the equality cannot depend on the rounding mode.
2. Cost (only with --catalog, the private prepared model catalog): executed
   ARM instructions of each producer stage for scenes of catalog models, cuts
   against BUILD 133, read through the producer's own stage clocks.

Instruction counts are not CPU cycles and not console FPS: a VFP add that
waits for the previous add of its sum, a divide or a cache miss each count as
one instruction. No ROM, GPU or 3DS runtime is involved.

  build/python/bin/python tools/test_native_producer_cuts_arm.py \
      [--catalog build/native-renderer/.../native_model_catalog.hpp]
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
SOURCES=['tests/native_renderer/producer_cuts.cpp',
    'source/integrated_native/renderer/scene_producer.cpp','source/integrated_native/renderer/game_materials.cpp',
    'source/integrated_native/renderer/texture_sequence_hooks.cpp','source/integrated_native/renderer/game_lighting.cpp',
    'source/native_renderer/scene_frame.cpp','source/native_renderer/model_instance.cpp','source/native_renderer/game_model_pose.cpp',
    'source/native_renderer/affine_compose_arm11.S','source/native_renderer/game_material.cpp','source/native_renderer/lighting_palette.cpp',
    'source/native_renderer/lighting_uniforms.cpp','source/native_renderer/lighting.cpp','source/native_renderer/contracts.cpp',
    'source/native_renderer/native_quads.cpp','source/native_renderer/image_layer.cpp']
# The console build's flags for these objects (build3ds flags.make). Added for
# a program without the 3DS runtime: no exceptions/RTTI, and static-local
# guards without the C++ runtime's lock (there is one thread and no libctru).
FLAGS=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-mword-relocations','-ffunction-sections','-fdata-sections',
    '-D__3DS__','-O2','-DNDEBUG','-std=gnu++17','-fsigned-char','-fno-strict-aliasing','-Wno-narrowing',
    '-DSM64DS_PLATFORM_PC','-DSM64DS_NATIVE_AFFINE_ASM=1','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics']
STACK,STOP,TEXT=0x08000000,0x09000000,0x00100000
# struct Coverage of the test, in order.
COVERAGE=('poses','pose_refusals','marked','mark_refusals','part_models','part_refusals','parts','lit_parts',
    'material_models','material_refusals','materials','sequences','poisoned',
    'updates','models','model_refusals','frame_parts','skin_pieces','skin_vertices','repeated_poses')
# Scenes of catalog models with the part and skin-vertex counts measured on
# the console per update (69-230 parts, 330-909 skin vertices).
SCENES={'small (one character, small props)':[577,315,1355],
        'large (two characters, a level)':[2146,231,1571]}


def build(output,catalog):
    tool=str(Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-')
    flags=FLAGS+['-I'+str(ROOT/'source/native_renderer'),'-I'+str(ROOT/'source/integrated_native/renderer'),
        '-I'+str(ROOT/'third_party/sm64ds-3ds/sm64ds-decomp/include')]
    if catalog:
        # A copy beside the objects: its own #include "model_instance.hpp" must reach the repository header.
        local=output/'producer_cuts_catalog.hpp';local.write_bytes(catalog.read_bytes())
        flags.append('-DSM64DS_PRODUCER_CUTS_CATALOG="%s"'%local)
    objects=[]
    for source in SOURCES:
        obj=output/(Path(source).stem+'.o');objects.append(str(obj))
        subprocess.run([tool+'g++',*flags,'-c',str(ROOT/source),'-o',str(obj)],check=True)
    elf=output/'producer_cuts.elf'
    subprocess.run([tool+'g++',*flags,'-nostartfiles','-Wl,--gc-sections,-z,noexecstack,-Ttext=0x%x,-e,producer_cuts_run'%TEXT+
        (',--undefined=producer_cuts_bench' if catalog else ''),*objects,'-lm','-o',str(elf)],check=True)
    names={}
    for line in subprocess.check_output([tool+'nm','--defined-only',str(elf)],text=True).splitlines():
        parts=line.split()
        if len(parts)==3:names[parts[2]]=int(parts[0],16)
    return elf,names


class Machine:
    def __init__(self,elf,names):
        from elftools.elf.elffile import ELFFile
        from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_MEM_INVALID
        from unicorn.arm_const import UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC
        self.uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
        self.uc.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);self.uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
        self.init=[]
        with elf.open('rb') as f:
            image=ELFFile(f);pages=set()
            segments=[s for s in image.iter_segments() if s['p_type']=='PT_LOAD' and s['p_memsz']]
            for s in segments:
                for page in range(s['p_vaddr']&~4095,(s['p_vaddr']+s['p_memsz']+4095)&~4095,4096):pages.add(page)
            for page in sorted(pages):self.uc.mem_map(page,4096)
            for s in segments:self.uc.mem_write(s['p_vaddr'],s.data())
            self.image=[(s['p_vaddr'],s.data(),s['p_memsz']) for s in segments]
            for name in ('.preinit_array','.init_array'):
                section=image.get_section_by_name(name)
                if section and section['sh_size']:
                    self.init+=list(struct.unpack('<%dI'%(section['sh_size']//4),section.data()))
        # The C++ runtime's own constructors (exception pools) want a heap; the test's code has none and never throws.
        runtime={address for name,address in names.items() if name.startswith('_GLOBAL__sub_I_') and ('__eh_globals' in name or '__gnu_cxx' in name)}
        self.init=[function for function in self.init if function not in runtime]
        self.uc.mem_map(STACK,0x100000);self.uc.mem_map(STOP,4096)
        self.invalid=[]
        self.uc.hook_add(UC_HOOK_MEM_INVALID,lambda uc,access,address,size,value,user:self.invalid.append((access,address)) or False)

    def reset(self):
        """Fresh statics: the image as linked, then its constructors."""
        for address,data,size in self.image:
            self.uc.mem_write(address,data+bytes(size-len(data)))
        for function in self.init:self.call(function)

    def call(self,function,*arguments,fpscr=None,limit=0):
        from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,
            UC_ARM_REG_PC,UC_ARM_REG_CPSR,UC_ARM_REG_FPSCR)
        uc=self.uc;uc.reg_write(UC_ARM_REG_CPSR,0x10)
        for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),arguments):uc.reg_write(reg,value)
        if fpscr is not None:uc.reg_write(UC_ARM_REG_FPSCR,fpscr)
        uc.reg_write(UC_ARM_REG_SP,STACK+0xffff0);uc.reg_write(UC_ARM_REG_LR,STOP)
        uc.emu_start(function,STOP,count=limit)
        if uc.reg_read(UC_ARM_REG_PC)!=STOP or self.invalid:
            raise SystemExit('did not return: pc=%#x invalid=%r'%(uc.reg_read(UC_ARM_REG_PC),self.invalid[:4]))
        return uc.reg_read(UC_ARM_REG_R0)


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--catalog',type=Path,help='private prepared model catalog header: also measure catalog scenes')
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-renderer/producer-cuts-arm')
    parser.add_argument('--updates',type=int,default=8)
    parser.add_argument('--effort',type=int,default=1,help='comparison length; the host test runs 8, three times')
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    catalog=args.catalog.resolve() if args.catalog else None
    elf,names=build(output,catalog)
    machine=Machine(elf,names)
    report={'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'equality':[],'scenes':[],
        'sources_sha256':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in SOURCES},
        'physical_hardware':False,'cycles_or_fps':False}
    # 1. Equality, in each floating-point environment.
    coverage=next(address for name,address in names.items() if name.endswith('8coverageE'))
    modes=[(0x00000000,'round to nearest'),(0x00400000,'round up'),(0x00800000,'round down'),(0x00c00000,'round toward zero'),
           (0x03000000,'flush-to-zero, default NaN'),(0x03c00000,'flush-to-zero, default NaN, toward zero')]
    for fpscr,label in modes:
        machine.reset();start=time.time()
        line=machine.call(names['producer_cuts_run'],args.effort,0x2468ace1+17*len(report['equality']),fpscr=fpscr)
        # What was compared (the test's own coverage counters).
        covered=dict(zip(COVERAGE,struct.unpack('<%dQ'%len(COVERAGE),machine.uc.mem_read(coverage,8*len(COVERAGE)))))
        report['equality'].append({'fpscr':'%#010x'%fpscr,'mode':label,'failure_source_line':line,'seconds':round(time.time()-start,1),'compared':covered})
        print('equality  FPSCR=%#010x (%s): %s  [%.0f s; %d pose matrices, %d parts, %d materials, %d producer updates, %d skin vertices]'%(
            fpscr,label,'same bits' if not line else 'MISMATCH at producer_cuts.cpp:%d'%line,time.time()-start,
            covered['poses'],covered['parts']+covered['frame_parts'],covered['materials'],covered['updates'],covered['skin_vertices']),flush=True)
    passed=all(not e['failure_source_line'] for e in report['equality'])
    # 2. Cost of catalog scenes.
    if catalog and passed:
        from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_READ
        executed=[0]
        def step(uc,address,size,user):executed[0]+=1
        clock=names['producer_cuts_clock']
        def read(uc,access,address,size,value,user):uc.mem_write(clock,struct.pack('<I',executed[0]&0xffffffff))
        machine.uc.hook_add(UC_HOOK_CODE,step);machine.uc.hook_add(UC_HOOK_MEM_READ,read,begin=clock,end=clock+3)
        machine.uc.ctl_flush_tb() # hooks reach code translated before they existed
        profile=names['_ZN6sm64ds6render10game_hooks13stage_profileE'];skin=names['_ZN6sm64ds6render10game_hooks10skin_ticksE']
        stages=('materials','poses','normal bases','lighting bases, lights','parts','skin')
        for scene,files in SCENES.items():
            for moving,motion in ((1,'every bone moves each update'),(0,'no bone moves')):
                rows={}
                for cuts in (0,1):
                    machine.reset()
                    machine.uc.mem_write(names['producer_cuts_scene'],struct.pack('<16I',*files,*[0]*(16-len(files))))
                    machine.uc.mem_write(skin,bytes(8))
                    line=machine.call(names['producer_cuts_bench'],cuts,args.updates,moving,fpscr=0)
                    if line:raise SystemExit('scene %r refused at producer_cuts.cpp:%d'%(scene,line))
                    ticks=struct.unpack('<6Q',machine.uc.mem_read(profile,48))
                    census=struct.unpack('<8I',machine.uc.mem_read(names['producer_cuts_census'],32))
                    rows[cuts]={'stage_instructions_per_update':[t/args.updates for t in ticks],'census':census[:6],'digest':census[6]}
                if rows[0]['digest']!=rows[1]['digest'] or rows[0]['census']!=rows[1]['census']:
                    raise SystemExit('scene %r: the two paths published different bits'%scene)
                c=rows[0]['census']
                print('\n%s; %s\n  models=%d parts=%d (lit %d) skin vertices=%d light states=%d materials=%d; published bits equal'%(
                    scene,motion,c[0],c[1],c[4],c[2],c[3],c[5]))
                print('  %-24s %12s %12s %8s'%('stage','BUILD 133','cuts','saved'))
                old=rows[0]['stage_instructions_per_update'];new=rows[1]['stage_instructions_per_update']
                for name,a,b in zip(stages,old,new):
                    print('  %-24s %12.0f %12.0f %7.0f%%'%(name,a,b,100*(a-b)/a if a else 0))
                print('  %-24s %12.0f %12.0f %7.0f%%'%('all stages',sum(old),sum(new),100*(sum(old)-sum(new))/sum(old)))
                report['scenes'].append({'scene':scene,'files':files,'motion':motion,'updates':args.updates,
                    'census':dict(zip(('models','parts','skin_vertices','light_states','lit_parts','materials'),c)),
                    'instructions_per_update':{name:{'build133':a,'cuts':b} for name,a,b in zip(stages,old,new)}})
    report['passed']=passed
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    return 0 if passed else 1


if __name__=='__main__':raise SystemExit(main())
