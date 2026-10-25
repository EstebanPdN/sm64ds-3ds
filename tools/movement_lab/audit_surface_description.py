#!/usr/bin/env python3
"""Verify stable-surface descriptions against a verified saved-prism fixture."""
from pathlib import Path
import argparse,hashlib,json,subprocess,time
ROOT=Path(__file__).resolve().parents[2];CORE=ROOT/'build/movement-lab/core'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--replay-report',type=Path,default=ROOT/'build/movement-adapter/captured-ground-gate-latest.json');a=p.parse_args()
    replay=json.loads(a.replay_report.read_text());assert replay['status']=='passed' and replay['executable_text_byte_exact']
    fixture=Path(replay['directory'])/'wall-records.bin';assert sha(fixture)==replay['wall_records_sha256']
    for name,digest in replay['source_sha256'].items():assert sha(ROOT/name)==digest,name
    out=ROOT/'build/movement-adapter'/('surface-description-'+str(time.time_ns()));out.mkdir(parents=True)
    commands=[]
    def run(command):
        commands.append(command);r=subprocess.run(command,capture_output=True,text=True)
        with (out/'run.log').open('a') as f:f.write(r.stdout+r.stderr)
        r.check_returncode();return r.stdout.strip()
    includes=['-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(CORE/'source/src'),'-I'+str(CORE/'source/src/decomp/include')]
    test=ROOT/'tests/movement_lab/test_surface_description.cpp'
    run(['clang++','-std=c++17','-O1','-g','-ffp-contract=off','-fsanitize=address,undefined','-fno-omit-frame-pointer',*includes,str(test),'-o',str(out/'test')])
    output=run([str(out/'test'),str(fixture)])
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-Os','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp',*includes,'-c',str(test),'-o',str(out/'test-arm.o')])
    sources=[Path(__file__),test]+[ROOT/'source/integrated_native/movement'/name for name in ['surface_description.hpp','world_triangle.hpp','kcl_prism.hpp','material_profile.hpp','stable_surfaces.hpp','ds_world_query.h']]+[CORE/'source/src/libsm64.h',CORE/'source/src/decomp/include/surface_terrains.h']
    report={'status':'passed','output':output,'saved_fixture_sha256':sha(fixture),'replay_report_sha256':sha(a.replay_report),'source_sha256':{str(s.relative_to(ROOT)):sha(s) for s in sources},'directory':str(out),'limits':'Captured ordinary-material surface description and explicit rejection states only. Controlled moving descriptor requires a stable handle but does not implement its displacement/lifetime. DS hazards, room/progression, actual platform behavior and main-game integration remain pending. No physical gameplay/FPS acceptance.'}
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(ROOT/'build/movement-adapter/surface-description-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':main()
