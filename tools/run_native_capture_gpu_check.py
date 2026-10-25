#!/usr/bin/env python3
"""Bounded isolated Azahar GPU fixture; never game or physical-console proof."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--azahar', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=40)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error('Bound each fixture execution to 1..60 seconds')
    root = Path(__file__).resolve().parents[1]
    runtime = root / 'build/native-capture-gpu-azahar'
    config = runtime / 'config/azahar-emu'
    data = runtime / 'data/azahar-emu'
    sd = data / 'sdmc/3ds/sm64ds/capture-check'
    for path in (config, sd, runtime / 'cache'):
        path.mkdir(parents=True, exist_ok=True)
    profile = config / 'qt-config.ini'
    if not profile.exists():
        profile.write_text(
            '[System]\nis_new_3ds=false\nis_new_3ds\\default=false\n'
            '[UI]\nfirstStart=false\ncalloutFlags=4294967295\n'
            '[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n'
            '[Layout]\nrender_3d=0\nrender_3d\\default=false\n'
            '[WebService]\nenable_telemetry=false\n')
    attempt = runtime / ('attempt-' + str(time.time_ns()))
    attempt.mkdir()
    image = attempt / 'sm64ds-capture-check.3dsx'
    shutil.copyfile(root / 'build/3ds/source/ctr/sm64ds-capture-check.3dsx', image)
    shutil.copyfile(root / 'build/3ds/source/ctr/sm64ds-capture-check.elf', attempt / 'sm64ds-capture-check.elf')
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    latest = runtime / 'latest.json'
    if latest.exists() and json.loads(latest.read_text())['image_sha256'] == digest:
        raise SystemExit('This exact fixture binary was already attempted; resolve a measured cause before rerunning.')
    for old in sd.glob('*.rgba'):
        old.unlink()
    logfile = sd / 'result.log'
    if logfile.exists():
        logfile.unlink()
    emulator_log = data / 'log/azahar_log.txt'
    if emulator_log.exists():
        emulator_log.unlink()
    env = {**os.environ, 'XDG_CONFIG_HOME': str(runtime / 'config'),
           'XDG_DATA_HOME': str(runtime / 'data'), 'XDG_CACHE_HOME': str(runtime / 'cache')}
    began = time.monotonic()
    reason, fault, offset = 'process_exit', None, 0
    with (attempt / 'process.log').open('w') as output:
        child = subprocess.Popen([str(args.azahar.resolve()), '-w', str(image)], cwd=runtime,
                                 env=env, stdout=output, stderr=subprocess.STDOUT)
        print(f'Capture GPU fixture PID {child.pid}: {attempt}', flush=True)
        while child.poll() is None:
            application_log = logfile.read_text(errors='replace') if logfile.exists() else ''
            if 'exit=' in application_log:
                reason = 'application_terminal_record'
                break
            if emulator_log.exists():
                with emulator_log.open('r', errors='replace') as stream:
                    stream.seek(offset)
                    chunk = stream.read(1024*1024)
                    offset = stream.tell()
                for line in chunk.splitlines():
                    if 'UnmappedAccess:' in line or 'Exception Type:' in line:
                        fault = line
                        break
                if fault:
                    reason = 'emulator_memory_or_cpu_fault'
                    break
            if time.monotonic() - began >= args.seconds:
                reason = 'time_bound'
                break
            time.sleep(.2)
        if child.poll() is None:
            if fault:
                child.kill()
                child.wait()
            else:
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
    application_log = logfile.read_text(errors='replace') if logfile.exists() else ''
    files = {}
    for path in sorted(sd.glob('*.rgba')):
        shutil.copyfile(path, attempt / path.name)
        files[path.name] = {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    comparisons = {}
    for fixture in ('layers-only', 'layers-and-geometry'):
        actual, expected = sd / (fixture + '-actual.rgba'), sd / (fixture + '-expected.rgba')
        if actual.exists() and expected.exists():
            a, e = actual.read_bytes(), expected.read_bytes()
            comparisons[fixture] = {'same_bytes': a == e, 'bytes': len(a),
                'mismatched_bytes': sum(x != y for x, y in zip(a, e)) + abs(len(a) - len(e))}
    passed = 'exit=passed' in application_log and len(comparisons) == 2 and all(
        item['same_bytes'] and item['bytes'] == 256*192*4 for item in comparisons.values())
    result = {'status': 'passed' if passed else 'failed',
        'scope': 'Synthetic GPU fixture on Old-profile Azahar; no game code executed',
        'physical_hardware': False, 'gameplay_executed': False, 'game_fps_verified': False,
        'model': 'old', 'image_sha256': digest, 'attempt': str(attempt),
        'elapsed_seconds': time.monotonic()-began, 'stop_reason': reason,
        'process_exit_code': child.returncode, 'first_emulator_fault': fault,
        'application_log': application_log, 'images': files, 'comparisons': comparisons}
    for path in (logfile, emulator_log):
        if path.exists():
            shutil.copyfile(path, attempt / path.name)
    encoded = json.dumps(result, indent=2)+'\n'
    (attempt / 'result.json').write_text(encoded)
    latest.write_text(encoded)
    print(encoded)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
