"""Compile a PRIVATE semantic component capture into a native GPU lab fixture.

This is offline diagnostic input, never a graphics instruction interpreter.
Every source policy is preserved. The separate lab must explicitly report any
unsupported-policy substitutions. Neither output is whole-scene acceptance.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def number(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError("Invalid integer in native component capture")
    return str(value)


def floating(value):
    if type(value) not in (int, float) or not math.isfinite(value) or abs(value) > 3.402823466e38:
        raise ValueError("Invalid float in native component capture")
    # Literal type/suffix prevent narrowing diagnostics in aggregate GPU vertices.
    result = format(float(value), ".9g")
    return result + ("f" if "." in result or "e" in result else ".0f")


def boolean(value):
    if type(value) is not bool:
        raise ValueError("Invalid bool in native component capture")
    return "true" if value else "false"


def sequence(values, count, convert):
    if not isinstance(values, list) or len(values) != count:
        raise ValueError("Invalid array in native component capture")
    return "{" + ",".join(convert(value) for value in values) + "}"


u8 = lambda v: number(v, 0, 255)
u16 = lambda v: number(v, 0, 65535)
u32 = lambda v: number(v, 0, 2**32 - 1) + "u"
i16 = lambda v: number(v, -32768, 32767)
i32 = lambda v: number(v, -2**31, 2**31 - 1)
u64 = lambda v: number(v, 0, 2**64 - 1) + "ull"


def make_fixture(capture):
    version = capture["format"]
    if (version, capture["scope"]) not in [("SM64_NATIVE_COMPONENT_SCENE_1", "native-3d-component"),
                                           ("SM64_NATIVE_COMPONENT_SCENE_2", "native-component"),
                                           ("SM64_NATIVE_COMPONENT_SCENE_3", "native-component"),
                                           ("SM64_NATIVE_COMPONENT_SCENE_4", "native-component"),
                                           ("SM64_NATIVE_COMPONENT_SCENE_5", "native-component")]:
        raise ValueError("Expected a native component capture")
    if capture["original_scene_accepted"] is not False or capture["physical_capture"] is not False:
        raise ValueError("Component capture cannot claim scene/hardware acceptance")
    names = [("occurrences", "Occurrence", 256), ("parts", "PreparedPart", 2048),
             ("quad_vertices", "Vertex", 8192), ("quad_states", "QuadState", 256),
             ("light_sets", "LightSet", 128), ("lighting_states", "PreparedLighting", 2048)]
    if version != "SM64_NATIVE_COMPONENT_SCENE_1":
        names.append(("image_layers", "ImageLayer", 127))
    complete = version == "SM64_NATIVE_COMPONENT_SCENE_5"
    if complete:
        names.append(("skin_vertices", "NormalVertex", 4095*3))
    lines = ['#pragma once', '#include "scene_frame.hpp"',
             'namespace sm64ds::render::component_fixture {']
    for name, kind, maximum in names:
        data = capture[name]
        if not isinstance(data, list) or len(data) > maximum:
            raise ValueError("Capture workspace exceeded")
        lines.append(f'inline std::array<{kind},{len(data)}> {name}{{}};')
    lines.append('inline SceneView init(){')

    def put(lhs, rhs):
        lines.append(lhs + '=' + rhs + ';')

    def matrix(lhs, data):
        put(lhs, '{' + sequence(data, 4, lambda row: sequence(row, 4, floating)) + '}')

    def material(lhs, data):
        for name, kind, maximum in [('blend', 'Blend', 3 if complete else 2), ('cull', 'Cull', 2), ('uv_mode', 'UvMode', 2)]:
            put(lhs + '.' + name, f'{kind}({number(data[name], 0, maximum)})')
        for index, suffix in enumerate(['wrap_s', 'wrap_t']):
            put(lhs + '.' + suffix, 'Wrap(' + number(data['wrap'][index], 0, 2) + ')')
        for index, suffix in enumerate(['depth_test', 'depth_write']):
            put(lhs + '.' + suffix, 'bool(' + number(data['depth'][index], 0, 1) + ')')
        put(lhs + '.sample_alpha', 'bool(' + number(data['sample_alpha'], 0, 1) + ')')
        put(lhs + '.alpha_cutoff', u8(data['alpha_cutoff']))
        put(lhs + '.tint', u32(data['tint']))
        put(lhs + '.brightness', number(data.get('brightness', 0), -16, 16))
        put(lhs + '.uv', sequence(data['uv'], 6, floating))
        uv = data['fixed_uv']
        put(lhs + '.fixed_uv.coefficient', sequence(uv['coefficient'], 6, i32))
        for index, suffix in enumerate(['width', 'height']):
            put(lhs + '.fixed_uv.' + suffix, u16(uv['dimensions'][index]))
        put(lhs + '.fixed_uv.offset', sequence(uv['offset'], 2, i16))

    for index, o in enumerate(capture['occurrences']):
        if len(o) != 5:
            raise ValueError('Invalid occurrence')
        put(f'occurrences[{index}]', '{' + ','.join([u32(o[0]), u32(o[2]), u32(o[3]),
            'Occurrence::Kind(' + number(o[1], 0, 3 if complete else 1) + ')', u16(o[4])]) + '}')
    for name in ['parts', 'quad_states']:
        for index, value in enumerate(capture[name]):
            lhs = f'{name}[{index}]'
            matrix(lhs + '.object_to_clip', value['matrix'])
            material(lhs + '.material', value['material'])
            put(lhs + '.image', (u32 if name == 'parts' else u16)(value['image']))
            if name == 'parts':
                put(lhs + '.visible', boolean(value['visible']))
                put(lhs + '.lighting', u16(value['lighting']))
                put(lhs + '.highlight', boolean(value['highlight'] if complete else False))
            else:
                put(lhs + '.polygon_id', u8(value['polygon_id']))
                put(lhs + '.fog', boolean(value['fog']))
                put(lhs + '.bank', 'QuadState::Bank(' + number(value['bank'], 0, 1) + ')')
    for index, value in enumerate(capture['quad_vertices']):
        if len(value) != 9:
            raise ValueError('Invalid vertex')
        put(f'quad_vertices[{index}]', '{' + ','.join([floating(v) for v in value[:5]] + [u8(v) for v in value[5:]]) + '}')
    if complete:
        for index, value in enumerate(capture['skin_vertices']):
            if len(value) != 13:
                raise ValueError('Invalid native skin vertex')
            base = '{' + ','.join([floating(v) for v in value[:5]] + [u8(v) for v in value[5:9]]) + '}'
            normal = [number(v, -512, 511) for v in value[9:12]]
            put(f'skin_vertices[{index}]', '{' + ','.join([base, *normal, number(value[12], 0, 2)]) + '}')
    for index, value in enumerate(capture['light_sets']):
        lhs = f'light_sets[{index}]'
        put(lhs + '.known', number(value['known'], 0, 15))
        put(lhs + '.generation', u64(value['generation']))
        put(lhs + '.table_known', boolean(value['table_known']))
        put(lhs + '.shininess', sequence(value['shininess'], 128, u8))
        if complete:
            put(lhs + '.toon_known', boolean(value['toon_known']))
            put(lhs + '.toon', sequence(value['toon'], 32, lambda v: number(v, 0, 65535)))
        if len(value['lights']) != 4:
            raise ValueError('Invalid native light set')
        for slot, light in enumerate(value['lights']):
            light_lhs = f'{lhs}.light[{slot}]'
            put(light_lhs + '.direction', sequence(light['direction'], 3, lambda v: number(v, -1024, 1023)))
            put(light_lhs + '.colour', sequence(light['colour'], 3, lambda v: number(v, 0, 31)))
            put(light_lhs + '.specular_reciprocal', i32(light['specular_reciprocal']))
    for index, value in enumerate(capture['lighting_states']):
        lhs = f'lighting_states[{index}]'
        put(lhs + '.normal', sequence(value['normal'], 9, i32))
        put(lhs + '.lights', u16(value['lights']))
        m = value['material']
        if len(m) != (8 if complete else 6):
            raise ValueError('Invalid lighting material')
        put(lhs + '.material', '{' + ','.join([number(v, 0, 32767) for v in m[:4]] +
            [number(m[4], 0, 15)] + ['bool(' + number(v, 0, 1) + ')' for v in m[5:]]) + '}')
    if version != "SM64_NATIVE_COMPONENT_SCENE_1":
        for index, value in enumerate(capture['image_layers']):
            lhs = f'image_layers[{index}]'
            put(lhs + '.bank_id', number(value['bank'], 0, 65535) + 'u')
            put(lhs + '.image', number(value['image'], 0, 127))
            put(lhs + '.order', u16(value['order']))
            put(lhs + '.screen', 'Screen(' + number(value['screen'], 0, 1) + ')')
            source = value.get('source', 0)
            part = value.get('part', 0)
            number(source, 0, 2 if complete else 1);number(part, 0, 1023)
            if (source == 0 and part or source == 1 and value['image'] or
                    source == 2 and (value['bank'] or value['image'] or part or value.get('vertices',0)) or
                    version == 'SM64_NATIVE_COMPONENT_SCENE_2' and (source or part)):
                raise ValueError('Noncanonical native layer source/part')
            put(lhs + '.source', 'LayerSource(' + str(source) + ')')
            put(lhs + '.part', str(part))
            if complete:
                tone=value.get('tone_target',0);number(tone,0,32)
                if tone and tone&(tone-1):raise ValueError('Invalid tone target')
                put(lhs+'.tone_target',str(tone))
            put(lhs + '.rectangle', sequence(value['rectangle'], 4, floating))
            clip = value.get('clip', [0, 0, 0, 0])
            if len(clip) != 4:
                raise ValueError('Invalid native clip')
            for component in clip:
                number(component, 0, 400)
            if any(clip) and (version not in ('SM64_NATIVE_COMPONENT_SCENE_4', 'SM64_NATIVE_COMPONENT_SCENE_5') or
                    not (clip[0] < clip[2] <= (320 if value['screen'] else 400) and
                         clip[1] < clip[3] <= 240)):
                raise ValueError('Invalid native clip')
            put(lhs + '.clip', sequence(clip, 4, u16))
            if version in ('SM64_NATIVE_COMPONENT_SCENE_4', 'SM64_NATIVE_COMPONENT_SCENE_5'):
                put(lhs + '.first_vertex', number(value.get('first_vertex', 0), 0, 8192) + 'u')
                put(lhs + '.vertices', number(value.get('vertices', 0), 0, 8192) + 'u')
            material(lhs + '.material', value['material'])
        presentation = ',image_layers.data(),image_layers.size(),' + u16(capture['scene_order']) + ',Screen(' + number(capture['scene_screen'], 0, 1) + ')'
    else:
        presentation = ''
    if complete:
        presentation += ',skin_vertices.data(),skin_vertices.size()'
    clear = capture['clear']
    if len(clear) != 3:
        raise ValueError('Invalid clear')
    lines.append('return {occurrences.data(),parts.data(),occurrences.size(),parts.size(),' +
        u64(capture['scene']) + ',' + u64(capture['serial']) +
        ',{light_sets.data(),lighting_states.data(),light_sets.size(),lighting_states.size()},' +
        '{' + u32(clear[0]) + ',' + number(clear[1], 0, 63) + ',bool(' + number(clear[2], 0, 1) + ')},' +
        'quad_vertices.data(),quad_states.data(),quad_vertices.size(),quad_states.size()' + presentation + '};')
    lines.extend(['}', '}'])
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    raw = args.capture.read_bytes()
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError('Oversized capture')
    # Build/validate completely before modifying an existing generated header.
    header = make_fixture(json.loads(raw))
    args.output.write_text(header)
    print('PRIVATE native component fixture; source SHA256=' + hashlib.sha256(raw).hexdigest())


if __name__ == '__main__':
    main()
