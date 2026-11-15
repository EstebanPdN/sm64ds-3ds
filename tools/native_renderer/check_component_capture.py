"""Check PRIVATE completed native component exports, not original-scene fidelity."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def checksum(data):
    result = 2166136261
    for value in data:
        result = ((result ^ value) * 16777619) & 0xffffffff
    return result


def inspect(session, direct=None):
    directory = session / 'user/sdmc/sm64ds'
    log = (directory / 'log.txt').read_text()
    if '\nFAIL' in log or not log.rstrip().endswith('PASS'):
        raise ValueError('Component lab failed')
    if 'scope=native-3d-component originalSceneAccepted=0 physicalCapture=0' not in log:
        raise ValueError('Missing diagnostic-only scope')
    data = {}
    captures = []
    for side, width in [('top', 400), ('bottom', 320)]:
        for phase in ['first', 'last', 'idle']:
            name = f'component-{side}-{phase}'
            raw = (directory / (name + '.bgr')).read_bytes()
            checks = re.findall(r'^readback=' + name + r' fnv1a=([a-f0-9]{8}) width=(\d+)$', log, re.M)
            if len(raw) != width * 240 * 3 or checks != [(f'{checksum(raw):08x}', str(width))]:
                raise ValueError('Incomplete or incoherent export: ' + name)
            data[name] = raw
            captures.append(dict(name=name, width=width, pixels=width*240,
                                 sha256=hashlib.sha256(raw).hexdigest(), source_checksum_matches=True))
        if data[f'component-{side}-first'] != data[f'component-{side}-last']:
            raise ValueError('Fixed native component changed over time')
        if any(data[f'component-{side}-idle']):
            raise ValueError('Previous scene leaked into inactive LCD')
    top = data['component-top-last']
    bottom = data['component-bottom-last']
    if bottom != top[40*240*3:360*240*3]:
        raise ValueError('4:3 component differs between physical LCD coordinate systems')
    if not any(top):
        raise ValueError('Empty black output cannot prove a native component')
    direct_equal = None
    if direct:
        direct_equal = top == (direct / 'user/sdmc/sm64ds/component-last.bgr').read_bytes()
        if not direct_equal:
            raise ValueError('New composition differs from prior direct native target')
    return dict(scope='native 3D acquisition/stability/orientation only; original 2D and pixel policy omitted',
                passed=True, captures=captures, captured_pixels=sum(c['pixels'] for c in captures),
                inactive_screens_black=True, fixed_frames_equal=True, top_bottom_component_exact=True,
                direct_native_target_exact=direct_equal, original_scene_accepted=False, physical_hardware=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', required=True, type=Path)
    parser.add_argument('--direct-session', type=Path)
    args = parser.parse_args()
    report = inspect(args.session, args.direct_session)
    (args.session / 'component-image-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
