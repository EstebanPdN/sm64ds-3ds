"""Native sparse2D PICA pixels versus an isolated ORIGINAL PPU RGB export."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inspect(session,reference):
    source=reference.read_bytes()
    if len(source)!=256*192*3:raise ValueError('Expected original PPU 256x192 RGB export')
    run=json.loads((session/'result/run.json').read_text());directory=session/'user/sdmc/sm64ds';log=(directory/'log.txt').read_text()
    if run['result']!='PASS' or not run['owned_process_group_stopped'] or run['unmapped_access_count'] or run['global_settings_modified'] or not log.rstrip().endswith('PASS'):
        raise ValueError('Failed/unisolated native canvas acquisition')
    if 'scope=prepared-native2D-mesh originalSceneAccepted=0 physicalCapture=0 authoredBlackBackdrop=1' not in log:
        raise ValueError('Incomplete native canvas scope')
    checks=[];buffers={}
    probe='authoredProbe=blue16x16 bottom304,224 aliasImageAndCanvasID=5001' in log
    for side,width,left in [('top',400,40),('bottom',320,0)]:
        for phase in ['first','last']:
            name=f'canvas-{side}-{phase}';raw=(directory/(name+'.bgr')).read_bytes()
            if len(raw)!=width*240*3:raise ValueError('Incomplete native canvas export')
            checksum=2166136261
            for value in raw:checksum=((checksum^value)*16777619)&0xffffffff
            source_checks=re.findall(r'^readback='+name+r' fnv1a=([a-f0-9]{8}) width=(\d+)$',log,re.M)
            if source_checks!=[(f'{checksum:08x}',str(width))]:raise ValueError('Completed target/export checksum mismatch')
            wrong=worst=0;examples=[]
            for y in range(240):
                sy=(2*y+1)*2//5
                for x in range(width):
                    expected=bytes(3)
                    if left<=x<left+320:
                        sx=(2*(x-left)+1)*2//5;offset=(sy*256+sx)*3;expected=source[offset:offset+3][::-1]
                    if probe and side=='bottom' and x>=304 and y>=224:expected=bytes([255,0,0])
                    offset=(x*240+239-y)*3;actual=raw[offset:offset+3];error=max(abs(a-b) for a,b in zip(actual,expected));worst=max(worst,error)
                    if error:
                        wrong+=1
                        if len(examples)<8:examples.append(dict(x=x,y=y,expected=list(expected),actual=list(actual)))
            buffers[side,phase]=raw;checks.append(dict(file=name+'.bgr',checked=width*240,mismatched=wrong,worst_channel_error=worst,
                source_checksum_matches=True,sha256=hashlib.sha256(raw).hexdigest(),examples=examples))
        if buffers[side,'first']!=buffers[side,'last']:raise ValueError('Static native background changed over time')
    if probe:
        name='canvas-alias-bottom';raw=(directory/(name+'.bgr')).read_bytes();checksum=2166136261
        for value in raw:checksum=((checksum^value)*16777619)&0xffffffff
        if len(raw)!=320*240*3 or re.findall(r'^readback='+name+r' fnv1a=([a-f0-9]{8}) width=(\d+)$',log,re.M)!=[(f'{checksum:08x}','320')]:raise ValueError('Invalid independent namespace probe export')
        wrong=sum(raw[i:i+3]!=bytes([255,0,0]) for i in range(0,len(raw),3))
        checks.append(dict(file=name+'.bgr',checked=320*240,mismatched=wrong,worst_channel_error=255 if wrong else 0,
            source_checksum_matches=True,sha256=hashlib.sha256(raw).hexdigest(),scope='Authored blue image namespace, same numeric assetID as canvas'))
    warm=[line for line in log.splitlines() if line.startswith('canvasFrame=') and not line.startswith('canvasFrame=0 ')]
    if not warm or any('meshUpload=0 imageUpload=0 creates=0' not in line for line in warm):raise ValueError('Unexpected warm resource uploads/creates')
    return dict(scope='Prepared background geometry versus original PPU, at1.25x with authored black backdrop; not live game crop,palette,font or complete-scene acceptance',
        passed=all(i['mismatched']==0 for i in checks),images=checks,fixed_frames_equal=True,warm_no_uploads_or_creates=True,independent_image_and_canvas_namespace=probe,
        reference_sha256=hashlib.sha256(source).hexdigest(),original_scene_accepted=False,physical_hardware=False)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--session',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);args=p.parse_args()
    report=inspect(args.session,args.reference);(args.session/'native-canvas-image-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
