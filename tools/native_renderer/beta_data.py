"""Reproduce the supplemental BUILD 142 bank from the verified ROM and public pins."""
import base64
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from types import SimpleNamespace

from ndspy.codeCompression import decompress
from ndspy.rom import NintendoDSRom

from tools.generate_integrated_rom_art import generate
from tools.setup_dependencies import checkout
from tools.sm64ds_packer.core import recipes

ROOT = Path(__file__).resolve().parents[2]
MOTION_COMMIT = '3f3c01eae67d6e9f3af89bc8073e37a5d153e00f'
MOTION_REPOSITORY = 'https://github.com/mkst/sm64-port.git'
ASSET_LAYOUT = Path(__file__).with_name('beta142-layout.json')
ART = re.compile(r'_ZN(?:16(retail_pause_art|native_lower_art))(\d+)(\w+)E$')
SKIN = re.compile(r'_ZN6sm64ds6render8prepared\d+model_(\d+)_skin_verticesE$')
ANIM = re.compile(r'anim_[\dA-F_]+_(?:values|indices)$')
INT = re.compile(r'-?(?:0x[0-9A-Fa-f]+|[0-9]+)')


def compiled_art(namespace):
    header = ROOT / 'source/integrated_native/options' / (namespace + '.hpp')
    for width, name, body in re.findall(
            r'inline constexpr uint(8|16|32)_t (\w+)(?:\[[0-9]*\])+\s*=\s*\{(.*?)\};',
            header.read_text(), re.S):
        code = {8: 'B', 16: 'H', 32: 'I'}[int(width)]
        values = [int(v, 0) for v in INT.findall(body)]
        yield name, struct.pack(f'<{len(values)}{code}', *values)


def motion_tables(motion_source):
    checkout(MOTION_REPOSITORY, MOTION_COMMIT, motion_source, check_only=False)
    paths = subprocess.check_output(
        ['git', '-C', motion_source, 'ls-files', 'assets/anims/'], text=True).splitlines()
    result = {}
    for path in paths:
        if not path.endswith('.inc.c'):
            continue
        text = (motion_source / path).read_text()
        for name, body in re.findall(r'static const [su]16 (anim_\w+_(?:values|indices))\[\] = \{(.*?)\};',
                                     text, re.S):
            values = [int(value, 0) & 0xffff for value in INT.findall(body)]
            result[name] = struct.pack(f'<{len(values)}H', *values)
    if len(result) != 386 or sum(map(len, result.values())) != 573754:
        raise ValueError('Unexpected animation tables in the pinned public source')
    return result


def prepare(rom, decomp, models, work, output, motion_source):
    recipe = json.loads(ASSET_LAYOUT.read_text())
    if recipe['build'] != 142:
        raise ValueError('Unsupported external data recipe')
    raw_rom = rom.read_bytes()
    if hashlib.sha1(raw_rom).hexdigest() != recipes()['rom']['sha1']:
        raise ValueError('Expected European ASMP revision 0')
    cart = NintendoDSRom(raw_rom)
    overlays = {int(name[2:]) for name, _, _ in recipe['rows'] if re.fullmatch(r'ov\d+', name)}
    chunks = {'arm9': decompress(cart.arm9), 'arm7': cart.arm7}
    chunks.update((f'ov{n}', overlay.data) for n, overlay in cart.loadArm9Overlays(overlays).items())
    chunks.update(motion_tables(motion_source))
    for namespace in ['retail_pause_art', 'native_lower_art']:
        chunks.update((f'_ZN16{namespace}{len(name)}{name}E', data) for name, data in compiled_art(namespace))
    generated = sorted((entry for entry in recipes()['files'] if 'generated' in entry),
                       key=lambda entry: entry['path'])
    for index, entry in enumerate(generated):
        name = f'generated_{index}'
        chunks[f'_ZN20rom_install_manifest{len(name)}{name}E'] = base64.b64decode(entry['generated'], validate=True)
    art = work / 'loading-art'
    generate(SimpleNamespace(rom=rom, decomp=decomp, rom_only=True, preview_dir=art,
                             output=work / 'art.hpp', report=work / 'art.json'))
    chunks['_ZN16rom_art_embedded6pixelsE'] = b''.join(
        (art / (name + '.a1r5g5b5')).read_bytes()
        for name in ['castle', 'logo', 'pattern', 'frame', 'star', 'title_font', 'body_font'])
    skin_identities = {name: int(match[1]) for name, _, _ in recipe['rows']
                       if (match := SKIN.fullmatch(name))}
    for name, file_id in skin_identities.items():
        report = json.loads((models / f'{file_id:05d}.json').read_text())
        chunks[name] = b''.join(struct.pack('<5fH3Bx4h2x', *vertex)
                                for group in report['skin'] for vertex in group['vertices'])

    payload = bytearray()
    for name, offset, size in recipe['rows']:
        source = chunks.get(name)
        if source is None or offset < 0 or size <= 0 or offset + size > len(source):
            raise ValueError(f'Invalid BUILD 142 source span: {name} at {offset} (+{size})')
        payload.extend(source[offset:offset+size])
    if len(payload) != recipe['bytes'] or hashlib.sha256(payload).hexdigest() != recipe['sha256']:
        raise ValueError('Supplemental bank differs from BUILD 142')
    output.write_bytes(payload)
