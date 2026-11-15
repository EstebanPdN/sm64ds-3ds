"""Original LCD caption pixels versus native PICA output, with explicit scope."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from PIL import Image


def inspect(session, reference):
    source=Image.open(reference).convert('RGB')
    if source.size!=(256,192):
        raise ValueError('Expected the completed original256x192 LCD capture')
    original=source.crop((0,128,256,144)).tobytes()
    directory=session/'user/sdmc/sm64ds';log=(directory/'log.txt').read_text();run=json.loads((session/'result/run.json').read_text())
    if run['result']!='PASS' or not run['owned_process_group_stopped'] or run['unmapped_access_count'] or run['global_settings_modified']:
        raise ValueError('Unsuccessful or unisolated native caption acquisition')
    images=[];buffers={}
    for side,width,left in [('top',400,40),('bottom',320,0)]:
        for phase in ['first','last']:
            name=f'menu2d-{side}-{phase}';data=(directory/(name+'.bgr')).read_bytes()
            if len(data)!=width*240*3:
                raise ValueError('Incomplete native LCD capture')
            checksum=2166136261
            for value in data:checksum=((checksum^value)*16777619)&0xffffffff
            checks=re.findall(r'^readback='+name+r' fnv1a=([a-f0-9]{8}) width=(\d+)$',log,re.M)
            if checks!=[(f'{checksum:08x}',str(width))]:
                raise ValueError('Source/export checksum mismatch')
            wrong=worst=0;examples=[]
            for y in range(240):
                for x in range(width):
                    expected=(0,0,0)
                    if left<=x<left+320 and 160<=y<180:
                        sx=((x-left)*2+1)*256//640;sy=((y-160)*2+1)*16//40
                        at=(sy*256+sx)*3;expected=tuple(original[at:at+3])
                    at=(x*240+239-y)*3;actual=tuple(data[at:at+3][::-1]);error=max(abs(a-b) for a,b in zip(actual,expected));worst=max(worst,error)
                    if error>1:
                        wrong+=1
                        if len(examples)<12:examples.append(dict(x=x,y=y,expected=expected,actual=actual))
            images.append(dict(file=name+'.bgr',width=width,checked=width*240,mismatched=wrong,worst_channel_error=worst,
                sha256=hashlib.sha256(data).hexdigest(),source_checksum_matches=True,examples=examples,passed=wrong==0))
            buffers[(side,phase)]=data
        if buffers[(side,'first')]!=buffers[(side,'last')]:
            raise ValueError('Static caption changed between frames')
    return dict(scope='Actual original caption only at native1.25x scale; no3D/other2D/window/blend/menu acceptance',
        reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),reference_rectangle=[0,128,256,144],tolerance=1,
        images=images,original_scene_accepted=False,physical_hardware=False,passed=all(i['passed'] for i in images))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--session',type=Path,required=True);parser.add_argument('--reference',type=Path,required=True);args=parser.parse_args()
    report=inspect(args.session,args.reference);(args.session/'native-menu2d-image-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
