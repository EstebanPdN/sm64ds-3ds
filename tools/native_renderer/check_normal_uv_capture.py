#!/usr/bin/env python3
"""Check completed PICA output against independent integer UV/texel results."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inspect(path,cases,mode):
    data=path.read_bytes()
    if len(data)!=288000:raise ValueError('Expected complete portrait BGR8 target')
    checked=mismatch=worst=0;examples=[]
    def pixel(x,y,expected):
        nonlocal checked,mismatch,worst
        at=(x*240+239-y)*3;actual=tuple(reversed(data[at:at+3]));error=max(abs(a-b) for a,b in zip(actual,expected))
        checked+=1;worst=max(worst,error)
        if error>1:
            mismatch+=1
            if len(examples)<8:examples.append(dict(x=x,y=y,expected=expected,actual=actual))
    for i,c in enumerate(cases):
        left,top=8+(i%16)*24,8+(i//16)*14
        for y in range(top+2,top+8):
            for x in range(left+2,left+18):pixel(x,y,c['expected'][mode])
    for y in range(226,234):
        for x in range(10,390):pixel(x,y,(68,68,0))
    checksum=2166136261
    for v in data:checksum=((checksum^v)*16777619)&0xffffffff
    checksums=re.findall(r'^readback='+re.escape(path.stem)+r' fnv1a=([0-9a-f]{8})$',(path.parent/'log.txt').read_text(),re.M)
    matches=len(checksums)==1 and int(checksums[0],16)==checksum
    return dict(file=path.name,sha256=hashlib.sha256(data).hexdigest(),checked=checked,mismatched=mismatch,
        worst_channel_error=worst,source_checksum_matches=matches,examples=examples,passed=mismatch==0 and matches)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--session',type=Path,required=True)
    parser.add_argument('--cases',type=Path,required=True);args=parser.parse_args()
    cases=json.loads(args.cases.read_text())['cases'];sd=args.session/'user/sdmc/sm64ds'
    images=[inspect(sd/f'normal-{mode}.bgr',cases,mode) for mode in ('clamp','repeat','mirror')]
    report=dict(scope='Native normal UV dot/latched offset, signed wrap, samplers, normal32/colour24 and fixed/float restoration',
        shader_cases=len(cases),cases_sha256=hashlib.sha256(args.cases.read_bytes()).hexdigest(),
        integer_oracle='Wide integer signed10 normal dot, arithmetic shift21, material/latched offset, signed16 reduction; gradient nearest addressing',
        full_game=False,physical_hardware_validated=False,tolerance=1,fixture_outer_edge_exclusion_pixels=2,
        images=images,passed=all(r['passed'] for r in images))
    (args.session/'native-normal-uv-image-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
