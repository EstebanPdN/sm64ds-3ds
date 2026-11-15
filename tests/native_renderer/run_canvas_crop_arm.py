"""ROM-free original ARM map cropping versus native canvas windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():parser.error('Use a new private directory under build/')
    out.mkdir(parents=True);sdk=Path(os.environ['DEVKITARM'])/'bin'
    original='third_party/sm64ds-3ds/sm64ds-decomp/src/func_ov007_020c0638.c'
    sources=['tests/native_renderer/canvas_crop_arm.cpp','source/native_renderer/canvas_windows.cpp',original]
    common=['-Os','-mword-relocations','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-ffunction-sections','-fdata-sections']
    commands=[];obj=out/'original-crop.o';elf=out/'canvas-crop.elf'
    commands.append([str(sdk/'arm-none-eabi-gcc'),'-std=c99',*common,'-fwrapv','-Ithird_party/sm64ds-3ds/sm64ds-decomp/include','-Dfunc_ov007_020c0638=reference_crop','-c',original,'-o',str(obj)])
    commands.append([str(sdk/'arm-none-eabi-g++'),'-std=c++17',*common,'-fno-exceptions','-fno-rtti','-Wall','-Wextra','-Werror','-Isource/native_renderer','-nostartfiles','-Wl,--gc-sections,-e,native_canvas_crop_regression,-Ttext=0x00100000',*sources[:2],str(obj),'-o',str(elf)])
    with (out/'build.log').open('w') as log:
        for cmd in commands:subprocess.run(cmd,cwd=ROOT,check=True,stdout=log,stderr=subprocess.STDOUT)
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_MEM_WRITE,UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);cpu.reg_write(UC_ARM_REG_FPEXC,1<<30)
    with elf.open('rb') as f:
        image=ELFFile(f);pages=set();segments=[s for s in image.iter_segments() if s['p_type']=='PT_LOAD' and s['p_memsz']]
        for s in segments:pages.update(range(s['p_vaddr']&~4095,(s['p_vaddr']+s['p_memsz']+4095)&~4095,4096))
        for page in sorted(pages):cpu.mem_map(page,4096)
        for s in segments:cpu.mem_write(s['p_vaddr'],s.data())
        entry=image['e_entry']
        symbols=image.get_section_by_name('.symtab')
        interval=symbols.get_symbol_by_name('canvas_native_interval')[0]['st_value']
        divide=symbols.get_symbol_by_name('__aeabi_idivmod')[0]['st_value']
    cpu.mem_map(0x04000000,8192);cpu.mem_map(0x08000000,65536);cpu.mem_map(0x09000000,4096);writes=[]
    def write(uc,access,address,size,value,user):writes.append(dict(address=address,size=size,pc=uc.reg_read(UC_ARM_REG_PC)))
    cpu.hook_add(UC_HOOK_MEM_WRITE,write,begin=0x04000000,end=0x04001fff)
    divisions=[]
    def division(uc,address,size,user):
        if int.from_bytes(uc.mem_read(interval,4),'little'):divisions.append(address)
    cpu.hook_add(UC_HOOK_CODE,division,begin=divide,end=divide)
    cpu.reg_write(UC_ARM_REG_SP,0x0800fff0);cpu.reg_write(UC_ARM_REG_LR,0x09000000)
    cpu.emu_start(entry,0x09000000,count=200_000_000)
    line=cpu.reg_read(UC_ARM_REG_R0);returned=cpu.reg_read(UC_ARM_REG_PC)==0x09000000
    report=dict(scope='4096 original ARM whole-map crop cases;196608 viewport probes including sub-tile remainder,scroll,negative shifts and seams; no PICA pixel or physical proof',passed=not line and returned and not writes and not divisions,returned=returned,failure_source_line=line,native_graphics_writes=writes,power2_native_divisions=len(divisions),cases=4096,viewport_probes=4096*48,elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),sources_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},headers_sha256={'source/native_renderer/canvas_windows.hpp':hashlib.sha256((ROOT/'source/native_renderer/canvas_windows.hpp').read_bytes()).hexdigest()},commands=commands)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='commands'},indent=2));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
