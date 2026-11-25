#!/usr/bin/env python3
"""Prepare BUILD 142's companion and native renderer banks from an owner's ASMP rev0 ROM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from tools.setup_dependencies import checkout
from tools.native_renderer import beta_data
from tools.sm64ds_packer.core import HEADER, convert, recipes

ROOT = Path(__file__).resolve().parents[2]
BANK_SOURCE = ROOT / 'third_party/sm64ds-3ds/sm64ds-decomp'
BANK_SET_SHA256 = 'aafffb633f8b214cda380aef994e1f98515f2bfdadb3c2edc5c69f656c7f6015'


def verify_bank_source():
    if not (BANK_SOURCE / '.git').exists():
        raise ValueError('Run python3 tools/setup_dependencies.py --backend-only before preparing SD data')
    lock = json.loads((ROOT / 'reference-native.lock.json').read_text())
    checkout(lock['decomp_repository'], lock['decomp_commit'], BANK_SOURCE, check_only=True)


def unpack_model_inputs(companion, directory):
    """The model cooker needs the archive index and named BMDs from the verified companion."""
    with companion.open('rb') as stream:
        stream.seek(HEADER.size)
        for entry in recipes()['files']:
            path = Path(entry['path'])
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('Invalid companion recipe path')
            size = entry['size']
            if path == Path('build/assets/archives.tsv') or (
                    path.is_relative_to('extracted/dsd/archives') and path.suffix.lower() == '.bmd'):
                dest = directory / path
                dest.parent.mkdir(parents=True, exist_ok=True)
                data = stream.read(size)
                if hashlib.sha256(data).hexdigest() != entry['sha256']:
                    raise ValueError(f'Companion member failed verification: {path}')
                dest.write_bytes(data)
            else:
                stream.seek(size, os.SEEK_CUR)


def prepare(rom, output, motion_source):
    rom, output = rom.resolve(strict=True), output.resolve()
    if output.exists() or not output.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('Choose a new output folder inside build/')
    verify_bank_source()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix='sm64ds-sd-') as work:
        work = Path(work)
        ready = work / 'sm64ds'
        native = ready / 'native'
        native.mkdir(parents=True)
        companion = ready / 'prepared.sm64ds'
        convert(rom, companion)
        unpack_model_inputs(companion, work)
        cooks = {
            'author_catalog': ['--archives', work / 'build/assets/archives.tsv',
                               '--archive-root', work / 'extracted/dsd'],
            'particles': [], 'menu2d': [], 'backgrounds': ['--variants'],
            'font': [], 'menu_obj': [], 'menu_backdrops': [],
        }
        for module, options in cooks.items():
            cooked = work / module
            subprocess.run([sys.executable, '-m', f'tools.native_renderer.{module}',
                            '--rom', rom, '--output', cooked, *options], cwd=ROOT,
                           env={**os.environ, 'SM64DS_COOK_APPROXIMATE': '1'},
                           stdout=subprocess.DEVNULL, check=True)
            for path in cooked.iterdir():
                if path.suffix in ('.nmd', '.nim'):
                    if (native / path.name).exists():
                        raise ValueError(f'Duplicate bank: {path.name}')
                    shutil.move(path, native / path.name)
        digest = hashlib.sha256()
        for bank in sorted(native.iterdir()):
            digest.update(bank.name.encode('ascii') + b'\0' + hashlib.sha256(bank.read_bytes()).digest())
        if digest.hexdigest() != BANK_SET_SHA256:
            raise ValueError('Generated native banks differ from BUILD 142')
        beta_data.prepare(rom, BANK_SOURCE, work / 'author_catalog', work,
                          ready / 'build142-data.bin', motion_source)
        ready.rename(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path, help='new directory under build/')
    parser.add_argument('--motion-source', type=Path,
                        default=ROOT / 'build/dependencies/sm64-port-3ds',
                        help='reuse a clean checkout at the required public motion source pin')
    args = parser.parse_args()
    try:
        prepare(args.rom, args.output, args.motion_source.resolve())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'SD preparation failed: {error}\n')
    print(f'Prepared the companion and verified native banks in {args.output}')
