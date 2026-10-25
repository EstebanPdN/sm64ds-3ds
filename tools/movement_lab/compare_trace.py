#!/usr/bin/env python3
"""Compare each real ARM lab tick against a host execution of imported 3DS code."""
from pathlib import Path
import json,subprocess,sys,math,hashlib
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/movement-lab'
instance=Path(sys.argv[1]) if len(sys.argv)>1 else Path((OUT/'latest-attempt.txt').read_text())
log=instance/'user/sdmc/3ds/sm64ds/movement-lab.log';rows=[];actual={};commits={};resets=[]
for line in log.read_text().splitlines():
 if not line.startswith('MOVEMENT_TRACE '):continue
 text=line.removeprefix('MOVEMENT_TRACE ');p=text.split()
 if p[0]=='RESET':rows.append(text);resets.append(text)
 elif p[0]=='TICK':rows.append(' '.join(p[:9]));actual[int(p[1])]=list(map(float,p[9:]))
 elif p[0]=='DS':commits[int(p[1])]=list(map(float,p[2:]))
ref=subprocess.run([OUT/'trajectory-reference'],input='\n'.join(rows)+'\n',text=True,capture_output=True,check=True)
(instance/'reference-trajectory.txt').write_text(ref.stdout)
expected={int(p[0]):list(map(float,p[1:])) for l in ref.stdout.splitlines() if len(p:=l.split())==14}
assert expected.keys()==actual.keys(),(len(expected),len(actual))
maxes=[0.]*8;mismatches=[];commit_errors=[]
for tick,got in actual.items():
 want=expected[tick]
 for i in range(8):maxes[i]=max(maxes[i],abs(got[i]-want[i]))
 if any(abs(got[i]-want[i])>.002 for i in range(8)) or got[8:]!=want[8:]:
  mismatches.append({'tick':tick,'actual':got,'reference':want})
 if tick in commits:
  ds=commits[tick];values=got[:3]+[got[6],got[4]]
  if any(abs(x-y)>.0006 for x,y in zip(ds,values)):commit_errors.append({'tick':tick,'ds':ds,'core':values})
report={'status':'sampled_trajectory_match' if not mismatches and not commit_errors else 'divergence_detected','ticks':len(actual),'resets':len(resets),'max_error':dict(zip(['x','y','z','vx','vy','vz','forward','yaw'],maxes)),'core_mismatches':len(mismatches),'commit_mismatches':len(commit_errors),'first_core_mismatches':mismatches[:8],'first_commit_mismatches':commit_errors[:8],'scope':'ARM versus host, same imported 3DS controller, matching recorded input/camera/water and converted static scene; not full original-game or physical acceptance','source_receipt_sha256':hashlib.sha256((OUT/'core/port-source.json').read_bytes()).hexdigest()}
(instance/'trajectory-comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if not k.startswith('first_')},indent=2))
