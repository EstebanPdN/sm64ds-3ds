#!/usr/bin/env python3
"""Replay two captured PICA200 command lists and compare the GPU state per draw.

    compare_gpu_state.py DUMP_A DUMP_B

Each DUMP is a test-mode dump folder holding comandos_gpu.bin (for example
.../06-bob-omb/volcado). The model keeps the 0x300 internal registers, with
byte masks applied, plus the float uniform memory written through 0x2c0
(index) and 0x2c1-0x2c8 (data). Data ports and the upload index are not
state and are left out; uniforms are compared only where both lists wrote
them. Two dumps must describe the same frame: a different model order means
the dumps were taken on different frames and cannot be compared.
Exit status: 0 when every draw matches, 1 otherwise.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from test_mode_dump import NAMES, decode_commands, read_commands  # noqa: E402

PORTS = (set(range(0x2c0, 0x2c9)) | set(range(0x2cb, 0x2d4)) | set(range(0x29b, 0x2a4))
         | {0x22e, 0x22f, 0x231, 0x111, 0x110, 0x010, 0x063})
STATE = [r for r in range(0x300) if r not in PORTS]


def replay(folder):
    capture = read_commands(Path(folder) / 'comandos_gpu.bin')
    registers = [0] * 0x300
    uniforms, pending, index, draws = {}, [], 0, []
    for _, register, values, mask, consecutive in decode_commands(capture['words']):
        full = sum(0xff << (8 * b) for b in range(4) if mask >> b & 1)
        for k, value in enumerate(values):
            r = register + k if consecutive else register
            if r >= 0x300:
                continue
            registers[r] = (registers[r] & ~full) | (value & full)
            if r == 0x2c0:
                index, pending = value & 0xff, []
            elif 0x2c1 <= r <= 0x2c8:
                pending.append(value)
                if len(pending) == 4:
                    uniforms[index] = tuple(pending)
                    index, pending = index + 1, []
            elif r in (0x22e, 0x22f) and value & 1:
                draws.append(([registers[i] for i in STATE], dict(uniforms), r, registers[0x228], registers[0x227]))
    return capture, draws


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    (a, first), (b, second) = replay(argv[1]), replay(argv[2])
    print(f"A: {len(a['words'])} words, {len(first)} draws, {a['passes']} passes | "
          f"B: {len(b['words'])} words, {len(second)} draws, {b['passes']} passes")
    print(f"words per draw: A {len(a['words']) / max(1, len(first)):.1f}  "
          f"B {len(b['words']) / max(1, len(second)):.1f}  (B/A = {len(b['words']) / max(1, len(a['words'])):.2f})")
    different = 0
    for n, (x, y) in enumerate(zip(first, second)):
        registers = [(STATE[i], u, v) for i, (u, v) in enumerate(zip(x[0], y[0])) if u != v]
        uniforms = [(k, x[1][k], y[1][k]) for k in x[1].keys() & y[1].keys() if x[1][k] != y[1][k]]
        if registers or uniforms or x[2:] != y[2:]:
            different += 1
            if different <= 6:
                print('draw', n, 'registers', [(hex(r), NAMES.get(r, ''), hex(u), hex(v)) for r, u, v in registers[:6]],
                      'uniforms', uniforms[:3])
    same_count = len(first) == len(second)
    print(f'draws compared {min(len(first), len(second))}, different {different}, same draw count: {same_count}')
    return 0 if same_count and not different else 1


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
