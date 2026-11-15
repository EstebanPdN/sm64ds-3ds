#!/usr/bin/env python3
"""Analytic independent oracle for native paired LCD RGB8 composition."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inspect(path):
    match=re.fullmatch(r'compositor-(top|bottom)-(first|last|solid|recovered)',path.stem)
    if not match:raise ValueError('Unknown LCD composition scene')
    side,phase=match.groups();width=400 if side=='top' else 320
    raw=path.read_bytes()
    if len(raw)!=width*240*3:raise ValueError('Incomplete portrait RGB8 LCD target')
    checked=wrong=worst=0;examples=[]
    for y in range(240):
        if phase!='solid' and any(abs(y-edge)<=2 for edge in (20,60,100,116,120,140,148,156,200)):continue
        for x in range(width):
            if phase!='solid' and any(abs(x-edge)<=2 for edge in (20,36,44,60,120,160,200,224,232,248,280,288,328)):continue
            if phase=='solid':expected=(255,255,255) if side=='top' else (0,0,0)
            elif side=='top':
                expected=(255,0,0) if 20<x<120 and 20<y<100 else (24,32,40)
                if 60<x<160 and 60<y<140:expected=tuple((c*127+g*128)//255 for c,g in zip(expected,(0,255,0)))
                if 200<x<232 and 116<y<148:expected=(140,16,148)
                if 248<x<280 and 116<y<148:expected=(12,144,148)
            else:
                expected=(40,48,56)
                if 20<x<120 and 20<y<100:expected=(0,0,255)
                if 224<x<288 and (20<y<100 or 120<y<200):expected=(0,0,255)
                if 160<x<224 and 120<y<200:expected=(255,255,0)
                if 20<x<36 and 140<y<156:expected=(255,0,255)
                if 44<x<60 and 140<y<156:expected=(0,255,255)
                if phase=='last':expected=tuple(c*127//255 for c in expected)
            at=(x*240+239-y)*3;actual=tuple(reversed(raw[at:at+3]));error=max(abs(a-b) for a,b in zip(actual,expected))
            checked+=1;worst=max(worst,error)
            if error>1:
                wrong+=1
                if len(examples)<12:examples.append(dict(x=x,y=y,expected=expected,actual=actual))
    checksum=2166136261
    for value in raw:checksum=((checksum^value)*16777619)&0xffffffff
    logged=re.findall(r'^readback='+re.escape(path.stem)+r' fnv1a=([0-9a-f]{8}) width=(\d+)$',(path.parent/'log.txt').read_text(),re.M)
    verified=len(logged)==1 and int(logged[0][0],16)==checksum and int(logged[0][1])==width
    return dict(file=path.name,width=width,sha256=hashlib.sha256(raw).hexdigest(),checked=checked,mismatched=wrong,
                worst_channel_error=worst,source_checksum_matches=verified,examples=examples,passed=not wrong and verified)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--session',type=Path,required=True);args=parser.parse_args()
    sd=args.session/'user/sdmc/sm64ds';images=[inspect(sd/f'compositor-{side}-{phase}.bgr')
        for phase in ('first','last','solid','recovered') for side in ('top','bottom')]
    run=json.loads((args.session/'result/run.json').read_text())
    report=dict(scope='ROM-free paired native RGB8 LCD outputs, logical layers, alpha, batched mesh, tone, refusal and recovery',
        original_scene=False,physical_hardware=False,performance_admissible=False,analytic_tolerance=1,images=images,
        passed=run['result']=='PASS' and not run['unmapped_access_count'] and run['owned_process_group_stopped'] and all(i['passed'] for i in images))
    (args.session/'native-compositor-image-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
