#!/usr/bin/env python3
"""Accept the native-renderer checkout's current file hashes in its overlay receipt.

refresh_native_overlay.py refuses to run when a file listed in
native-renderer-overlay.json differs from its recorded hash. After an
intentional edit inside the generated checkout (mirror the same change in
the matching tools/prepare_integrated_*.py first), run this from anywhere.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'build/native-renderer/game-runtime-20261026-r1/source'


def main():
    receipt = SOURCE / 'native-renderer-overlay.json'
    record = json.loads(receipt.read_text(encoding='utf-8'))
    changed = []
    for name, expected in record['files'].items():
        current = hashlib.sha256((SOURCE / name).read_bytes()).hexdigest()
        if current != expected:
            record['files'][name] = current
            changed.append(name)
    receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('rehashed', changed)


if __name__ == '__main__':
    main()
