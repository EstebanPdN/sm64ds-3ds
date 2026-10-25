#!/usr/bin/env python3
"""Audit DSMT upper-screen 2D title bytes and replay native scanout on host.

Reads preserved register/palette/VRAM dumps or SM64RAM1 native video views and
the supplied verified cartridge. RAM mode restores captured OAM as well; old
video-only mode disables unavailable objects. A labeled counterfactual enables the title
BGs with original map templates and zero fades to check their compositor path;
it does not claim the game produced those registers or reached that image.
No game/emulator launch, physical test, or cartridge-byte output is performed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'build/game-data/python'))
import test_native_menu_callbacks as base

PROBE=r'''
#include "sm64ds/native_2d.hpp"
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/game_draw.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
namespace { unsigned failures,side_effects; }
namespace sm64ds {
void native_platform_fail(const char*) {++failures;}
void native_platform_irq_set_master(bool) {++side_effects;}
void native_platform_irq_raise(std::uint32_t) {++side_effects;}
bool native_video_gx_status(std::uint32_t&) {++side_effects;return false;}
}
extern "C" {
void sm64ds_draw_port(std::uint32_t,std::uint32_t) {++side_effects;}
void sm64ds_draw_fifo(std::uint32_t) {++side_effects;}
bool sm64ds_draw_ok() {return false;}
}
int main() {
    std::vector<unsigned char> dump(0x2000+0x800+0xa4000);
    if(std::fread(dump.data(),1,dump.size(),stdin)!=dump.size())return 2;
    sm64ds::native_video_reset();
    std::memcpy(sm64ds::native_video_pointer(0x04000000,0x2000),dump.data(),0x2000);
    std::memcpy(sm64ds::native_video_pointer(0x05000000,0x800),dump.data()+0x2000,0x800);
    std::memcpy(sm64ds::native_video_pointer(0x06800000,0xa4000),dump.data()+0x2800,0xa4000);
    auto* oam=static_cast<unsigned short*>(sm64ds::native_video_pointer(0x07000000,0x800));
    const auto oam_bytes=std::fread(oam,1,0x800,stdin);
    if(oam_bytes==0)for(unsigned i=0;i<256;++i)oam[i*4]=0x0200;
    else if(oam_bytes!=0x800)return 4;
    auto frame=std::make_unique<sm64ds::Native2DFrame>();
    sm64ds::native_2d_scanout(*frame);
    if(failures || side_effects || !sm64ds::native_video_ok()) {
        std::fprintf(stderr,"video failure %08x failures=%u side_effects=%u\n",
            unsigned(sm64ds::native_video_bad_address()),failures,side_effects);return 3;
    }
    const unsigned header[]={frame->main_on_top,frame->has_3d};
    std::fwrite(header,4,2,stdout);
    for(const auto* layer:{&frame->underlay,&frame->overlay,&frame->bottom})
        std::fwrite(layer->pixels,4,256*256,stdout);
}
'''


def sha(data):return hashlib.sha256(data).hexdigest()


def color(value):
    r,g,b=value&31,(value>>5)&31,(value>>10)&31
    return 0xff000000|((r<<3|r>>2)<<16)|((g<<3|g>>2)<<8)|(b<<3|b>>2)


def replay(executable,dump):
    result=subprocess.run([str(executable)],input=dump,capture_output=True,check=False)
    if result.returncode:raise RuntimeError(result.stderr.decode())
    assert len(result.stdout)==8+3*256*256*4
    routing=struct.unpack_from('<2I',result.stdout)
    layers=[result.stdout[8+i*256*256*4:8+(i+1)*256*256*4] for i in range(3)]
    return routing,layers


def statistics(layer):
    visible=layer[:256*192*4]
    pixels=struct.unpack('<'+str(256*192)+'I',visible)
    return dict(sha256=sha(visible),distinct_colors=len(set(pixels)),
                black_pixels=sum((p&0xffffff)==0 for p in pixels),
                white_pixels=sum((p&0xffffff)==0xffffff for p in pixels),
                nonblack_pixels=sum((p&0xffffff)!=0 for p in pixels),
                transparent_pixels=sum((p>>24)==0 for p in pixels),
                opaque_pixels=sum((p>>24)==255 for p in pixels),
                opaque_black_pixels=sum(p==0xff000000 for p in pixels))


def object_inventory(oam):
    if not oam:return None
    sizes=(((8,8),(16,16),(32,32),(64,64)),
           ((16,8),(32,8),(32,16),(64,32)),
           ((8,16),(8,32),(16,32),(32,64)))
    output={}
    for engine,offset in (('main',0),('sub',0x400)):
        enabled=[];visible=[]
        for slot in range(128):
            a0,a1,a2,_=struct.unpack_from('<4H',oam,offset+slot*8)
            if not a0&0x100 and a0&0x200:continue
            shape=(a0>>14)&3
            if shape==3:continue
            width,height=sizes[shape][(a1>>14)&3]
            if a0&0x100 and a0&0x200:width*=2;height*=2
            x,y=a1&511,a0&255
            if x>=256:x-=512
            if y>=192 and y>=256-height:y-=256
            entry=dict(slot=slot,attributes=[hex(a0),hex(a1),hex(a2)],
                affine=bool(a0&0x100),mode=(a0>>10)&3,priority=(a2>>10)&3,
                x=x,y=y,width=width,height=height)
            enabled.append(entry)
            if x+width>0 and x<256 and y+height>0 and y<192:visible.append(entry)
        output[engine]=dict(enabled_count=len(enabled),visible_rectangle_count=len(visible),visible_rectangles=visible,
            attributes_sha256=sha(oam[offset:offset+0x400]))
    return output


def registers(dump):
    r=dump[:0x2000]
    def u16(at):return struct.unpack_from('<H',r,at)[0]
    def u32(at):return struct.unpack_from('<I',r,at)[0]
    return dict(power=hex(u16(0x304)),vram_banks=[hex(r[x]) for x in (0x240,0x241,0x242,0x243,0x244,0x245,0x246,0x248,0x249)],
        main=dict(display=hex(u32(0)),backgrounds=[hex(u16(8+2*i)) for i in range(4)],
                  blend_control=hex(u16(0x50)),blend_alpha=hex(u16(0x52)),blend_y=u16(0x54),brightness=hex(u16(0x6c))),
        sub=dict(display=hex(u32(0x1000)),backgrounds=[hex(u16(0x1008+2*i)) for i in range(4)],
                 scroll=[hex(u32(0x1010+4*i)) for i in range(4)],blend_control=hex(u16(0x1050)),
                 blend_alpha=hex(u16(0x1052)),blend_y=u16(0x1054),brightness=hex(u16(0x106c))))


def independent_title(dump):
    palette=struct.unpack('<256H',dump[0x2400:0x2600])
    v=dump[0x2800:]
    tiles=v[0x44000:0x4c000]
    # Both use char block1; priority ties select the lower BG number.
    maps=[struct.unpack('<1024H',v[0x40800:0x41000]),struct.unpack('<1024H',v[0x40000:0x40800])]
    order=sorted(range(2),key=lambda index:(struct.unpack_from('<H',dump,0x100e-index*2)[0]&3,3-index))
    result=[]
    for y in range(192):
        for x in range(256):
            chosen=0
            for index in order:
                tilemap=maps[index]
                entry=tilemap[(y//8)*32+x//8]
                fx,fy=x&7,y&7
                if entry&0x400:fx^=7
                if entry&0x800:fy^=7
                index=tiles[(entry&0x3ff)*64+fy*8+fx]
                if index:
                    chosen=index;break
            result.append(color(palette[chosen]))
    return struct.pack('<'+str(len(result))+'I',*result)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    inputs=parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--video',type=Path)
    inputs.add_argument('--ram',type=Path)
    parser.add_argument('--ram-index',type=Path)
    parser.add_argument('--scanout',type=Path)
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-title-video/report.json')
    parser.add_argument('--compiler',default='c++')
    args=parser.parse_args()
    oam=b'';snapshot=None
    if args.ram:
        from test_native_title_fade_arm import Snapshot
        snapshot=Snapshot(args.ram,args.ram_index)
        views=[snapshot.video_view(address) for address in (0x04000000,0x05000000,0x06800000)]
        dump=b''.join(snapshot.read(view['address'],view['size']) for view in views)
        view=snapshot.video_view(0x07000000)
        oam=snapshot.read(view['address'],view['size'])
        assert len(oam)==0x800
    else:dump=args.video.read_bytes()
    assert len(dump)==0xa6800
    cart=base.Cartridge(ROOT/'build/europe.pak')
    start,_,overlay=cart.image(7)
    def original(address,size):return overlay[address-start:address-start+size]
    spans=[('title_tiles',0x020ea710,0x8000,0x2800+0x44000),
           ('title_palette',0x020d9588,0x200,0x2400),
           ('title_map_bg3',0x020dd6f0,0x800,0x2800+0x40800),
           ('title_map_bg2',0x020df004,0x800,0x2800+0x40000)]
    content={}
    for label,at,size,offset in spans:
        source,actual=original(at,size),dump[offset:offset+size]
        differences=[i for i,(a,b) in enumerate(zip(source,actual)) if a!=b]
        content[label]=dict(original_address=hex(at),size=size,original_sha256=sha(source),
                            actual_sha256=sha(actual),exact_match=not differences,
                            differing_bytes=len(differences),nonzero_actual_bytes=sum(b!=0 for b in actual),
                            first_differing_offsets=differences[:16])
    synthetic=bytearray(dump)
    # Explicit isolated host scenario; preserve tile and palette bytes while
    # supplying ROM templates and enabling the two title BGs with fade zero.
    struct.pack_into('<I',synthetic,0x1000,0x10c00)
    struct.pack_into('<H',synthetic,0x100c,0x87)
    struct.pack_into('<H',synthetic,0x100e,0x184)
    struct.pack_into('<H',synthetic,0x1050,0)
    struct.pack_into('<H',synthetic,0x1054,0)
    struct.pack_into('<H',synthetic,0x106c,0)
    struct.pack_into('<II',synthetic,0x1018,0,0)
    synthetic[0x2800+0x40800:0x2800+0x41000]=original(0x020dd6f0,0x800)
    synthetic[0x2800+0x40000:0x2800+0x40800]=original(0x020df004,0x800)
    fade_disabled=bytearray(dump)
    struct.pack_into('<H',fade_disabled,0x1050,0)
    struct.pack_into('<H',fade_disabled,0x1054,0)
    with tempfile.TemporaryDirectory(prefix='sm64ds-title-video-') as directory:
        temp=Path(directory)
        source,executable=temp/'replay.cpp',temp/'replay'
        source.write_text(PROBE)
        subprocess.run([args.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'include'),str(source),str(ROOT/'source/game_port/native_2d.cpp'),
            str(ROOT/'source/game_port/native_video.cpp'),'-o',str(executable)],check=True)
        route,layers=replay(executable,dump+oam)
        synthetic_route,synthetic_layers=replay(executable,bytes(synthetic))
        fade_disabled_route,fade_disabled_layers=replay(executable,bytes(fade_disabled)+oam)
    expected=independent_title(synthetic)
    assert synthetic_layers[2][:256*192*4]==expected,'Native logo scanout differs from independent text BG decoder'
    # A correctly loaded title has visible color. Do not silently accept an
    # empty native tile upload even if the current hardware state is faded off.
    assert statistics(expected)['nonblack_pixels']>1000
    actual_effect_counterfactual=dict(
        label='Captured registers, maps, tiles, palette and OAM; only sub BLDCNT/BLDY cleared in the host copy',
        original_game_state_claimed=False,sub_layer=statistics(fade_disabled_layers[2]))
    if oam and not object_inventory(oam)['sub']['visible_rectangle_count']:
        expected_actual=independent_title(fade_disabled)
        actual_effect_counterfactual['native_matches_independent_decoder']=fade_disabled_layers[2][:256*192*4]==expected_actual
        assert actual_effect_counterfactual['native_matches_independent_decoder']
    saved=None
    if args.scanout:
        raw=args.scanout.read_bytes()
        assert len(raw)==3*256*256*4
        saved={'sha256':sha(raw),'replay_matches':[
            layers[i]==raw[i*256*256*4:(i+1)*256*256*4] for i in range(3)],
            'objects_missing_from_video_dump':not bool(oam)}
        if not oam:saved['bg_only_replay_matches']=saved['replay_matches']
    report=dict(status='passed',scope='Host DSMT native compositor and supplied snapshot byte audit',
        video=str(args.video) if args.video else None,video_sha256=sha(dump),
        snapshot=snapshot.summary() if snapshot else None,
        oam_available=bool(oam),oam_sha256=sha(oam) if oam else None,
        object_inventory=object_inventory(oam),
        registers=registers(dump),content=content,
        actual_snapshot=dict(main_on_top=bool(route[0]),has_3d=bool(route[1]),
                             layers=[statistics(layer) for layer in layers]),
        counterfactual=dict(label='Isolated host scenario: original BG templates, title BGs enabled, fades zero',
            original_game_state_claimed=False,native_matches_independent_decoder=True,
            sub_layer=statistics(synthetic_layers[2])),saved_scanout=saved,
        captured_fade_disabled_counterfactual=actual_effect_counterfactual,
        native_sources_sha256={name:sha((ROOT/name).read_bytes()) for name in
            ('source/game_port/native_2d.cpp','source/game_port/native_video.cpp','tools/audit_native_title_video.py')},
        cartridge_bytes_written_to_disk=False,azahar_executed=False,physical_hardware=False,
        title_visual_verified=False,playability_verified=False,performance_verified=False)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({name:report[name] for name in ('status','registers','content','actual_snapshot','counterfactual','saved_scanout')},indent=2))
    print('Report:',args.report)


if __name__=='__main__':main()
