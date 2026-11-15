#!/usr/bin/env python3
"""BUILD 114 sequential Azahar verification of the level tour (one instance at a time).

Usage (project root):
  python3 build/native-renderer/playable-r1/verify114.py chord [N]
  python3 build/native-renderer/playable-r1/verify114.py tour MODEL FIRST COUNT STEPS PREFIX [N]

chord: scripted SELECT+START on the title writes a tour state and exits.
tour: starts the tour at level index FIRST and follows it for COUNT boots.
  Azahar does not relaunch an app that exits through the HOME chainloader, so
  each level boot is a fresh isolated session; the SD folder prueba/ is
  carried from one session to the next. STEPS>0 stops each level after that
  many situations (env.txt, development .3dsx only; 0 = the full script).
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
def launch(name,model,variables,env):
    global active
    # Another project's session may own the emulator: wait, never close it.
    waited=time.monotonic()
    while live_azahar():
        if time.monotonic()-waited>3600:raise SystemExit('Azahar busy for an hour: '+repr(live_azahar()))
        print('waiting for another Azahar session to end...',flush=True);time.sleep(20)
    shutil.rmtree(base/'sessions'/name,ignore_errors=True)
    subprocess.run(['python3',str(base/'launch_playable.py'),str(base/('game-p'+N)),name,model,*variables],env=env,check=True)
    active=base/'sessions'/name
    return json.loads((active/'process.json').read_text())['pid']
def alive(pid):return subprocess.run(['ps','-p',str(pid),'-o','comm='],stdout=subprocess.PIPE).returncode==0
def sd(name):return base/'sessions'/name/'user/sdmc/3ds/sm64ds'
step=sys.argv[1]
env=os.environ.copy()
env.setdefault('PLAYABLE_SETTINGS_FROM',str(root/'build/native-renderer/hardware-report91/settings.ini')) # override to test another settings.ini
prepared=base/'sessions/prepared110.sm64ds'
if prepared.exists():env['PLAYABLE_PREPARED_FROM']=str(prepared)
if step=='chord':
    N=sys.argv[2] if len(sys.argv)>2 else '114'
    name='chord'+N+'-old'
    pid=launch(name,'old',['SM64DS_NATIVE_LOAD_TRACE=0','SM64DS_PLAYABLE_SCRIPT=k:700:c:4'],env)
    state=sd(name)/'prueba/estado.txt';log=sd(name)/'log.txt';deadline=time.monotonic()+600
    while True:
        exited=log.exists() and 'Native game exit code: 0' in log.read_text(errors='replace')
        if state.exists() and exited:print('CHORD FINISHED: state written and game exited',flush=True);print(state.read_text(),flush=True);stop();break
        if time.monotonic()>deadline:print('CHORD TIMED OUT; state exists:',state.exists(),flush=True);stop();raise SystemExit(1)
        if not alive(pid) and not state.exists():print('CHORD exited without state',flush=True);stop();raise SystemExit(1)
        time.sleep(1)
    raise SystemExit(0)
model,first,count,steps,prefix=sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5]),sys.argv[6]
N=sys.argv[7] if len(sys.argv)>7 else '114'
carry=base/'sessions'/(prefix+'-sd')
if not (carry/'prueba/estado.txt').exists():
    shutil.rmtree(carry,ignore_errors=True);(carry/'prueba/01').mkdir(parents=True)
    (carry/'prueba/estado.txt').write_text(f'SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nindice={first}\nnivel=-1\narea=-1\npersonaje=0\nposicion=0\nx=0\ny=0\nz=0\nangulo=0\n'
        'completos=0\nincompletos=0\ncolgados=0\nfps_decimas=0\nfps_niveles=0\np95_peor_decimas=0\n')
env['PLAYABLE_SD_EXTRA']=str(carry)
variables=['SM64DS_NATIVE_LOAD_TRACE=0']+([f'SM64DS_PRUEBA_PASOS={steps}'] if steps>0 else [])
# VERIFY_FIRST_BOOT_VARS='A=1 B=2': extra env.txt variables for the first boot only
# (e.g. SM64DS_DEV_FAULT=terminate:1200 to check the exit path and the crash record).
first_vars=os.environ.get('VERIFY_FIRST_BOOT_VARS','').split()
# BUILD131: 24 entrance situations, then up to four zones of three reports and
# their dumps; the development limit shortens the entrance script only.
per_level=180+((steps if steps>0 else 24)+12)*25+120
started=time.monotonic()
for boot in range(count):
    name=f'{prefix}-{boot:02d}'
    boot_vars=variables
    if boot==0 and first_vars:
        keys={v.split('=',1)[0] for v in first_vars}
        boot_vars=[v for v in variables if v.split('=',1)[0] not in keys]+first_vars
    pid=launch(name,model,boot_vars,env)
    # The installed native-test CIA logs to native-test.log and ignores env.txt.
    run=sd(name)/'prueba';log=sd(name)/('native-test.log' if env.get('PLAYABLE_CIA') else 'log.txt');deadline=time.monotonic()+per_level;beat=time.monotonic()+30;final=False
    # A carried log is the previous boot's: read the log only after the game
    # has renamed it to *-anterior (BUILD115+), or its exit line ends this boot.
    rotated=not any((carry/n).exists() for n in ('log.txt','native-test.log'))
    while True:
        if not rotated:rotated=any((sd(name)/n).exists() for n in ('log-anterior.txt','native-test-anterior.log'))
        text=log.read_text(errors='replace') if rotated and log.exists() else ''
        result=(run/'01/resultado.txt').read_text(errors='replace') if (run/'01/resultado.txt').exists() else ''
        if re.search(r'^RECORRIDO (FINALIZADO|CANCELADO|DETENIDO)',result,re.M):final=True;time.sleep(3);break
        if 'Native game exit code: 0' in text:break
        # BUILD115+: an abnormal exit (abort, terminate) now ends cleanly.
        if 'Native exit without runtime shutdown' in text:time.sleep(3);break
        if time.monotonic()>deadline:print(name,'TIMED OUT',flush=True);break
        if not alive(pid):print(name,'PROCESS DIED',flush=True);break
        if time.monotonic()>beat:
            reports=len(list(run.glob('01/*/*/resumen.txt')))+len(list(run.glob('01/*/*.resumen.txt')))  # BUILD131: flat report files
            print(name,'live: reports so far',reports,'elapsed %.0f s'%(time.monotonic()-started),flush=True);beat=time.monotonic()+60
        time.sleep(1)
    if final:
        shot=active/'screenshots'
        print(name,'final panel shown',flush=True)
    stop()
    # Carry the SD results (and the state for the next level) forward.
    if run.exists():
        shutil.rmtree(carry/'prueba',ignore_errors=True);shutil.copytree(run,carry/'prueba')
    # The log too: the next boot keeps it as *-anterior and a crashed level copies it.
    for logname in ('log.txt','native-test.log'):
        if (sd(name)/logname).exists():shutil.copy2(sd(name)/logname,carry/logname)
    lines=[l for l in (carry/'prueba/01/resultado.txt').read_text(errors='replace').splitlines() if l.startswith(('== ','RECORRIDO'))] if (carry/'prueba/01/resultado.txt').exists() else []
    print(name,'->',lines[-1] if lines else 'no result line yet',flush=True)
    if final or not (carry/'prueba/estado.txt').exists():break
print('TOUR DRIVER DONE in %.0f s'%(time.monotonic()-started),flush=True)
