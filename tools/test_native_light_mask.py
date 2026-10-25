#!/usr/bin/env python3
"""Prove POLYGON_ATTR's light mask changes actual emitted vertex colours.

Runs the full host game_draw regression under ASan/UBSan. A temporary baseline
removes only the reviewed light-mask assignment and must fail the new colour
assertion; the production source is never modified. This is host decoder
evidence, not a game image, emulator run or physical-console measurement.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('tests/test_game_draw.cpp', 'source/core/game_draw.cpp',
           'source/core/game_geometry.cpp', 'source/core/game_textures.cpp',
           'source/vendor/ntr/gx.cpp', 'source/vendor/ntr/texture.cpp')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-gx-audit/light-mask-test.json')
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    original = (ROOT/'source/vendor/ntr/gx.cpp').read_text()
    assignment = 'g.light_mask = p[0] & 15;'
    assert original.count(assignment) == 1, 'Reviewed POLYGON_ATTR assignment changed'
    hashes = {source: sha(ROOT/source) for source in SOURCES}
    with tempfile.TemporaryDirectory(prefix='sm64ds-light-mask-') as directory:
        temp = Path(directory)
        baseline = temp/'gx-before-light-mask.cpp'
        baseline.write_text(original.replace(assignment, ''))
        flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                 '-I'+str(ROOT/'include'), '-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
        environment = os.environ.copy()
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1'
        def compile_run(path, decoder):
            sources = [str(decoder) if source == 'source/vendor/ntr/gx.cpp' else str(ROOT/source)
                       for source in SOURCES]
            subprocess.run([args.compiler, *flags, *sources, '-o', str(path)], check=True, cwd=ROOT)
            return subprocess.run([str(path)], cwd=temp, env=environment,
                                  text=True, capture_output=True)
        before = compile_run(temp/'before', baseline)
        assert before.returncode in (-signal.SIGABRT, 128+signal.SIGABRT), before
        assert 'vertex.color==lit_colors[mask]' in before.stderr, before.stderr
        assert 'AddressSanitizer' not in before.stderr and 'runtime error' not in before.stderr
        after = compile_run(temp/'after', ROOT/'source/vendor/ntr/gx.cpp')
        assert after.returncode == 0 and not after.stderr, after
        baseline_sha = sha(baseline)
    assert hashes == {source: sha(ROOT/source) for source in SOURCES}, 'Sources changed during proof'
    report = {
        'status': 'passed', 'scope': 'host GX decoder vertex colours and existing game_draw regression',
        'sanitizers': ['address', 'undefined'],
        'masks': [0, 1, 2, 3], 'expected_argb': ['0xff000000', '0xffff0000', '0xff00ff00', '0xffffff00'],
        'vertices_checked': 12,
        'input_commands': ['LIGHT_VECTOR', 'LIGHT_COLOR', 'DIF_AMB', 'SPE_EMI',
                           'POLYGON_ATTR', 'NORMAL', 'BEGIN_VTXS', 'VTX_10', 'END_VTXS'],
        'baseline': {'change': 'removed only g.light_mask = p[0] & 15; in temporary copy',
                     'sha256': baseline_sha, 'failed_expected_colour_assertion': True,
                     'returncode': before.returncode},
        'fixed': {'returncode': after.returncode, 'full_game_draw_regression_passed': True},
        'source_sha256': hashes,
        'emulator_executed': False, 'physical_hardware': False,
        'gameplay_verified': False, 'performance_verified': False,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
