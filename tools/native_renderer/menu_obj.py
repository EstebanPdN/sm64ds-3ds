"""Cook all original menu OBJ cells for explicit layout/engine/language views.

Source tiles and palette splices are handled offline. Native outputs contain
resident RGBA textures and logical cell IDs, never runtime VRAM lookups.
"""
import argparse
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib

from .audit_models import ROOT, SourceError, span, u32
from .bank import Image, tiled_rgba8
from .image_bank import Entry, build
from .menu2d import bake_cell
from .nitro2d import Characters, characters, palette, cells
from .particles import catalog_header
from .source_members import menu_member

DEFAULT = Path(__file__).with_name('menu_obj_profile.json')


def obj_view(title, file_menu, colours, icons, icon_colours, replacement, layout, engine):
    """Mirror the original semantic asset loads, including untouched suffixes.

    Inputs are decoded/validated assets. Layout3 does not reload MAIN's default
    title characters. A language replacement updates SUB's counted extent and
    MAIN's first8KiB; MAIN's icon half/palette tail survive both layouts.
    """
    base = file_menu if layout == 3 and engine == 1 else title
    pixels = bytearray(base.pixels)
    if replacement is not None:
        count = len(replacement.pixels) if engine == 1 else 8192
        pixels[:count] = replacement.pixels[:count]
    colours = bytearray(colours)
    if engine == 0:
        pixels[8192:] = icons
        colours[480:] = icon_colours[192:224]
    return Characters(256, 128, False, bytes(pixels)), bytes(colours)


def source_assets(rom, profile):
    if profile['version'] != 1 or profile['first_bank'] != 4100 or profile['layouts'] != [1, 3]:
        raise SourceError('Menu OBJ profile version/namespace/layouts')
    if profile['tables'] != {'characters': {'address': 0x020d7e28, 'count': 4},
                             'palettes': {'address': 0x020d7e38, 'count': 6},
                             'cells': {'address': 0x020d7e50, 'count': 6}}:
        raise SourceError('Original menu OBJ table identities')
    for row in profile['evidence']:
        path = ROOT / 'third_party/sm64ds-3ds/sm64ds-decomp' / row['source']
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']:
            raise SourceError('Original menu OBJ source evidence changed: ' + row['source'])
    ov = rom.loadArm9Overlays([7])[7]
    resources = {}
    for kind, row in profile['tables'].items():
        resources[kind] = []
        for index in range(row['count']):
            pointer = u32(ov.data, row['address'] - ov.ramAddress + index * 4)
            size = u32(ov.data, pointer - ov.ramAddress + 8)
            if not 16 <= size <= 1024 * 1024:
                raise SourceError('Counted embedded menu OBJ resource extent')
            resources[kind].append(bytes(span(ov.data, pointer - ov.ramAddress, size)))
    if tuple(map(len, (resources['characters'], resources['palettes'], resources['cells']))) != (4, 6, 6):
        raise SourceError('Original menu OBJ resource roster')
    title, file_menu = (characters(resources['characters'][i]) for i in (1, 2))
    for source in (title, file_menu):
        if (source.width, source.height, source.eight_bit, len(source.pixels)) != (256, 128, False, 16384):
            raise SourceError('Original default OBJ atlas extent/format')
    colours = palette(resources['palettes'][1])
    cell_banks = [cells(data) for data in resources['cells']]
    if [len(bank) for bank in cell_banks] != [5, 6, 21, 16, 1, 1]:
        raise SourceError('Original menu OBJ cell roster')
    external = {}
    import ndspy.lz10
    for index in list(range(24, 32)) + [44, 45]:
        member = menu_member(rom, index, decompress=index == 44)
        decoded = member.data
        if index < 32:
            if decoded[:5] != b'LZ77\x10' or not 8192 < int.from_bytes(decoded[5:8], 'little') < 32768:
                raise SourceError('Original localized OBJ compression/extent')
            decoded = bytes(ndspy.lz10.decompress(decoded[4:]))
            atlas = characters(decoded)
            if atlas.width != 256 or atlas.eight_bit or not 8192 <= len(atlas.pixels) <= 16384:
                raise SourceError('Original localized OBJ character extent/format')
        external[index] = dict(member=member, decoded=decoded)
    icons, icon_colours = external[44]['decoded'], external[45]['decoded']
    if len(icons) != 8192 or len(icon_colours) != 224:
        raise SourceError('Original MAIN icon character/palette extent')
    return resources, title, file_menu, colours, cell_banks, external


