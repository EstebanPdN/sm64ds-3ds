#!/usr/bin/env python3
"""Launch the PLAYABLE fresh-native game in an isolated, interactive Azahar.

Usage (from the project root):
  python3 build/native-renderer/playable-r1/launch_playable.py GAME_DIR INSTANCE_NAME [old|new] [KEY=VALUE ...]

No frame limit and no autoexit: the instance stays open for manual/agent play
and Azahar save states. Everything stays local under playable-r1/sessions.
The personal Azahar profile is never touched. Stop with stop_playable.py.
"""
from pathlib import Path
import hashlib, json, os, shutil, subprocess, sys
import fcntl

root = Path.cwd()
# Serialize preparations too: a second launcher must never race the first.
launch_lock = (root / 'build/native-renderer/playable-r1/azahar-launch.lock').open('a')
fcntl.flock(launch_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
processes = subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True)
active = []
for line in processes.splitlines():
    fields = line.strip().split(None, 1)
    if len(fields) == 2 and Path(fields[1]).name.lower() == 'azahar':
        active.append(line.strip())
if active:
    raise SystemExit('Azahar already running; close the existing instance before launching another:\n' + '\n'.join(active))

sys.path.insert(0, str(root / 'third_party/sm64ds-3ds/build3ds'))
import run_isolated

game_dir = Path(sys.argv[1]).resolve()
name = sys.argv[2]
model = sys.argv[3] if len(sys.argv) > 3 else 'new'
extra = sys.argv[4:]
here = root / 'build/native-renderer/playable-r1'
instance = here / 'sessions' / name
defaults = ['SM64DS_FRESH_NATIVE=1', 'SM64DS_FRESH_NATIVE_ASSETS=sdmc:/3ds/sm64ds/native',
            'SM64DS_SAVE_PATH=sdmc:/3ds/sm64ds/playable.sav']
overridden = {e.split('=', 1)[0] for e in extra}
variables = [v for v in defaults if v.split('=', 1)[0] not in overridden] + extra
app = Path.home() / 'Applications/azahar/azahar-macos-arm64-2126.2/Azahar.app'  # same Azahar 2126.2 binary (sha256 7095473e...)
plan = run_isolated.prepare(game_dir / 'sm64ds.3dsx', instance, app, variables, model)
user = Path(plan['user_directory'])
game = user / 'sdmc/3ds/sm64ds'
game.mkdir(parents=True)
shutil.copy2(Path(plan['sd_directory']) / 'env.txt', game / 'env.txt')
(Path(plan['sd_directory']) / 'autoexit').unlink(missing_ok=True)
dump_option=os.environ.get('PLAYABLE_DUMPS','0')
if dump_option not in ('0','1'):raise SystemExit('PLAYABLE_DUMPS must be0or1')
(game / 'settings.ini').write_text('[debug]\ndumps='+dump_option+'\nshow_fps=1\n')
settings_from=os.environ.get('PLAYABLE_SETTINGS_FROM')
if settings_from:shutil.copy2(Path(settings_from).resolve(strict=True),game/'settings.ini')
run_isolated.independent_copy(root / 'build/roms/europe.nds', game / 'owner-rom.nds')
firmware = Path.home() / 'Library/Application Support/Azahar/sdmc/3ds/dspfirm.cdc'
shutil.copy2(firmware, user / 'sdmc/3ds/dspfirm.cdc')
nr = root / 'build/native-renderer'
shutil.copytree(nr / 'playable-r1/catalog-approx-r8', game / 'native')
for p in (nr / 'catalog-particles-20261027-r1').glob('*.nim'):
    shutil.copy2(p, game / 'native' / p.name)
shutil.copy2(nr / 'menu-frame-r1/catalog/04096.nim', game / 'native/04096.nim')
shutil.copy2(nr / 'live-backgrounds-r1/catalog-r1/05001.nmd', game / 'native/05001.nmd')
shutil.copy2(nr / 'text-r1/font-r1/06000.nim', game / 'native/06000.nim')
for folder, pattern in [('menu-obj-r1/catalog-r2', '*.nim'),
                        ('menu-backdrops-r1/catalog-r7', '*.nmd')]:
    for p in (nr / folder).glob(pattern):
        shutil.copy2(p, game / 'native' / p.name)
# Standalone component labs use the original isolated SD root. Their native
# bank is staged before the process starts, under the same one-instance guard.
lab_bank = os.environ.get('PLAYABLE_LAB_BANK')
if lab_bank:
    lab_bank = Path(lab_bank).resolve(strict=True)
    shutil.copy2(lab_bank, Path(plan['sd_directory']) / lab_bank.name)
# Optional standalone-lab fixtures, staged before the sole process starts.
lab_native = os.environ.get('PLAYABLE_LAB_NATIVE_FROM')
if lab_native:
    shutil.copytree(Path(lab_native).resolve(strict=True),Path(plan['sd_directory'])/'native',dirs_exist_ok=True)
plan['sd_directory'] = str(game)
# Reuse a matching private pack for comparisons; the game loader still owns
# pack identity/version validation. Copy before launching the sole emulator.
prepared = os.environ.get('PLAYABLE_PREPARED_FROM')
if prepared:
    prepared = Path(prepared).resolve(strict=True)
    shutil.copy2(prepared, game / 'prepared.sm64ds')
    plan['prepared_input_sha256'] = hashlib.sha256(prepared.read_bytes()).hexdigest()
# Optional files staged into sdmc:/3ds/sm64ds before the sole process starts
# (e.g. a SELECT+START test-mode state, prueba/estado.txt).
extra_sd = os.environ.get('PLAYABLE_SD_EXTRA')
if extra_sd:
    shutil.copytree(Path(extra_sd).resolve(strict=True), game, dirs_exist_ok=True)
    plan['sd_extra'] = str(Path(extra_sd).resolve())
# Save states require HLE applets (isolated profile only).
config = Path(plan['config']); text = config.read_text()
text = text.replace('[System]\n', '[System]\nlle_applets=false\nlle_applets\\default=false\n', 1)
screenshots = instance / 'screenshots'
screenshots.mkdir()
import re
text = re.sub(r'^Paths\\screenshotPath(?:\\default)?=.*\n', '', text, flags=re.M)
text = text.replace('[UI]\n', '[UI]\nPaths\\screenshotPath=' + str(screenshots) + '\nPaths\\screenshotPath\\default=false\n', 1)
config.write_text(text)
plan['screenshots'] = str(screenshots)
plan['scope'] = 'PLAYABLE mode interactive session; temporary 2D bridge; not acceptance/performance evidence'
(instance / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
log = (instance / 'launcher.log').open('w')
import os
cia = os.environ.get('PLAYABLE_CIA')
if cia:
    # Installed title => Azahar save states are enabled (they are disabled for .3dsx).
    subprocess.run([plan['executable'], '-i', str(Path(cia).resolve())], cwd=plan['cwd'], stdout=log,
                   stderr=subprocess.STDOUT, check=True, timeout=300)
    apps = sorted(user.glob('sdmc/Nintendo 3DS/*/*/title/00040000/0f5365*/content/*.app'))
    if not apps: raise SystemExit('CIA install produced no content .app')
    plan['launch'] = [plan['executable'], str(apps[0])]
    (instance / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
process = subprocess.Popen(plan['launch'], cwd=plan['cwd'], stdout=log, stderr=subprocess.STDOUT,
                           start_new_session=True)
(instance / 'process.json').write_text(json.dumps({'pid': process.pid}) + '\n')
print('LAUNCHED', process.pid, instance)
