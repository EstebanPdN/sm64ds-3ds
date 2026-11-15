#!/usr/bin/env python3
"""Build and execute the real32-bit menu sprite seam on authored ARM objects.

No ROM, GPU emulation, source packets or hardware FPS claims. Requires the
project devkitARM and Python Unicorn/pyelftools development dependencies.
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
    parser.add_argument('--compiler',type=Path);args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to((ROOT/'build').resolve()):parser.error('Keep regression artifacts under build/')
    if output.exists():parser.error('Use a new output directory to preserve earlier evidence')
    compiler=args.compiler or (Path(os.environ['DEVKITARM'])/'bin/arm-none-eabi-g++' if os.getenv('DEVKITARM') else shutil.which('arm-none-eabi-g++'))
    if not compiler or not Path(compiler).is_file():parser.error('Set DEVKITARM or --compiler to the detected project SDK')
    output.mkdir(parents=True)
    sources=['tests/native_renderer/menu_sprite_arm.cpp','source/integrated_native/renderer/menu_sprite_hooks.cpp',
        'source/integrated_native/renderer/model_components_hooks.cpp','source/native_renderer/model_catalog.cpp',
        'source/native_renderer/native_quads.cpp','source/native_renderer/game_model_pose.cpp','source/native_renderer/contracts.cpp']
    elf=output/'menu-sprite.elf';command=[str(compiler),'-std=c++17','-Os','-mword-relocations','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard',
        '-fno-exceptions','-fno-rtti','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-D__3DS__=1','-DSM64DS_FRESH_NATIVE_GAME=1','-DSM64DS_PLATFORM_PC=1',
        '-Isource/native_renderer','-Isource/integrated_native/renderer','-nostartfiles','-Wl,--gc-sections,-e,native_menu_sprite_regression,-Ttext=0x00100000',
        '-Ithird_party/sm64ds-3ds/sm64ds-decomp/include',
        *sources,'-o',str(elf)]
    with (output/'build.log').open('w') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
    from unicorn.arm_const import UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);cpu.reg_write(UC_ARM_REG_FPEXC,1<<30)
    with elf.open('rb') as f:
        image=ELFFile(f);pages=set();segments=[s for s in image.iter_segments() if s['p_type']=='PT_LOAD' and s['p_memsz']]
        for s in segments:
            for p in range(s['p_vaddr']&~4095,(s['p_vaddr']+s['p_memsz']+4095)&~4095,4096):pages.add(p)
        for p in sorted(pages):cpu.mem_map(p,4096)
        for s in segments:cpu.mem_write(s['p_vaddr'],s.data())
        entry=image['e_entry']
    cpu.mem_map(0x08000000,0x10000);cpu.mem_map(0x09000000,4096);cpu.reg_write(UC_ARM_REG_SP,0x0800fff0);cpu.reg_write(UC_ARM_REG_LR,0x09000000)
    cpu.emu_start(entry,0x09000000,count=2_000_000);line=cpu.reg_read(UC_ARM_REG_R0);returned=cpu.reg_read(UC_ARM_REG_PC)==0x09000000
    report=dict(scope='Authored original32-bit menu sprite ABI; main/shadow poses, alpha-mask state, source-file release, language rebind, descriptor destructor, inactive forwarding',
        elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),sources_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},
        failure_source_line=line,returned=returned,passed=line==0 and returned,original_visuals=False,physical_hardware=False,command=command)
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
