"""Prepare the reviewed menu font as one native PICA A4 mask, offline only."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
try:
    from .audit_models import ROOT,SourceError,span
    from .source_members import menu_member
    from .bank import Image
    from .image_bank import Entry,build
    from .particles import catalog_header
except ImportError:
    from audit_models import ROOT,SourceError,span
    from source_members import menu_member
    from bank import Image
    from image_bank import Entry,build
    from particles import catalog_header


def prepare_mask(characters):
    if len(characters)!=16384:raise SourceError('Reviewed 256-glyph monochrome font extent')
    # Remap the original8x16 glyph halves offline. This is not a runtime tile
    # reader: the final payload is the GPU's own A4 Morton layout/orientation.
    pixels=bytearray(256*128)
    for code in range(256):
        for y in range(16):
            for x in range(8):
                tile=(code//32)*64+code%32+(y//8)*32
                value=(characters[tile*32+(y%8)*4+x//2]>>((x%2)*4))&15
                if value not in (0,15):raise SourceError('Font is not a binary mask; coloured glyphs require another policy')
                pixels[((code//32)*16+y)*256+(code%32)*8+x]=value
    tiled=bytearray(16384)
    for y in range(128):
        gy=127-y
        for x in range(256):
            morton=sum(((x>>bit)&1)<<(bit*2)|((gy>>bit)&1)<<(bit*2+1) for bit in range(3))
            at=((gy//8)*32+x//8)*64+morton
            tiled[at//2]|=pixels[y*256+x]<<((at%2)*4)
    return bytes(tiled),bytes(pixels)


def prepare(rom,profile):
    if profile['version']!=1 or profile['glyphs']!=256:raise SourceError('Font profile version/repertoire')
    source=menu_member(rom,profile['font_index'])
    if source.source_sha256!=profile['font_sha256']:raise SourceError('Original font fingerprint differs')
    ov=rom.loadArm9Overlays([7])[7]
    tables={}
    for name,row in profile['tables'].items():
        data=span(ov.data,row['address']-ov.ramAddress,row['bytes'])
        if hashlib.sha256(data).hexdigest()!=row['sha256']:raise SourceError('Original font table differs: '+name)
        tables[name]=data
    if tables['masks']!=b''.join(struct.pack('<I',i*0x11111111) for i in range(16)):
        raise SourceError('Original nibble tint policy differs')
    if len(tables['widths'])!=256 or max(tables['widths'])>16:raise SourceError('Glyph advance extent')
    palette=bytearray(menu_member(rom,profile['palette_index']).data)
    if len(palette)!=512:raise SourceError('Font palette extent')
    palette[0x1f6:]=tables['palette_override']
    colours=[]
    for i in range(16):
        word=struct.unpack_from('<H',palette,480+i*2)[0]
        rgb=[((word>>s)&31) for s in (0,5,10)]
        r,g,b=[(c<<3)|(c>>3) for c in rgb]
        colours.append((r<<24)|(g<<16)|(b<<8)|(255 if i else 0))
    payload,mask=prepare_mask(source.data)
    identity=hashlib.sha256(source.data+tables['widths']+tables['masks']+bytes(palette)).digest()
    native=build([Entry(Image(256,128,payload),texture_format=1)],identity)
    report=dict(scope='Prepared native font mask; original glyph painting/source lifetime and complete scene integration pending',
        bank_id=profile['bank_id'],source_sha256=identity.hex(),font_member=dict(handle=source.handle,path=source.path,sha256=source.source_sha256),
        font_crc32=zlib.crc32(source.data),font_bytes=len(source.data),glyphs=256,atlas_width=256,atlas_height=128,
        images=1,records=[dict(width=256,height=128,palette_zero_mask=False,texture_format='alpha4')],
        gpu_payload_bytes=len(payload),rgba8_alternative_bytes=len(payload)*8,mask_sha256=hashlib.sha256(mask).hexdigest(),
        native_sha256=hashlib.sha256(native).hexdigest(),widths=list(tables['widths']),colours=colours,
        colour_rule='Original PPU RGB5: (c<<3)|(c>>3); index0 transparent; source monochrome glyph AND nibble selector')
    return native,report,mask


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--rom',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--profile',type=Path,default=Path(__file__).with_name('font_profile.json'));args=parser.parse_args()
    out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():parser.error('Use a new private build/ directory')
    profile=json.loads(args.profile.read_text());raw=args.rom.read_bytes()
    if hashlib.sha1(raw).hexdigest()!=profile['rom_sha1']:parser.error('Expected reviewed original ROM')
    for evidence in profile['evidence']:
        if hashlib.sha256((ROOT/'third_party/sm64ds-3ds/sm64ds-decomp'/evidence['source']).read_bytes()).hexdigest()!=evidence['sha256']:
            parser.error('Original source evidence differs')
    import ndspy.rom
    native,report,mask=prepare(ndspy.rom.NintendoDSRom(raw),profile)
    report['profile_sha256']=hashlib.sha256(args.profile.read_bytes()).hexdigest()
    out.mkdir(parents=True);(out/f'{profile["bank_id"]:05d}.nim').write_bytes(native)
    (out/'native_image_catalog.hpp').write_text(catalog_header([report]))
    rows=['#pragma once','#include <array>','#include <cstdint>','namespace sm64ds::render::font_fixture {',
          f'inline constexpr unsigned bank={profile["bank_id"]};',
          'inline constexpr std::array<std::uint8_t,32> source_sha256={'+','.join(str(v) for v in bytes.fromhex(report['source_sha256']))+'};',
          'inline constexpr std::uint32_t colours[]={'+','.join(str(v)+'u' for v in report['colours'])+'};',
          'inline constexpr std::uint8_t widths[]={'+','.join(str(v) for v in report['widths'])+'};','}']
    (out/'native_font_fixture.hpp').write_text('\n'.join(rows)+'\n')
    (out/'font-mask.bin').write_bytes(mask);(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('widths','colours','records')},indent=2));return 0


if __name__=='__main__':raise SystemExit(main())
