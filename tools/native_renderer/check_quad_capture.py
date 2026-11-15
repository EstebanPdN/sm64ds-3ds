#!/usr/bin/env python3
"""Check ordered model/quad rendering and the short/general shader comparison.

This ROM-free lab is not acceptance of the original game menu. Colours/order
are checked analytically; every pixel of the gradient is also compared between
the two actual PICA entry points. Guest timing is not hardware performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inspect(path,alpha,controls=False):
    data=path.read_bytes()
    if len(data)!=288000:raise ValueError('Expected complete portrait BGR8 target')
    checked=mismatched=worst=0;examples=[]
    for y in range(240):
        if any(abs(y-edge)<=2 for edge in [20,40,60,80,100,130,210]):continue
        for x in range(400):
            if any(abs(x-edge)<=2 for edge in [20,60,100,140,180,220,260,300,340,380]):continue
            if not controls and 220<x<380 and 20<y<100:continue # gradient is checked over its entire image by GPU A/B below
            expected=(24,32,40)
            if controls:
                if 20<y<100 and (20<x<180 or 220<x<380):expected=(255,0,0) if x%200<100 else (0,255,0)
            elif 20<x<180 and 20<y<100:
                expected=(0,0,255) if 60<x<140 and 40<y<80 else (255,0,0)
                if y>60:expected=tuple((a*(255-alpha)+b*alpha)//255 for a,b in zip(expected,(0,255,0)))
            if not controls:
                if 20<x<180 and 130<y<210:expected=(0,255,255)
                if 260<x<300 and 130<y<210:expected=(0,255,0)
                if 300<x<340 and 130<y<210:expected=(255,0,0)
                if 340<x<380 and 130<y<210:expected=(0,255,0)
            at=(x*240+239-y)*3;actual=tuple(reversed(data[at:at+3]));error=max(abs(a-b) for a,b in zip(actual,expected))
            checked+=1;worst=max(worst,error)
            if error>1:
                mismatched+=1
                if len(examples)<12:examples.append(dict(x=x,y=y,expected=expected,actual=actual))
    checksum=2166136261
    for value in data:checksum=((checksum^value)*16777619)&0xffffffff
    source=re.findall(r'^readback='+re.escape(path.stem)+r' fnv1a=([0-9a-f]{8})$',(path.parent/'log.txt').read_text(),re.M)
    matches=len(source)==1 and int(source[0],16)==checksum
    return dict(file=path.name,sha256=hashlib.sha256(data).hexdigest(),checked=checked,mismatched=mismatched,
        worst_channel_error=worst,source_checksum_matches=matches,examples=examples,passed=not mismatched and matches)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--session',type=Path,required=True);args=parser.parse_args()
    sd=args.session/'user/sdmc/sm64ds';images=[];comparisons=[]
    for phase,alpha in [('first',128),('last',64)]:
        for mode in ['affine','general']:images.append(inspect(sd/f'quads-{mode}-{phase}.bgr',alpha))
        a=(sd/f'quads-affine-{phase}.bgr').read_bytes();b=(sd/f'quads-general-{phase}.bgr').read_bytes()
        different=sum(a[i:i+3]!=b[i:i+3] for i in range(0,len(a),3))
        comparisons.append(dict(phase=phase,checked_pixels=96000,mismatched_pixels=different,exact=a==b))
    images.append(inspect(sd/'quads-mode-controls.bgr',255,True))
    run=json.loads((args.session/'result/run.json').read_text())
    report=dict(scope='ROM-free ordered quad/model scene, gradients, dynamic alpha, adjacent batching, persistent vertex stream and shader A/B',
        original_scene=False,physical_hardware=False,performance_admissible=False,analytic_tolerance=1,
        images=images,shader_comparisons=comparisons,passed=run['result']=='PASS' and not run['unmapped_access_count'] and
        run['owned_process_group_stopped'] and all(i['passed'] for i in images) and all(i['exact'] for i in comparisons))
    (args.session/'native-quad-image-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
