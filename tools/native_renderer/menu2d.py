"""Prepare reviewed original menu cells as immutable native images, offline.

Only the explicit first menu cell bank is admitted. Unsupported affine/mosaic/
mixed-priority policies refuse cooking, rather than silently flattening them.
The typed game producer must still resolve live state before presentation.
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

try:
    from .audit_models import ROOT, SourceError, span, u32
    from .nitro2d import characters, palette, cells
    from .bank import Image, tiled_rgba8
    from .image_bank import Entry, build
    from .particles import catalog_header
except ImportError:
    from audit_models import ROOT, SourceError, span, u32
    from nitro2d import characters, palette, cells
    from bank import Image, tiled_rgba8
    from image_bank import Entry, build
    from particles import catalog_header

DEFAULT = Path(__file__).with_name('menu2d_profile.json')


def bake_cell(source, colours, parts, reference_colours=True):
    if not parts or len(parts) > 128 or len(colours) != 512:
        raise SourceError('Menu cell bake bounds')
    if len({p.priority for p in parts}) != 1:
        raise SourceError('Mixed-priority cell requires separate native layers')
    for p in parts:
        if p.affine or p.hidden or p.mode or p.mosaic or p.eight_bit != source.eight_bit:
            raise SourceError('Menu cell requires unsupported pixel/affine policy')
        if not -256 <= p.x <= 255 or not -128 <= p.y <= 127:
            raise SourceError('Cell needs explicit source-coordinate wrap')
    left = min(p.x for p in parts);top = min(p.y for p in parts)
    right = max(p.x+p.width for p in parts);bottom = max(p.y+p.height for p in parts)
    width = max(8, 1 << (right-left-1).bit_length())
    height = max(8, 1 << (bottom-top-1).bit_length())
    if width > 1024 or height > 1024:
        raise SourceError('Cell image exceeds native texture dimensions')
    output = bytearray(width*height*4)
    # Original 2D OBJ mapping has a 32*32-byte row. 8-bit tiles occupy two
    # units; this is an offline source layout rule, absent from the renderer.
    row_tiles = 16 if source.eight_bit else 32
    if source.width != row_tiles*8:
        raise SourceError('Character atlas does not match reviewed 2D OBJ layout')
    for p in reversed(parts):  # lower original OAM index wins an equal-priority overlap
        if source.eight_bit and p.tile%2:
            raise SourceError('Odd 8-bit OBJ tile address')
        tile = p.tile//2 if source.eight_bit else p.tile
        tx = (tile%row_tiles)*8;ty = (tile//row_tiles)*8
        for y in range(p.height):
            for x in range(p.width):
                index = source.index(tx+(p.width-1-x if p.flip_x else x),ty+(p.height-1-y if p.flip_y else y))
                if not index:
                    continue
                colour = struct.unpack_from('<H', colours, (index if source.eight_bit else p.palette*16+index)*2)[0]
                rgb = [((colour>>shift)&31) for shift in (0,5,10)]
                # Preserve the approved reference's 2D colour quantization.
                # It differs from ordinary RGB5 replication by up to4 levels.
                # This is baked offline, with no added runtime colour work.
                rgb = [(v<<3)|(v>>(3 if reference_colours else 2)) for v in rgb]
                at = ((p.y-top+y)*width+p.x-left+x)*4
                output[at:at+4] = bytes(rgb+[255])
    return bytes(output),dict(left=left,top=top,width=width,height=height,content_width=right-left,content_height=bottom-top,
        parts=len(parts),priority=parts[0].priority,palette_zero_mask=False)


def reviewed_sources(rom, config):
    if config.get('version')!=1 or config.get('bank_id')!=4096 or config.get('cell_bank')!=0 or config.get('overlay')!=7:
        raise SourceError('Unreviewed menu2D profile')
    vendor=(ROOT/'third_party/sm64ds-3ds/sm64ds-decomp').resolve()
    if not config.get('evidence'):
        raise SourceError('Missing original-source evidence')
    for proof in config['evidence']:
        path=(vendor/proof['source']).resolve()
        if not path.is_relative_to(vendor) or hashlib.sha256(path.read_bytes()).hexdigest()!=proof['sha256']:
            raise SourceError('Original menu2D source contract changed')
    colour=config['reference_colour_source'];provider=vendor.parent
    source=(provider/colour['source']).resolve()
    if not source.is_relative_to(provider) or hashlib.sha256(source.read_bytes()).hexdigest()!=colour['sha256']:
        raise SourceError('Reference 2D colour quantization changed')
    overlay=rom.loadArm9Overlays([7])[7]
    output=[]
    for kind in ['characters','palette','cells']:
        row=config[kind]
        pointer=u32(overlay.data,row['table']-overlay.ramAddress+row['index']*4)
        if pointer!=row['address']:
            raise SourceError('Reviewed menu resource-table identity changed')
        offset=pointer-overlay.ramAddress;size=u32(overlay.data,offset+8)
        if size>16*1024*1024:
            raise SourceError('Menu resource bound')
        output.append(bytes(span(overlay.data,offset,size)))
    return output


def prepare(source_data, config):
    character_data,palette_data,cell_data=source_data
    character=characters(character_data);colours=palette(palette_data);cell_list=cells(cell_data)
    if len(cell_list)!=5:
        raise SourceError('Reviewed first cell bank roster changed')
    # Cooking policy participates in identity: a prior normally-expanded RGB5
    # bank must not be accepted by a reference-quantized native catalog.
    identity=hashlib.sha256(b'SM64_NATIVE_MENU_CELL_1\0reference2d-rgb5-shift3\0'+
        b''.join(struct.pack('<I',len(d))+d for d in source_data)).digest()
    entries=[];records=[];pixels=[]
    for index,parts in enumerate(cell_list):
        data,record=bake_cell(character,colours,parts);pixels.append(data)
        native=tiled_rgba8(data,record['width'],record['height'])
        entries.append(Entry(Image(record['width'],record['height'],native),False))
        record.update(image=index,gpu_bytes=len(native),rgba_sha256=hashlib.sha256(data).hexdigest(),source_parts=[asdict(p) for p in parts])
        record['character_end']=max(((p.tile//2 if character.eight_bit else p.tile)+
            (p.height//8-1)*(16 if character.eight_bit else 32)+p.width//8)*
            (64 if character.eight_bit else 32) for p in parts)
        records.append(record)
    bank=build(entries,identity)
    report=dict(bank_id=config['bank_id'],source='reviewed menu cell bank0/characters1/palette1',source_sha256=identity.hex(),
        input_sha256=[hashlib.sha256(d).hexdigest() for d in source_data],images=len(entries),records=records,gpu_bytes=sum(r['gpu_bytes'] for r in records),
        native_sha256=hashlib.sha256(bank).hexdigest(),colour_rule='reference2D: (c<<3)|(c>>3), offline only',original_scene_accepted=False,
        source_bytes=[len(d) for d in source_data],source_crc32=[zlib.crc32(d) for d in source_data])
    return bank,report,pixels


def cell_catalog_header(report):
    lines=['#pragma once','#include "menu_cell_capture.hpp"',
        'namespace sm64ds::render::game_hooks::prepared_menu {']
    rows=[]
    for cell in report['records']:
        name='cell_'+str(cell['image'])+'_parts'
        lines.append('inline constexpr MenuCellPart '+name+'[]={'+','.join(
            '{'+','.join(str(p[k]) for k in ['x','y','width','height'])+'}' for p in cell['source_parts'])+'};')
        rows.append('{'+name+','+','.join(str(cell[k]) for k in ['parts','image','left','top','width','height','character_end'])+'}')
    lines.append('inline constexpr MenuCellImage cells[]={'+','.join(rows)+'};')
    sizes=report['source_bytes'];crc=report['source_crc32']
    image_identity='{'+','.join(str(v) for v in bytes.fromhex(report['source_sha256']))+'}'
    lines.append('inline constexpr MenuCellBank banks[]={{0,'+str(report['bank_id'])+',cells,'+str(report['images'])+','+
        ','.join(str(v)+'u' for v in [sizes[2],crc[2],sizes[0],crc[0],sizes[1],crc[1]])+','+image_identity+'}};')
    lines.extend(['}',''])
    return '\n'.join(lines)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--profile',type=Path,default=DEFAULT);parser.add_argument('--preview',action='store_true')
    args=parser.parse_args();config=json.loads(args.profile.read_text());raw=args.rom.read_bytes()
    if hashlib.sha1(raw).hexdigest()!=config['rom_sha1']:
        parser.error('Expected the reviewed supported ROM')
    output=args.output.resolve()
    if not output.is_relative_to((ROOT/'build').resolve()) or output.exists():
        parser.error('Use a new private output directory under build/')
    import ndspy.rom
    try:
        bank,report,pixels=prepare(reviewed_sources(ndspy.rom.NintendoDSRom(raw),config),config)
    except (SourceError,KeyError,ValueError,struct.error) as error:
        parser.error(str(error))
    report.update(rom_sha1=config['rom_sha1'],profile_sha256=hashlib.sha256(args.profile.read_bytes()).hexdigest())
    header=catalog_header([report]);report['header_sha256']=hashlib.sha256(header.encode()).hexdigest()
    cell_header=cell_catalog_header(report);report['cell_header_sha256']=hashlib.sha256(cell_header.encode()).hexdigest()
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.menu2d-',dir=output.parent) as temporary:
        staging=Path(temporary)/'catalog';staging.mkdir()
        (staging/f'{config["bank_id"]:05d}.nim').write_bytes(bank)
        (staging/'native_image_catalog.hpp').write_text(header)
        (staging/'native_menu_cell_catalog.hpp').write_text(cell_header)
        (staging/'catalog.json').write_text(json.dumps(report,indent=2)+'\n')
        if args.preview:
            from PIL import Image as Preview
            for record,data in zip(report['records'],pixels):
                Preview.frombytes('RGBA',(record['width'],record['height']),data).save(staging/f'cell-{record["image"]}.png')
        os.rename(staging,output)
    print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2))


if __name__=='__main__':main()
