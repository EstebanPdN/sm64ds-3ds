"""Portable SDK paths and fail-closed generated linker preparation."""
from pathlib import Path
import os
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from devkitpro_tools import arm_tool, sdk_tool
from prepare_integrated_toolchain import prepare
from build_version import reserve
from build_integrated_native import digest, verify_closed_candidate, workspace_snapshot, require_unchanged_snapshot


class BuildToolsTests(unittest.TestCase):
    def test_build_number_is_reserved_and_not_reused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'source/integrated_native/options/build_version.hpp'
            path.parent.mkdir(parents=True)
            path.write_text('#pragma once\n#define SM64DS_BUILD_NUMBER 22\n')
            self.assertEqual(reserve(root), 23)
            self.assertEqual(reserve(root), 24)
            self.assertIn('#define SM64DS_BUILD_NUMBER 24', path.read_text())
            path.write_text('invalid contract\n')
            with self.assertRaises(ValueError): reserve(root)
            self.assertEqual(path.read_text(), 'invalid contract\n')

    def test_freeze_rejects_changed_added_or_removed_build_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'tools').mkdir()
            p=root/'tools/build.py';p.write_text('original\n')
            snapshot=workspace_snapshot(root);require_unchanged_snapshot(snapshot,root)
            p.write_text('changed\n')
            with self.assertRaises(ValueError):require_unchanged_snapshot(snapshot,root)
            p.write_text('original\n');new=root/'tools/new.py';new.write_text('new\n')
            with self.assertRaises(ValueError):require_unchanged_snapshot(snapshot,root)
            new.unlink();p.unlink()
            with self.assertRaises(ValueError):require_unchanged_snapshot(snapshot,root)

    def test_reuse_checks_every_executable_copy_and_frozen_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);candidate=root/'candidate';candidate.mkdir()
            elf=root/'new.elf';elf.write_bytes(b'ELF oracle')
            image=root/'new.3dsx';image.write_bytes(b'3DSX oracle')
            payloads={'sm64ds-integrated.elf':elf.read_bytes(),
                      'sm64ds-integrated.3dsx':image.read_bytes(),
                      'sd/3ds/sm64ds/sm64ds.3dsx':image.read_bytes(),
                      'source-evidence/fixture':b'frozen source'}
            for name,raw in payloads.items():
                p=candidate/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
            build={'status':'integrated_built_and_audited_emulator_pending','rom_only':True,
                   'ELF_sha256':digest(elf),'image_sha256':digest(image),
                   'files':{name:{'bytes':len(raw),'sha256':digest(candidate/name)}
                            for name,raw in payloads.items()}}
            (candidate/'BUILD.json').write_text(json.dumps(build))
            self.assertEqual(verify_closed_candidate(candidate,elf,image),build)
            for name,raw in payloads.items():
                p=candidate/name;p.write_bytes(raw[:-1])
                with self.assertRaises(ValueError):verify_closed_candidate(candidate,elf,image)
                p.unlink()
                with self.assertRaises(ValueError):verify_closed_candidate(candidate,elf,image)
                p.write_bytes(raw)
            name='sd/3ds/sm64ds/sm64ds.3dsx'
            del build['files'][name]
            (candidate/'BUILD.json').write_text(json.dumps(build))
            with self.assertRaises(ValueError):verify_closed_candidate(candidate,elf,image)

    def test_sdk_with_spaces_and_explicit_arm_override(self):
        with tempfile.TemporaryDirectory(prefix='sm64ds SDK ') as tmp:
            root = Path(tmp)
            compiler = root / 'custom ARM/bin/arm-none-eabi-g++'
            packer = root / 'tools/bin/3dsxtool'
            for path in (compiler, packer):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#!/bin/sh\nexit 0\n');path.chmod(0o755)
            with patch.dict(os.environ, {'DEVKITPRO': str(root), 'DEVKITARM': str(compiler.parent.parent)}):
                self.assertEqual(arm_tool('g++'), str(compiler.resolve()))
                self.assertEqual(sdk_tool('3dsxtool'), str(packer.resolve()))
                with self.assertRaises(FileNotFoundError):
                    arm_tool('not-an-installed-tool')

    def test_explicit_missing_sdk_never_falls_back_to_other_compiler(self):
        with patch.dict(os.environ, {'DEVKITPRO': '/missing/sm64ds-sdk', 'DEVKITARM': '/missing/sm64ds-arm'}):
            with patch('devkitpro_tools.shutil.which', return_value='/other/compiler'):
                with self.assertRaises(FileNotFoundError):arm_tool('g++')
                with self.assertRaises(FileNotFoundError):sdk_tool('3dsxtool')

    def test_both_final_link_helpers_are_portable_and_drift_is_transactional(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp);(root / 'build3ds').mkdir()
            names = ('build_game.py', 'build_menu_probe.py')
            for name in names:
                shutil.copyfile(ROOT / 'third_party/sm64ds-3ds/build3ds' / name, root / 'build3ds' / name)
            pristine = {name: (root / 'build3ds' / name).read_bytes() for name in names}
            self.assertEqual(len(prepare(root)), 3)
            for name in names:
                self.assertNotIn('/opt/devkitpro', (root / 'build3ds' / name).read_text())
            for name in names:(root / 'build3ds' / name).write_bytes(pristine[name])
            bad = root / 'build3ds/build_menu_probe.py';bad.write_text('changed contract\n')
            with self.assertRaises(ValueError):prepare(root)
            self.assertEqual((root / 'build3ds/build_game.py').read_bytes(), pristine['build_game.py'])

    def test_optimized_python_cannot_disable_candidate_assertions(self):
        result = subprocess.run([sys.executable, '-O', str(ROOT / 'tools/build_integrated_native.py'), '--help'],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('verification requires Python assertions', result.stderr)


if __name__ == '__main__':
    unittest.main()
