#!/usr/bin/env python3
"""Sanitizer lifetime/churn gate and ARM-size receipt for contact storage."""
from pathlib import Path
import hashlib,json,subprocess,time
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    out=ROOT/'build/movement-adapter'/('stable-surfaces-'+str(time.time_ns()));out.mkdir(parents=True)
    includes=['-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(ROOT/'third_party/libsm64-motion/src')]
    commands=[]
    def run(command):
        commands.append(command);r=subprocess.run(command,capture_output=True,text=True)
        with (out/'run.log').open('a') as f:f.write(r.stdout+r.stderr)
        r.check_returncode();return r.stdout.strip()
    test=ROOT/'tests/movement_lab/test_stable_surfaces.cpp';binary=out/'test'
    run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*includes,str(test),'-o',str(binary)])
    output=run([str(binary)])
    size_source=out/'size.cpp';size_source.write_text('#include "stable_surfaces.hpp"\nextern "C" const unsigned contact_cache_arm_bytes=sizeof(movement_geometry::StableSurfaces<256>);\n')
    obj=out/'size.o'
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-Os','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-D__3DS__',*includes,'-c',str(size_source),'-o',str(obj)])
    with obj.open('rb') as f:
        elf=ELFFile(f);symbol=elf.get_section_by_name('.symtab').get_symbol_by_name('contact_cache_arm_bytes')[0]
        section=elf.get_section(symbol['st_shndx']);offset=symbol['st_value']-section['sh_addr']
        arm_bytes=int.from_bytes(section.data()[offset:offset+4],'little')
    assert 0<arm_bytes<64*1024
    sources=[test,Path(__file__),ROOT/'source/integrated_native/movement/stable_surfaces.hpp',ROOT/'third_party/libsm64-motion/src/libsm64.h']
    report={'status':'passed','churn_ticks':10000,'new_contacts_per_churn_tick':5,'retained_contacts':3,'sanitizers':['address','undefined'],'arm_256_slot_bytes':arm_bytes,'capacity_exhaustion_preserves_issued_and_retained_contacts':True,'foreign_pins_rejected':True,'copy_and_move_disabled':True,'output':output,'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in sources},'test_binary_sha256':sha(binary),'arm_size_object_sha256':sha(obj),'directory':str(out),'limits':'Contact-storage component only. The production controller must enumerate every retained contact before begin_tick; no actor integration, wall response, platform riding, hardware timing or FPS proof.'}
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(ROOT/'build/movement-adapter/stable-surfaces-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':main()
