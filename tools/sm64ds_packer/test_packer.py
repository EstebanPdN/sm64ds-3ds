#!/usr/bin/env python3
"""Private ROM fixture tests; no ROM bytes or output packaged with utility."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from core import HEADER, Cancelled, convert, recipes


class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.output = self.directory / 'Mário file.sm64ds'
    def tearDown(self):
        self.temporary.cleanup()
    def test_canonical_output(self):
        manifest = recipes()
        result = convert(ROM, self.output)
        self.assertEqual(result['bytes'], 15686873)
        with self.output.open('rb') as archive:
            magic, version, count, total, schema, metadata, reserved = HEADER.unpack(archive.read(128))
            self.assertEqual((magic, version, count, total, reserved), (b'SM64DSP1', 1, 2450, 15686745, bytes(24)))
            self.assertEqual(schema.decode(), manifest['schema_id'])
            self.assertEqual(metadata.decode(), manifest['metadata_sha256'])
            for entry in manifest['files']:
                payload = archive.read(entry['size'])
                self.assertEqual(hashlib.sha256(payload).hexdigest(), entry['sha256'])
                self.assertEqual(payload, (REFERENCE / entry['path']).read_bytes())
            self.assertEqual(archive.read(), b'')
    def test_wrong_rom_and_size_leave_existing_output(self):
        self.output.write_bytes(b'original')
        bad = self.directory / 'bad.NdS'
        bad.write_bytes(ROM.read_bytes())
        with bad.open('r+b') as target:
            target.seek(-1, os.SEEK_END)
            target.write(bytes([target.read(1)[0] ^ 1]))
        for candidate in (bad, self.directory / 'short.nds'):
            if candidate.name == 'short.nds':
                candidate.write_bytes(b'bad')
            with self.assertRaises(ValueError):
                convert(candidate, self.output)
            self.assertEqual(self.output.read_bytes(), b'original')
            self.assertEqual(list(self.directory.glob('*.part')), [])
    def test_cancellation_at_all_phases(self):
        for phase in ('Checking ROM', 'Creating .sm64ds', 'Verifying .sm64ds'):
            with self.subTest(phase=phase):
                self.output.write_bytes(b'original')
                cancellation = threading.Event()
                def progress(current, done, total):
                    if current == phase:
                        cancellation.set()
                with self.assertRaises(Cancelled):
                    convert(ROM, self.output, progress, cancellation)
                self.assertEqual(self.output.read_bytes(), b'original')
                self.assertEqual(list(self.directory.glob('*.part')), [])
    def test_input_alias_and_failure(self):
        alias = self.directory / 'alias.sm64ds'
        alias.symlink_to(ROM)
        before = hashlib.sha256(ROM.read_bytes()).hexdigest()
        with self.assertRaises(ValueError):
            convert(ROM, alias)
        with self.assertRaises(FileNotFoundError):
            convert(ROM, self.directory / 'missing' / 'game.sm64ds')
        self.assertEqual(hashlib.sha256(ROM.read_bytes()).hexdigest(), before)
    def test_corrupt_recipe_payload_fails_before_commit(self):
        import core
        manifest = recipes()
        manifest['files'][0]['sha256'] = '0' * 64
        original = core.recipes
        core.recipes = lambda: manifest
        try:
            self.output.write_bytes(b'original')
            with self.assertRaises(ValueError):
                convert(ROM, self.output)
            self.assertEqual(self.output.read_bytes(), b'original')
            self.assertEqual(list(self.directory.glob('*.part')), [])
        finally:
            core.recipes = original


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('rom', type=Path)
    parser.add_argument('reference_repo', type=Path)
    args = parser.parse_args()
    ROM, REFERENCE = args.rom.resolve(), args.reference_repo.resolve()
    unittest.main(argv=['test_packer.py'], verbosity=2)
