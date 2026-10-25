#!/usr/bin/env python3
"""Isolated, bounded emulator run. This does not measure physical hardware."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--azahar',type=pathlib.Path,required=True)
parser.add_argument('--model',choices=['old','new'],required=True)
parser.add_argument('--seconds',type=int,default=45)
parser.add_argument('--name',required=True)
parser.add_argument('--stereo',action='store_true')
args=parser.parse_args()
if not 1<=args.seconds<=300 or not args.name.replace('-','').isalnum():parser.error('Invalid run name or duration')
root=pathlib.Path(__file__).resolve().parents[1]
runtime=root/'build'/args.name
user=runtime/'xdg-data/azahar-emu';config=runtime/'xdg-config/azahar-emu';sd=user/'sdmc/3ds/sm64ds'
for directory in (config,sd,runtime/'xdg-cache'):directory.mkdir(parents=True,exist_ok=True)
(config/'qt-config.ini').write_text(
    '[System]\nis_new_3ds='+str(args.model=='new').lower()+'\nis_new_3ds\\default=false\n'
    '[UI]\nfirstStart=false\ncalloutFlags=4294967295\n'
    '[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n'
    '[Layout]\nrender_3d='+('1' if args.stereo else '0')+'\nrender_3d\\default=false\nfactor_3d='+('75' if args.stereo else '0')+'\nfactor_3d\\default=false\n'
    '[WebService]\nenable_telemetry=false\n')
rom=root/'Super Mario 64 DS.nds'
link=sd/'rom.nds'
if not link.exists():link.symlink_to(rom)
image=root/'build/3ds/source/ctr/sm64ds-platform.3dsx'
before=hashlib.sha256(rom.read_bytes()).hexdigest()
start=time.monotonic()
with (runtime/'process.log').open('w') as log:
    p=subprocess.Popen([str(args.azahar.resolve()),'-w',str(image)],cwd=runtime,
        env={**os.environ,'XDG_DATA_HOME':str(runtime/'xdg-data'),'XDG_CONFIG_HOME':str(runtime/'xdg-config'),'XDG_CACHE_HOME':str(runtime/'xdg-cache')},
        stdout=log,stderr=subprocess.STDOUT)
    (runtime/'pid').write_text(str(p.pid))
    print(f'Running PID {p.pid} in {runtime}',flush=True)
    timed_out=False
    try:p.wait(timeout=args.seconds)
    except subprocess.TimeoutExpired:
        timed_out=True;p.terminate()
        try:p.wait(timeout=10)
        except subprocess.TimeoutExpired:p.kill();p.wait()
report={'emulator':str(args.azahar),'model':args.model,'stereo_requested':args.stereo,
        'duration':time.monotonic()-start,'bounded_stop':timed_out,'returncode':p.returncode,
        'image_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),
        'rom_unchanged':hashlib.sha256(rom.read_bytes()).hexdigest()==before}
logfile=sd/'platform.log'
report['platform_log']=logfile.read_text() if logfile.exists() else None
(runtime/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
