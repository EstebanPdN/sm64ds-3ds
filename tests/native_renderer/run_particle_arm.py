#!/usr/bin/env python3
"""Build/run native particle hooks against pinned original ARM producers.

Authored32-bit objects only, no ROM or GPU runtime. Unicorn maps original MMIO
solely so the unchanged reference can write its polygon value. Native hooks
must emit owned semantic geometry instead of writing graphics registers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--compiler',type=Path);args=parser.parse_args();output=args.output.resolve()
    if not output.is_relative_to((ROOT/'build').resolve()):parser.error('Keep regression artifacts under build/')
    if output.exists():parser.error('Use a new output directory to preserve earlier evidence')
    compiler=args.compiler or (Path(os.environ['DEVKITARM'])/'bin/arm-none-eabi-g++' if os.getenv('DEVKITARM') else shutil.which('arm-none-eabi-g++'))
    if not compiler or not Path(compiler).is_file():parser.error('Set DEVKITARM or --compiler to the project SDK')
    compiler=Path(compiler);gcc=compiler.with_name('arm-none-eabi-gcc');output.mkdir(parents=True)
    sources=['tests/native_renderer/particle_arm.cpp','source/integrated_native/renderer/particle_hooks.cpp',
             'source/native_renderer/image_catalog.cpp','source/native_renderer/game_particle_geometry.cpp',
             'source/native_renderer/game_model_pose.cpp','source/native_renderer/contracts.cpp']
    originals={'func_0204be40.c':'reference_billboard','func_0204bbd8.c':'reference_child_billboard','MulVec3Mat4x3.c':None}
    common=['-Os','-mword-relocations','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard',
        '-ffunction-sections','-fdata-sections','-D__3DS__=1','-DSM64DS_PLATFORM_PC=1',
        '-Ithird_party/sm64ds-3ds/sm64ds-decomp/include']
    commands=[];objects=[]
    with (output/'build.log').open('w') as log:
        for name,symbol in originals.items():
            path='third_party/sm64ds-3ds/sm64ds-decomp/src/'+name;sources.append(path);obj=output/(name+'.o');objects.append(str(obj))
            command=[str(gcc),'-std=c99',*common,'-fwrapv','-c',path,'-o',str(obj)]
            if symbol:command+=['-D'+name.removesuffix('.c')+'='+symbol]
            commands.append(command);subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        elf=output/'particle.elf'
        command=[str(compiler),'-std=c++17',*common,'-fno-exceptions','-fno-rtti','-Wall','-Wextra','-Werror',
            '-DSM64DS_FRESH_NATIVE_GAME=1','-Isource/native_renderer','-Isource/integrated_native/renderer',
            '-nostartfiles','-Wl,--gc-sections,-e,native_particle_regression,-Ttext=0x00100000',*sources[:6],*objects,'-o',str(elf)]
        commands.append(command);subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_MEM_WRITE
    from unicorn.arm_const import UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);cpu.reg_write(UC_ARM_REG_FPEXC,1<<30)
    with elf.open('rb') as f:
        image=ELFFile(f);pages=set();segments=[s for s in image.iter_segments() if s['p_type']=='PT_LOAD' and s['p_memsz']]
        for segment in segments:
            for page in range(segment['p_vaddr']&~4095,(segment['p_vaddr']+segment['p_memsz']+4095)&~4095,4096):pages.add(page)
        for page in sorted(pages):cpu.mem_map(page,4096)
        for segment in segments:cpu.mem_write(segment['p_vaddr'],segment.data())
        entry=image['e_entry']
        interval=image.get_section_by_name('.symtab').get_symbol_by_name('particle_native_interval')[0]['st_value']
    cpu.mem_map(0x04000000,4096);cpu.mem_map(0x08000000,0x10000);cpu.mem_map(0x09000000,4096)
    graphics_writes=[]
    def write(uc,access,address,size,value,user):
        if int.from_bytes(uc.mem_read(interval,4),'little'):
            graphics_writes.append(dict(address=address,size=size,pc=uc.reg_read(UC_ARM_REG_PC)))
    cpu.hook_add(UC_HOOK_MEM_WRITE,write,begin=0x04000000,end=0x04000fff)
    cpu.reg_write(UC_ARM_REG_SP,0x0800fff0);cpu.reg_write(UC_ARM_REG_LR,0x09000000)
    cpu.emu_start(entry,0x09000000,count=25_000_000)
    line=cpu.reg_read(UC_ARM_REG_R0);returned=cpu.reg_read(UC_ARM_REG_PC)==0x09000000
    report=dict(scope='2048 authored32-bit primary/child particle cases against original poses, polygon counters, opacity and source UV exponents; lifetime/flush/inactive contracts',
        elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),sources_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},
        headers_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('source/native_renderer','source/integrated_native/renderer') for p in (ROOT/folder).glob('*.hpp')},
        failure_source_line=line,returned=returned,native_graphics_writes=graphics_writes,passed=line==0 and returned and not graphics_writes,
        original_visuals=False,physical_hardware=False,commands=commands)
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
