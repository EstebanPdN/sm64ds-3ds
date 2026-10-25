#!/usr/bin/env python3
"""Read immutable title RAM/GX evidence and audit backdrop provenance.

Only rebuilds a bounded host GX decoder probe. No game execution, GPU fixture,
Azahar launch, production edit, ROM export, or physical-target claim.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-integration-check/renderer-backdrop'
NM = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
OBJDUMP = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-objdump'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as file:
        while chunk := file.read(1024*1024):
            digest.update(chunk)
    return digest.hexdigest()


def run(arguments):
    return subprocess.check_output([str(value) for value in arguments], cwd=ROOT, text=True)


class RAM:
    """Validate the portable descriptors, then seek only within their ranges."""
    def __init__(self, path):
        self.path = Path(path)
        self.index = json.loads(self.path.with_suffix('.json').read_text())
        assert self.index['io_status'] == 'complete'
        self.file = self.path.open('rb')
        magic, version, payload, count, views, total, flags = struct.unpack('<8s6I', self.file.read(32))
        assert magic == b'SM64RAM1' and version == 1 and flags == 0
        assert 1 <= count <= 4 and views <= 272
        assert payload == 32+16*count+28*views and total == self.path.stat().st_size
        assert self.index['binary_bytes'] == total
        self.spans = self.index['spans']
        assert len(self.spans) == count and len(self.index['views']) == views
        cursor = payload
        for i, span in enumerate(self.spans):
            values = struct.unpack('<4I', self.file.read(16))
            assert values == (span['address'], span['size'], span['file_offset'], i)
            assert values[0] and values[1] and values[0]+values[1] <= 0xffffffff
            assert values[2] == cursor
            assert not any(span['address'] < prior['address']+prior['size'] and
                           prior['address'] < span['address']+span['size'] for prior in self.spans[:i])
            cursor += span['size']
        assert cursor == total
        for i, view in enumerate(self.index['views']):
            values = struct.unpack('<7I', self.file.read(28))
            assert values == tuple(view[key] for key in (
                'address', 'size', 'file_offset', 'span', 'module', 'original_address', 'flags'))
            assert view['id'] == i and 0 <= view['span'] < count
            parent = self.spans[view['span']]
            assert self.contains(parent, view['address'], view['size'])
            assert view['file_offset'] == parent['file_offset']+view['address']-parent['address']
        self.root = next(view for view in self.index['views'] if view['name'] == 'original-root-heap')

    @staticmethod
    def contains(span, address, size):
        return (address >= span['address'] and 0 < size <= span['size'] and
                address-span['address'] <= span['size']-size)

    def owner(self, address, size):
        matching = [span for span in self.spans if self.contains(span, address, size)]
        assert len(matching) == 1, 'read outside a captured physical span'
        return matching[0]

    def read(self, address, size, game=False):
        if game:
            assert self.contains(self.root, address, size), 'game pointer outside root arena'
        span = self.owner(address, size)
        self.file.seek(span['file_offset']+address-span['address'])
        value = self.file.read(size)
        assert len(value) == size
        return value

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]


def decode(ram, names):
    ctx = ram.u32(names['data_ov007_0210342c'])
    record = ram.read(ctx, 0x188, True)
    states = []
    for offset in (4, 8, 12):
        address = struct.unpack_from('<I', record, offset)[0]
        state = ram.read(address, 16, True)
        current, pending = struct.unpack_from('<hh', state)
        states.append({'context_offset': offset, 'address': address, 'current': current,
                       'pending': pending, 'flags': struct.unpack_from('<I', state, 4)[0],
                       'counter': struct.unpack_from('<i', state, 12)[0]})
    widgets = []
    for offset in (0x108, 0x10c, 0x110):
        address = struct.unpack_from('<I', record, offset)[0]
        value = ram.read(address, 0x88, True)
        flags, x, y = struct.unpack_from('<Iii', value)
        table = struct.unpack_from('<I', value, 0x5c)[0]
        count, slot = struct.unpack_from('<HH', value, 0x58)
        assert 1 <= count <= 256 and slot < count
        entries = struct.unpack('<'+'I'*count, ram.read(table, count*4, True))
        widgets.append({
            'context_offset': offset, 'address': address, 'validated_bytes': 0x88,
            'flags': flags, 'center_fixed': [x, y],
            'dimensions': list(struct.unpack_from('<HH', value, 12)),
            'z_fixed': struct.unpack_from('<i', value, 16)[0],
            'transformed_dimensions_fixed': list(struct.unpack_from('<ii', value, 0x20)),
            'uniform_color_rgb555': struct.unpack_from('<H', value, 0x44)[0],
            'corner_colors_rgb555': list(struct.unpack_from('<4H', value, 0x46)),
            'polygon_id': value[0x4e], 'alpha_fixed': struct.unpack_from('<i', value, 0x50)[0],
            'alpha_5bit': (struct.unpack_from('<i', value, 0x50)[0] >> 12) & 255,
            'polygon_flags': struct.unpack_from('<I', value, 0x54)[0],
            'texture_table': table, 'texture_entries': list(entries), 'texture_slot': slot,
            'raw_record_sha256': hashlib.sha256(value).hexdigest()})
    probe = ram.index['stack_probe_address']
    owner = ram.owner(probe, 1)
    runtime = {name: {'symbol_address': names[name], 'value': ram.u32(names[name])}
               for name in ('__stacksize__', '__ctru_heap', '__ctru_heap_size',
                            'fake_heap_start', 'fake_heap_end', 'saved_stack')}
    heap = runtime['__ctru_heap']['value']
    allocated_stack = runtime['__stacksize__']['value']
    assert runtime['fake_heap_start']['value'] == heap+allocated_stack
    assert heap <= probe < heap+allocated_stack
    ram.owner(heap, allocated_stack)
    return {'tick': ram.index['tick'], 'presentation': ram.index['presentation'],
            'context': ctx, 'states': states, 'widgets': widgets,
            'input_lock': struct.unpack('<i', ram.read(names['data_ov007_02104bec'], 4))[0],
            'stack': {'original_index_stack_available': ram.index['stack_available'],
                      'probe_address': probe, 'probe_physical_span': owner['name'],
                      'probe_file_offset': owner['file_offset']+probe-owner['address'],
                      'libctru_globals': runtime, 'allocated_main_stack_address': heap,
                      'allocated_main_stack_size': allocated_stack,
                      'all_allocated_stack_bytes_already_captured': True,
                      'OS_query_result_preserved_in_metadata': False,
                      'interpretation': 'CRT reserves the first __stacksize__ bytes of app heap and sets SP there. Saved_stack is the previous loader SP. OS query mapping size was not recorded; exact rejected-query cause is inferred, not directly observed.'}}


def scanout(path):
    raw = path.read_bytes()
    assert len(raw) == 3*256*256*4
    result = {}
    for i, name in enumerate(('underlay', 'overlay', 'bottom')):
        values = struct.unpack_from('<65536I', raw, i*65536*4)
        result[name] = {'visible_counts_argb': dict(Counter(f'{value:08x}' for value in values[:256*192])),
                        'padding_counts_argb': dict(Counter(f'{value:08x}' for value in values[256*192:]))}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--acquisition', type=Path, default=ROOT/'build/native-runtime-azahar/attempt-1791613748756720000')
    args = parser.parse_args()
    attempt = args.acquisition.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    elf = attempt/'sm64ds-native.elf'
    names = {parts[2]: int(parts[0], 16) for line in run([NM, '--defined-only', elf]).splitlines()
             if len(parts := line.split()) == 3}
    ram_decoded = []
    inputs = {'elf': elf}
    for prefix in ('ram-title-180', 'ram-final'):
        path = attempt/(prefix+'.ram.bin')
        ram = RAM(path)
        ram_decoded.append(decode(ram, names))
        ram.file.close()
        inputs[prefix+'.bin'] = path
        inputs[prefix+'.json'] = path.with_suffix('.json')
    assert ram_decoded[0]['widgets'] == ram_decoded[1]['widgets']
    assert all(row['states'][0]['current'] == 0 and row['states'][1]['current'] == 11 for row in ram_decoded)
    assert [w['flags'] for w in ram_decoded[0]['widgets']] == [2, 2, 1]
    assert [w['alpha_5bit'] for w in ram_decoded[0]['widgets']] == [31, 31, 1]
    assert all(w['corner_colors_rgb555'] == [0]*4 for w in ram_decoded[0]['widgets'])
    geometry = json.loads((attempt/'scene-1-visible-geometry.json').read_text())
    assert [geometry[key] for key in ('texture_capacity', 'palette_capacity', 'texture_generation', 'palette_generation')] == [262144, 16384, 68, 22]
    probe_cpp = OUT/'stream_probe.cpp'
    probe_binary = OUT/'stream-probe-ram-acquisition'
    replay_sources = [ROOT/'source/vendor/ntr/gx.cpp', ROOT/'source/vendor/ntr/texture.cpp',
                      ROOT/'source/core/game_geometry.cpp', ROOT/'source/core/game_textures.cpp']
    run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', ROOT/'include',
         probe_cpp, *replay_sources, '-o', probe_binary])
    samples = []
    for initial in ('reset', attempt/'scene-1-first-gx.bin'):
        sample = json.loads(run([probe_binary, attempt/'scene-1-visible-textures.bin',
                                 attempt/'scene-1-visible-palettes.bin', initial, attempt/'scene-1-visible-gx.bin']))
        samples.append(sample)
    assert samples[0]['triangles'] == samples[1]['triangles']
    triangles = samples[0]['triangles']
    first_quad = triangles[:2]
    assert all(t['texture'] == 0 and t['alpha'] == 31 and t['native_projection'] for t in first_quad)
    assert all(v['color'] == 0xff000000 for t in first_quad for v in t['vertices'])
    vertices = [v for t in first_quad for v in t['vertices']]
    bounds = {axis: [min(v[axis] for v in vertices), max(v[axis] for v in vertices)] for axis in ('x', 'y', 'z')}
    assert bounds['x'][0] <= 0 and bounds['x'][1] >= 256 and bounds['y'][0] <= 0 and bounds['y'][1] >= 192
    assert all(0 <= v['z'] <= 1 for v in vertices)
    layers = scanout(attempt/'scene-1-visible-scanout.bin')
    visible_white = layers['underlay']['visible_counts_argb'].get('ffffffff', 0)
    assert visible_white > 47000
    assert layers['overlay']['visible_counts_argb'].get('00000000', 0) == visible_white
    baseline = ROOT/'build/native-runtime-azahar/attempt-1791612346875167000'
    baseline_layers = scanout(baseline/'scene-1-visible-scanout.bin')
    assert baseline_layers['underlay']['visible_counts_argb'] == {'ffffffff': 49152}
    assert baseline_layers['overlay']['visible_counts_argb'] == {'00000000': 49152}
    metadata = json.loads((attempt/'scene-1-visible.json').read_text())
    assert metadata['triangles'] == len(triangles) and not metadata['main_on_top'] and metadata['has_3d']
    commands = list(struct.iter_unpack('<2I', (attempt/'scene-1-visible-gx.bin').read_bytes()))
    assert len(commands) == metadata['gx_words']
    quad_commands = []
    pending = {'texture_image': None, 'polygon_attr': None, 'initial_color_rgb555': None}
    active_quad = None
    for i, (port, word) in enumerate(commands):
        if port == 0x2a:
            pending['texture_image'] = word
        elif port == 0x29:
            pending['polygon_attr'] = word
        elif port == 0x40 and len(quad_commands) < 3:
            quad_commands.append({'begin_record': i, 'begin_word': word, **pending, 'colors': []})
            active_quad = quad_commands[-1]
        elif port == 0x20:
            pending['initial_color_rgb555'] = word & 0x7fff
            if active_quad is not None:
                active_quad['colors'].append({'record': i, 'rgb555': word & 0x7fff})
        elif port == 0x41:
            active_quad = None
            if len(quad_commands) == 3:
                break
    assert len(quad_commands[0]['colors']) == 4 and all(c['rgb555'] == 0 for c in quad_commands[0]['colors'])
    assert [quad['polygon_attr'] for quad in quad_commands] == [0x141f0080, 0x151f0080, 0x16010080]
    init_disassembly = run([OBJDUMP, '-d', '--disassemble=func_ov007_020b2998', elf])
    # Real linked instructions write all four corner fields using zero r3.
    for instruction in ('mov\tr3, #0', 'strh\tr3, [r4, #70]', 'str\tr3, [r4, #72]',
                        'strh\tr3, [r4, #76]', 'str\tr2, [r4, #80]'):
        assert instruction in init_disassembly
    crt_disassembly = run([OBJDUMP, '-d', '--disassemble=__system_allocateHeaps', elf])
    crt_disassembly += run([OBJDUMP, '-d', '--disassemble=initSystem', elf])
    for instruction in ('ldr\tsp, [r2]', 'add\tsp, sp, r3', 'str\tsp, [r2]'):
        assert instruction in crt_disassembly
    disassembly_path = OUT/(attempt.name+'-initializer-crt.txt')
    disassembly_path.write_text(init_disassembly+crt_disassembly)
    source_files = [ROOT/'source/ctr/game_renderer.cpp', ROOT/'source/ctr/scene.v.pica',
                    ROOT/'source/game_port/native_2d.cpp', ROOT/'include/sm64ds/game_projection.hpp', *replay_sources,
                    *(ROOT/'upstream/src'/name for name in (
                        'func_ov007_020b2998.c', 'func_ov007_020c44e4.c', 'func_ov007_020c421c.c',
                        'func_ov007_020c4684.c', 'func_ov007_020b2728.c', 'func_ov007_020bcf90.c',
                        'func_ov007_020b2764.c', 'func_ov007_020b0548.c', 'func_ov007_020b0a20.c',
                        'func_ov007_020b0da0.c', 'func_ov007_020b10dc.c'))]
    for name in ('scene-1-visible-gx.bin', 'scene-1-first-gx.bin', 'scene-1-visible-scanout.bin',
                 'scene-1-visible-textures.bin', 'scene-1-visible-palettes.bin', 'scene-1-visible-video.bin',
                 'scene-1-visible-geometry.json', 'scene-1-visible.json', 'input.log', 'title-state.log'):
        inputs[name] = attempt/name
    inputs['baseline_scene-visible-scanout.bin'] = baseline/'scene-1-visible-scanout.bin'
    inputs['baseline_scene-visible.json'] = baseline/'scene-1-visible.json'
    report = {
        'status': 'backdrop_provenance_confirmed', 'acquisition': str(attempt),
        'evidence_type': 'Actual Azahar RAM/GX/CPU scanout artifacts inspected read-only; bounded host GX replay',
        'RAM': ram_decoded, 'widgets_byte_identical_at_180_and_960': True,
        'visible_snapshot': metadata, 'CPU_scanout': layers,
        'earlier_baseline': {'attempt': str(baseline), 'CPU_scanout': baseline_layers,
                             'visible_snapshot': json.loads((baseline/'scene-1-visible.json').read_text())},
        'linked_initializer_and_CRT_disassembly': str(disassembly_path),
        'host_replay': {'triangle_count': len(triangles), 'reset_vs_after_first_equal': True,
                        'first_six_triangles': triangles[:6], 'first_opaque_black_quad_bounds': bounds,
                        'first_three_quad_commands': quad_commands, 'textures': samples[0]['textures'],
                        'alpha_histogram': dict(Counter(t['alpha'] for t in triangles))},
        'causal_chain': [
            'Original b2998 creates 0x88-byte widgets, sets corner colors to zero, flags=2/2/1, depth and IDs20/21/22. Constructor defaults alpha31; third widget explicitly alpha1 and uniform white.',
            'Original bcf90 calls b2728 in mainstate0; it calls c421c on all three widgets before the models. c421c passes fixed alpha and flags to c4684; flags2 selects the four zero corner colors.',
            'Captured GX and real native decoder agree: first two triangles are untextured, black, alpha31, valid projected depth and cover all 256x192 visible pixels.',
            'Renderer draws CPU underlay first, then the opaque black polygon using white fallback texture and texture*primary-color modulation. Overlay background is transparent: 49152pixels in the earlier baseline, 47158 in this acquisition with 1994sprite pixels present in both CPU layers.',
            'Substate11 keeps these colors. b2764 changes the first two gradients from later substates; no such transition occurred by finaltick960. Correcting the native clear to white would still be covered by this game polygon.'
        ],
        'renderer_review': {
            'clear': 'Target clear=0xdee5ebff; original DS clear RGBwhite alpha0. Neither produces the explicit black quad.',
            'texture_cache': 'Per-frame image CRC and pointer/dimension key; white fallback cannot produce stale black texture data.',
            'synchronization': 'C3D_FrameBegin(SYNCDRAW) before modifying vertices/textures; explicit flushes follow uploads.',
            'depth': 'Projection remaps DS clipdepth[-w,w] to PICA[-w,0]. First quad is inside clip range; opaque pass writes depth/color with GREATER against cleared0.',
            'ordering': 'Opaque pass precedes translucent. First black quad is in opaque pass; alpha1 white fade is translucent. A separate full DS translucent-order/fidelity audit is outside this finding.'},
        'foreground_sprite_duplication': 'native_2d scan_engine first writes every composite to underlay, then copies foreground pixels to overlay. Opaque OBJ at priority0 before BG0priority2 explains the 1994 matching colored pixels; their duplication does not change the backdrop cause.',
        'limits': [
            'No current original-resolution GPU color-buffer readback was supplied. Exact final black/near-black shade and every compositing pixel are not proven by CPU scanout.',
            'Snapshots are sequential CPU-visible spans after parent GPU sync, not simultaneous immutable CPU/GPU state. OS query result was not indexed.',
            'Host replay executes the decoder and texture resolver, not PICA, a complete game loop, or physical hardware.',
            'Reset and after-first replay use the current saved texture assets and two initial decoder states. This is not a replay of every omitted intermediate frame.',
            'The failed touch delivery is absent from consumed input.log. B875 was consumed, but backdrop colors remained unchanged; this report does not replace the sibling input/intro gate audit.'
        ],
        'input_sha256': {key: sha(path) for key, path in inputs.items()},
        'source_sha256': {str(path.relative_to(ROOT)): sha(path) for path in source_files},
        'probe_sha256': {str(path.relative_to(ROOT)): sha(path) for path in (probe_cpp, probe_binary)},
        'production_edits': False, 'GPU_fixture_or_Azahar_launched': False,
        'general_build_executed': False, 'physical_hardware_evidence': False}
    destination = OUT/(attempt.name+'-ram-backdrop.json')
    destination.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': report['status'], 'report': str(destination), 'triangles': len(triangles),
                      'widgets_unchanged': True, 'actual_main_stack_captured': True}))


if __name__ == '__main__':
    main()
