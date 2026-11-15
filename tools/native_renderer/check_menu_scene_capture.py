"""Check captured native image placements composed with a native 3D component.

The original caption is an independent pixel oracle. The older native replay
only proves 3D stability; it cannot prove original 3D or full-scene fidelity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from PIL import Image


def checksum(data):
    result = 2166136261
    for value in data:
        result = ((result ^ value) * 16777619) & 0xffffffff
    return result


def inspect(session, reference, previous, composition_bytes=256):
    directory = session / 'user/sdmc/sm64ds'
    log = (directory / 'log.txt').read_text()
    run = json.loads((session / 'result/run.json').read_text())
    if (run['result'] != 'PASS' or not run['owned_process_group_stopped'] or
            run['unmapped_access_count'] or run['global_settings_modified'] or
            not log.rstrip().endswith('PASS') or '\nFAIL' in log):
        raise ValueError('Failed or unisolated component acquisition')
    if 'originalSceneAccepted=0 physicalCapture=0 images=1 sceneScreen=1 sceneOrder=1408' not in log:
        raise ValueError('Expected captured bottom-screen native composition')
    if 'diagnosticSubstitution: clear=opaqueBlack missing2D=omitted preparedImages=1 output=capturedLCD' not in log:
        raise ValueError('Diagnostic substitutions must remain explicit')
    source = Image.open(reference).convert('RGB')
    if source.size != (256, 192):
        raise ValueError('Expected original 256x192 LCD reference')
    caption = source.crop((0, 128, 256, 144)).tobytes()
    before = (previous / 'user/sdmc/sm64ds/component-bottom-first.bgr').read_bytes()
    if len(before) != 320 * 240 * 3:
        raise ValueError('Incomplete prior native 3D stability reference')
    captures = []
    buffers = {}
    for side, width in [('top', 400), ('bottom', 320)]:
        for phase in ['first', 'last']:
            name = f'captured-{side}-{phase}'
            raw = (directory / (name + '.bgr')).read_bytes()
            checks = re.findall(r'^readback=' + name + r' fnv1a=([a-f0-9]{8}) width=(\d+)$', log, re.M)
            if len(raw) != width * 240 * 3 or checks != [(f'{checksum(raw):08x}', str(width))]:
                raise ValueError('Incomplete or incoherent completed-target export')
            buffers[side, phase] = raw
            wrong = worst = caption_pixels = 0
            examples = []
            for y in range(240):
                for x in range(width):
                    at = (x * 240 + 239 - y) * 3
                    if side == 'top':
                        expected = bytes(3)
                    elif 160 <= y < 180:
                        sx = (2 * x + 1) * 256 // 640
                        sy = (2 * (y - 160) + 1) * 16 // 40
                        offset = (sy * 256 + sx) * 3
                        expected = caption[offset:offset + 3][::-1]
                        caption_pixels += 1
                    else:
                        expected = before[at:at + 3]
                    error = max(abs(a - b) for a, b in zip(raw[at:at + 3], expected))
                    worst = max(worst, error)
                    if error:
                        wrong += 1
                        if len(examples) < 8:
                            examples.append(dict(x=x, y=y, actual=list(raw[at:at+3]), expected=list(expected)))
            captures.append(dict(file=name + '.bgr', checked_pixels=width * 240,
                caption_reference_pixels=caption_pixels, mismatched=wrong, worst_channel_error=worst,
                sha256=hashlib.sha256(raw).hexdigest(), examples=examples))
        if buffers[side, 'first'] != buffers[side, 'last']:
            raise ValueError('Fixed captured composition changed over time')
    warm = [line for line in log.splitlines() if line.startswith('componentFrame=') and not line.startswith('componentFrame=0 ')]
    if not warm or any('textureUploadBytes=0 creates=0' not in line or f'compositionBytes={composition_bytes}' not in line for line in warm):
        raise ValueError('Warm composition unexpectedly creates/uploads resources or changes metadata storage')
    return dict(scope='Original caption pixel fidelity + prior native3D stability only; explicit pixel-policy substitutions remain',
        passed=all(item['mismatched'] == 0 for item in captures), images=captures,
        original_scene_accepted=False, physical_hardware=False, fixed_frames_equal=True,
        reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
        previous_native_3d_sha256=hashlib.sha256(before).hexdigest(), warm_no_image_uploads_or_creates=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', required=True, type=Path)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--previous-native', required=True, type=Path)
    parser.add_argument('--composition-bytes', type=int, default=256)
    args = parser.parse_args()
    report = inspect(args.session, args.reference, args.previous_native, args.composition_bytes)
    (args.session / 'menu-scene-image-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
