#!/usr/bin/env python3
"""Run the movement lab in a fresh owned emulator profile; functional evidence only."""
from pathlib import Path
import sys,time,shutil,json,re,argparse,hashlib
parser=argparse.ArgumentParser();parser.add_argument('--replay',action='store_true');parser.add_argument('--presentation',action='store_true');parser.add_argument('--build-directory',type=Path);parser.add_argument('--app',type=Path);parser.add_argument('--trace',action='store_true');parser.add_argument('--model',choices=['old','new'],default='new');parser.add_argument('--seconds',type=int,default=300);parser.add_argument('--dumps',action='store_true',help='Enable real RAM captures and profile-local Shift+A chord');options=parser.parse_args()
if options.presentation:options.replay=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'third_party/sm64ds-3ds/build3ds'))
import run_isolated as runner
instance=ROOT/'build/movement-lab'/('attempt-'+str(time.time_ns()))
build=(options.build_directory or ROOT/'build/movement-lab').resolve()
artifact=build/'sm64ds-movement-lab.3dsx'
receipt=json.loads((build/'build.json').read_text())
assert hashlib.sha256(artifact.read_bytes()).hexdigest()==receipt['sha256'][artifact.name]
for name,digest in receipt.get('source_sha256',{}).items():
 assert hashlib.sha256((build/'source-evidence'/name).read_bytes()).hexdigest()==digest,name
environment=(['SM64DS_LAB_REPLAY=1']+(['SM64DS_LAB_PRESENTATION_REPLAY=1'] if options.presentation else [])) if options.replay else []
if options.trace:environment.append('SM64DS_LAB_TRACE=1')
plan=runner.prepare(artifact,instance,options.app or ROOT/'build/integrated-native/acquisitions/attempt-1792292843159319000/Azahar.app',environment,options.model,save_name='movement-lab.sav')
config=Path(plan['config']);original_config=hashlib.sha256(config.read_bytes()).hexdigest()
config_text=config.read_text()
if options.dumps:
 config_text+='\n[Controls]\nprofiles\\size=1\nprofiles\\1\\name=Movement Lab\n'
 for button in ('l','r'):
  config_text+='profiles\\1\\button_'+button+'="code:16777248,engine:keyboard"\nprofiles\\1\\button_'+button+'\\default=false\n'
config.write_text(config_text+'\n[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n')
plan['lab_config_adjustments']={'disable_update_modal':True,'diagnostic_shift_a':options.dumps,'before_sha256':original_config}
plan['config_initial_sha256']=hashlib.sha256(config.read_bytes()).hexdigest()
(instance/'plan.json').write_text(json.dumps(plan,indent=2))

sd=Path(plan['user_directory'])/'sdmc/3ds/sm64ds';sd.mkdir(parents=True,exist_ok=True);shutil.copyfile(Path(plan['sd_directory'])/'env.txt',sd/'env.txt');shutil.copyfile(ROOT/'build/movement-lab/attempt-1792182706865673000/user/sdmc/3ds/sm64ds/prepared.sm64ds',sd/'prepared.sm64ds')
if options.dumps:
 (sd/'settings.ini').write_text('[debug]\ndumps=1\nselect_overlay=0\n[3d]\nresolution=native\n[display]\nmode=wide\n[input]\ncircle_pad=analog\n')
firmware=Path.home()/'Library/Application Support/Azahar/sdmc/3ds/dspfirm.cdc'
shutil.copyfile(firmware,sd.parent/'dspfirm.cdc')
(instance/'artifact.json').write_text(json.dumps({'build_directory':str(build),'image_sha256':hashlib.sha256(artifact.read_bytes()).hexdigest(),'source_receipt_sha256':hashlib.sha256((build/'build.json').read_bytes()).hexdigest()},indent=2))
(ROOT/'build/movement-lab/latest-attempt.txt').write_text(str(instance))
with runner.emulator_slot(),runner.legacy_exclusion(),(instance/'launcher.log').open('w') as launcher:
 p=runner.launch_owned(plan,launcher)
 (instance/'process.json').write_text(json.dumps({'pid':p.pid,'identity':getattr(p,'identity',None)},indent=2))
 print(instance,flush=True)
 start=time.monotonic();reason='bounded-stop';fault=None
 try:
  while time.monotonic()-start<options.seconds:
   if (instance/'stop').exists():reason='requested-stop';break
   app=sd/'movement-lab.log';emu=Path(plan['user_directory'])/'log/azahar_log.txt'
   logs=(app.read_text(errors='replace') if app.exists() else '')+'\n'+(emu.read_text(errors='replace') if emu.exists() else '')
   m=re.search(r'^FATAL:.*|Undefined instruction.*|Segmentation fault.*|Unhandled exception.*',logs,re.M|re.I)
   if m:fault=m[0];reason='fault';break
   if options.replay and 'MOVEMENT_LAB REPLAY complete' in logs:reason='replay-complete';break
   if p.poll() is not None:reason='process-exit';break
   time.sleep(.25)
 finally:runner.stop_owned(p,p.pid)
 (instance/'result.json').write_text(json.dumps({'reason':reason,'fault':fault,'elapsed':time.monotonic()-start,'physical_hardware_tested':False,'model':options.model,'trace':options.trace,'dumps':options.dumps},indent=2))
 print(reason,fault,flush=True)
