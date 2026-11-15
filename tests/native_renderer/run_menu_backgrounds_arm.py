"""ROM-free native source background bridge against original ARM crop/palette/select."""
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
    native_sources=['tests/native_renderer/menu_backgrounds_arm.cpp','source/integrated_native/renderer/menu_backgrounds.cpp','source/native_renderer/canvas_windows.cpp','source/native_renderer/screen_scene.cpp','source/native_renderer/canvas_catalog.cpp','source/native_renderer/contracts.cpp','source/native_renderer/image_layer.cpp']
    originals=[(original,'func_ov007_020c0638','reference_crop'),('third_party/sm64ds-3ds/sm64ds-decomp/src/func_ov007_020c0288.c','func_ov007_020c0288','reference_palette'),('third_party/sm64ds-3ds/sm64ds-decomp/src/func_ov007_020b1390.c','func_ov007_020b1390','original_select')]
    sources=native_sources+[p for p,_,_ in originals]
    common=['-Os','-mword-relocations','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-ffunction-sections','-fdata-sections']
    commands=[];objects=[];elf=out/'background-bridge.elf'
    for i,(path,symbol,alias) in enumerate(originals):
        obj=out/f'original-{i}.o';objects.append(str(obj))
        commands.append([str(sdk/'arm-none-eabi-gcc'),'-std=c99',*common,'-fwrapv','-Ithird_party/sm64ds-3ds/sm64ds-decomp/include','-D'+symbol+'='+alias,'-c',path,'-o',str(obj)])
    commands.append([str(sdk/'arm-none-eabi-g++'),'-std=c++17',*common,'-fno-exceptions','-fno-rtti','-Wall','-Wextra','-Werror','-DSM64DS_FRESH_NATIVE_GAME=1','-DSM64DS_PLATFORM_PC=1','-Isource/native_renderer','-Isource/integrated_native/renderer','-Ithird_party/sm64ds-3ds/sm64ds-decomp/include','-nostartfiles','-Wl,--gc-sections,-e,native_background_bridge_regression,-Ttext=0x00100000',*native_sources,*objects,'-o',str(elf)])
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
        interval=symbols.get_symbol_by_name('background_native_interval')[0]['st_value']
    cpu.mem_map(0x04000000,8192);cpu.mem_map(0x08000000,65536);cpu.mem_map(0x09000000,4096);writes=[]
    def write(uc,access,address,size,value,user):writes.append(dict(address=address,size=size,pc=uc.reg_read(UC_ARM_REG_PC)))
    cpu.hook_add(UC_HOOK_MEM_WRITE,write,begin=0x04000000,end=0x04001fff)
    cpu.reg_write(UC_ARM_REG_SP,0x0800fff0);cpu.reg_write(UC_ARM_REG_LR,0x09000000)
    cpu.emu_start(entry,0x09000000,count=300_000_000)
    line=cpu.reg_read(UC_ARM_REG_R0);returned=cpu.reg_read(UC_ARM_REG_PC)==0x09000000
    report=dict(scope='2048 native adapter crops against original ARM; original palette writes/selection/cursor state; skipped copies, lifetime, inactive forwarding and unknown-mutation refusal; no physical proof',passed=not line and returned and not writes,returned=returned,failure_source_line=line,native_graphics_writes=writes,crop_cases=2048,palette_selection_cases=6,elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),sources_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},headers_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ['source/native_renderer/canvas_windows.hpp','source/native_renderer/image_layer.hpp','source/integrated_native/renderer/menu_backgrounds.hpp']},commands=commands)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='commands'},indent=2));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
