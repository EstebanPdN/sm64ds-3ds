#!/usr/bin/env python3
"""Observe real CPU renderer vertices/uploads against immutable ready RAM.

The sampler below independently models the documented software sampler's
signed truncation, wrap and inverted Morton row. It does not execute PICA,
float24 rasterization, depth/blend, a complete game frame, or live hardware.
"""
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'build/native-integration-check/title-ready-orientation'
OUTPUT = FIXTURE / 'renderer-regression'
ATTEMPT = ROOT / 'build/native-runtime-azahar/attempt-1791616229939712000'
SOURCES = ('tests/test_native_texture_orientation.cpp', 'source/ctr/game_renderer.cpp',
           'source/core/assets.cpp', 'include/sm64ds/game_renderer.hpp',
           'include/ntr/gx.h', 'tests/support/native_renderer_host/3ds.h',
           'tools/test_native_texture_orientation.py', 'source/core/game_textures.cpp',
           'source/vendor/ntr/texture.cpp')
SAMPLER_SOURCE = 'https://github.com/azahar-emu/azahar/blob/2126.1.2/src/video_core/renderer_software/sw_rasterizer.cpp#L480-L508'
WRAP_SOURCE = 'https://github.com/azahar-emu/azahar/blob/2126.1.2/src/video_core/renderer_software/sw_texturing.cpp#L14-L42'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def wrap(coordinate, size, repeat, mirror):
    if not repeat:
        return min(max(coordinate, 0), size - 1)
    # The referenced software source converts signed indices to uint32 first.
    # All supported sizes are powers of two, so this also gives floor modulo.
    unsigned = coordinate & 0xffffffff
    if mirror:
        folded = unsigned % (2 * size)
        return 2 * size - 1 - folded if folded >= size else folded
    return unsigned % size


