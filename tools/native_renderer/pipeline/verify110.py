#!/usr/bin/env python3
"""BUILD 110 sequential Azahar verification (one instance at a time).

A: first boot + Bob-omb with the recorded BUILD 107 input recipe (frame limit).
C: same entry, then a scripted SELECT+START inside the level: state file + exit.
B: boot with C's state file: test mode returns to the spot and writes reports.
"""
from pathlib import Path
import json,os,re,shutil,signal,subprocess,sys,time
root=Path.cwd();base=root/'build/native-renderer/playable-r1';active=None
def live_azahar():
    out=subprocess.check_output(['ps','-axo','pid=,comm='],text=True)
    return [l for l in out.splitlines() if len(l.split(None,1))==2 and Path(l.split(None,1)[1]).name.lower()=='azahar']
def stop():
    global active
    if active is None:return
    subprocess.run(['python3',str(base/'stop_playable.py'),str(active)],check=False)
    pid=json.loads((active/'process.json').read_text())['pid']
    deadline=time.monotonic()+15
    while subprocess.run(['ps','-p',str(pid),'-o','comm='],stdout=subprocess.PIPE).returncode==0:
        if time.monotonic()>deadline:raise SystemExit('Owned Azahar still live; no further launch')
        time.sleep(.2)
    active=None
def interrupted(signum,frame):
    stop();raise SystemExit(128+signum)
signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
BOB='SM64DS_PLAYABLE_SCRIPT=k:900:1:2,k:1000:1:2,k:1100:1:2,k:1200:1:2,k:1300:40:160,k:1480:10:160,k:1660:41:100,k:1780:80:50'
def launch(name,model,variables,env):
    global active
    if live_azahar():raise SystemExit('Azahar already running: '+repr(live_azahar()))
    subprocess.run(['python3',str(base/'launch_playable.py'),str(base/('game-p'+N)),name,model,*variables],env=env,check=True)
    active=base/'sessions'/name
    return json.loads((active/'process.json').read_text())['pid']
def alive(pid):return subprocess.run(['ps','-p',str(pid),'-o','comm='],stdout=subprocess.PIPE).returncode==0
def sd(name):return base/'sessions'/name/'user/sdmc/3ds/sm64ds'
def progress(text):
    f=re.findall(r'Fresh native PLAYABLE: frames=(\d+)',text);return f[-1] if f else 'startup'
step=sys.argv[1] if len(sys.argv)>1 else 'all'
N=sys.argv[2] if len(sys.argv)>2 else '110'
env=os.environ.copy()
env['PLAYABLE_SETTINGS_FROM']=str(root/'build/native-renderer/hardware-report91/settings.ini')
env['PLAYABLE_DUMPS']='1'
prepared=base/'sessions/prepared110.sm64ds'  # any matching BUILD's pack; the loader validates it
if step in('all','A'):
    pid=launch('bob'+N+'-old','old',['SM64DS_FRESH_NATIVE_PROFILE=0','SM64DS_NATIVE_LOAD_TRACE=0','SM64DS_GAME_FRAME_LIMIT=3000','SM64DS_NATIVE_COURSE=bob',BOB,'SM64DS_PLAYABLE_SHOTS=1500,2400'],env)
    log=sd('bob'+N+'-old')/'log.txt';deadline=time.monotonic()+1500;beat=time.monotonic()+30
    while True:
        text=log.read_text(errors='replace') if log.exists() else ''
        if 'Native game exit code: 0\nPASS' in text:print('A FINISHED',progress(text),flush=True);stop();break
        if time.monotonic()>deadline or not alive(pid):print('A FAILED or timed out; last:',text[-1500:],flush=True);stop();raise SystemExit(1)
        if time.monotonic()>beat:print('A LIVE frames',progress(text),flush=True);beat=time.monotonic()+30
        time.sleep(1)
    made=sd('bob'+N+'-old')/'prepared.sm64ds'
    if made.exists() and not prepared.exists():shutil.copy2(made,prepared)
if step in('all','C'):
    if prepared.exists():env['PLAYABLE_PREPARED_FROM']=str(prepared)
    pid=launch('chord'+N+'-old','old',['SM64DS_FRESH_NATIVE_PROFILE=0','SM64DS_NATIVE_LOAD_TRACE=0','SM64DS_NATIVE_COURSE=bob',BOB+',k:1900:c:4'],env)
    state=sd('chord'+N+'-old')/'prueba/estado.txt';log=sd('chord'+N+'-old')/'log.txt';deadline=time.monotonic()+900
    while True:
        # Azahar keeps its window after the app exits (no chainloader relaunch).
        exited=log.exists() and 'Native game exit code: 0' in log.read_text(errors='replace')
        if state.exists() and exited:print('C FINISHED: state written and game exited',flush=True);print(state.read_text(),flush=True);stop();break
        if time.monotonic()>deadline:print('C TIMED OUT; state exists:',state.exists(),flush=True);stop();raise SystemExit(1)
        if not alive(pid) and not state.exists():print('C exited without state',flush=True);stop();raise SystemExit(1)
        time.sleep(1)
if step in('all','B'):
    if prepared.exists():env['PLAYABLE_PREPARED_FROM']=str(prepared)
    extra=base/('sessions/test'+N+'-sd-extra');shutil.rmtree(extra,ignore_errors=True)
    source=Path(os.environ.get('TEST_STATE_FROM',str(sd('chord'+N+'-old')/'prueba')))
    shutil.copytree(source,extra/'prueba');env['PLAYABLE_SD_EXTRA']=str(extra)
    pid=launch('test'+N+'-old','old',['SM64DS_NATIVE_LOAD_TRACE=0'],env)
    run=sd('test'+N+'-old')/'prueba';deadline=time.monotonic()+2400;beat=time.monotonic()+30
    while True:
        results=sorted(run.glob('*/resultado.txt'))
        text=results[-1].read_text(errors='replace') if results else ''
        reports=len(list(run.glob('*/informes/*/resumen.txt')))
        if re.search(r'^(COMPLETA|INCOMPLETA|CANCELADA)',text,re.M):print('B FINISHED reports',reports,flush=True);print(text,flush=True);break
        if time.monotonic()>deadline or not alive(pid):print('B FAILED or timed out; reports',reports,'\n',text,flush=True);stop();raise SystemExit(1)
        if time.monotonic()>beat:print('B LIVE reports',reports,flush=True);beat=time.monotonic()+30
        time.sleep(1)
    print('B leaving the final panel open for a screenshot; stop with stop_playable.py',flush=True)
