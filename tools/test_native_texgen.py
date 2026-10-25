#!/usr/bin/env python3
"""Check emitted TEXGEN2 UVs against NITRO's integer formula, without an emulator."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GX = ROOT/'source/vendor/ntr/gx.cpp'
BASELINE = ROOT/'build/native-gx-audit/texgen2-baseline/gx.cpp'
SNAPSHOT = ROOT/'build/native-runtime-azahar/attempt-1791596592223444000'
SOURCES = ['tools/native_texgen_probe.cpp', 'source/vendor/ntr/gx.cpp',
           'source/vendor/ntr/texture.cpp', 'include/ntr/gx.h']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def signed(value, bits):
    value &= (1 << bits)-1
    return value-(1 << bits) if value & (1 << (bits-1)) else value


def identity():
    return [4096 if r == c else 0 for r in range(4) for c in range(4)]


def multiply(left, right):
    # Python's unbounded integers deliberately differ from production's
    # modular accumulator. Truncate once, after the entire dot product.
    return [signed(sum(left[r*4+k]*right[k*4+c] for k in range(4)) >> 12, 32)
            for r in range(4) for c in range(4)]


def reshape(values):
    result = identity()
    columns = 4 if len(values) == 16 else 3
    rows = 3 if len(values) == 9 else 4
    for r in range(rows):
        for c in range(columns):
            result[r*4+c] = signed(values[r*columns+c], 32)
    return result


def primary_uv(matrix, normal, raw):
    # Original NITRO V1.44 pp. 213-214 expression, retaining its <<3,
    # <<12 and >>24 rather than simplifying to the production >>21 form.
    return [signed((sum(matrix[r*4+c]*(normal[r] << 3) for r in range(3))
                    + ((raw[c] << 12)*4096)) >> 24, 16)/16 for c in range(2)]


def packet(command, values=()):
    return [(command, value & 0xffffffff) for value in (values or [0])]


def triangle():
    return [(0x40, 0), (0x24, 0), (0x24, 64), (0x24, 64 << 10), (0x41, 0)]


def normal_word(normal):
    return sum((value & 1023) << (10*i) for i, value in enumerate(normal))


def raw_word(raw):
    return (raw[0] & 65535) | ((raw[1] & 65535) << 16)


def fixture_cases():
    cases = []

    def add(name, words, uv=(), positions=None, valid=True, command=0):
        cases.append(dict(name=name, words=words, uv=list(uv), positions=positions,
                          valid=valid, command=command))

    def normal_case(name, values, normals, raw=(96, 352), tail=(), transformed=False):
        setup = [(0x10, 3)] + packet(0x16, values)
        matrix = list(values)
        for command, parameters in tail:
            setup += packet(command, parameters)
            if command in (0x18, 0x19, 0x1a):
                matrix = multiply(reshape(parameters), matrix)
            elif command == 0x1b:
                scale = identity()
                scale[0], scale[5], scale[10] = parameters
                matrix = multiply(scale, matrix)
            elif command == 0x1c:
                translation = identity();translation[12:15] = parameters
                matrix = multiply(translation, matrix)
            elif command in (0x16, 0x17):
                matrix = reshape(parameters)
            elif command == 0x15:
                matrix = identity()
        if transformed:
            # Rotation and nonunit scaling in the directional matrix must not
            # be applied again to the NORMAL used for texture generation.
            vector = [0, 8192, 0, 0, -4096, 0, 0, 0,
                      0, 0, -12288, 0, 0, 0, 0, 4096]
            setup = [(0x10, 2)] + packet(0x16, vector) + setup
        words = setup + [(0x2a, 0x80000000), (0x22, raw_word(raw)), (0x40, 0)]
        expected = []
        for n, vertex in zip(normals, (0, 64, 64 << 10)):
            words += [(0x21, normal_word(n)), (0x24, vertex)]
            expected.append(primary_uv(matrix, n, raw))
        words.append((0x41, 0));add(name, words, expected)

    observed = [0, -0x100000, 0, 0, -0x100000, 0, 0, 0,
                0, 0, 0x10000, 0, 0, 0, 0, 0x1000]
    normals = [(511, 0, 0), (0, -512, 0), (73, 146, 0)]
    normal_case('primary-precision', observed, normals)
    normal_case('original-normal-before-directional-transform', observed, normals,
                transformed=True)
    normal_case('nonunit-normals', observed,
                [(511, 511, 511), (-512, -512, -512), (0, 0, 0)])
    tiny = identity();tiny[0], tiny[1] = -1, 2097151
    normal_case('floor-negative-and-positive-fraction', tiny,
                [(1, 0, 0), (-1, 0, 0), (511, 511, -512)], raw=(0, 0))
    boundary = identity();boundary[0], boundary[1] = 2097152, -2097152
    normal_case('one-sixteenth-texel-threshold', boundary,
                [(1, 0, 0), (-1, 0, 0), (0, 0, 0)], raw=(-32768, 32767))
    extreme = [2147483647 if i % 2 else -2147483648 for i in range(16)]
    normal_case('signed-coordinate-wrap-not-saturation', extreme,
                [(511, 511, 511), (-512, -512, -512), (511, -512, 511)],
                raw=(32767, -32768))
    translated = observed[:];translated[12:15] = [0x7fffffff, -0x80000000, 123456789]
    normal_case('raw-texcoord-replaces-matrix-translation', translated, normals)
    normal_case('identity-resets-fixed-shadow', translated, normals,
                tail=[(0x15, [])])
    load43 = [71321, -945981, 0, -615793, 964289, 0,
              1719, -59116, 4096, 1234567, -2345678, 4096]
    normal_case('load4x3-fixed-shadow', observed, normals, tail=[(0x17, load43)])
    normal_case('scale-fixed-shadow', observed, normals,
                tail=[(0x1b, [4095, 4097, -8193])])
    normal_case('translation-does-not-replace-raw-texcoord', observed, normals,
                tail=[(0x1c, [0x7fffffff, -0x80000000, 9001])])

    rng = random.Random(0x95230000)
    for i in range(240):
        values = [rng.randint(-2147483648, 2147483647) for _ in range(16)]
        n = [tuple(rng.randint(-512, 511) for _ in range(3)) for _ in range(3)]
        raw = tuple(rng.randint(-32768, 32767) for _ in range(2))
        normal_case(f'random-loaded-matrix-{i}', values, n, raw)
    for i in range(120):
        values = [rng.randint(-2147483648, 2147483647) for _ in range(16)]
        tail = []
        for command, count in ((0x18, 16), (0x19, 12), (0x1a, 9), (0x1b, 3), (0x1c, 3)):
            tail.append((command, [rng.randint(-2147483648, 2147483647) for _ in range(count)]))
        n = [tuple(rng.randint(-512, 511) for _ in range(3)) for _ in range(3)]
        raw = tuple(rng.randint(-32768, 32767) for _ in range(2))
        normal_case(f'random-composed-matrix-{i}', values, n, raw, tail)

    # Commands alter generated UV only at their specified execution trigger.
    n = (127, -255, 99);raw = (96, 352);changed = (33, -19)
    words = [(0x10, 3)] + packet(0x16, observed) + [(0x2a, 0x80000000),
        (0x22, raw_word(raw)), (0x21, normal_word(n))] + triangle()
    words += [(0x15, 0)] + triangle()
    words += [(0x22, raw_word(changed))] + triangle()
    words += [(0x21, normal_word(n))] + triangle()
    add('generation-trigger-and-raw-texcoord-reset', words,
        [primary_uv(observed, n, raw)]*6 + [[v/16 for v in changed]]*3 +
        [primary_uv(identity(), n, changed)]*3)

    words = [(0x10, 3)] + packet(0x16, observed) + [(0x13, 0), (0x15, 0),
        (0x14, 0), (0x2a, 0x80000000), (0x22, raw_word(raw)), (0x21, normal_word(n))] + triangle()
    add('texture-store0-restores-fixed-shadow', words, [primary_uv(observed, n, raw)]*3)
    tex1 = identity();tex1[0], tex1[5] = 8192, 12288
    tex1[8], tex1[9], tex1[12], tex1[13] = 8192, 16384, 4096, -4096
    words = [(0x10, 3)] + packet(0x16, tex1) + [(0x13, 0), (0x15, 0),
        (0x14, 0), (0x2a, 0x40000000), (0x22, raw_word((16, 32)))] + triangle()
    add('texture-store0-restores-legacy-texgen1-matrix', words, [[2.1875, 6.1875]]*3)

    pos_a = identity();pos_a[12:15] = [3*4096, -2*4096, 5*4096]
    pos_b = identity();pos_b[12:15] = [-9*4096, 7*4096, -4*4096]
    words = [(0x10, 1)] + packet(0x16, pos_a) + [(0x13, 0)] + packet(0x16, pos_b)
    words += [(0x10, 3)] + packet(0x16, observed) + [(0x13, 0), (0x15, 0), (0x14, 0),
        (0x10, 1), (0x14, 0)] + triangle()
    add('texture-stack-keeps-position-bone-slot0', words, [[0, 0]]*3,
        positions=[[3, -2, 5], [4, -2, 5], [3, -1, 5]])

    add('texture-restore-before-store-explicit-reject', [(0x10, 3), (0x14, 0)],
        valid=False, command=0x14)
    for i in range(1, 32):
        add(f'texture-store-index-{i}-explicit-reject', [(0x10, 3), (0x13, i)],
            valid=False, command=0x13)
        add(f'texture-restore-index-{i}-explicit-reject', [(0x10, 3), (0x13, 0), (0x14, i)],
            valid=False, command=0x14)
    add('texture-push-explicit-reject', [(0x10, 3), (0x11, 0)], valid=False, command=0x11)
    add('texture-pop-explicit-reject', [(0x10, 3), (0x12, 1)], valid=False, command=0x12)
    add('texgen3-explicit-reject', [(0x2a, 0xc0000000)], valid=False, command=0x2a)
    add('texture-matrix-first-failure-latched', [(0x10, 3), (0x11, 0), (0x2a, 0xc0000000),
        (0x61, 0)], valid=False, command=0x11)
    return cases


COUNTS = {0: 0, 0x11: 0, 0x15: 0, 0x41: 0, 0x16: 16, 0x18: 16,
          0x17: 12, 0x19: 12, 0x1a: 9, 0x1b: 3, 0x1c: 3,
          0x70: 3, 0x23: 2, 0x71: 2, 0x34: 32}


def commands_from_draw_words(words):
    queue = [];params = [];port_command = None;port_params = []
    for index, (port, value) in enumerate(words):
        if port:
            count = COUNTS.get(port, 1)
            if count <= 1:
                yield index, port, [value] if count else []
            else:
                if port_command != port:
                    assert not port_params, 'Interrupted direct-port packet'
                    port_command = port
                port_params.append(value)
                if len(port_params) == count:
                    yield index, port, port_params;port_params = [];port_command = None
        else:
            if not queue:
                queue = [(value >> (8*i)) & 255 for i in range(4)]
            else:
                params.append(value)
                if len(params) == COUNTS.get(queue[0], 1):
                    yield index, queue.pop(0), params;params = []
            while queue and COUNTS.get(queue[0], 1) == 0:
                yield index, queue.pop(0), []
    assert not queue and not params and not port_params, 'Incomplete preserved stream'


def snapshot_uv_oracle(words):
    matrix = identity();saved = None;mode = 1;image = 0;raw = (0, 0)
    uv = None;primitive = None;vertices = [];triangles = []
    matrix_loads = []
    for index, command, values in commands_from_draw_words(words):
        if command == 0x10:
            mode = values[0] & 3
        elif mode == 3 and command in (0x16, 0x17):
            matrix = reshape(values);matrix_loads.append(dict(word=index, matrix=matrix[:]))
        elif mode == 3 and command in (0x18, 0x19, 0x1a):
            matrix = multiply(reshape(values), matrix)
        elif mode == 3 and command == 0x15:
            matrix = identity()
        elif mode == 3 and command == 0x1b:
            scale = identity();scale[0], scale[5], scale[10] = map(lambda v: signed(v, 32), values)
            matrix = multiply(scale, matrix)
        elif mode == 3 and command == 0x1c:
            trans = identity();trans[12:15] = map(lambda v: signed(v, 32), values)
            matrix = multiply(trans, matrix)
        elif mode == 3 and command == 0x13:
            assert (values[0] & 31) == 0;saved = matrix[:]
        elif mode == 3 and command == 0x14:
            assert (values[0] & 31) == 0 and saved is not None;matrix = saved[:]
        elif command == 0x2a:
            image = values[0]
        elif command == 0x22:
            raw = (signed(values[0], 16), signed(values[0] >> 16, 16))
            uv = [value/16 for value in raw] if image >> 30 != 1 else None
        elif command == 0x21 and image >> 30 == 2:
            normal = tuple(signed(values[0] >> (10*i), 10) for i in range(3))
            uv = primary_uv(matrix, normal, raw)
        elif command == 0x40:
            primitive = values[0] & 3;vertices = []
        elif command == 0x41:
            primitive = None;vertices = []
        elif 0x23 <= command <= 0x28 and primitive is not None:
            vertex = dict(uv=uv, texgen=image >> 30, word=index);vertices.append(vertex)
            size = len(vertices)
            if primitive == 0 and size == 3:
                triangles.append(vertices[:]);vertices = []
            elif primitive == 1 and size == 4:
                triangles += [[vertices[0], vertices[1], vertices[2]],
                              [vertices[0], vertices[2], vertices[3]]];vertices = []
            elif primitive == 2 and size >= 3:
                a, b, c = vertices[-3:]
                triangles.append([b, a, c] if (size-3) & 1 else [a, b, c])
            elif primitive == 3 and size >= 4 and not size & 1:
                a, b, c, d = vertices[-4:]
                triangles += [[a, b, d], [a, d, c]]
    return triangles, matrix_loads


def run_batch(executable, batch, environment):
    result = subprocess.run([str(executable), str(batch)], check=True, text=True,
                            capture_output=True, env=environment)
    if result.stderr:
        raise RuntimeError(result.stderr)
    return json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot-dir', type=Path, default=SNAPSHOT)
    parser.add_argument('--replay-report', type=Path,
                        default=ROOT/'build/native-gx-audit/attempt-1791596592223444000-texgen2.json')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-gx-audit/texgen2-test.json')
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    sources = {source: sha(ROOT/source) for source in SOURCES}
    cases = fixture_cases()
    with tempfile.TemporaryDirectory(prefix='sm64ds-texgen2-tests-') as directory:
        temp = Path(directory);batch = temp/'cases.bin'
        batch.write_bytes(struct.pack('<I', len(cases))+b''.join(
            struct.pack('<I', len(case['words']))+b''.join(struct.pack('<2I', *word)
                for word in case['words']) for case in cases))
        flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-I'+str(ROOT/'include'), '-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
        env = os.environ.copy();env['UBSAN_OPTIONS'] = 'halt_on_error=1'
        guard_only = temp/'guard-only-gx.cpp'
        original = BASELINE.read_text()
        assert original.count('if((image>>30)>1)') == 1
        guard_only.write_text(original.replace('if((image>>30)>1)', 'if((image>>30)==3)'))
        executions = {}
        for name, gx in [('before', BASELINE), ('guard_only', guard_only), ('fixed', GX)]:
            executable = temp/name
            subprocess.run([args.compiler, *flags, str(ROOT/'tools/native_texgen_probe.cpp'),
                            str(gx), str(ROOT/'source/vendor/ntr/texture.cpp'),
                            '-o', str(executable)], check=True, cwd=ROOT)
            executions[name] = run_batch(executable, batch, env)
        mismatches = {}
        for stage, results in executions.items():
            assert len(results) == len(cases)
            failed = []
            for case, result in zip(cases, results):
                good = result['valid'] is case['valid']
                if case['valid']:
                    good &= [v[:2] for v in result['vertices']] == case['uv']
                    good &= not result['failure'] and not result['fifo_pending'] and not result['port_pending']
                    good &= result['stack_available'] and result['position_depth'] == 0 and result['projection_depth'] == 0
                    if case['positions'] is not None:
                        good &= [v[2:] for v in result['vertices']] == case['positions']
                else:
                    good &= result['command'] == case['command'] and not result['stack_available']
                    good &= result['failure'] == (2 if case['command'] == 0x2a else 4)
                if not good:
                    failed.append(case['name'])
                    if stage == 'fixed':
                        raise AssertionError((case['name'], case, result))
            mismatches[stage] = failed
        assert 'primary-precision' in mismatches['before']
        assert 'primary-precision' in mismatches['guard_only'], 'Removing a guard must not pass the UV oracle'
        assert 'texture-stack-keeps-position-bone-slot0' in mismatches['guard_only']
        assert not mismatches['fixed']
    assert sources == {source: sha(ROOT/source) for source in SOURCES}, 'Sources changed during test'

    files = {kind: args.snapshot_dir/('scene-1-first-'+kind+extension) for kind, extension in
             [('gx', '.bin'), ('textures', '.bin'), ('palettes', '.bin'), ('geometry', '.json')]}
    replay = json.loads(args.replay_report.read_text())
    assert replay['source_sha256']['source/vendor/ntr/gx.cpp'] == sources['source/vendor/ntr/gx.cpp']
    for kind, path in files.items():
        assert sha(path) == replay['inputs'][kind]['sha256'], 'Replay input differs from UV oracle input'
    data = files['gx'].read_bytes();words = list(struct.iter_unpack('<2I', data))
    expected, texture_loads = snapshot_uv_oracle(words)
    actual = replay['snapshot']['real']
    assert actual['stream_valid'] and actual['first_failure_word'] is None
    assert actual['entries'] == len(words) and actual['triangles'] == len(expected)
    assert not actual['final_diagnostic']['fifo_commands_remaining'] and not actual['final_diagnostic']['port_parameters']
    assert all(query['accepted'] for query in actual['texture_queries'])
    comparisons = []
    for index, (triangle_expected, triangle_actual) in enumerate(zip(expected, actual['triangles_uv'])):
        for vertex, (reference, observed) in enumerate(zip(triangle_expected, triangle_actual)):
            if reference['texgen'] == 2:
                assert observed == reference['uv'], (index, vertex, reference, observed)
                comparisons.append(dict(triangle=index, vertex=vertex, word=reference['word'], uv=observed))
    assert comparisons, 'Preserved stream must exercise generated UVs'
    report = dict(status='passed', scope='host emitted GX vertex UV/position and preserved-stream replay',
                  reference='Nintendo NITRO Programming Manual V1.44 pp.213-214; unbounded integer expression',
                  source_sha256=sources, baseline_sha256=sha(BASELINE), sanitizers=['address', 'undefined'],
                  cases=len(cases), vertex_uv_comparisons=sum(len(case['uv']) for case in cases),
                  before_mismatches=mismatches['before'], guard_only_mismatches=mismatches['guard_only'],
                  fixed_mismatches=mismatches['fixed'], snapshot=dict(entries=len(words), triangles=len(expected),
                  texture_queries=actual['texture_queries'], generated_uv_comparisons=len(comparisons),
                  generated_uv= comparisons, texture_matrix_loads=texture_loads,
                  input_sha256={kind:sha(path) for kind, path in files.items()}),
                  emulator_executed=False, physical_hardware=False, gameplay_verified=False,
                  performance_verified=False)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(status=report['status'], cases=report['cases'],
        vertex_uv_comparisons=report['vertex_uv_comparisons'], snapshot_entries=len(words),
        snapshot_triangles=len(expected), snapshot_generated_uv_comparisons=len(comparisons),
        before_mismatches=len(mismatches['before']), guard_only_mismatches=len(mismatches['guard_only']),
        report=str(args.report)), indent=2))


if __name__ == '__main__':
    main()
