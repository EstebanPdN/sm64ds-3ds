#!/usr/bin/env python3
"""Isolated Azahar check of recorded Player graphics, explicitly not gameplay."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--azahar',type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=90)
    args=parser.parse_args()
    if not 1<=args.seconds<=120:parser.error('Duration must be 1..120 seconds')
    root=Path(__file__).resolve().parents[1]
    runtime=root/'build/player-render-azahar'
    config=runtime/'config/azahar-emu';data=runtime/'data/azahar-emu'
    sd=data/'sdmc/3ds/sm64ds/render-check'
    for path in (config,sd,runtime/'cache'):path.mkdir(parents=True,exist_ok=True)
    (config/'qt-config.ini').write_text(
        '[System]\nis_new_3ds=false\nis_new_3ds\\default=false\n'
        '[UI]\nfirstStart=false\ncalloutFlags=4294967295\n'
        '[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n'
        '[Layout]\nrender_3d=1\nrender_3d\\default=false\nfactor_3d=75\nfactor_3d\\default=false\n'
        '[WebService]\nenable_telemetry=false\n')
    evidence={}
    for frame in (0,90,179):
        source=root/'build/player-render-diagnostic'/f'frame-{frame:03d}'
        target=sd/source.name;target.mkdir(exist_ok=True)
        for name in ('commands.bin','textures.bin','palettes.bin'):
            shutil.copyfile(source/name,target/name)
            evidence[str(Path(source.name)/name)]=hashlib.sha256((target/name).read_bytes()).hexdigest()
    logfile=sd/'result.log'
    if logfile.exists():logfile.rename(sd/f'result-{time.time_ns()}.log')
    image=runtime/'render-check.3dsx'
    # Freeze the exact input before launch. A concurrent build must not alter
    # the binary whose hash is recorded as the attempted emulator execution.
    shutil.copyfile(root/'build/3ds/source/ctr/sm64ds-render-check.3dsx',image)
    image_hash=hashlib.sha256(image.read_bytes()).hexdigest()
    env={**os.environ,'XDG_CONFIG_HOME':str(runtime/'config'),
         'XDG_DATA_HOME':str(runtime/'data'),'XDG_CACHE_HOME':str(runtime/'cache')}
    start=time.monotonic();bounded=False
    with (runtime/'process.log').open('w') as out:
        child=subprocess.Popen([str(args.azahar.resolve()),'-w',str(image)],cwd=runtime,env=env,
                               stdout=out,stderr=subprocess.STDOUT)
        (runtime/'pid').write_text(str(child.pid));print(f'Azahar PID {child.pid}: {runtime}',flush=True)
        try:child.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            bounded=True;child.terminate()
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:child.kill();child.wait()
    result={'scope':'Recorded Player graphics on emulated PICA; no gameplay',
        'model':'old','physical_hardware':False,'gameplay_executed':False,
        'image_sha256':image_hash,
        'input_sha256':evidence,'exit_code':child.returncode,'bounded_stop':bounded,
        'elapsed_seconds':time.monotonic()-start,
        'application_log':logfile.read_text() if logfile.exists() else None,
        'visual_verified':False}
    result['reached_gpu_submission']=bool(result['application_log'] and 'triangles=456' in result['application_log'])
    (runtime/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    return 0 if result['application_log'] and 'triangles=456' in result['application_log'] else 1

if __name__=='__main__':raise SystemExit(main())
