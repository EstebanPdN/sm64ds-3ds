#!/usr/bin/env python3
"""Check observed interactive DS/native handoffs and held-button resumption."""
from pathlib import Path
import argparse,hashlib,json,re
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('attempt',type=Path);a=p.parse_args()
    result=json.loads((a.attempt/'result.json').read_text());assert result['fault'] is None and result['trace']
    artifact=json.loads((a.attempt/'artifact.json').read_text());build=Path(artifact['build_directory'])
    assert sha(build/'sm64ds-movement-lab.3dsx')==artifact['image_sha256']
    log_path=a.attempt/'user/sdmc/3ds/sm64ds/movement-lab.log';log=log_path.read_text();lines=log.splitlines()
    assert not re.search(r'FATAL:|MOVEMENT_LAB_NATIVE_FAULT',log)
    owner=re.compile(r'MOVEMENT_OWNERSHIP update=(\d+) mode=(\d+) char=(\d+) native=(\d+) keys=([0-9a-f]+) air=(\d+) pos=([^ ]+) vy=([^ ]+) state=([0-9a-f]+) reason=(.*)')
    events=[]
    for index,line in enumerate(lines):
        if match:=owner.fullmatch(line):
            update,mode,char,native,keys,air,position,vy,state,reason=match.groups()
            events.append({'line':index,'update':int(update),'mode':int(mode),'character':int(char),'native':bool(int(native)),'keys':int(keys,16),'airborne':bool(int(air)),'position':list(map(float,position.split(','))),'vertical_speed':float(vy),'reason':reason})
    def delegated(character,reason,mode):
        start=next(e for e in events if e['character']==character and e['mode']==mode and not e['native'] and e['reason']==reason)
        end=next(e for e in events if e['line']>start['line'] and e['character']==character and e['mode']==mode and e['native'])
        return {'start':start,'return':end}
    handoffs={'luigi_hybrid':delegated(1,'DS held-jump ability',1),'yoshi_hybrid':delegated(3,'DS held-jump ability',1),'yoshi_n64_action':delegated(3,'DS tongue / egg action',2)}
    sync=next(i for i,line in enumerate(lines) if line=='MOVEMENT_INPUT_SYNC previous=00000002 current=00000002 down=00000000')
    resumed=next(e for e in events if e['line']>sync)
    assert resumed['character']==1 and resumed['native'] and not resumed['airborne'] and resumed['keys']&2
    native_ticks=[]
    for index,line in enumerate(lines):
        if index<=sync or not line.startswith('MOVEMENT_TRACE TICK '):continue
        parts=line.split();native_ticks.append({'line':index,'jump':bool(int(parts[6])),'y':float(parts[11]),'vy':float(parts[14])})
    held=[]
    for tick in native_ticks:
        if not tick['jump']:break
        held.append(tick)
    assert len(held)>=2 and all(t['y']==resumed['position'][1] and t['vy']<=0 for t in held)
    release=next(t for t in native_ticks if not t['jump'])
    new_jump=next(t for t in native_ticks if t['line']>release['line'] and t['jump'] and t['y']>resumed['position'][1] and t['vy']>0)
    report={'status':'passed','model':result['model'],'physical_hardware_tested':False,'image_sha256':artifact['image_sha256'],'log_sha256':sha(log_path),'source_sha256':sha(Path(__file__)),'handoffs':handoffs,'held_resume_native_ticks_without_rejump':len(held),'release_then_new_press_jumps':True,'new_jump_sample':new_jump,'limits':'Observed interactive castle laboratory inputs/handoffs only. Empty-target Yoshi action is not swallow/egg progression proof. No whole-game ability, scene-transition or physical FPS acceptance.'}
    (a.attempt/'handoff-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='handoffs'},indent=2))
if __name__=='__main__':main()