def cell_header(report):
    lines = ['#pragma once', '#include "menu_cell_capture.hpp"',
             'namespace sm64ds::render::game_hooks::prepared_menu {']
    bindings = []
    for bank in report['cell_banks']:
        rows = []
        for cell in bank['records']:
            name = f'cell_{bank["source_bank"]}_{cell["cell"]}_parts'
            lines.append('inline constexpr MenuCellPart ' + name + '[]={' + ','.join(
                '{' + ','.join(str(p[k]) for k in ('x', 'y', 'width', 'height')) + '}' for p in cell['source_parts']) + '};')
            rows.append('{' + name + ',' + ','.join(str(cell[k]) for k in
                ('parts', 'image', 'left', 'top', 'width', 'height', 'character_end')) + '}')
        name = f'cells_{bank["source_bank"]}'
        lines.append('inline constexpr MenuCellImage ' + name + '[]={' + ','.join(rows) + '};')
        sizes, crc = bank['source_bytes'], bank['source_crc32']
        identity = '{' + ','.join(str(v) for v in bytes.fromhex(report['source_sha256'])) + '}'
        bindings.append('{' + ','.join((str(bank['source_bank']), str(report['bank_id']), name, str(len(rows)))) + ',' +
            ','.join(str(v) + 'u' for v in (sizes[2], crc[2], sizes[0], crc[0], sizes[1], crc[1])) + ',' + identity + '}')
    lines.append('inline constexpr MenuCellBank banks[]={' + ','.join(bindings) + '};')
    lines += ['}', '']
    return '\n'.join(lines)


