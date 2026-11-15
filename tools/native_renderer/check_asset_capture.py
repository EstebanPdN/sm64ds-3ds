#!/usr/bin/env python3
"""Compare the planar 32x32 resource preview with decoded source RGBA.

This oracle covers the first resource's XY quad, ordinary UVs, opaque/binary
alpha and explicit default material. It is not an original-game screenshot.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inspect(path,reference,offset,hidden=False,tint=False):
    data=path.read_bytes()
    if len(data)!=400*240*3 or len(reference)!=32*32*4:raise ValueError('Unexpected capture/reference size')
    checked=mismatches=worst=0;examples=[]
    for y in range(240):
        for x in range(400):
            local=x-offset
            if abs(local-220)<=1 or abs(local-380)<=1 or abs(y-120)<=1 or abs(y-216)<=1:continue
            expected=(24,32,40)
            if not hidden and 220<local<380 and 120<y<216:
                # Pixel centres, nearest filtering and source top-left UV.
                tx=(2*(local-220)+1)*32//320;ty=(2*(y-120)+1)*32//192
                at=(ty*32+tx)*4
                if reference[at+3] not in (0,255):raise ValueError('Oracle requires binary alpha')
                if reference[at+3]:expected=(reference[at],0,0) if tint else tuple(reference[at:at+3])
            at=(x*240+239-y)*3;actual=tuple(reversed(data[at:at+3]))
            error=max(abs(a-b) for a,b in zip(actual,expected));worst=max(worst,error);checked+=1
            if error>1:
                mismatches+=1
                if len(examples)<8:examples.append(dict(x=x,y=y,expected=expected,actual=actual))
    checksum=2166136261
    for value in data:checksum=((checksum^value)*16777619)&0xffffffff
    checksums=re.findall(r'^readback='+re.escape(path.stem)+r' fnv1a=([0-9a-f]{8})$',(path.parent/'log.txt').read_text(),re.M)
    source_matches=len(checksums)==1 and int(checksums[0],16)==checksum
    return dict(file=path.name,sha256=hashlib.sha256(data).hexdigest(),offset=offset,checked=checked,
        mismatched=mismatches,worst_channel_error=worst,source_checksum_matches=source_matches,
        examples=examples,passed=mismatches==0 and source_matches)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,required=True)
    parser.add_argument('--reference',type=Path,required=True)
    args=parser.parse_args();reference=args.reference.read_bytes();sd=args.session/'user/sdmc/sm64ds'
    results=[inspect(sd/(name+'.bgr'),reference,offset) for name,offset in
        (('asset-mono',0),('asset-left',-3),('asset-right',3))]
    log=(sd/'log.txt').read_text()
    producer='instance_source=prepared-bindings ' in log
    if producer:
        results.append(inspect(sd/'asset-hidden.bgr',reference,0,hidden=True))
        results.append(inspect(sd/'asset-tint.bgr',reference,-3,tint=True))
    report=dict(scope='First original resource through prepared native bank; fitted planar quad with explicit default material',
        full_game=False,original_actor_behaviour_validated=False,physical_hardware_validated=False,
        prepared_instance_producer_exercised=producer,
        source_rgba_sha256=hashlib.sha256(reference).hexdigest(),tolerance=1,ignored_outer_edge_radius_pixels=1,
        images=results,passed=all(r['passed'] for r in results))
    (args.session/'native-asset-image-check.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
