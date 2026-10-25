#!/usr/bin/env python3
"""Compare fractional wall narrow phase against the actual imported N64 path."""
from pathlib import Path
import hashlib,json,subprocess,time
ROOT=Path(__file__).resolve().parents[2];CORE=ROOT/'build/movement-lab/core'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    out=ROOT/'build/movement-adapter'/('wall-contact-'+str(time.time_ns()));out.mkdir(parents=True)
    manifest=json.loads((CORE/'host/sources.json').read_text())
    for name,digest in manifest.items():assert sha(Path(name))==digest,name
    commands=[]
    def run(command):
        commands.append(command);r=subprocess.run(command,capture_output=True,text=True)
        with (out/'run.log').open('a') as f:f.write(r.stdout+r.stderr)
        r.check_returncode();return r.stdout.strip()
    includes=['-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(CORE/'source/src'),'-I'+str(CORE/'source/src/decomp/include')]
    flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    defines=['-DGBI_FLOATS','-DVERSION_US','-DNO_SEGMENTED_MEMORY']
    sources=[ROOT/'tests/movement_lab/wall_contact_oracle.c',ROOT/'tests/movement_lab/solver_contacts_fixture.c',CORE/'source/src/decomp/engine/surface_collision.c',CORE/'source/src/load_surfaces.c']
    objects=[]
    for index,source in enumerate(sources):
        obj=out/(str(index)+'.o');objects.append(str(obj))
        run(['clang','-std=gnu11',*flags,*defines,*includes,'-Wno-incompatible-pointer-types','-c',str(source),'-o',str(obj)])
    test=ROOT/'tests/movement_lab/test_wall_contact.cpp'
    run(['clang++','-std=c++17',*flags,*includes,str(test),*objects,str(CORE/'host/libmotion64.a'),'-lm','-o',str(out/'test')])
    output=run([str(out/'test')])
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-Os','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp',*includes,'-fsyntax-only',str(test)])
    evidence=[Path(__file__),test,*sources]+[ROOT/'source/integrated_native/movement'/name for name in ['wall_contact.hpp','world_triangle.hpp','kcl_prism.hpp','ds_world_query.h']]
    report={'status':'passed','output':output,'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in evidence},'host_core_sha256':sha(CORE/'host/libmotion64.a'),'directory':str(out),'limits':'CPU narrow-phase admission/correction only. Test, actual imported collision TU and surface loader are ASan/UBSan instrumented; remaining archive members are not. Candidate enumeration, DS pass-through/material policy and moving-platform/gameplay integration remain pending. No Azahar/physical FPS proof.'}
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(ROOT/'build/movement-adapter/wall-contact-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':main()
