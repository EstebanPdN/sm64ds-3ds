#!/usr/bin/env python3
"""Compare the test-tour screenshots of two emulator sessions.

    compare_screenshots.py SESSION_A SESSION_B

Each SESSION is a carried tour folder under
build/native-renderer/playable-r1/sessions (for example run-l60-5-sd). For
every screenshot present in both, prints the share of differing pixels, the
share differing by more than 24 in some channel, the largest channel
difference and the share of near-black pixels in each. The tour drives the
same inputs on every run, so an unchanged renderer gives 0.00 %; any other
value is a difference to investigate, not noise to accept.
Exit status: 0 when every compared screenshot is identical, 1 otherwise.
"""
from pathlib import Path
import sys

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
SESSIONS = ROOT / 'build/native-renderer/playable-r1/sessions'


def pixels(path):
    with Image.open(path) as image:
        return np.asarray(image.convert('RGB'), dtype=np.int16)


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    first, second = (SESSIONS / name / 'prueba/01' for name in argv[1:3])
    compared = identical = 0
    for shot in sorted(first.glob('*/*.bmp')):
        other = second / shot.relative_to(first)
        if not other.exists():
            continue
        a, b = pixels(shot), pixels(other)
        if a.shape != b.shape:
            print(f'{shot.relative_to(first)}  different size {a.shape} / {b.shape}')
            compared += 1
            continue
        delta = np.abs(a - b).max(axis=2)
        share = float((delta > 0).mean()) * 100
        large = float((delta > 24).mean()) * 100
        black = [float((x.max(axis=2) < 8).mean()) * 100 for x in (a, b)]
        compared += 1
        identical += share == 0
        print(f'{str(shot.relative_to(first)):58s} diff {share:6.2f}%  >24: {large:6.2f}%  '
              f'max {int(delta.max()):3d}  black {black[0]:5.1f}% / {black[1]:5.1f}%')
    print(f'shots {compared} identical {identical}')
    return 0 if compared and identical == compared else 1


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
