#!/usr/bin/env python3
"""Check the public Git tree, pinned source, maintained links and exclusions."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def git(*args, **kwargs):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], **kwargs)


def main():
    layout = json.loads((ROOT / 'docs/repository-layout.json').read_text())
    entries = {}
    for record in git('ls-files', '--stage', '-z').decode().split('\0'):
        if record:
            metadata, name = record.split('\t', 1)
            mode, oid, stage = metadata.split()
            if stage != '0':
                raise ValueError('Unmerged path: ' + name)
            entries[name] = (mode, oid)
    if not entries:
        raise ValueError('No tracked/staged source; stage the intended public tree first')
    errors = []
    forbidden = {'.nds', '.sm64ds', '.sav', '.dmp', '.vm', '.render', '.trace',
                 '.lcd', '.upper', '.lower', '.cia', '.cxi', '.3dsx', '.elf',
                 '.o', '.a', '.pak', '.log', '.zip', '.bin', '.part'}
    secrets = [re.compile(rb'gh[pousr]_[A-Za-z0-9]{30,}'),
               re.compile(rb'github_pat_[A-Za-z0-9_]{40,}'),
               re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')]
    total = 0
    for name, (mode, oid) in entries.items():
        rel = PurePosixPath(name)
        if len(rel.parts) == 1 and name not in layout['root_files']:
            errors.append('Unexpected root file: ' + name)
        elif len(rel.parts) > 1 and rel.parts[0] not in layout['directories']:
            errors.append('Unexpected root directory: ' + name)
        if mode == '160000':
            if layout['submodules'].get(name) != oid:
                errors.append('Unknown or changed submodule: ' + name)
            continue
        if mode not in ('100644', '100755'):
            errors.append('Unsupported file mode (including symlink): ' + name)
            continue
        if rel.suffix.lower() in forbidden or rel.name.startswith('.env'):
            errors.append('Private/generated file type: ' + name)
        if any(part in {'__pycache__', '.venv', 'node_modules', '.git'} for part in rel.parts):
            errors.append('Cache/repository metadata: ' + name)
        path = ROOT / name
        if not path.is_file() or path.is_symlink():
            errors.append('Missing or linked tracked file: ' + name)
            continue
        raw = path.read_bytes()
        total += len(raw)
        if len(raw) > 10 * 1024 * 1024:
            errors.append('Oversized source file needs explicit review: ' + name)
        if b'\0' not in raw[:8192] and any(pattern.search(raw) for pattern in secrets):
            errors.append('Possible credential; inspect locally: ' + name)

    lock = json.loads((ROOT / 'reference-native.lock.json').read_text())
    for name, expected in lock['vendored_files_sha256'].items():
        rel = 'third_party/sm64ds-3ds/' + name
        path = ROOT / rel
        if rel not in entries or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            errors.append('Missing or changed pinned provider file: ' + name)
    for name, expected in layout['submodules'].items():
        if entries.get(name) != ('160000', expected):
            errors.append('Missing expected Git submodule pin: ' + name)

    # These probes must stay ignored even on a clean checkout. No probe files
    # are created, and private directories are never enumerated or read.
    probes = ['build/roms/example.nds', 'dist/example.cia', 'dumps/example.json',
              'local/settings.ini', '.env', 'game.sm64ds', 'crash.dmp',
              'save.sav', 'memory.vm', 'tools/sm64ds_packer/dist/example.exe']
    ignored = set(git('check-ignore', '--no-index', '--stdin', input='\n'.join(probes).encode()).decode().splitlines())
    for name in set(probes) - ignored:
        errors.append('Private output not ignored: ' + name)

    for name in layout['maintained_docs']:
        if name not in entries:
            errors.append('Maintained document not tracked: ' + name)
            continue
        path = ROOT / name
        text = path.read_text()
        destinations = re.findall(r'\]\(([^\s)]+)(?:\s+[^)]*)?\)', text)
        destinations += re.findall(r'(?:src|href)="([^"]+)"', text)
        for destination in destinations:
            parsed = urlsplit(destination)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            target = (path.parent / unquote(parsed.path)).resolve()
            if not target.is_relative_to(ROOT) or not target.exists():
                errors.append(f'Broken maintained link: {name} -> {destination}')
            elif target.is_file() and target.relative_to(ROOT).as_posix() not in entries:
                errors.append(f'Link only exists locally: {name} -> {destination}')
    readme = (ROOT / 'README.md').read_text()
    if readme.count('This project was developed with the help of') != 1 or '(AI_DEVELOPMENT.md)' not in readme:
        errors.append('Missing or duplicated approved development disclosure')
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print(f'Repository checks pass: {len(entries)} paths, {total:,} bytes; '
          f'{len(lock["vendored_files_sha256"])} pinned provider files and '
          f'{len(layout["maintained_docs"])} maintained documents.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
