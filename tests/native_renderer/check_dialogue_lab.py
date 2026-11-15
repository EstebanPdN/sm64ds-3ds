"""Compare completed PICA dialogue targets against independent bitmap painting."""
import argparse
import json
import re
from pathlib import Path


def check(directory, mask_path):
    mask = mask_path.read_bytes()
    assert len(mask) == 256 * 128
    results = []
    hashes = {(int(c), int(s), int(side)): int(h, 16) for c, s, side, h in re.findall(
        r'case=(\d+) step=(\d+) side=(\d+) hash=([0-9a-f]{8})', (directory / 'log.txt').read_text())}
    for case in range(4):
        page = bytearray(256 * 96)

        def paint(row, y_origin):
            x_origin = case
            for i in range(32):
                code = (i * 73 + row * 41 + case * 19) & 255
                ink = 13 if case == 3 and i % 3 == 0 else 15
                for y in range(16):
                    for x in range(8):
                        px, py = x_origin + x, y_origin + y
                        if 0 <= px < 256 and 0 <= py < 96 and mask[(code // 32 * 16 + y) * 256 + code % 32 * 8 + x]:
                            page[py * 256 + px] |= ink
                x_origin = (x_origin + 3 + (i * 5 + row + case) % 6) & 255

        for row in range(6):
            paint(row, row * 16)
        incoming = bytearray(256 * 96)
        # Paint the incoming line once into an independent scratch bitmap.
        before = page
        page = incoming
        paint(6, 80)
        page = before
        for step in range(5):
            if step:
                page[:92 * 256] = page[4 * 256:]
                page[92 * 256:] = incoming[(80 + (step - 1) * 4) * 256:(84 + (step - 1) * 4) * 256]
            expected = bytearray(320 * 240 * 3)
            for x in range(320):
                for y in range(24, 216):
                    rgb = (32, 48, 80)
                    sx = (4 * x + 2) // 5 - case
                    sy = (4 * (y - 40) + 2) // 5 - case
                    if (5 * case + 1) // 4 <= x and 40 + (5 * case + 1) // 4 <= y < 160 + (5 * case + 1) // 4 and 0 <= sx < 256 and 0 <= sy < 96:
                        ink = page[sy * 256 + sx]
                        if ink:
                            rgb = (255, 255, 255) if ink == 15 else (0, 0, 0)
                    at = (x * 240 + 239 - y) * 3
                    expected[at:at + 3] = bytes(reversed(rgb))
            bottom = (directory / f'dialogue-{case}-{step}-bottom.bgr').read_bytes()
            (directory / f'expected-{case}-{step}-bottom.bgr').write_bytes(expected)
            top = (directory / f'dialogue-{case}-{step}-top.bgr').read_bytes()
            assert len(bottom) == len(expected) and len(top) == 400 * 240 * 3
            for side, raw in enumerate((top, bottom)):
                checksum = 2166136261
                for value in raw:
                    checksum = ((checksum ^ value) * 16777619) & 0xffffffff
                assert hashes[case, step, side] == checksum, 'Export differs from CPU-visible completed target'
            errors = sum(bottom[i:i + 3] != expected[i:i + 3] for i in range(0, len(expected), 3))
            results.append(dict(case=case, step=step, differing_pixels=errors,
                                screens_equal=top[40 * 240 * 3:360 * 240 * 3] == bottom))
    return dict(pixels=4 * 5 * 320 * 240, differing_pixels=sum(r['differing_pixels'] for r in results), cases=results)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('mask', type=Path)
    args = parser.parse_args()
    report = check(args.directory, args.mask)
    (args.directory / 'pixel-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(bool(report['differing_pixels'] or not all(r['screens_equal'] for r in report['cases'])))