def prepare(rom, profile):
    resources, title, file_menu, colours, source_cells, external = source_assets(rom, profile)
    views = []
    # Validate the explicit language routing at the owning profile boundary.
    languages = profile['languages']
    if [(r['mask'], r['index']) for r in languages] != [(3, None), (4, 2), (8, 3), (16, 4), (32, 5)]:
        raise SourceError('Original menu language routing')
    for layout in (1, 3):
        for language in languages:
            index = None if language['index'] is None else (1 if layout == 1 else 2) + 23 + (language['index'] - 2) * 2
            localized = characters(external[index]['decoded']) if index is not None else None
            for engine in (0, 1):
                source, view_colours = obj_view(title, file_menu, colours, external[44]['decoded'], external[45]['decoded'], localized, layout, engine)
                recipe = dict(layout=layout, engine=engine, language_mask=language['mask'], replacement_index=index,
                              preserved_suffix=True, main_icon_splice=engine == 0)
                identity = hashlib.sha256(b'SM64_NATIVE_MENU_OBJ_1\0reference2d-rgb5-shift3\0' +
                    json.dumps(recipe, sort_keys=True).encode() + source.pixels + view_colours + b''.join(resources['cells'])).digest()
                entries, records, banks, previews = [], [], [], []
                for source_bank, parts_list in enumerate(source_cells):
                    bank_records = []
                    for cell, parts in enumerate(parts_list):
                        rgba, record = bake_cell(source, view_colours, parts)
                        payload = tiled_rgba8(rgba, record['width'], record['height'])
                        record.update(cell=cell, image=len(entries), source_bank=source_bank,
                                      character_end=max((p.tile + (p.height // 8 - 1) * 32 + p.width // 8) * 32 for p in parts),
                                      gpu_bytes=len(payload), rgba_sha256=hashlib.sha256(rgba).hexdigest(),
                                      source_parts=[asdict(p) for p in parts])
                        entries.append(Entry(Image(record['width'], record['height'], payload), False))
                        records.append(record);bank_records.append(record);previews.append(rgba)
                    char_data = resources['characters'][2 if layout == 3 and engine == 1 else 1]
                    inputs = [char_data, resources['palettes'][1], resources['cells'][source_bank]]
                    banks.append(dict(source_bank=source_bank, records=bank_records,
                                      source_bytes=[len(data) for data in inputs], source_crc32=[zlib.crc32(data) for data in inputs]))
                native = build(entries, identity)
                report = dict(bank_id=profile['first_bank'] + len(views), source_sha256=identity.hex(),
                              native_sha256=hashlib.sha256(native).hexdigest(), recipe=recipe,
                              name=f'layout-{layout}-{"main" if engine == 0 else "sub"}-{language["name"]}',
                              images=len(records), records=records, cell_banks=banks,
                              gpu_bytes=sum(record['gpu_bytes'] for record in records),
                              atlas_sha256=hashlib.sha256(source.pixels).hexdigest(), palette_sha256=hashlib.sha256(view_colours).hexdigest(),
                              replacement_file=[index, len(external[index]['decoded']), zlib.crc32(external[index]['decoded'])] if index is not None else [0, 0, 0],
                              icon_file=[44, 8192, zlib.crc32(external[44]['decoded'])] if engine == 0 else [0, 0, 0],
                              icon_palette_file=[45, 224, zlib.crc32(external[45]['decoded'])] if engine == 0 else [0, 0, 0],
                              original_scene_accepted=False)
                views.append((native, report, previews))
    members = [dict(index=index, path=row['member'].path, source_sha256=row['member'].source_sha256,
                    decoded_sha256=hashlib.sha256(row['decoded']).hexdigest(), decoded_bytes=len(row['decoded']))
               for index, row in external.items()]
    return views, members


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', type=Path, default=DEFAULT)
    parser.add_argument('--preview', action='store_true')
    args = parser.parse_args()
    raw = args.rom.read_bytes();profile = json.loads(args.profile.read_text())
    if hashlib.sha1(raw).hexdigest() != profile['rom_sha1']:
        parser.error('Expected the reviewed supported ROM')
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / 'build').resolve()) or output.exists():
        parser.error('Use a new private build/ directory')
    import ndspy.rom
    views, members = prepare(ndspy.rom.NintendoDSRom(raw), profile)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.menu-obj-', dir=output.parent) as temporary:
        staging = Path(temporary) / 'catalog';staging.mkdir()
        reports = []
        master = ['#pragma once', '#include "menu_obj_catalog.hpp"']
        selectors = []
        for native, report, previews in views:
            (staging / f'{report["bank_id"]:05d}.nim').write_bytes(native)
            view = staging / report['name'];view.mkdir()
            (view / 'native_menu_cell_catalog.hpp').write_text(cell_header(report))
            (view / 'native_image_catalog.hpp').write_text(catalog_header([report]))
            (view / 'catalog.json').write_text(json.dumps(report, indent=2) + '\n')
            namespace = f'prepared_menu_obj::view_{len(selectors)}'
            master.append(cell_header(report).replace('prepared_menu {', namespace + ' {'))
            recipe = report['recipe']
            fields = [str(recipe['layout']), str(recipe['engine']), str(recipe['language_mask']), '{'+namespace+'::banks,6}']
            fields += ['{' + ','.join(str(n)+'u' for n in report[key]) + '}' for key in ('replacement_file', 'icon_file', 'icon_palette_file')]
            selectors.append('{' + ','.join(fields) + '}')
            if args.preview:
                from PIL import Image as Preview, ImageDraw
                width = max(r['width'] for r in report['records'])
                height = max(r['height'] for r in report['records']) + 16
                sheet = Preview.new('RGBA', (5 * width, 10 * height), (40, 48, 64, 255));draw = ImageDraw.Draw(sheet)
                for i, (record, rgba) in enumerate(zip(report['records'], previews)):
                    x, y = i % 5 * width, i // 5 * height
                    draw.text((x, y), f'{record["source_bank"]}:{record["cell"]}', fill='white')
                    sheet.alpha_composite(Preview.frombytes('RGBA', (record['width'], record['height']), rgba), (x, y + 16))
                sheet.save(view / 'cells.png')
            reports.append({k: v for k, v in report.items() if k not in ('records', 'cell_banks')})
        (staging / 'native_image_catalog.hpp').write_text(catalog_header([r for _, r, _ in views]))
        master += ['namespace sm64ds::render::game_hooks::prepared_menu_obj {',
                   'inline constexpr MenuObjView views[]={' + ','.join(selectors) + '};', '}']
        (staging / 'native_menu_obj_catalog.hpp').write_text('\n'.join(master) + '\n')
        manifest = dict(scope=profile['scope'], rom_sha1=profile['rom_sha1'],
                        profile_sha256=hashlib.sha256(args.profile.read_bytes()).hexdigest(), members=members, views=reports,
                        runtime_routing_accepted=False, physical_hardware=False)
        (staging / 'catalog.json').write_text(json.dumps(manifest, indent=2) + '\n')
        os.rename(staging, output)
    print(json.dumps(dict(views=len(views), cells_per_view=50, native_images=sum(r['images'] for r in reports),
                         gpu_bytes_if_all_views_resident=sum(r['gpu_bytes'] for r in reports), output=str(output)), indent=2))


if __name__ == '__main__':
    main()
