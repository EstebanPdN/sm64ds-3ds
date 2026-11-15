#!/usr/bin/env python3
"""Prepare a native model catalog and immutable banks from the local EU ROM.

The generated C++ associations retain source identities for live pose/material
producers. They contain no source graphics instructions or extracted pixels.
Private native banks and this generated header stay inside ignored build/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

try:
    from .cook_model import ROOT, cook, NeedsNativeFeature, SourceError
    from .animation_profiles import load as load_profiles,DEFAULT
    from .sprite_profiles import load as load_sprites,DEFAULT as SPRITES
except ImportError:
    from cook_model import ROOT, cook, NeedsNativeFeature, SourceError
    from animation_profiles import load as load_profiles,DEFAULT
    from sprite_profiles import load as load_sprites,DEFAULT as SPRITES


def fl(value):
    text=repr(float(value))
    return text+'f'


def definition(file_id,report):
    if not 0<=file_id<=65535:raise ValueError('Source file identity outside catalog')
    images=report['prepared_image_pairs'];materials=report['native_material_sources'];parts=report['native_part_sources']
    if (len(images)!=report['images'] or len(materials)!=report['materials'] or len(parts)!=report['parts'] or
        not 1<=len(materials)<=512 or not 1<=len(parts)<=1024):raise ValueError('Native association counts')
    prefix=f'model_{file_id}'
    rows=[]
    if images:
        rows.append('inline constexpr ImageBinding '+prefix+'_images[]={'+','.join(
            '{'+f'{i["texture"]}u,{i["palette"]}u,{n}u,{i["width"]}u,{i["height"]}u,ImageUse::'+(
                'sprite' if i.get('sprite_only') else 'model')+'}' for n,i in enumerate(images))+'};')
    roles={'literal':'literal','diffuse':'diffuse','emission':'emission','lighting':'lighting'}
    rows.append('inline constexpr MaterialBinding '+prefix+'_materials[]={'+','.join(
        '{'+f'{m["source_material"]},{m["width"]},{m["height"]},ColourSource::{roles[m["colour_source"]]},{str(m["uv_transform"]).lower()},{str(m.get("normal_uv",False)).lower()},{str(m.get("diffuse_override",False)).lower()},{m.get("diffuse_ambient",0)}u'+'}'
        for m in materials)+'};')
    rows.append('inline constexpr PartBinding '+prefix+'_parts[]={'+','.join(
        '{'+f'{p["bone"]},{p["material"]},{str(p["billboard"]).lower()},{p.get("normal_bone",65535)},{p.get("lighting_bone",65535)}'+'}' for p in parts)+'};')
    skin=report.get('skin') or []
    if skin:
        verts=[];groups=[]
        for g in skin:
            groups.append('{'+f'{g["part"]}u,{len(verts)}u,{len(g["vertices"])}u'+'}');verts.extend(g['vertices'])
        rows.append('inline constexpr SkinVertex '+prefix+'_skin_vertices[]={'+','.join(
            '{'+','.join(fl(x) for x in v[:5])+f',{v[5]}u,{v[6]}u,{v[7]}u,{v[8]}u,{v[9]},{v[10]},{v[11]},{v[12]}'+'}' for v in verts)+'};')
        rows.append('inline constexpr SkinGroup '+prefix+'_skin_groups[]={'+','.join(groups)+'};')
    sha=','.join(str(v) for v in bytes.fromhex(report['source_sha256']))
    entry='{'+f'{file_id}u,'+'{'+sha+'},'+f'{report["source_bone_count"]}u,{report["source_material_count"]}u,'+(
        prefix+'_images' if images else 'nullptr')+f',{len(images)}u,{prefix}_materials,{len(materials)}u,{prefix}_parts,{len(parts)}u'+(
        f',{prefix}_skin_groups,{len(skin)}u,{prefix}_skin_vertices' if skin else '')+'}'
    return rows,entry


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--animation-profiles',type=Path,default=DEFAULT)
    parser.add_argument('--sprite-profiles',type=Path,default=SPRITES)
    parser.add_argument('--archives',type=Path,help='archives.tsv (id, hex, archive, path); adds NARC member BMDs with ids >= 0x8000')
    parser.add_argument('--archive-root',type=Path,help='Directory holding the extracted archive paths from archives.tsv')
    args=parser.parse_args()
    if not args.output.resolve().is_relative_to((ROOT/'build').resolve()):parser.error('Private catalog stays under ignored build/')
    if args.output.exists():parser.error('Use a new output directory to retain earlier evidence')
    import ndspy.rom,ndspy.lz10
    source=args.rom.read_bytes();rom_sha=hashlib.sha1(source).hexdigest()
    if rom_sha!='1367529f2cb23e76ef295cb1727333ae8f0a6cd7':parser.error('Expected supported EU revision 0')
    rom=ndspy.rom.NintendoDSRom(source);rows=[];entries=[];records=[];refused=[];source_models=[]
    try:
        profiles=load_profiles(rom,args.animation_profiles)
        sprites=load_sprites(rom,args.sprite_profiles)
    except (SourceError,ValueError,OSError,KeyError,struct.error) as error:parser.error(str(error))
    args.output.mkdir(parents=True)
    sources=[(i,rom.filenames.filenameOf(i),packed) for i,packed in enumerate(rom.files)]
    if args.archives:
        root=args.archive_root or args.archives.parent
        for line in args.archives.read_text().splitlines()[1:]:
            fields=line.split('\t')
            if len(fields)>=4 and fields[3].lower().endswith('.bmd'):
                member=(root/fields[3]) if (root/fields[3]).exists() else (root.parent/'extracted/dsd'/fields[3])
                sources.append((int(fields[0]),fields[3].split('/',2)[-1] if fields[3].count('/')>=2 else fields[3],member.read_bytes()))
    for file_id,name,packed in sources:
        if not name or not name.lower().endswith('.bmd'):continue
        source_models.append(file_id)
        try:
            if packed.startswith(b'LZ77'):
                if len(packed)<8 or packed[4]!=0x10 or not 0<int.from_bytes(packed[5:8],'little')<=16*1024*1024:
                    raise SourceError('Compressed model bound')
                data=ndspy.lz10.decompress(packed[4:])
            else:data=packed
            profile=profiles.get(name,{})
            sprite=sprites.get(name,{})
            bank,report,_=cook(data,name,profile.get('sequences',()),sprite.get('images',()))
            if profile:report['animation_association']=profile['proof']
            if sprite:report['sprite_association']=sprite['proof']
            lines,entry=definition(file_id,report)
            path=args.output/f'{file_id:05d}.nmd';path.write_bytes(bank)
            report.update(file_id=file_id,rom_sha1=rom_sha,native_sha256=hashlib.sha256(bank).hexdigest())
            path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
            rows.extend(lines);entries.append(entry);records.append(dict(file_id=file_id,path=path.name,sha256=report['native_sha256']))
        except (NeedsNativeFeature,SourceError,ValueError,struct.error) as error:
            refused.append(dict(file_id=file_id,source=name,reason=str(error)))
    header='#pragma once\n#include "model_instance.hpp"\nnamespace sm64ds::render::prepared {\n'+ '\n'.join(rows)+ '\ninline constexpr ModelBindings models[]={\n'+',\n'.join(entries)+'\n};\ninline constexpr std::uint16_t source_model_files[]={'+','.join(str(i) for i in source_models)+'};\n}\n'
    (args.output/'native_catalog.hpp').write_text(header)
    report=dict(scope='Prepared resource/catalog associations only; live game integration pending',rom_sha1=rom_sha,
        models=len(records),header_sha256=hashlib.sha256(header.encode()).hexdigest(),records=records,refused=refused,
        explicit_animation_models=len(profiles),animation_profiles_sha256=hashlib.sha256(args.animation_profiles.read_bytes()).hexdigest(),
        explicit_sprite_models=len(sprites),sprite_profiles_sha256=hashlib.sha256(args.sprite_profiles.read_bytes()).hexdigest(),
        game_behaviour_ready=False)
    (args.output/'catalog.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('records','refused')},indent=2))


if __name__=='__main__':main()
