"""Prepared palette variants versus original ARM selection/crop + PPU RGB."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inspect(session,reference):
    run=json.loads((session/'result/run.json').read_text());directory=session/'user/sdmc/sm64ds';log=(directory/'log.txt').read_text()
    if run['result']!='PASS' or not run['owned_process_group_stopped'] or run['unmapped_access_count'] or run['global_settings_modified'] or not log.rstrip().endswith('PASS'):
        raise ValueError('Failed or unisolated canvas crop acquisition')
    report=json.loads((reference/'report.json').read_text())
    if 'scope=prepared-original-palette-variants originalSceneAccepted=0 physicalCapture=0' not in log or f"completed_frames={len(report['cases'])*10}" not in log:
        raise ValueError('Incomplete native crop scope/frames')
    checks=[]
    for case in report['cases']:
        id=case['case'];source=(reference/f'{id}.rgb').read_bytes()
        if len(source)!=256*192*3 or hashlib.sha256(source).hexdigest()!=case['original_ppu_rgb_sha256']:
            raise ValueError('Incomplete original ARM -> PPU reference')
        for side,width,left in [('top',400,40),('bottom',320,0)]:
            name=f'palette-{id}-{side}';raw=(directory/(name+'.bgr')).read_bytes()
            checksum=2166136261
            for value in raw:checksum=((checksum^value)*16777619)&0xffffffff
            if len(raw)!=width*240*3 or re.findall(r'^readback='+name+r' fnv1a=([a-f0-9]{8}) width=(\d+)$',log,re.M)!=[(f'{checksum:08x}',str(width))]:
                raise ValueError('Incomplete completed-target export/checksum')
            wrong=worst=0;examples=[]
            for y in range(240):
                sy=(2*y+1)*2//5
                for x in range(width):
                    expected=bytes(3)
                    if left<=x<left+320:
                        sx=(2*(x-left)+1)*2//5;at=(sy*256+sx)*3;expected=source[at:at+3][::-1]
                    at=(x*240+239-y)*3;actual=raw[at:at+3];error=max(abs(a-b) for a,b in zip(actual,expected));worst=max(worst,error)
                    if error:
                        wrong+=1
                        if len(examples)<8:examples.append(dict(x=x,y=y,actual=list(actual),expected=list(expected)))
            checks.append(dict(file=name+'.bgr',case=id,checked_pixels=width*240,mismatched=wrong,worst_channel_error=worst,sha256=hashlib.sha256(raw).hexdigest(),source_checksum_matches=True,examples=examples))
    warm=[line for line in log.splitlines() if line.startswith('palettePart=')]
    if len(warm)!=5 or any('ownedLinear=30720 compositionBytes=544 meshUpload=0 imageUpload=0 creates=0' not in line for line in warm):raise ValueError('Unexpected variant residency or warm uploads')
    return dict(scope='Native PICA prepared palette variants versus original ARM selection/crop + original PPU, at1.25x with authored black backdrop; not live game/full scene/physical proof',passed=all(not i['mismatched'] for i in checks),original_scene_accepted=False,physical_hardware=False,images=checks,checked_pixels=sum(i['checked_pixels'] for i in checks))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--session',type=Path,required=True);parser.add_argument('--reference',type=Path,required=True);args=parser.parse_args()
    report=inspect(args.session,args.reference);(args.session/'native-palette-image-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
