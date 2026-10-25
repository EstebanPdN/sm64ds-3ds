#!/usr/bin/env python3
"""Describe authentic loading artwork as checked private EU-ROM spans.

Only offsets, checksums, dimensions and font metadata go into the generated
header. Decoded Nintendo artwork remains in the requested private preview tree.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT=Path(__file__).resolve().parents[1]
OV_BASE=0x020AD660
LOGO_TILES=0x3D0B0
LOGO_PALETTE=0x2BF28
CASTLE_TILES=0x45320
CASTLE_PALETTE=0x2B678
CASTLE_MAP=0x2E784
PATTERN_COLORS=0x1F5FC

def check(value,message):
    if not value:raise ValueError(message)
def crc(b):return zlib.crc32(b)&0xffffffff
def sha(b):return hashlib.sha256(b).hexdigest()
def word(b,o):return struct.unpack_from('<I',b,o)[0]
def ds_color(c,opaque=True):return (0x8000 if opaque else 0)|((c&31)<<10)|(c&0x3e0)|((c>>10)&31)
def at4(data,width,x,y):
    i=((y//8)*(width//8)+x//8)*64+(y%8)*8+x%8
    return (data[i//2]>>(4*(i%2)))&15
def blend_color(a,b,y,total=255):
    return sum(((((a>>s)&31)*(total-y)+((b>>s)&31)*y)//total)<<s for s in [0,5,10])

def generate(args):
    import ndspy.codeCompression
    import ndspy.lz10
    from PIL import Image
    lock=json.loads((ROOT/'reference-native.lock.json').read_text())
    revision=subprocess.check_output(['git','-C',str(args.decomp),'rev-parse','HEAD'],text=True).strip()
    check(revision==lock['decomp_commit'],'decomp revision differs from the pinned source')
    rom=args.rom.read_bytes();check(hashlib.sha1(rom).hexdigest()==lock['ROM_SHA1'],'unsupported EU ROM revision')
    fat_o,fat_n=struct.unpack_from('<II',rom,0x48)
    fat=[struct.unpack_from('<II',rom,fat_o+i*8)for i in range(fat_n//8)]
    ovt_o,ovt_n=struct.unpack_from('<II',rom,0x50)
    records=[struct.unpack_from('<8I',rom,ovt_o+i*32)for i in range(ovt_n//32)]
    ov7=next(r for r in records if r[0]==7);check(ov7[1]==OV_BASE,'overlay 7 base changed')
    spans={}
    def add(name,offset,size):
        check(0<=offset<=len(rom) and 0<size<=len(rom)-offset,'invalid '+name+' span')
        b=rom[offset:offset+size];spans[name]=dict(offset=offset,size=size,crc32=crc(b),sha256=sha(b));return b
    first,end=fat[ov7[6]];packed=add('overlay7',first,end-first)
    overlay=ndspy.codeCompression.decompress(packed)
    check(len(overlay)==351456 and overlay==(args.decomp/'extracted/overlays/overlay_0007.bin').read_bytes(),
          'overlay 7 decoded bytes differ from pinned extraction')
    first,end=fat[222];tex_packed=add('texbank',first,end-first)
    check(tex_packed.startswith(b'LZ77'),'texbank compression changed')
    tex=ndspy.lz10.decompress(tex_packed[4:]);check(len(tex)==210884,'texbank decoded size changed')
    def member(name,index,narc_id=106):
        narc_first,narc_end=fat[narc_id];narc=rom[narc_first:narc_end];cursor=struct.unpack_from('<H',narc,12)[0];blocks={}
        for _ in range(struct.unpack_from('<H',narc,14)[0]):
            tag=narc[cursor:cursor+4];length=word(narc,cursor+4);blocks[tag]=(cursor+8,length-8);cursor+=length
        check(cursor==len(narc) and set(blocks)=={b'BTAF',b'BTNF',b'GMIF'},'NARC block layout changed')
        begin,finish=struct.unpack_from('<II',narc,blocks[b'BTAF'][0]+4+index*8)
        check(begin<=finish<=blocks[b'GMIF'][1],'invalid NARC member span')
        return add(name,narc_first+blocks[b'GMIF'][0]+begin,finish-begin)
    font16_packed=member('font16',13);font16=ndspy.lz10.decompress(font16_packed)
    font8=member('font8',14);palette_bytes=member('font_palette',7)
    check(len(font16)==32768 and len(font8)==16384 and len(palette_bytes)==512,'font size changed')
    font_palette=struct.unpack('<256H',palette_bytes)
    frame_packed=member('frame_tiles',13,107);frame_tiles=ndspy.lz10.decompress(frame_packed)
    check(len(frame_tiles)==16384,'original menu button graphic size changed')
    first,end=fat[137];small_palette=add('small_palette',first,end-first)
    check(len(small_palette)==32,'small font palette changed')
    arm9_o,_,arm9_base,arm9_n=struct.unpack_from('<IIII',rom,0x20)
    arm9=ndspy.codeCompression.decompress(rom[arm9_o:arm9_o+arm9_n])
    check(arm9==(args.decomp/'extracted/arm9_dec.bin').read_bytes(),'ARM9 differs from pinned extraction')
    widths8=arm9[0x0208F074-arm9_base:0x0208F174-arm9_base]
    widths16=arm9[0x0208F174-arm9_base:0x0208F274-arm9_base]
    module_path=args.decomp/'tools/message_bank.py'
    spec=importlib.util.spec_from_file_location('rom_art_original_charset',module_path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    charset={ord(char):code for code,char in module.BASIC_CHARS.items()if len(char)==1 and 32<=ord(char)<127}
    check(len(charset)==74,'canonical ASCII font repertoire changed')
    missing_latin1={}
    def glyph_has_ink(data,code,cw,columns):
        sx=(code%columns)*cw;sy=(code//columns)*16
        return any(at4(data,256,sx+x,sy+y)for y in range(16)for x in range(cw))
    # Reuse the original encoder's canonical CP1252 mapping, retaining only
    # printable Unicode Latin1 cells that really contain ink in both atlases.
    # Do not invent symbols for the game's intentionally empty glyph slots.
    for char,code in module.CHAR_ENCODER.items():
        if len(char)!=1 or not 0xa0<=ord(char)<256 or not char.isprintable():continue
        check(code==ord(char)-0x30,'canonical Latin1 mapping changed')
        if glyph_has_ink(font16,code,16,16)and glyph_has_ink(font8,code,8,32):charset[ord(char)]=code
        else:missing_latin1[char]=code
    codes=[charset[a]for a in sorted(charset)]
    check(len(codes)==166,'canonical printable Latin1 repertoire changed')
    for char in 'ÉÈÊÀÇÑÃÕÜßéèêàçñãõü':check(ord(char)in charset,'required loading-language accent is absent: '+char)
    font_rows=(len(codes)+15)//16;font_height=font_rows*16
    glyphs=[0xffff]*256;advance8=[0]*256;advance16=[0]*256
    for cell,a in enumerate(sorted(charset)):
        glyphs[a]=cell;advance8[a]=widths8[charset[a]];advance16[a]=widths16[charset[a]]
    images={}
    def image(name,w,h,pixels):
        check(len(pixels)==w*h,'incorrect image dimensions '+name)
        images[name]=dict(width=w,height=h,pixels=pixels)
    # Reconstruct the original castle text BG through its original SCRN map.
    castle=[]
    pal=struct.unpack_from('<256H',overlay,CASTLE_PALETTE)
    for y in range(192):
        for x in range(256):
            entry=struct.unpack_from('<H',overlay,CASTLE_MAP+((y//8)*32+x//8)*2)[0]
            u=(7-x%8)if entry&0x400 else x%8;v=(7-y%8)if entry&0x800 else y%8
            index=overlay[CASTLE_TILES+(entry&0x3ff)*64+v*8+u]
            castle.append(ds_color(pal[index]))
    image('castle',256,192,castle);image('sky',256,64,castle[:256*64])
    pal=struct.unpack_from('<256H',overlay,LOGO_PALETTE);logo=[]
    for y in range(128):
        for x in range(240):
            index=overlay[LOGO_TILES+((y//8)*32+x//8)*64+y%8*8+x%8]
            logo.append(ds_color(pal[index])if index else 0)
    # The CHAR sheet is 256 wide, but its last two tile columns belong to the
    # neighbouring BG resource. All logo ink and the registered mark fit in
    # x=0..239. Keep that original branding region, never the adjacent sky.
    image('logo',240,128,logo)
    def texture(index,palette):
        to=word(tex,0x18)+index*20;po=word(tex,0x20)+palette*16
        _,data,size,_,flags=struct.unpack_from('<5I',tex,to);_,pd,pn,_=struct.unpack_from('<4I',tex,po)
        check(data+size<=len(tex) and pd+pn<=len(tex),'BMD texture span outside file')
        return tex[data:data+size],struct.unpack_from('<'+str(pn//2)+'H',tex,pd),flags
    raw,pal,flags=texture(9,9);check(((flags>>26)&7)==6 and len(raw)==16384,'pattern format differs')
    check(pal[0]==0x7fff and all((p&7)==0 for p in raw),'pattern palette/index convention changed')
    top,bottom,ink_top,ink_bottom=struct.unpack_from('<4H',overlay,PATTERN_COLORS)
    pattern=[]
    for y in range(256):
        bg=blend_color(top,bottom,y);ink=blend_color(ink_top,ink_bottom,y)
        for x in range(64):
            value=raw[y*64+x];alpha=value>>3
            # A5I3 texture alpha is preserved by compositing against the menu's
            # original two gradient colors, rather than thresholding it to A1.
            color=blend_color(bg,ink,alpha,31)
            pattern.append(ds_color(color))
    image('pattern',64,256,pattern)
    frame=[]
    # Native file/options button nine-slice, normal orange palette bank 1.
    # These tile IDs are the top/middle/bottom rows of handle 0x230's SCRN.
    frame_ids=[1,2,3,0x21,0x22,0x23,0x41,0x42,0x43]
    first,end=fat[130];frame_map=ndspy.lz10.decompress(rom[first:end])
    check(len(frame_map)==2048,'original menu button map size changed')
    check(set(frame_ids).issubset({v&0x3ff for v in struct.unpack('<1024H',frame_map)}),'frame tiles no longer occur in original map')
    for y in range(24):
        for x in range(24):
            tile=frame_ids[(y//8)*3+x//8];at=tile*64+(y%8)*8+x%8
            index=(frame_tiles[at//2]>>(4*(at%2)))&15
            frame.append(ds_color(font_palette[16+index])if index else 0)
    image('frame',24,24,frame)
    raw,pal,flags=texture(2,2);check(((flags>>26)&7)==4 and len(raw)==65536,'file menu format differs')
    star=[]
    for y in range(236,252):
        for x in range(4,20):
            index=raw[y*256+x];star.append(ds_color(pal[index])if index else 0)
    image('star',16,16,star)
    for name,data,cw,source_columns,palette in [('title_font',font16,16,16,font_palette[16:32]),
             ('body_font',font8,8,32,struct.unpack('<16H',small_palette))]:
        width,height=16*cw,font_height;pixels=[0]*(width*height)
        for cell,code in enumerate(codes):
            sx=(code%source_columns)*cw;sy=(code//source_columns)*16
            tx=(cell%16)*cw;ty=(cell//16)*16
            for y in range(16):
                for x in range(cw):
                    index=at4(data,256,sx+x,sy+y)
                    pixels[(ty+y)*width+tx+x]=ds_color(palette[index])if index else 0
        image(name,width,height,pixels)
    args.preview_dir.mkdir(parents=True,exist_ok=True)
    for name,spec in images.items():
        raw=struct.pack('<'+str(len(spec['pixels']))+'H',*spec['pixels'])
        (args.preview_dir/(name+'.a1r5g5b5')).write_bytes(raw)
        png=Image.new('RGBA',(spec['width'],spec['height']))
        png.putdata([(((p>>10)&31)*255//31,((p>>5)&31)*255//31,(p&31)*255//31,255 if p&0x8000 else 0)for p in spec['pixels']])
        png.save(args.preview_dir/(name+'.png'))
        spec.update(bytes=len(raw),crc32=crc(raw),sha256=sha(raw));del spec['pixels']
    # The approved boot UI is available even with no readable ROM. Store only
    # the selected menu graphics, never gameplay data or the ROM itself.
    embedded=[]
    for name in ['castle','logo','pattern','frame','star','title_font','body_font']:
        raw=(args.preview_dir/(name+'.a1r5g5b5')).read_bytes()
        embedded.extend(struct.unpack('<'+str(len(raw)//2)+'H',raw))
    payload=['// Original Nintendo menu artwork; generated from the pinned reference.',
             '#pragma once','#include <cstdint>','namespace rom_art_embedded {',
             'alignas(4) inline constexpr uint16_t pixels[]={']
    payload.extend(','.join('0x%04x'%v for v in embedded[i:i+16])+',' for i in range(0,len(embedded),16))
    payload.extend(['};','} // namespace rom_art_embedded',''])
    args.output.with_name('rom_art_embedded.hpp').write_text('\n'.join(payload))
    schema=sha(json.dumps(dict(spans=spans,images=images,codes=codes,glyphs=glyphs),sort_keys=True).encode())[:16]
    lines=['// Private generated metadata only. No Nintendo artwork payload is embedded.',
        '#pragma once','#include <cstdint>','namespace rom_art_manifest {',
        'struct Span { uint32_t offset,size,crc32; };',f'inline constexpr uint32_t rom_size={len(rom)}u;',
        f'inline constexpr char schema_id[]="{schema}";',f'inline constexpr uint32_t overlay_decoded={len(overlay)}u;',
        f'inline constexpr uint32_t texbank_decoded={len(tex)}u;',f'inline constexpr uint32_t glyph_count={len(codes)}u;',
        f'inline constexpr uint32_t font_rows={font_rows}u;',f'inline constexpr uint32_t font_height={font_height}u;']
    for name,s in spans.items():lines.append(f'inline constexpr Span {name}={{{s["offset"]}u,{s["size"]}u,0x{s["crc32"]:08x}u}};')
    for name,value in [('logo_tiles',LOGO_TILES),('logo_palette',LOGO_PALETTE),('castle_tiles',CASTLE_TILES),
                      ('castle_palette',CASTLE_PALETTE),('castle_map',CASTLE_MAP),('pattern_colors',PATTERN_COLORS)]:
        lines.append(f'inline constexpr uint32_t {name}=0x{value:x}u;')
    for name,values,ctype in [('codes',codes,'uint8_t'),('glyphs',glyphs,'uint16_t'),
                            ('frame_ids',frame_ids,'uint8_t'),
                            ('advances8',advance8,'uint8_t'),('advances16',advance16,'uint8_t')]:
        lines.append(f'inline constexpr {ctype} {name}[]={{'+','.join(str(v)for v in values)+'};')
    lines.append('struct ImageInfo { const char* name; uint32_t width,height,crc32; };')
    lines.append('inline constexpr ImageInfo images[]={')
    for name,s in images.items():lines.append(f'    {{"{name}",{s["width"]}u,{s["height"]}u,0x{s["crc32"]:08x}u}},')
    lines.extend(['};','} // namespace rom_art_manifest',''])
    header='\n'.join(lines).encode();args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(header)
    report=dict(status='passed_private_ROM_artwork_generated',schema_id=schema,ROM_sha1=lock['ROM_SHA1'],ROM_sha256=sha(rom),
        decomp_commit=revision,spans=spans,images=images,glyph_count=len(codes),unsupported_ASCII=[chr(i)for i in range(32,127)if glyphs[i]==0xffff],
        printable_latin1_without_atlas_ink=missing_latin1,font_rows=font_rows,font_height=font_height,
        glyph_mapping={f'U+{a:04X}':dict(character=chr(a),code=charset[a],cell=glyphs[a],advance8=advance8[a],advance16=advance16[a])for a in sorted(charset)},
        retained_bytes=sum(s['bytes']for n,s in images.items()if n!='sky'),sky_alias='first 64 rows of castle; no extra retained allocation',
        pattern='Original A5I3 wallpaper_logo_U texture composited against original ov007 gradient colors at offset1f5fc',
        logo='Original CHAR branding region x0..239,y0..127; adjacent sky tile columns 240..255 excluded; registered mark retained',
        frame='Original file/options menu button 0x230, nine tiles from A00D with palette 9807 bank1; shadow omitted for nine-slice',
        preview_dir=str(args.preview_dir.resolve()),header_sha256=sha(header),
        sources={str(Path(__file__).resolve()):sha(Path(__file__).read_bytes()),str(module_path.resolve()):sha(module_path.read_bytes())})
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['rom','decomp','output','report','preview-dir']:p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    try:r=generate(a)
    except (ValueError,OSError,StopIteration,struct.error)as e:p.exit(1,f'ROM artwork rejected: {e}\n')
    print(json.dumps({k:r[k]for k in ['status','schema_id','glyph_count','retained_bytes','header_sha256']},indent=2))
if __name__=='__main__':main()
