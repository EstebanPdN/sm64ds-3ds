#!/usr/bin/env python3
"""Prepare public source pins; never download ROMs or animation/game data."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(list(map(str, args)), text=True).strip()


def checkout(repository, commit, target, check_only):
    if not (target / '.git').exists():
        if check_only:
            raise ValueError(f'Missing checkout: {target}')
        if target.exists():
            raise ValueError(f'Refusing to replace an existing directory: {target}')
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--no-checkout', repository, str(target)], check=True)
        subprocess.run(['git', '-C', str(target), 'checkout', '--detach', commit], check=True)
    actual = run('git', '-C', target, 'rev-parse', 'HEAD')
    if actual != commit:
        raise ValueError(f'Revision mismatch at {target}: {actual}; expected {commit}')
    if run('git', '-C', target, '-c', 'core.fileMode=false', 'status', '--porcelain', '--untracked-files=no'):
        raise ValueError(f'Tracked content has local edits: {target}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify only; do not clone or update anything')
    parser.add_argument('--backend-only', action='store_true', help='Prepare only the backend decompilation needed by source/host contract tests')
    args = parser.parse_args()
    lock = json.loads((ROOT / 'reference-native.lock.json').read_text())
    vendor = ROOT / 'third_party/sm64ds-3ds'
    for name, expected in lock['vendored_files_sha256'].items():
        path = vendor / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'Pinned provider changed or missing: {name}')
    checkout(lock['decomp_repository'], lock['decomp_commit'], vendor / 'sm64ds-decomp', args.check)
    if args.backend_only:
        print(f'Backend public source verified; {len(lock["vendored_files_sha256"])} provider files match.')
        return
    # This is the sole top-level submodule; its contents are public source.
    lib = ROOT / 'third_party/libsm64-motion'
    if not (lib / '.git').exists() and not args.check:
        subprocess.run(['git', 'submodule', 'update', '--init', '--', 'third_party/libsm64-motion'], cwd=ROOT, check=True)
    checkout('https://github.com/libsm64/libsm64.git',
             'fd11813208272b4271d92bd92feb8f3fdbe61be5', lib, True)
    # Import only after dependency preparation. The module honors the explicit
    # override and the historical local checkout without moving either tree.
    from movement_lab.port_source import PORT, COMMIT
    checkout('https://github.com/mkst/sm64-port.git', COMMIT, PORT, args.check)
    print(f'Public source pins verified; {len(lock["vendored_files_sha256"])} provider files match.')
    print('Motion source:', PORT)
    print('Full motion builds also require 209 local animation definitions; see docs/BUILDING.md.')


if __name__ == '__main__':
    main()
