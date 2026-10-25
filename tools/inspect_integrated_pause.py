#!/usr/bin/env python3
"""Inspect the original pause UI in a closed historical candidate, not a new-code acceptance run."""
import hashlib,importlib.util,json,shutil,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
candidate=ROOT/'build/integrated-native/candidate-0f499a66dee9b9cb'
build=json.loads((candidate/'BUILD.json').read_text())
for name,record in build['files'].items():assert sha(candidate/name)==record['sha256'],name
assert 'sm64ds-decomp/src/_ZN5Stage9PS_UpdateEv.cpp' not in build['render60_overlay']['files']
tools=ROOT/'third_party/sm64ds-3ds/build3ds';sys.path.insert(0,str(tools))
spec=importlib.util.spec_from_file_location('pause_reference_runner',tools/'run_isolated.py');runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
instance=ROOT/'build/integrated-native/pause-reference'/('attempt-'+str(time.time_ns()))
plan=runner.prepare(candidate/'sm64ds-integrated.3dsx',instance,ROOT/'build/integrated-native/acquisitions/attempt-1792127820692207000/Azahar.app',[],'old',save_name='pause-reference.sav')
sd=Path(plan['user_directory'])/'sdmc';shutil.copytree(candidate/'sd',sd,dirs_exist_ok=True)
folder=sd/'3ds/sm64ds';folder.mkdir(parents=True,exist_ok=True)
pack=ROOT/'build/integrated-native/desktop-packer/Mario-GUI-v5.sm64ds'
shutil.copyfile(pack,folder/'Mario.sm64ds')
shutil.copyfile(Path.home()/'Library/Application Support/Azahar/sdmc/3ds/dspfirm.cdc',sd/'3ds/dspfirm.cdc')
screenshots=instance/'screenshots';screenshots.mkdir()
with Path(plan['config']).open('a') as f:
 f.write('\n[Audio]\nvolume=0\nvolume\\default=false\n[UI]\nPaths\\screenshotPath='+str(screenshots)+'\nPaths\\screenshotPath\\default=false\n[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n')
plan['purpose']='User requested visual inspection of existing in-game pause menus; not validation of current modified code'
plan['baseline_ELF_sha256']=build['ELF_sha256'];plan['package_sha256']=sha(pack)
(instance/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
with (instance/'launcher.log').open('w') as log:
 process=runner.launch_owned(plan,log)
 print(json.dumps(dict(instance=str(instance),app=plan['app'],pid=process.pid)),flush=True)
 reason='time_bound';began=time.monotonic()
 try:
  while time.monotonic()-began<1200:
   if process.poll() is not None:reason='process_exit';break
   time.sleep(.25)
 finally:
  if process.poll() is None:runner.stop_owned(process,process.pid)
  (instance/'result.json').write_text(json.dumps(dict(reason=reason,elapsed=time.monotonic()-began,baseline_ELF_sha256=build['ELF_sha256']),indent=2)+'\n')
