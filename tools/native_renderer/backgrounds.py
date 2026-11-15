"""Offline sparse native background authoring; no runtime tile interpretation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib
try:
    from .audit_models import ROOT,SourceError,span,u32
    from .nitro2d import tilemap
    from .source_members import menu_member
    from .bank import Mesh,Image,Material,Part,build,tiled_rgba8
except ImportError:
    from audit_models import ROOT,SourceError,span,u32
    from nitro2d import tilemap
    from source_members import menu_member
    from bank import Mesh,Image,Material,Part,build,tiled_rgba8


def tile_rgba(characters,colours,entry,eight_bit=False):
    if len(colours)!=512 or len(characters)%(64 if eight_bit else 32):
        raise SourceError('Background character/palette extent')
    tile=entry&1023;stride=64 if eight_bit else 32
    raw=span(characters,tile*stride,stride)
    output=bytearray(256);palette=0 if eight_bit else (entry>>12)*16
    for y in range(8):
        for x in range(8):
            sx=7-x if entry&1024 else x;sy=7-y if entry&2048 else y
            value=raw[sy*(8 if eight_bit else 4)+(sx if eight_bit else sx//2)]
            index=value if eight_bit else (value>>((sx&1)*4))&15
            if not index:continue
            colour=struct.unpack_from('<H',colours,(palette+index)*2)[0]
            rgb=[(colour>>shift)&31 for shift in (0,5,10)]
            output[(y*8+x)*4:(y*8+x+1)*4]=bytes([(v<<3)|(v>>3) for v in rgb]+[255])
    return bytes(output)


def prepare(screen,characters,colours,source_sha256,eight_bit=False):
    if len(source_sha256)!=32 or not 1<=screen.width<=64 or not 1<=screen.height<=64 or len(screen.entries)!=screen.width*screen.height:
        raise SourceError('Native background mesh bound')
    tiles=[];unique={};placements=[]
    for at,word in enumerate(screen.entries):
        pixels=tile_rgba(characters,colours,word,eight_bit)
        if not any(pixels[3::4]):continue
        if pixels not in unique:unique[pixels]=len(tiles);tiles.append(pixels)
        placements.append((at,unique[pixels]))
    if not placements:
        raise SourceError('Empty background has no native drawable; represent explicit empty coverage')
    columns=1
    while columns*columns<len(tiles):columns*=2
    rows=1
    while columns*rows<len(tiles):rows*=2
    if columns>128 or rows>128:raise SourceError('Background atlas exceeds native texture bound')
    width,height=columns*8,rows*8;atlas=bytearray(width*height*4)
    for i,pixels in enumerate(tiles):
        x=(i%columns)*8;y=(i//columns)*8
        for row in range(8):atlas[((y+row)*width+x)*4:((y+row)*width+x+8)*4]=pixels[row*32:(row+1)*32]
    vertices=bytearray();indices=bytearray();preview=bytearray(screen.width*screen.height*64*4)
    for n,(at,index) in enumerate(placements):
        x,y=at%screen.width,at//screen.width;tx,ty=index%columns,index//columns
        for dx,dy in [(0,0),(1,0),(1,1),(0,1)]:
            vertices.extend(struct.pack('<5f4B',(x+dx)/screen.width,(y+dy)/screen.height,0,
                ((tx+dx)*8+1/64)/width,((ty+dy)*8+1/64)/height,255,255,255,255))
        indices.extend(struct.pack('<6H',*(n*4+i for i in [0,1,2,0,2,3])))
        pixels=tiles[index]
        for row in range(8):
            at=((y*8+row)*screen.width*8+x*8)*4;preview[at:at+32]=pixels[row*32:(row+1)*32]
    native=build([Mesh(bytes(vertices),bytes(indices))],[Image(width,height,tiled_rgba8(atlas,width,height))],
        [Material(texture=0,blend=1,cull=0,depth_test=False,depth_write=False)],[Part(0,0,0,0,len(indices)//2)],1,source_sha256)
    geometry_allocation=((len(vertices)+127)&~127)+((len(indices)+127)&~127)
    report=dict(width=screen.width*8,height=screen.height*8,source_tiles=len(screen.entries),visible_tiles=len(placements),
        unique_tiles=len(tiles),atlas_width=width,atlas_height=height,atlas_bytes=len(atlas),geometry_bytes=len(vertices)+len(indices),
        geometry_allocation_bytes=geometry_allocation,native_gpu_payload=len(atlas)+geometry_allocation,full_bitmap_payload=len(preview),native_sha256=hashlib.sha256(native).hexdigest(),
        rgba_sha256=hashlib.sha256(preview).hexdigest(),colour_rule='reference2D: (c<<3)|(c>>3), offline only',nearest_bias_texels=1/64)
    return native,report,bytes(preview),bytes(atlas)


def palette_variants(screen):
    """The six reviewed source writes, preserving tile identities/flips."""
    screens=[screen]
    for mode in range(4):
        words=list(screen.entries)
        for row,offset in enumerate([0x106,0x146,0x286,0x2c6,0x386,0x3c6]):
            for at in range(offset//2,offset//2+32):words[at]=(words[at]&0xfff)|((4 if row//2==mode else 3)<<12)
        screens.append(type(screen)(screen.width,screen.height,screen.fields,tuple(words)))
    return tuple(screens)


def prepare_variants(screens,characters,colours,source_sha256,eight_bit=False,palettes=None):
    """One deduplicated RGBA atlas, shared meshes for equivalent variants."""
    if not screens or len(screens)>16 or len(source_sha256)!=32:raise SourceError('Native background variants bound')
    first=screens[0];tiles=[];unique={};all_placements=[];bounds=[]
    if not 1<=first.width<=64 or not 1<=first.height<=64:raise SourceError('Native background shape')
    if palettes is None:palettes=(colours,)*len(screens)
    if len(palettes)!=len(screens) or any(len(p)!=512 for p in palettes):raise SourceError('Native variant palette roster')
    for screen,palette in zip(screens,palettes):
        if (screen.width,screen.height)!=(first.width,first.height) or len(screen.entries)!=screen.width*screen.height:raise SourceError('Native variant shape changed')
        placements=[]
        for at,word in enumerate(screen.entries):
            pixels=tile_rgba(characters,palette,word,eight_bit)
            if not any(pixels[3::4]):continue
            if pixels not in unique:unique[pixels]=len(tiles);tiles.append(pixels)
            placements.append((at,unique[pixels]))
        if not placements:raise SourceError('Empty variant requires explicit empty coverage')
        all_placements.append(placements)
        xs=[at%screen.width for at,_ in placements];ys=[at//screen.width for at,_ in placements]
        bounds.append([min(xs)*8,min(ys)*8,(max(xs)+1)*8,(max(ys)+1)*8])
    columns=1
    while columns*columns<len(tiles):columns*=2
    rows=1
    while columns*rows<len(tiles):rows*=2
    if columns>128 or rows>128:raise SourceError('Native variant atlas too large')
    width,height=columns*8,rows*8;atlas=bytearray(width*height*4)
    for i,pixels in enumerate(tiles):
        x,y=(i%columns)*8,(i//columns)*8
        for row in range(8):atlas[((y+row)*width+x)*4:((y+row)*width+x+8)*4]=pixels[row*32:(row+1)*32]
    meshes=[];mesh_ids={};parts=[];previews=[]
    for placements in all_placements:
        vertices=bytearray();indices=bytearray();preview=bytearray(first.width*first.height*64*4)
        for n,(at,index) in enumerate(placements):
            x,y=at%first.width,at//first.width;tx,ty=index%columns,index//columns
            for dx,dy in [(0,0),(1,0),(1,1),(0,1)]:
                vertices.extend(struct.pack('<5f4B',(x+dx)/first.width,(y+dy)/first.height,0,((tx+dx)*8+1/64)/width,((ty+dy)*8+1/64)/height,255,255,255,255))
            indices.extend(struct.pack('<6H',*(n*4+i for i in [0,1,2,0,2,3])))
            for row in range(8):
                to=((y*8+row)*first.width*8+x*8)*4;preview[to:to+32]=tiles[index][row*32:(row+1)*32]
        key=(bytes(vertices),bytes(indices))
        if key not in mesh_ids:mesh_ids[key]=len(meshes);meshes.append(Mesh(*key))
        parts.append(Part(mesh_ids[key],0,0,0,len(indices)//2));previews.append(bytes(preview))
    native=build(meshes,[Image(width,height,tiled_rgba8(atlas,width,height))],[Material(texture=0,blend=1,cull=0,depth_test=False,depth_write=False)],parts,1,source_sha256)
    geometry=sum(((len(m.vertices)+127)&~127)+((len(m.indices)+127)&~127) for m in meshes)
    return native,dict(width=first.width*8,height=first.height*8,variants=len(screens),meshes=len(meshes),parts=len(parts),visible_tiles=[len(p) for p in all_placements],unique_tiles=len(tiles),atlas_width=width,atlas_height=height,atlas_bytes=len(atlas),geometry_allocation_bytes=geometry,native_gpu_payload=len(atlas)+geometry,full_bitmap_payload=len(previews[0])*len(screens),bounds=bounds,nearest_bias_texels=1/64,native_sha256=hashlib.sha256(native).hexdigest()),tuple(previews),bytes(atlas)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--rom',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--preview',action='store_true');parser.add_argument('--variants',action='store_true',help='Prepare the reviewed original palette-state variants and live producer binding');parser.add_argument('--profile',type=Path,default=Path(__file__).with_name('background_profiles.json'));args=parser.parse_args()
    config=json.loads(args.profile.read_text())
    if config.get('version')!=1 or config.get('bank_id')!=5001 or config.get('cell_resource')!=1 or not config.get('evidence'):
        parser.error('Expected reviewed native background profile')
    raw=args.rom.read_bytes()
    if hashlib.sha1(raw).hexdigest()!=config['rom_sha1']:parser.error('Expected reviewed supported ROM')
    vendor=(ROOT/'third_party/sm64ds-3ds/sm64ds-decomp').resolve()
    for proof in config['evidence']:
        source=(vendor/proof['source']).resolve()
        if not source.is_relative_to(vendor) or hashlib.sha256(source.read_bytes()).hexdigest()!=proof['sha256']:
            parser.error('Reviewed background source contract changed')
    out=args.output.resolve()
    if not out.is_relative_to((ROOT/'build').resolve()) or out.exists():parser.error('Use a new private output directory under build/')
    import ndspy.rom
    rom=ndspy.rom.NintendoDSRom(raw);overlay=rom.loadArm9Overlays([7])[7]
    # Explicit reviewed MAIN BG3/English layout1. Source callbacks still need
    # to preserve dynamic map cropping/palette changes before game admission.
    char=menu_member(rom,config['characters']['index'],decompress=True);pal=menu_member(rom,config['palette']['index'])
    for member,row in [(char,config['characters']),(pal,config['palette'])]:
        if (member.handle,member.path,member.source_sha256)!=(row['handle'],row['path'],row['source_sha256']):
            parser.error('Reviewed background asset association changed')
    colours=bytearray(pal.data);colours[0x1f6:0x200]=span(overlay.data,0x020d7a44-overlay.ramAddress,10)
    pointer=u32(overlay.data,0x020d7ec8-overlay.ramAddress+4);at=pointer-overlay.ramAddress;size=u32(overlay.data,at+8);source=bytes(span(overlay.data,at,size));screen=tilemap(source)
    if len(char.data)!=16384 or len(colours)!=512 or (screen.width,screen.height)!=(32,32):parser.error('Reviewed background layout changed')
    screens=palette_variants(screen) if args.variants else (screen,)
    map_bytes=[struct.pack('<'+str(len(s.entries))+'H',*s.entries) for s in screens]
    identity=hashlib.sha256((b'SM64_NATIVE_BG_VARIANTS_1\0'+b''.join(map_bytes) if args.variants else b'SM64_NATIVE_BG_1\0sparse-rgba8/reference2d/bias64\0'+source)+char.data+colours).digest()
    if args.variants:
        if bytes(span(overlay.data,0x020ccc44-overlay.ramAddress,2))!=b'\x03\x04':parser.error('Reviewed palette choices changed')
        if tile_rgba(char.data,colours,0)!=bytes(256):parser.error('Reviewed transparent fill tile changed')
        native,report,previews,atlas=prepare_variants(screens,char.data,colours,identity);preview=previews[0]
    else:native,report,preview,atlas=prepare(screen,char.data,colours,identity);previews=(preview,)
    report.update(scope='Original MAIN BG3 resource1, with reviewed palette variants and producer binding; complete game coverage pending' if args.variants else 'Original MAIN BG3 resource1 preparation only; live cropping,palette variants and complete game coverage pending',bank_id=5001,profile_sha256=hashlib.sha256(args.profile.read_bytes()).hexdigest(),
        source_sha256=identity.hex(),input_sha256=[hashlib.sha256(v).hexdigest() for v in [source,char.data,bytes(colours)]],
        character_member=dict(handle=char.handle,path=char.path,source_sha256=char.source_sha256),palette_member=dict(handle=pal.handle,path=pal.path,source_sha256=pal.source_sha256),
        original_scene_accepted=False,physical_hardware=False)
    header='#pragma once\n#include "canvas_catalog.hpp"\nnamespace sm64ds::render::prepared_canvases {\ninline constexpr CanvasBinding banks[]={{5001u,{'+','.join(str(v) for v in identity)+'},'+str(len(screens))+',1}};\n}\n'
    binding=''
    if args.variants:
        report['source_map_crc32']=[zlib.crc32(raw) for raw in map_bytes]
        rows=[str(zlib.crc32(raw))+','+str(i)+',{'+','.join(map(str,report['bounds'][i]))+'}' for i,raw in enumerate(map_bytes)]
        binding='#pragma once\n#include "menu_backgrounds.hpp"\nnamespace sm64ds::render::game_hooks::prepared_backgrounds {\ninline constexpr MenuBackgroundVariant variants[]={'+','.join('{'+r+'}' for r in rows)+'};\ninline constexpr MenuBackgroundBinding bank={1,5001,32,32,38,5,16384,'+str(zlib.crc32(char.data))+',512,'+str(zlib.crc32(pal.data))+',variants,{'+','.join(map(str,identity))+'}};\n}\n'
    out.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.background-',dir=out.parent) as temp:
        stage=Path(temp)/'catalog';stage.mkdir();(stage/'05001.nmd').write_bytes(native);(stage/'native_canvas_catalog.hpp').write_text(header);(stage/'catalog.json').write_text(json.dumps(report,indent=2)+'\n')
        if binding:
            (stage/'native_background_catalog.hpp').write_text(binding)
            for i,raw in enumerate(map_bytes):(stage/f'map-{i}.u16').write_bytes(raw)
        if args.preview:
            from PIL import Image as Preview
            Preview.frombytes('RGBA',(report['width'],report['height']),preview).save(stage/'background.png');Preview.frombytes('RGBA',(report['atlas_width'],report['atlas_height']),atlas).save(stage/'atlas.png')
            if args.variants:
                for i,pixels in enumerate(previews):Preview.frombytes('RGBA',(report['width'],report['height']),pixels).save(stage/f'variant-{i}.png')
        os.rename(stage,out)
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
