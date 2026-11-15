"""Compare native completed LCD targets with the maintained CPU UI artwork.

This checks the UI plane over an authored backdrop, never a complete game.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import re


def inspect(session):
    run=json.loads((session/'result/run.json').read_text())
    directory=session/'user/sdmc/sm64ds';log=(directory/'log.txt').read_text()
    if (run['result']!='PASS' or not run['owned_process_group_stopped'] or
            run['unmapped_access_count'] or run['global_settings_modified'] or
            not log.rstrip().endswith('PASS') or 'completed_frames=70' not in log):
        raise ValueError('Failed/incomplete or unisolated native UI acquisition')
    checks=[]
    def verified(name):
        raw=(directory/name).read_bytes();checksum=2166136261
        for value in raw:checksum=((checksum^value)*16777619)&0xffffffff
        if re.findall(r'^export='+re.escape(name)+r' bytes=(\d+) fnv1a=([0-9a-f]{8})$',log,re.M)!=[(str(len(raw)),f'{checksum:08x}')]:
            raise ValueError('Source/export checksum differs: '+name)
        return raw
    for state in range(7):
        lower=verified(f'interface-{state}.argb')
        upper=verified(f'interface-{state}-fps.argb')
        if len(lower)!=320*240*4 or len(upper)!=256*16*4:
            raise ValueError('Incomplete source UI snapshot')
        sources=[struct.unpack('<4096I',upper),struct.unpack('<76800I',lower)]
        for side,width,index in [('top',400,0),('bottom',320,1)]:
            name=f'interface-{state}-{side}.bgr';raw=verified(name)
            if len(raw)!=width*240*3:raise ValueError('Incomplete LCD snapshot')
            wrong=worst=0;examples=[]
            for y in range(240):
                for x in range(width):
                    background=(0,0,0) if index==0 else (0x51,0x34,0x17)
                    if index:
                        color=sources[index][y*320+x]
                    else:
                        color=sources[index][(y-26)*256+x-140] if 140<=x<396 and 26<=y<42 else 0
                    alpha=color>>24
                    ink=(color&255,(color>>8)&255,(color>>16)&255)
                    expected=tuple((c*alpha+b*(255-alpha)+127)//255 for c,b in zip(ink,background))
                    at=(x*240+239-y)*3;actual=raw[at:at+3]
                    error=max(abs(a-b) for a,b in zip(actual,expected));worst=max(worst,error)
                    if error:
                        wrong+=1
                        if len(examples)<8:examples.append(dict(x=x,y=y,expected=list(expected),actual=list(actual)))
            checks.append(dict(file=name,checked_pixels=width*240,mismatched=wrong,worst_channel_error=worst,examples=examples,
                sha256=hashlib.sha256(raw).hexdigest(),source_argb_sha256=hashlib.sha256(lower if index else upper).hexdigest()))
    return dict(scope='Maintained UI artwork via native cropped GPU textures over authored backdrop; not full game or physical proof',
        passed=all(not i['mismatched'] for i in checks),original_scene_accepted=False,physical_hardware=False,
        checked_pixels=sum(i['checked_pixels'] for i in checks),images=checks)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--session',type=Path,required=True);args=parser.parse_args()
    report=inspect(args.session);(args.session/'native-interface-image-check.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
