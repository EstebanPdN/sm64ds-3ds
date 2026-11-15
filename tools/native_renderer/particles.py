#!/usr/bin/env python3
"""Prepare reviewed particle images on the host, never during rendering.

The original loader/CPU simulation remains authoritative for definitions,
lifetimes and behaviour. Native banks contain only tiled GPU images. Pack
identities come from pinned original source and the supported local ROM.
"""
import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

try:
    from .audit_models import ROOT,SourceError,span,u32
    from .bank import Image,tiled_rgba8
    from .image_bank import Entry,build
    from .textures import decode_texture
    from .texture_sequence import string
except ImportError:
    from audit_models import ROOT,SourceError,span,u32
    from bank import Image,tiled_rgba8
    from image_bank import Entry,build
    from textures import decode_texture
    from texture_sequence import string

DEFAULT=Path(__file__).with_name('particle_profiles.json')


@dataclass(frozen=True)
class Texture:
    width: int
    height: int
    fmt: int
    flags: int
    pixels: bytes
    palette: bytes


def parse(data):
    span(data,0,32)
    if len(data)>16*1024*1024 or bytes(data[:8])!=b' APS10_1' or u32(data,12) or u32(data,28):
        raise SourceError('Particle resource header/version')
    definitions,images=struct.unpack_from('<HH',data,8)
    definition_bytes,texture_bytes,texture_at=struct.unpack_from('<3I',data,16)
    if not 1<=definitions<=1024 or not 1<=images<=128:
        raise SourceError('Particle resource count bounds')
    if texture_at!=32+definition_bytes or texture_at+texture_bytes!=len(data):
        raise SourceError('Particle resource extent')
    cursor=32;flags_list=[]
    for _ in range(definitions):
        flags=u32(data,cursor);flags_list.append(flags)
        size=56+sum(size for bit,size in ((8,12),(9,12),(10,8),(11,12),(16,20),
                    (24,8),(25,8),(26,16),(27,4),(28,8),(29,16)) if flags&(1<<bit))
        span(data,cursor,size)
        cursor+=size
        if cursor>texture_at:raise SourceError('Particle definition crosses texture table')
    if cursor!=texture_at:raise SourceError('Particle definition layout differs from original loader')
    textures=[]
    for _ in range(images):
        span(data,cursor,32)
        magic,flags,pixels_bytes,palette_at,palette_bytes,_,_,total=struct.unpack_from('<8I',data,cursor)
        if magic!=0x53505420 or total<32:
            raise SourceError('Particle texture header/extent')
        record=span(data,cursor,total)
        fmt=flags&15;size_s=(flags>>4)&15;size_t=(flags>>8)&15
        if size_s>7 or size_t>7 or fmt not in (1,2,3,4,6,7):
            raise SourceError(f'Particle texture requires unsupported format/size: {fmt}/{size_s}/{size_t}')
        # The reviewed packs have no compressed4x4 side tables or padding.
        # Reject a new layout explicitly rather than guessing another field.
        if palette_at!=32+pixels_bytes or total!=palette_at+palette_bytes:
            raise SourceError('Unreviewed particle texture payload layout')
        pixels=bytes(span(record,32,pixels_bytes));palette=bytes(span(record,palette_at,palette_bytes))
        width,height=8<<size_s,8<<size_t
        expected=width*height*{1:8,2:2,3:4,4:8,6:8,7:16}[fmt]//8
        if pixels_bytes!=expected or palette_bytes%2 or (fmt==7 and palette_bytes):
            raise SourceError('Particle texture dimensions/palette bytes')
        textures.append(Texture(width,height,fmt,flags,pixels,palette))
        cursor+=total
    if cursor!=len(data):raise SourceError('Particle textures leave trailing data')
    return tuple(flags_list),tuple(textures)


def prepare(data,name):
    definitions,textures=parse(data);entries=[];records=[]
    for index,texture in enumerate(textures):
        mask=texture.fmt in (2,3,4)
        rgba=decode_texture(texture.pixels,texture.palette,texture.width,texture.height,texture.fmt,
                            zero_transparent=mask,retain_zero_rgb=mask)
        native=tiled_rgba8(rgba,texture.width,texture.height)
        entries.append(Entry(Image(texture.width,texture.height,native),mask))
        records.append(dict(image=index,width=texture.width,height=texture.height,source_format=texture.fmt,
            palette_zero_mask=mask,source_zero_transparent=bool(texture.flags&(1<<16)),
            rgba_sha256=hashlib.sha256(rgba).hexdigest(),gpu_bytes=len(native)))
    sha=hashlib.sha256(data).digest();bank=build(entries,sha)
    report=dict(source=name,source_sha256=sha.hex(),definitions=len(definitions),images=len(textures),
        primary_shapes=dict(Counter(str((flags>>4)&3) for flags in definitions)),
        source_formats=dict(Counter(str(texture.fmt) for texture in textures)),
        native_bytes=len(bank),gpu_bytes=sum(record['gpu_bytes'] for record in records),records=records,
        native_sha256=hashlib.sha256(bank).hexdigest(),game_behaviour_accepted=False)
    return bank,report


