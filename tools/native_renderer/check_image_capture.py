#!/usr/bin/env python3
"""Analytic native image-only lab readback checks; no original scene claim."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inspect(path):
    raw=path.read_bytes()
    if len(raw)!=288000:raise ValueError('Incomplete portrait BGR8 target')
    phase=path.stem.removeprefix('native-images-')
    if phase not in ('0','1','89','only'):raise ValueError('Unknown captured scene')
    checked=wrong=worst=0;examples=[]
    for y in range(240):
        if any(abs(y-edge)<=2 for edge in (20,100,130,210)):continue
        for x in range(400):
            if any(abs(x-edge)<=2 for edge in (20,100,120,140,180,200,240,260,280,360)):continue
            expected=(24,32,40)
            if 20<y<100:
                if 20<x<120:expected=(0,255,0) if phase=='only' else (255,0,0)
                if phase in ('1','89'):
                    if 140<x<240:expected=(0,0,255)
                    if 260<x<360:expected=(0,255,0)
            if phase in ('1','89') and 130<y<210:
                if 100<x<180 or 280<x<360:expected=(0,0,255)
                if 200<x<280:expected=(255,255,0)
            at=(x*240+239-y)*3;actual=tuple(reversed(raw[at:at+3]));difference=max(abs(a-b) for a,b in zip(expected,actual))
            checked+=1;worst=max(worst,difference)
            if difference:
                wrong+=1
                if len(examples)<12:examples.append(dict(x=x,y=y,expected=expected,actual=actual))
    checksum=2166136261
    for value in raw:checksum=((checksum^value)*16777619)&0xffffffff
    logged=re.findall(r'^readback='+re.escape(path.stem)+r' fnv1a=([0-9a-f]{8})$',(path.parent/'log.txt').read_text(),re.M)
    matches=len(logged)==1 and int(logged[0],16)==checksum
    return dict(file=path.name,sha256=hashlib.sha256(raw).hexdigest(),checked=checked,mismatched=wrong,
                worst_channel_error=worst,examples=examples,source_checksum_matches=matches,passed=not wrong and matches)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--session',type=Path,required=True);args=parser.parse_args()
    sd=args.session/'user/sdmc/sm64ds'
    images=[inspect(sd/f'native-images-{phase}.bgr') for phase in ('0','1','89','only')]
    run=json.loads((args.session/'result/run.json').read_text())
    report=dict(scope='ROM-free image residency, rollback, two resource namespaces and ordered scene',
        original_scene=False,physical_hardware=False,performance_admissible=False,images=images,
        passed=run['result']=='PASS' and not run['unmapped_access_count'] and run['owned_process_group_stopped'] and all(i['passed'] for i in images))
    (args.session/'native-image-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
