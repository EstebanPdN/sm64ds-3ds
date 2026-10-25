#!/usr/bin/env python3
"""Install and boot a packaged CIA in an isolated Azahar profile, not hardware.

Use the emulator's START binding to leave the app, then quit Azahar before
the bounded timeout. On a default keyboard profile START is M.
"""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import time

from package_verify import verify


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--azahar', type=pathlib.Path, required=True)
    parser.add_argument('--cia', type=pathlib.Path, default=root/'dist/sm64ds-native-preview/sm64ds-preview.cia')
    parser.add_argument('--rom', type=pathlib.Path, default=root/'Super Mario 64 DS.nds')
    parser.add_argument('--model', choices=['old', 'new'], required=True)
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--require-original-heap', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 120:
        parser.error('--seconds must be between 1 and 120')
    verified = verify(args.cia)
    rom = args.rom.resolve(strict=True)
    rom_bytes = rom.read_bytes()
    game_code = rom_bytes[12:16].decode('ascii')
    if game_code not in ('ASME', 'ASMP'):
        parser.error('Smoke test requires an investigated ASME/ASMP ROM')
    region_suffix = '-asmp' if game_code == 'ASMP' else ''
    runtime = root/'build/package'/f'azahar-{args.model}{region_suffix}'
    config = runtime/'config/azahar-emu'
    data = runtime/'data/azahar-emu'
    sd = data/'sdmc/3ds/sm64ds'
    for folder in (config, sd, runtime/'cache'):
        folder.mkdir(parents=True, exist_ok=True)
    (config/'qt-config.ini').write_text(
        '[System]\nis_new_3ds='+str(args.model=='new').lower()+'\nis_new_3ds\\default=false\n'
        '[UI]\nfirstStart=false\ncalloutFlags=4294967295\n'
        '[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n'
        '[Layout]\nrender_3d=1\nrender_3d\\default=false\nfactor_3d=75\nfactor_3d\\default=false\n'
        '[WebService]\nenable_telemetry=false\n')
    link = sd/'rom.nds'
    if not link.exists():
        link.symlink_to(rom)
    elif link.resolve() != rom:
        parser.error('Existing test SD ROM does not match --rom')
    rom_hash = hashlib.sha256(rom.read_bytes()).hexdigest()
    env = {**os.environ, 'XDG_DATA_HOME': str(runtime/'data'),
           'XDG_CONFIG_HOME': str(runtime/'config'), 'XDG_CACHE_HOME': str(runtime/'cache')}
    app = args.azahar.resolve(strict=True)
    installed = subprocess.run([str(app), '-i', str(args.cia.resolve())], env=env, cwd=runtime,
                               text=True, capture_output=True, timeout=30)
    (runtime/'install.log').write_text(installed.stdout + installed.stderr)
    installed.check_returncode()
    image = data/'sdmc/Nintendo 3DS'/('0'*32)/('0'*32)/'title/00040000/0f646d00/content/00000000.app'
    image_hash = hashlib.sha256(image.read_bytes()).hexdigest()
    if image_hash != verified['content_sha256']:
        raise RuntimeError('Installed content does not match CIA')
    log = sd/'platform.log'
    if log.exists():
        log.rename(sd/f'platform-{time.time_ns()}.log')
    cold = not (sd/'assets.pak').exists()
    start = time.monotonic()
    with (runtime/'process.log').open('w') as stream:
        child = subprocess.Popen([str(app), '-w', str(image)], env=env, cwd=runtime,
                                 stdout=stream, stderr=subprocess.STDOUT)
        (runtime/'pid').write_text(str(child.pid))
        print(f'Azahar PID {child.pid}: {runtime}', flush=True)
        bounded = False
        try:
            child.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            bounded = True
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill(); child.wait()
    platform_log = log.read_text() if log.exists() else ''
    result = {'cia_sha256': verified['sha256'], 'installed_app_sha256': image_hash,
              'game_code': game_code,
              'profile': args.model, 'cold_import': cold, 'elapsed_seconds': time.monotonic()-start,
              'exit_code': child.returncode, 'bounded_stop': bounded,
              'platform_log': platform_log, 'physical_hardware': False,
              'rom_unchanged': rom_hash == hashlib.sha256(rom.read_bytes()).hexdigest(),
              'reached_renderer': 'render READY' in platform_log, 'clean_app_exit': 'exit clean' in platform_log,
              'original_heap_verified': 'original heap READY arena_bytes=4194304 allocation_round_trip=pass' in platform_log,
              'game_data_verified': ('game data READY handles='+str(2058 if game_code=='ASMP' else 1824)+
                                     ' castle_groups=15 castle_entrances=14') in platform_log}
    (runtime/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    if not result['rom_unchanged'] or not result['reached_renderer']:
        raise SystemExit(1)
    if args.require_original_heap and not result['original_heap_verified']:
        raise SystemExit('The original allocator did not pass its native startup check')
    if not result['game_data_verified']:
        raise SystemExit('The regional game data did not pass its native startup check')


if __name__ == '__main__':
    main()