def reviewed_sources(rom,path=DEFAULT):
    config=json.loads(Path(path).read_text())
    if config.get('version')!=1 or not isinstance(config.get('packs'),list):raise SourceError('Particle profile schema')
    vendor=(ROOT/'third_party/sm64ds-3ds/sm64ds-decomp').resolve()
    import ndspy.codeCompression
    arm9=ndspy.codeCompression.decompress(rom.arm9)
    result=[];seen=set()
    for row in config['packs']:
        identity=row['bank_id']
        if not isinstance(identity,int) or not 0<=identity<=65535 or identity in seen or not row.get('evidence'):
            raise SourceError('Particle profile identity/evidence')
        seen.add(identity)
        for proof in row['evidence']:
            source=(vendor/proof['source']).resolve()
            if not source.is_relative_to(vendor) or hashlib.sha256(source.read_bytes()).hexdigest()!=proof['sha256']:
                raise SourceError('Particle original source evidence changed')
        if row['kind']=='arm9':
            address=row['address'];at=address-rom.arm9RamAddress
            header=span(arm9,at,32);size=u32(header,24)+u32(header,20)
            if size>16*1024*1024:raise SourceError('Embedded particle resource bound')
            data=bytes(span(arm9,at,size))
        elif row['kind']=='file':
            file_id=row['file_id'];name=row['source'];handle=row['handle']
            if not 0<=file_id<len(rom.files) or rom.filenames.filenameOf(file_id)!=name or not 0<=handle<0x80a:
                raise SourceError('Particle source file identity')
            overlay=rom.loadArm9Overlays([0])[0];table=0x020bd4b8-overlay.ramAddress
            offset=u32(overlay.data,table+handle*4)-overlay.ramAddress
            if string(overlay.data,offset,0).decode('ascii')!=name:
                raise SourceError('Particle handle path differs from supported ROM')
            data=bytes(rom.files[file_id])
        else:raise SourceError('Particle profile source kind')
        if hashlib.sha256(data).hexdigest()!=row['source_sha256']:
            raise SourceError('Particle resource fingerprint differs')
        result.append((identity,data,row))
    return sorted(result,key=lambda value:value[0])


def catalog_header(reports):
    rows=['#pragma once','#include "image_catalog.hpp"','namespace sm64ds::render::prepared_images {'];entries=[]
    for report in reports:
        identity=report['bank_id'];name=f'bank_{identity}_images'
        rows.append('inline constexpr ImageAsset '+name+'[]={'+','.join(
            '{'+f'{r["width"]},{r["height"]},ImageBank::Alpha::'+('palette_zero_mask' if r['palette_zero_mask'] else 'independent')+
            (',TextureFormat::alpha4' if r.get('texture_format')=='alpha4' else '')+'}'
            for r in report['records'])+'};')
        sha=','.join(str(v) for v in bytes.fromhex(report['source_sha256']))
        entries.append('{'+f'{identity}u,'+'{'+sha+'},'+name+','+str(report['images'])+'u}')
    rows.append('inline constexpr ImageBankBinding banks[]={'+','.join(entries)+'};\n}\n')
    return '\n'.join(rows)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--profiles',type=Path,default=DEFAULT)
    args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to((ROOT/'build').resolve()) or output==(ROOT/'build').resolve():
        parser.error('Private native banks stay under an ignored build/ subdirectory')
    if output.exists():parser.error('Use a new output directory to preserve prior evidence')
    import ndspy.rom
    raw=args.rom.read_bytes();sha=hashlib.sha1(raw).hexdigest()
    if sha!='1367529f2cb23e76ef295cb1727333ae8f0a6cd7':parser.error('Expected supported EU revision0')
    try:
        rom=ndspy.rom.NintendoDSRom(raw);prepared=[];reports=[]
        for identity,data,profile in reviewed_sources(rom,args.profiles):
            bank,report=prepare(data,profile['source'])
            if report['definitions']!=profile['definitions'] or report['images']!=profile['images']:
                raise SourceError('Particle reviewed roster changed')
            report.update(bank_id=identity,proof=profile,rom_sha1=sha)
            prepared.append((identity,bank));reports.append(report)
    except (SourceError,ValueError,KeyError,struct.error) as error:parser.error(str(error))
    header=catalog_header(reports)
    report=dict(scope='Particle image preparation only; runtime/visual acceptance pending',rom_sha1=sha,
        profile_sha256=hashlib.sha256(args.profiles.read_bytes()).hexdigest(),banks=len(reports),
        images=sum(r['images'] for r in reports),gpu_bytes=sum(r['gpu_bytes'] for r in reports),
        header_sha256=hashlib.sha256(header.encode()).hexdigest(),records=reports)
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.particle-preparation-',dir=output.parent) as temporary:
        staging=Path(temporary)/'catalog';staging.mkdir()
        for identity,bank in prepared:(staging/f'{identity:05d}.nim').write_bytes(bank)
        (staging/'native_image_catalog.hpp').write_text(header)
        (staging/'catalog.json').write_text(json.dumps(report,indent=2)+'\n')
        os.rename(staging,output)
    print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2))


if __name__=='__main__':main()