def morton_offset(x, y, width):
    # Independent bit interleaving; never calls the production swizzle helper.
    interleaved = sum(((x >> bit) & 1) << (2 * bit) |
                      ((y >> bit) & 1) << (2 * bit + 1) for bit in range(3))
    return (((y // 8) * (width // 8) + x // 8) * 64 + interleaved) * 4


def lookup(upload, width, x, y):
    at = morton_offset(x, y, width)
    a, b, g, r = upload[at:at + 4]
    return (a << 24) | (r << 16) | (g << 8) | b


def sample(upload, width, height, u, v, flags, wrong_floor=False, double_flip=False):
    convert = math.floor if wrong_floor else math.trunc
    s, t = convert(f32(u * width)), convert(f32(v * height))
    x = wrap(s, width, flags & 1, flags & 4)
    y = height - 1 - wrap(t, height, flags & 2, flags & 8)
    if double_flip:
        y = height - 1 - y
    return lookup(upload, width, x, y), (x, y)


def parse_triangle(raw, index):
    at = index * 204
    vertices = []
    for number in range(3):
        values = struct.unpack_from('<6fI3f', raw, at + number * 40)
        vertices.append(dict(zip(('x', 'y', 'z', 'w', 'u', 'v', 'color', 'vx', 'vy', 'vz'), values)))
    pointer, width, height, alpha, flags = struct.unpack_from('<IiiBB', raw, at + 120)
    return dict(id=index, vertices=vertices, pointer=pointer, width=width, height=height,
                alpha=alpha, flags=flags)


def checked_source():
    source = (ROOT / 'source/ctr/game_renderer.cpp').read_text()
    assert 'vertices_[index++]=triangle_vertex(t,v);' in source
    block = re.search(r'const Vertex quad\[\]=\{(.*?)\};', source, re.S)
    assert block and 'triangle_vertex' not in block[1]
    rows = re.findall(r'\{([^{}]+)\}', block[1])
    uv = [[float(value.strip().rstrip('f')) for value in row.split(',')[4:]] for row in rows]
    assert uv == [[0, 1], [1, 1], [0, .25], [1, 1], [1, .25], [0, .25]]
    return uv


def build_fixture(index, raw, scanout):
    data = bytearray(b'TEXUV01\0' + struct.pack('<II', len(index['textures']), index['triangles']))
    for texture in index['textures']:
        pixels = (ROOT / texture['path']).read_bytes()
        assert sha(pixels) == texture['sha256']
        assert len(pixels) == texture['width'] * texture['height'] * 4
        data += struct.pack('<III', int(texture['pointer'], 16), texture['width'], texture['height'])
        data += pixels
    assert len(raw) == index['triangles'] * 204 and sha(raw) == index['raw_sha256']
    assert len(scanout) == 3 * 256 * 256 * 4
    return bytes(data + raw + scanout)


def compile_observer(fixture_binary):
    flags = ['-std=c++20', '-O2', '-D__3DS__', '-UNDEBUG', '-Wall', '-Wextra',
             '-Wno-misleading-indentation', '-ffunction-sections', '-fdata-sections',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
             '-I' + str(ROOT / 'tests/support/native_renderer_host'), '-I' + str(ROOT / 'include'),
             '-I' + str(ROOT / 'build/3ds/source/ctr'), '-I/opt/devkitpro/libctru/include']
    with tempfile.TemporaryDirectory(prefix='sm64ds-texture-orientation-') as directory:
        temporary = Path(directory)
        objects = []
        for number, path in enumerate(p for p in SOURCES if p.endswith('.cpp')):
            obj = temporary / (str(number) + '.o')
            subprocess.run(['c++', *flags, '-c', str(ROOT / path), '-o', str(obj)], check=True)
            objects.append(str(obj))
        executable = temporary / 'orientation'
        subprocess.run(['c++', *flags, *objects, '-Wl,-dead_strip', '-o', str(executable)], check=True)
        result = subprocess.run([str(executable), str(fixture_binary), str(OUTPUT)],
                                check=True, capture_output=True, text=True)
        assert not result.stderr, result.stderr
        return json.loads(result.stdout)


def raster_labels(triangles, actual_vertices, textures):
    atlas = next(texture for texture in textures.values() if texture['width'] == 128 and texture['height'] == 128)
    selected = [triangle for triangle in triangles if triangle['pointer'] == atlas['pointer']]
    assert len(selected) == 352
    images = [[0] * (256 * 192) for _ in range(3)]
    coverage, seam_differences, nonseam_differences = set(), 0, 0
    samples = 0
    for triangle in selected:
        vertices = triangle['vertices']
        converted = actual_vertices[triangle['id'] * 3:triangle['id'] * 3 + 3]
        a, b, c = vertices
        area = (b['x'] - a['x']) * (c['y'] - a['y']) - (b['y'] - a['y']) * (c['x'] - a['x'])
        if abs(area) < 1e-8:
            continue
        x0, x1 = max(0, math.floor(min(v['x'] for v in vertices))), min(255, math.ceil(max(v['x'] for v in vertices)))
        y0, y1 = max(0, math.floor(min(v['y'] for v in vertices))), min(191, math.ceil(max(v['y'] for v in vertices)))
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px, py = x + .5, y + .5
                wb = ((px - a['x']) * (c['y'] - a['y']) - (py - a['y']) * (c['x'] - a['x'])) / area
                wc = ((b['x'] - a['x']) * (py - a['y']) - (b['y'] - a['y']) * (px - a['x'])) / area
                weights = (1 - wb - wc, wb, wc)
                if min(weights) < -1e-7:
                    continue
                invw = sum(weight / vertex['w'] for weight, vertex in zip(weights, vertices))
                if abs(invw) < 1e-8:
                    continue
                u = sum(weight * vertex['u'] / vertex['w'] for weight, vertex in zip(weights, vertices)) / invw
                t = sum(weight * vertex['v'] / vertex['w'] for weight, vertex in zip(weights, vertices)) / invw
                output_u = sum(weight * out[4] / vertex['w'] for weight, out, vertex in zip(weights, converted, vertices)) / invw
                output_v = sum(weight * out[5] / vertex['w'] for weight, out, vertex in zip(weights, converted, vertices)) / invw
                original_x = wrap(math.floor(u), atlas['width'], triangle['flags'] & 1, triangle['flags'] & 4)
                original_y = wrap(math.floor(t), atlas['height'], triangle['flags'] & 2, triangle['flags'] & 8)
                original = atlas['pixels'][original_y * atlas['width'] + original_x]
                actual, _ = sample(atlas['upload'], atlas['width'], atlas['height'], output_u, output_v, triangle['flags'])
                old, _ = sample(atlas['upload'], atlas['width'], atlas['height'], u / atlas['width'], t / atlas['height'], triangle['flags'])
                if actual != original:
                    on_seam = min(abs(u - round(u)), abs(t - round(t))) < 1e-5
                    seam_differences += on_seam
                    nonseam_differences += not on_seam
                for image, pixel in zip(images, (original, old, actual)):
                    if pixel >> 24:
                        image[y * 256 + x] = pixel
                samples += 1
                coverage.add((x, y))
    differences = sum(a != b for a, b in zip(images[0], images[2]))
    old_differences = sum(a != b for a, b in zip(images[0], images[1]))
    assert not nonseam_differences, ('Renderer label texels differ away from a UV seam', nonseam_differences)
    # If an exact UV border exposes a float32 versus omitted float24 ambiguity,
    # keep it visible in the report instead of silently counting it as a pass.
    result = dict(atlas_triangles=352, raster_samples=samples, covered_pixels=len(coverage),
        complete_reconstructed_frame_pixels=256 * 192, actual_renderer_frame_pixel_differences=differences,
        old_V_frame_pixel_differences=old_differences, sample_differences_on_UV_seams=seam_differences,
        sample_differences_away_from_UV_seams=nonseam_differences,
        depth_blend_and_float24_not_emulated=True, images_are_host_reconstruction_not_GPU_screenshot=True)
    names = ('labels-original-ds', 'labels-old-V', 'labels-actual-renderer')
    for name, image in zip(names, images):
        (OUTPUT / (name + '.argb')).write_bytes(struct.pack('<' + str(len(image)) + 'I', *image))
    try:
        from PIL import Image, ImageDraw
        contact = Image.new('RGB', (768, 214), (255, 255, 255))
        draw = ImageDraw.Draw(contact)
        for number, (name, image) in enumerate(zip(names, images)):
            rgba = bytes(channel for pixel in image for channel in ((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255, pixel >> 24))
            raster = Image.frombytes('RGBA', (256, 192), rgba)
            raster.save(OUTPUT / (name + '.png'))
            background = Image.new('RGBA', raster.size, (70, 70, 70, 255));background.alpha_composite(raster)
            contact.paste(background.convert('RGB'), (number * 256, 22))
            label = ('Original DS UV + captured atlas', 'Old V=T/h + real upload', 'Actual renderer UV + real upload')[number]
            draw.text((number * 256, 0), label, fill='black')
        contact.save(OUTPUT / 'labels-comparison.png')
        result['visual_comparison'] = str((OUTPUT / 'labels-comparison.png').relative_to(ROOT))
    except ImportError:
        result['PNG_preview_unavailable'] = 'Pillow not installed; complete ARGB outputs are retained.'
    return result


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    hashes_before = {path: sha((ROOT / path).read_bytes()) for path in SOURCES}
    screen_uv = checked_source()
    index = json.loads((FIXTURE / 'captured-geometry-index.json').read_text())
    raw = (FIXTURE / 'ready-captured-triangles-arm32.bin').read_bytes()
    scanout = (ATTEMPT / 'scene-1-ready-scanout.bin').read_bytes()
    fixture = build_fixture(index, raw, scanout)
    fixture_path = OUTPUT / 'captured-inputs.bin';fixture_path.write_bytes(fixture)
    cpp = compile_observer(fixture_path)
    hashes_after = {path: sha((ROOT / path).read_bytes()) for path in SOURCES}
    assert hashes_before == hashes_after, 'Production or probe source changed during compilation'
    actual = list(struct.iter_unpack('<3fI2f', (OUTPUT / 'actual-ready-vertices.bin').read_bytes()))
    assert len(actual) == index['triangles'] * 3
    triangles = [parse_triangle(raw, number) for number in range(index['triangles'])]
    for triangle in triangles:
        for vertex, out in zip(triangle['vertices'], actual[triangle['id'] * 3:triangle['id'] * 3 + 3]):
            assert out[:3] == (vertex['vx'], vertex['vy'], vertex['vz'])
            color = vertex['color']
            expected_color = ((color >> 16) & 255) | (color & 0xff00) | ((color & 255) << 16) | (triangle['alpha'] * 255 // 31 << 24)
            assert out[3] == expected_color
            uv = (f32(vertex['u'] / triangle['width']), f32(1 - f32(vertex['v'] / triangle['height']))) if triangle['pointer'] else (0.0, 0.0)
            assert struct.pack('<2f', *out[4:]) == struct.pack('<2f', *uv)
    textures = {}
    upload_bytes = 0
    for texture in index['textures']:
        pointer, width, height = int(texture['pointer'], 16), texture['width'], texture['height']
        pixels_raw = (ROOT / texture['path']).read_bytes()
        pixels = struct.unpack('<' + str(width * height) + 'I', pixels_raw)
        upload = (OUTPUT / ('actual-texture-%08x-%dx%d.morton8' % (pointer, width, height))).read_bytes()
        assert len(upload) == len(pixels_raw)
        for y in range(height):
            for x in range(width):
                assert lookup(upload, width, x, y) == pixels[y * width + x]
        upload_bytes += len(upload)
        textures[pointer] = dict(pointer=pointer, width=width, height=height, pixels=pixels, upload=upload)
    centers = iter(struct.iter_unpack('<2f', (OUTPUT / 'actual-centre-UV.bin').read_bytes()))
    center_count, old_bad, double_bad = 0, 0, 0
    for texture in textures.values():
        width, height, upload = texture['width'], texture['height'], texture['upload']
        for y in range(height):
            for x in range(width):
                uv = next(centers);expected = texture['pixels'][y * width + x]
                observed, address = sample(upload, width, height, *uv, 3)
                assert address == (x, y) and observed == expected
                old, _ = sample(upload, width, height, f32((x + .5) / width), f32((y + .5) / height), 3)
                double, _ = sample(upload, width, height, *uv, 3, double_flip=True)
                old_bad += old != expected;double_bad += double != expected;center_count += 1
    assert next(centers, None) is None and old_bad and double_bad
    signed_count, negative_uv_count, floor_differences = 0, 0, 0
    signed_hash = hashlib.sha256()
    for pointer, flags, original_u, original_t, vx, vy, vz, color, u, v in struct.iter_unpack('<II2f3fI2f', (OUTPUT / 'actual-signed-vertices.bin').read_bytes()):
        texture = textures[pointer];width, height = texture['width'], texture['height']
        assert (vx, vy, vz, color) == (1.25, -2.5, -3.75, 0xff123456)
        expected_u, expected_v = f32(original_u / width), f32(1 - f32(original_t / height))
        assert (u, v) == (expected_u, expected_v)
        observed, address = sample(texture['upload'], width, height, u, v, flags)
        sx = wrap(math.trunc(f32(expected_u * width)), width, flags & 1, flags & 4)
        sy = height - 1 - wrap(math.trunc(f32(expected_v * height)), height, flags & 2, flags & 8)
        assert address == (sx, sy) and observed == texture['pixels'][sy * width + sx]
        _, wrong_address = sample(texture['upload'], width, height, u, v, flags, wrong_floor=True)
        floor_differences += wrong_address != address
        negative_uv_count += u < 0 or v < 0
        signed_count += 1;signed_hash.update(struct.pack('<4I', pointer, flags, sx, sy))
    assert signed_count == cpp['signed_helper_calls'] and negative_uv_count and floor_differences
    screens_count, screen_bad = 0, 0
    for layer in range(3):
        pixels = struct.unpack('<65536I', scanout[layer * 262144:(layer + 1) * 262144])
        upload = (OUTPUT / ('actual-screen-%u.morton8' % layer)).read_bytes()
        assert len(upload) == 262144
        for y in range(256):
            for x in range(256):
                assert lookup(upload, 256, x, y) == pixels[y * 256 + x]
        upload_bytes += len(upload)
        # Interpolate the production screen quad's own top/bottom UV constants.
        top, bottom = screen_uv[0][1], screen_uv[2][1]
        for y in range(192):
            for x in range(256):
                u = f32((x + .5) / 256)
                v = f32(top + (bottom - top) * (y + .5) / 192)
                observed, address = sample(upload, 256, 256, u, v, 0)
                assert address == (x, y) and observed == pixels[y * 256 + x]
                wrong, _ = sample(upload, 256, 256, u, f32(1 - v), 0)
                screen_bad += wrong != observed;screens_count += 1
    assert screen_bad
    labels = raster_labels(triangles, actual, textures)
    assert labels['old_V_frame_pixel_differences'] > 0
    report = dict(status='passed' if not labels['actual_renderer_frame_pixel_differences'] else 'passed-with-explicit-UV-seam-limit',
        scope=__doc__, cpp_observer=cpp, source_sha256=hashes_before,
        captured_ELF_sha256=index['ELF_sha256'], captured_RAM_sha256=index['RAM_sha256'],
        captured_triangle_sha256=index['raw_sha256'], captured_input_sha256=sha(fixture),
        captured_input_bytes=len(fixture), captured_scanout_sha256=sha(scanout),
        captured_texture_sha256={texture['pointer']: texture['sha256'] for texture in index['textures']},
        actual_geometry_and_color_vertices_checked=len(actual), complete_upload_bytes_checked=upload_bytes,
        actual_renderer_pixel_centres_checked=center_count,
        old_V_negative_control_wrong_pixels=old_bad, double_flip_negative_control_wrong_pixels=double_bad,
        signed_cases_checked=signed_count, cases_with_negative_normalized_U_or_V=negative_uv_count,
        signed_floor_mutation_changes_addresses=floor_differences, signed_observed_addresses_sha256=signed_hash.hexdigest(),
        original_DS_identity_for_all_negative_wrapped_coordinates_claimed=False,
        actual_screen_uploads_checked=3, screen_quad_UVs_guarded_from_production_source=screen_uv,
        screen_quad_GPU_draw_executed=False, original_screen_pixels_checked=screens_count,
        applying_an_extra_V_flip_to_screens_wrong_pixels=screen_bad, labels=labels,
        primary_sampler_source=SAMPLER_SOURCE, primary_wrap_source=WRAP_SOURCE,
        signed_conversion='C++ static_cast<s32> semantics: truncate toward zero; uint32 modulo in repeat/mirror',
        screen_limit='Real screen uploads plus source-guarded quad UV constants; draw and PICA are not executed.',
        sampler_limit='Independent nearest sampler uses float32 multiplication. PICA float24, edge rules, depth and blend are not emulated; negative cases check this source-derived model, not physical GPU equivalence.',
        allocator_seam='C3D allocation receives guarded host memory; flush/delete are observed. No GPU service success is claimed.',
        sanitizers=['address', 'undefined'], GPU_executed=False, Azahar_launched=False,
        physical_hardware_executed=False, game_clock_advanced=False, production_changed_by_this_test=False)
    report['actual_output_sha256'] = {str(path.relative_to(OUTPUT)): sha(path.read_bytes()) for path in sorted(OUTPUT.iterdir()) if path.suffix in ('.bin', '.morton8', '.argb') and path != fixture_path}
    (OUTPUT / 'renderer-orientation-test.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('status', 'actual_geometry_and_color_vertices_checked',
        'complete_upload_bytes_checked', 'actual_renderer_pixel_centres_checked', 'signed_cases_checked',
        'signed_floor_mutation_changes_addresses', 'original_screen_pixels_checked', 'labels')}, indent=2))


if __name__ == '__main__':
    main()
