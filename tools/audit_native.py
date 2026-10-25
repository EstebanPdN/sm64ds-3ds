#!/usr/bin/env python3
"""Compile the enrolled ARM9 source corpus for ARM11; not a game/link test.

No PC compatibility macro, dummy symbols or disabled layout assertions are used.
The resulting objects are diagnostic artifacts, not linked into the preview.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error('jobs must be 1..16')
    root = Path(__file__).resolve().parents[1]
    upstream = root / 'upstream'
    output = root / 'build/native-audit'
    output.mkdir(parents=True, exist_ok=True)
    compiler = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro')) / 'devkitARM/bin/arm-none-eabi-g++'
    source_names = set()
    for manifest in (upstream / 'config/arm9').rglob('delinks.txt'):
        source_names.update(re.findall(r'^(src/[^\r\n:]+\.(?:cpp|c)):', manifest.read_text(), re.M))
    missing = sorted(name for name in source_names if not (upstream / name).is_file())
    source_names.difference_update(missing)
    flags = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
             '-D__3DS__', '-Os', '-fno-strict-aliasing', '-ffunction-sections',
             '-fdata-sections', '-Wno-builtin-declaration-mismatch', '-I', str(upstream / 'include')]

    def compile_one(name):
        source = upstream / name
        cpp = source.suffix == '.cpp' or source.read_bytes().lstrip().startswith(b'//cpp')
        object_file = output / (hashlib.sha256(name.encode()).hexdigest()[:20] + '.o')
        command = [str(compiler), *flags, '-x', 'c++' if cpp else 'c',
                   '-std=gnu++17' if cpp else '-std=gnu11', '-c', str(source), '-o', str(object_file)]
        result = subprocess.run(command, capture_output=True, text=True)
        entry = {'source': name, 'compiled': result.returncode == 0}
        if result.returncode:
            entry['diagnostics'] = result.stderr.replace(str(upstream) + '/', '')[:16000]
        else:
            entry['object_bytes'] = object_file.stat().st_size
        return entry

    started = time.monotonic()
    entries = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for entry in pool.map(compile_one, sorted(source_names)):
            entries.append(entry)
            if len(entries) % 500 == 0:
                print(f'{len(entries)}/{len(source_names)} compiled or diagnosed', flush=True)
    report = {
        'scope': 'Enrolled ARM9 sources compiled to ARM11 objects; no link or execution',
        'upstream_commit': subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip(),
        'compiler': subprocess.check_output([str(compiler), '--version'], text=True).splitlines()[0],
        'flags': flags, 'seconds': time.monotonic() - started,
        'total': len(entries), 'compiled': sum(x['compiled'] for x in entries),
        'missing_sources': missing, 'entries': entries,
    }
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'entries'}, indent=2))
    return 0 if report['compiled'] == report['total'] and not missing else 1


if __name__ == '__main__':
    raise SystemExit(main())
