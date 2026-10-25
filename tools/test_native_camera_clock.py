#!/usr/bin/env python3
"""Host sanitizer verification of new publisher's logical60/1 timestamp contract.

Unready camera only. All original math/lifecycle test imports abort if called;
this does not fake successful original initialization, service or collision.
"""
import hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/native-camera-plan/clock-tests'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=['source/game_port/native_camera.cpp','source/core/camera_orbit.cpp','tests/native_camera_clock.cpp']
    exe=OUT/'camera-clock-host'
    command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-Iinclude',*sources,'-o',str(exe)]
    build=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
    (OUT/'compile.log').write_text(build.stdout+build.stderr);build.check_returncode()
    execution=subprocess.run([str(exe)],cwd=ROOT,text=True,capture_output=True)
    (OUT/'run.log').write_text(execution.stdout+execution.stderr);execution.check_returncode()
    assert execution.stdout.startswith('PASS:')
    sha=lambda p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
    report={'status':'pass','scope':'Host ASan/UBSan unready publisher clock; actual Camera world/lifecycle verified separately on ARM11',
        'logical_vblank_rate':[60,1],'duration_checks':[{'vblanks_per_tick':1,'ticks':120,'sum_microseconds':2000000},
                                                   {'vblanks_per_tick':2,'ticks':30,'sum_microseconds':1000000}],
        'other_checks':['first tick0us','duplicate sample/time immutability','stale does not rewind','short pause stamps omitted body ticks',
                        'long gap resets residue without catch-up','backwards VBlank no duration','6VBlank boundary100000us','startup reset'],
        'original_imports_nonreturning_abort':True,'compile_command':command,
        'source_sha256':{p:sha(p) for p in [*sources,'include/sm64ds/native_camera.hpp','tools/test_native_camera_clock.py']},
        'output':execution.stdout.strip()}
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
