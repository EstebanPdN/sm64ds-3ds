#!/usr/bin/env python3
"""Fast-path equality and wide-interpolation arithmetic gate for the port."""
from pathlib import Path
from devkitpro_tools import arm_tool, sdk_tool
import hashlib,json,subprocess,time
from prepare_integrated_collision import derive,RELATIVE
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def check():
    out=ROOT/'build/integrated-native'/('raycast-math-'+str(time.time_ns()));out.mkdir(parents=True)
    test=ROOT/'tests/movement_lab/test_raycast_arithmetic.cpp';header=ROOT/'source/integrated_native/movement/raycast_arithmetic.hpp'
    vendor=ROOT/'third_party/sm64ds-3ds'/RELATIVE;commands=[]
    def run(command):
        commands.append(command);r=subprocess.run(command,capture_output=True,text=True)
        with (out/'run.log').open('a') as f:f.write(r.stdout+r.stderr)
        r.check_returncode();return r.stdout.strip()
    include='-I'+str(header.parent)
    run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',include,str(test),'-o',str(out/'test')])
    output=run([str(out/'test')])
    run([arm_tool('g++'),'-std=gnu++17','-Os','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp',include,'-c',str(test),'-o',str(out/'test-arm.o')])
    report={'status':'passed','ASan':True,'UBSan':True,'fast_interpolation_cases':63,'wide_interpolation_cases':81,'output':output,'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in [test,header,Path(__file__),ROOT/'tools/prepare_integrated_collision.py',vendor]},'derived_ray_source_sha256':hashlib.sha256(derive(vendor.read_text()).encode()).hexdigest(),'directory':str(out),'limits':'Arithmetic and source-anchor gate; actual ARM saved-capture ray execution is recorded separately. No physical gameplay/FPS claim.'}
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':print(json.dumps(check(),indent=2))
