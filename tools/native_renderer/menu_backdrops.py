"""Prepare every static original menu background and its palette states.

Character/palette splices and source maps are consumed offline. The native
output is immutable geometry/atlas data; the game selects a logical part.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib
from .audit_models import ROOT, SourceError, span, u32
from .backgrounds import prepare_variants, palette_variants
from .nitro2d import characters, palette, tilemap
from .source_members import menu_member


def sub_palettes(base, replacements, override):
    """208-colour loads preserve the last48 colours; font tail is explicit."""
    result = []
    for source in (None, *replacements):
        colours = bytearray(base)
        if source is not None:colours[:416] = source[:416]
        colours[502:] = override
        result.append(bytes(colours))
    return tuple(result)


def prepare_all(rom, profile):
    if profile['version'] != 1 or profile['first_bank'] != 5200:
        raise SourceError('Menu backdrop profile version/namespace')
    for proof in profile['evidence']:
        path = ROOT / 'third_party/sm64ds-3ds/sm64ds-decomp' / proof['source']
        if hashlib.sha256(path.read_bytes()).hexdigest() != proof['sha256']:
            raise SourceError('Original backdrop source changed: ' + proof['source'])
    ov = rom.loadArm9Overlays([7])[7]
    resources = {}
    for name, table, indices in [('characters', 0x020d7e28, (0, 3)), ('palettes', 0x020d7e38, range(6)), ('maps', 0x020d7ec8, range(1, 5))]:
        resources[name] = {}
        for index in indices:
            at = u32(ov.data, table - ov.ramAddress + index * 4) - ov.ramAddress
            size = u32(ov.data, at + 8)
            if not 16 <= size <= 1024 * 1024:raise SourceError('Embedded backdrop extent')
            resources[name][index] = bytes(span(ov.data, at, size))
    maps = {i: tilemap(raw) for i, raw in resources['maps'].items()}
    if any((s.width, s.height) != (32, 32) for s in maps.values()):raise SourceError('Original backdrop map extent')
    source_char = {i: characters(raw) for i, raw in resources['characters'].items()}
    if [(source_char[i].width, source_char[i].height, source_char[i].eight_bit) for i in (0, 3)] != [(256, 128, True), (256, 256, True)]:
        raise SourceError('Original SUB background atlas format')
    colours = {i: palette(raw) for i, raw in resources['palettes'].items()}
    override = bytes(span(ov.data, 0x020d7a44 - ov.ramAddress, 10))
    alternatives = sub_palettes(colours[0], [colours[i] for i in range(2, 6)], override)
    recipes = []
    main_palette = bytearray(menu_member(rom, 43).data)
    if len(main_palette) != 512:raise SourceError('Original MAIN palette extent')
    main_palette[502:] = override
    for language in range(6):
        member = menu_member(rom, 37 + language, decompress=language != 0)
        if len(member.data) != 16384:raise SourceError('Original MAIN background character extent')
        screens = palette_variants(maps[1])
        recipes.append((dict(layout=1, engine=0, language_mask=1 << language, resource=1,
                             palette_states=list(range(5)), character_member=37 + language),
                        screens, member.data, (bytes(main_palette),) * 5, False))
    for layout, char_index, states, map_indices in [(1, 0, range(5), (2, 3)), (3, 3, range(1, 5), (2, 3, 4))]:
        for resource in map_indices:
            recipe = dict(layout=layout, engine=1, language_mask=63, resource=resource,
                          palette_states=list(states), character_resource=char_index)
            selected = tuple(alternatives[i] for i in states)
            recipes.append((recipe, (maps[resource],) * len(selected), source_char[char_index].pixels, selected, True))
    output = []
    for recipe, screens, chars, palettes, eight in recipes:
        raw_maps = [struct.pack('<1024H', *s.entries) for s in screens]
        identity = hashlib.sha256(b'SM64_NATIVE_MENU_BACKDROP_1\0' + json.dumps(recipe, sort_keys=True).encode() +
                                  chars + b''.join(raw_maps) + b''.join(palettes)).digest()
        native, report, previews, _ = prepare_variants(screens, chars, palettes[0], identity, eight, palettes)
        report.update(bank_id=profile['first_bank'] + len(output), recipe=recipe, source_sha256=identity.hex(),
                      character_bytes=len(chars), character_crc=zlib.crc32(chars), eight_bit=eight,
                      map_crcs=[zlib.crc32(m) for m in raw_maps], palette_crcs=[zlib.crc32(p) for p in palettes],
                      backdrop_rgb5=[int.from_bytes(p[:2], 'little') for p in palettes],
                      repeat_edges=[repeating_gradient(preview) for preview in previews],
                      original_scene_accepted=False)
        if recipe['engine']==0:
            report.update(binding_character=recipe['character_member'],binding_character_bytes=len(chars),binding_character_crc=zlib.crc32(chars),
                          binding_palette_bytes=512,binding_palette_crc=zlib.crc32(menu_member(rom,43).data))
        else:
            char_source=resources['characters'][recipe['character_resource']]
            report.update(binding_character=recipe['character_resource'],binding_character_bytes=len(char_source),binding_character_crc=zlib.crc32(char_source),
                          binding_palette_bytes=len(resources['palettes'][0]),binding_palette_crc=zlib.crc32(resources['palettes'][0]),
                          colour_palette_bytes=[len(resources['palettes'][i]) for i in range(2,6)],colour_palette_crc=[zlib.crc32(resources['palettes'][i]) for i in range(2,6)])
        output.append((native, report, previews, chars, raw_maps, palettes))
    return output


def repeating_gradient(preview):
    """Admit row gradients, including their original low-contrast dithering.

    Repeat prepared geometry at its original scale. Transparent rows stay
    transparent; pictures/lettering with contrast within a row are excluded.
    """
    for y in range(256):
        row = preview[y*1024:(y+1)*1024]
        colours = set(row[x:x+4] for x in range(0,1024,4))
        if len({c[3] for c in colours}) != 1:return False
        # Two original RGB5 quantization steps expand to at most17 levels.
        if any(max(c[channel] for c in colours)-min(c[channel] for c in colours)>17 for channel in range(3)):return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', type=Path, default=Path(__file__).with_name('menu_backdrop_profile.json'))
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text());raw = args.rom.read_bytes()
    if hashlib.sha1(raw).hexdigest() != profile['rom_sha1']:parser.error('Expected reviewed supported ROM')
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / 'build').resolve()) or output.exists():parser.error('Use a new private build/ directory')
    import ndspy.rom
    banks = prepare_all(ndspy.rom.NintendoDSRom(raw), profile)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.menu-backdrops-', dir=output.parent) as temporary:
        staging = Path(temporary) / 'catalog';staging.mkdir()
        reports = [];bindings = [];native_views = []
        view_header=['#pragma once','#include "menu_backdrops.hpp"','namespace sm64ds::render::game_hooks::prepared_backdrop_views {']
        for native, report, previews, chars, maps, palettes in banks:
            bank = report['bank_id'];name = str(bank)
            (staging / f'{bank:05d}.nmd').write_bytes(native)
            folder = staging / name;folder.mkdir();(folder / 'characters.bin').write_bytes(chars)
            for i, (preview, screen, colours) in enumerate(zip(previews, maps, palettes)):
                (folder / f'map-{i}.bin').write_bytes(screen)
                (folder / f'palette-{i}.bin').write_bytes(colours)
                (folder / f'preview-{i}.rgba').write_bytes(preview)
            (folder / 'catalog.json').write_text(json.dumps(report, indent=2) + '\n')
            bindings.append('{' + str(bank) + 'u,{' + ','.join(str(v) for v in bytes.fromhex(report['source_sha256'])) + '},' + str(report['parts']) + ',1}')
            parts=[]
            for i,state in enumerate(report['recipe']['palette_states']):
                parts.append('{'+str(report['map_crcs'][i])+'u,'+str(i)+','+str(state)+',{'+','.join(str(x) for x in report['bounds'][i])+'},'+str(report['backdrop_rgb5'][i])+','+str(report['repeat_edges'][i]).lower()+'}')
            name='parts_'+str(bank)
            view_header.append('inline constexpr MenuBackdropPart '+name+'[]={'+','.join(parts)+'};')
            recipe=report['recipe']
            native_views.append('{'+','.join(str(x) for x in [bank,recipe['layout'],recipe['engine'],recipe['language_mask'],recipe['resource'],report['parts']])+','+name+',{'+
                ','.join(str(v) for v in bytes.fromhex(report['source_sha256']))+'},'+','.join(str(report[k])+'u' for k in
                ['binding_character','binding_character_bytes','binding_character_crc','binding_palette_bytes','binding_palette_crc'])+','+
                ','.join('{'+','.join(str(v)+'u' for v in report.get(k,[0]*4))+'}' for k in ['colour_palette_bytes','colour_palette_crc'])+'}')
            reports.append(report)
        header = '#pragma once\n#include "canvas_catalog.hpp"\nnamespace sm64ds::render::prepared_canvases {\ninline constexpr CanvasBinding banks[]={' + ','.join(bindings) + '};\n}\n'
        (staging / 'native_canvas_catalog.hpp').write_text(header)
        view_header+=['inline constexpr MenuBackdropView views[]={'+','.join(native_views)+'};','}']
        (staging / 'native_menu_backdrop_catalog.hpp').write_text('\n'.join(view_header)+'\n')
        manifest = dict(scope='Prepared original menu backdrop resources and palette states; live routing/effects/GPU acceptance pending',
                        profile_sha256=hashlib.sha256(args.profile.read_bytes()).hexdigest(), banks=reports,
                        original_scene_accepted=False, physical_hardware=False)
        (staging / 'catalog.json').write_text(json.dumps(manifest, indent=2) + '\n')
        os.rename(staging, output)
    print(json.dumps(dict(banks=len(banks), variants=sum(r['variants'] for r in reports),
                         native_payload_bytes=sum(r['native_gpu_payload'] for r in reports), output=str(output)), indent=2))


if __name__ == '__main__':main()
