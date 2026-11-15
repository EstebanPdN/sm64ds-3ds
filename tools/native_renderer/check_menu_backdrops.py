"""Compare prepared menu backgrounds with the isolated original PPU oracle."""
import argparse
import json
from pathlib import Path
import subprocess


def check(catalog, oracle):
    manifest = json.loads((catalog / 'catalog.json').read_text())
    results = []
    for bank in manifest['banks']:
        directory = catalog / str(bank['bank_id'])
        for part in range(bank['variants']):
            preview = (directory / f'preview-{part}.rgba').read_bytes()
            assert len(preview) == 256 * 256 * 4
            for scroll in (0, 64):
                target = directory / f'reference-{part}-{scroll}.rgb'
                subprocess.run([str(oracle), str(directory / 'characters.bin'),
                                str(directory / f'palette-{part}.bin'), str(directory / f'map-{part}.bin'),
                                str(target), '0', str(scroll), str(bank['character_bytes']), str(int(bank['eight_bit']))],
                               check=True, stdout=subprocess.DEVNULL)
                actual = target.read_bytes();assert len(actual) == 256 * 192 * 3
                wrong = 0;examples = []
                for y in range(192):
                    for x in range(256):
                        at = (((y + scroll) % 256) * 256 + x) * 4
                        expected = preview[at:at + 3] if preview[at + 3] else bytes(3)
                        got = actual[(y * 256 + x) * 3:(y * 256 + x + 1) * 3]
                        if got != expected:
                            wrong += 1
                            if len(examples) < 8:examples.append(dict(x=x, y=y, expected=list(expected), actual=list(got)))
                results.append(dict(bank=bank['bank_id'], part=part, scroll_y=scroll, pixels=256 * 192,
                                    differing_pixels=wrong, examples=examples))
    report = dict(scope='Offline prepared pixels versus original PPU; black authored backdrop, no GPU/runtime acceptance',
                  banks=len(manifest['banks']), variants=sum(b['variants'] for b in manifest['banks']),
                  pixels=sum(r['pixels'] for r in results), differing_pixels=sum(r['differing_pixels'] for r in results),
                  physical_hardware=False, cases=results)
    (catalog / 'source-pixel-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('catalog', type=Path)
    parser.add_argument('oracle', type=Path)
    args = parser.parse_args()
    report = check(args.catalog.resolve(), args.oracle.resolve())
    print(json.dumps({k: v for k, v in report.items() if k != 'cases'}, indent=2))
    raise SystemExit(bool(report['differing_pixels']))
