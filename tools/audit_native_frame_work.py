#!/usr/bin/env python3
"""Count frozen native-frame work from captured RAM/GX without FPS claims.

Counters run only in temporary host copies of production source. Renderer byte
counts describe its token-zero CRC baseline, not an observed GPU execution.
Instrumented
and intact 2D pixel buffers must match exactly. No production edits, emulator,
UI, physical console, simulation clock or GPU submission is performed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import audit_native_title_video as video
import audit_native_gx_stream as gx
from test_native_title_fade_arm import Snapshot
from test_native_menu_callbacks import symbols

ROOT=Path(__file__).resolve().parents[1]
COUNTERS=('read8','read16','read32','native_read_fallback','vram_page_resolutions',
 'palette_queries','palette_conversions','BG_samples_main','BG_samples_sub',
 'blend_calls_main','blend_calls_sub','OAM_slots_main','OAM_slots_sub',
 'OBJ_candidate_pixels_main','OBJ_candidate_pixels_sub','scanout_pixels_main',
 'scanout_pixels_sub','overlay_pixel_copies','OBJ_memset_bytes','OBJ_window_memset_bytes',
 'overlay_memset_bytes','pointer_palette_cache_memset_bytes')
SOURCES=('source/game_port/native_frame.cpp','source/game_port/native_2d.cpp',
 'source/game_port/native_video.cpp','source/game_port/native_video_services.cpp',
 'source/ctr/game_renderer.cpp','source/core/game_textures.cpp','source/core/game_geometry.cpp',
 'source/core/assets.cpp','source/vendor/ntr/gx.cpp','source/vendor/ntr/texture.cpp',
 'source/ctr/native_main.cpp','include/sm64ds/game_projection.hpp','include/sm64ds/game_geometry.hpp',
 'include/ntr/gx.h','source/ctr/input.cpp','include/sm64ds/native_frame.hpp')

GX_PROBE=r'''
#include "ntr/gx.h"
#include "sm64ds/game_geometry.hpp"
#include "sm64ds/game_textures.hpp"
#include "sm64ds/game_projection.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
std::vector<unsigned char> read(const char* name) {
    std::ifstream f(name,std::ios::binary);if(!f)throw std::runtime_error("open");
    f.seekg(0,std::ios::end);const auto n=f.tellg();f.seekg(0);
    if(n<0)throw std::runtime_error("size");
    std::vector<unsigned char> v(static_cast<size_t>(n));
    if(!f.read(reinterpret_cast<char*>(v.data()),v.size()))throw std::runtime_error("read");
    return v;
}
unsigned u32(const unsigned char* p) {return unsigned(p[0])|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24;}
int main(int argc,char** argv) {
  try {
    if(argc!=5)return 2;
    const auto stream=read(argv[1]),meta=read(argv[2]),texels=read(argv[3]),palettes=read(argv[4]);
    if(stream.size()%8 || meta.size()!=44)return 3;
    auto g=std::make_unique<sm64ds::GameGeometry>();
    if(texels.size()!=sizeof g->textures || palettes.size()!=sizeof g->palettes)return 4;
    std::memcpy(g->textures,texels.data(),texels.size());std::memcpy(g->palettes,palettes.data(),palettes.size());
    g->texture_capacity=u32(meta.data());g->palette_capacity=u32(meta.data()+4);
    g->texture_generation=u32(meta.data()+8);g->palette_generation=u32(meta.data()+12);
    g->texture_transfer=u32(meta.data()+32);g->palette_transfer=u32(meta.data()+36);g->highlight_shading=u32(meta.data()+40);
    sm64ds::GameTextures textures(*g);ntr::gx_reset();
    ntr::gx_set_texture_resolver(sm64ds::GameTextures::resolver,&textures);
    for(size_t i=0;i<stream.size();i+=8) {
        const auto port=u32(stream.data()+i),value=u32(stream.data()+i+4);
        if(port)ntr::gx_write_port(0x04000400+port*4,value);else ntr::gx_write_fifo(value);
    }
    if(!ntr::gx_asset_stream_valid())return 5;
    size_t count=0;const auto* triangles=ntr::gx_polygons(count);
    struct Texture {int width=8,height=8;bool translucent=false;size_t uses=0;};
    std::map<const unsigned*,Texture> used;
    sm64ds::GameProjection projection;projection.native_matrix=true;ntr::gx_get_projection(projection.matrix.data());
    sm64ds::GameBatchKey previous{};bool has_previous=false;unsigned batches=0,translucent_batches=0;
    for(size_t i=0;i<count;++i) {
        const auto& t=triangles[i];auto inserted=used.try_emplace(t.tex);
        auto& image=inserted.first->second;++image.uses;
        if(inserted.second && t.tex) {
            image.width=t.tw;image.height=t.th;
            for(int j=0;j<t.tw*t.th;++j) {const auto a=t.tex[j]>>24;image.translucent|=a!=0 && a!=255;}
        }
        sm64ds::GameBatchKey key;
        if(!sm64ds::game_batch_key(t,projection,t.tex,t.alpha<31 || image.translucent,key))return 6;
        if(!has_previous || key!=previous) {++batches;translucent_batches+=key.translucent;}
        previous=key;has_previous=true;
    }
    std::printf("{\"GX_entries\":%zu,\"triangles\":%zu,\"adjacent_material_batches\":%u,\"translucent_batches\":%u,\"decoded_texture_cache_entries\":%zu,\"textures_used_by_triangles\":[",stream.size()/8,count,batches,translucent_batches,textures.cached_textures());
    bool comma=false;size_t bytes=0;
    for(const auto& item:used) {
        const auto& t=item.second;const auto size=size_t(t.width)*t.height*4;bytes+=size;
        std::printf("%s{\"untextured_white\":%s,\"width\":%d,\"height\":%d,\"RGBA_bytes\":%zu,\"triangle_uses\":%zu,\"translucent_texels\":%s}",comma?",":"",item.first?"false":"true",t.width,t.height,size,t.uses,t.translucent?"true":"false");comma=true;
    }
    std::printf("],\"distinct_triangle_texture_CRC_bytes_per_draw\":%zu,\"triangle_vertices\":%zu,\"host_GxTriangle_size\":%zu}\n",bytes,count*3,sizeof(ntr::GxTriangle));
  } catch(const std::exception& e) {std::fprintf(stderr,"%s",e.what());return 7;}
}
'''


def sha(value):return hashlib.sha256(value).hexdigest()


def conservative_2d_inputs(snapshot):
    """Exact state key proposal, deliberately independent of write hooks.

    Current scanout consumes neither VCOUNT/IRQ/GX state nor DISPCAPCNT. Bank
    controls and POWCNT determine aliases/routing; reserve bytes and MOSAIC in
    the documented 2D span are included conservatively. This key is audited
    here, not installed in production.
    """
    registers=snapshot.video_view(0x04000000)
    regs=snapshot.read(registers['address'],registers['size'])
    parts=[]
    for base in (0,0x1000):
        for start,end in ((0,4),(8,0x56),(0x6c,0x6e)):
            parts.append(regs[base+start:base+end])
    parts.extend((regs[0x240:0x24a],regs[0x304:0x306]))
    for address in (0x06800000,0x05000000,0x07000000):
        view=snapshot.video_view(address)
        parts.append(snapshot.read(view['address'],view['size']))
    data=b''.join(parts)
    if len(data)!=676020:raise ValueError('Owned video/key dimensions changed')
    return data


def tap_2d(source):
    def replace(old,new,count=1):
        nonlocal source
        if source.count(old)!=count:raise ValueError('Reviewed counter site changed: '+old[:70])
        source=source.replace(old,new)
    replace('    static_assert(Bytes==1 || Bytes==2 || Bytes==4);',
            '    ++::sm64ds_work[Bytes==1?0:Bytes==2?1:2];\n    static_assert(Bytes==1 || Bytes==2 || Bytes==4);')
    replace('return uint32_t(native_video_read(address,Bytes));',
            'return (++::sm64ds_work[3],uint32_t(native_video_read(address,Bytes)));')
    replace('    if(!page) {','    if(!page) {\n        ++::sm64ds_work[4];')
    replace('inline uint32_t palette_color(uint32_t address) {',
            'inline uint32_t palette_color(uint32_t address) {\n    ++::sm64ds_work[5];')
    replace('        if(!g_palette_valid[slot]) {','        if(!g_palette_valid[slot]) {\n            ++::sm64ds_work[6];')
    replace('bool sample_bg(const BgLayer &c, int x, int y, uint32_t &out) {',
            'bool sample_bg(const BgLayer &c, int x, int y, uint32_t &out) {\n    ++::sm64ds_work[kRegBase==0x04000000?7:8];')
    replace('                            int below_id) {','                            int below_id) {\n    ++::sm64ds_work[kRegBase==0x04000000?9:10];')
    replace('        const uint16_t a0 = rd16(oam_b + i * 8u);',
            '        ++::sm64ds_work[kRegBase==0x04000000?11:12];\n        const uint16_t a0 = rd16(oam_b + i * 8u);')
    replace('                const int tcol = tx >> 3, trow = ty >> 3;',
            '                ++::sm64ds_work[kRegBase==0x04000000?13:14];\n                const int tcol = tx >> 3, trow = ty >> 3;')
    replace('            fb.pixels[y*256+x] = apply_bright(c, br);',
            '            ++::sm64ds_work[kRegBase==0x04000000?15:16];\n            fb.pixels[y*256+x] = apply_bright(c, br);')
    replace('if(!(mask&1) || foreground)overlay->pixels[y*256+x]=fb.pixels[y*256+x];',
            'if(!(mask&1) || foreground) {++::sm64ds_work[17];overlay->pixels[y*256+x]=fb.pixels[y*256+x];}')
    replace('    std::memset(g_obj, 0, sizeof g_obj);','    ::sm64ds_work[18]+=sizeof g_obj;\n    std::memset(g_obj, 0, sizeof g_obj);')
    replace('    std::memset(g_objwin, 0, sizeof g_objwin);','    ::sm64ds_work[19]+=sizeof g_objwin;\n    std::memset(g_objwin, 0, sizeof g_objwin);')
    replace('if(overlay)std::memset(overlay->pixels,0,sizeof overlay->pixels);',
            'if(overlay) {::sm64ds_work[20]+=sizeof overlay->pixels;std::memset(overlay->pixels,0,sizeof overlay->pixels);}')
    replace('    std::memset(g_vram_pages,0,sizeof g_vram_pages);',
            '    ::sm64ds_work[21]+=sizeof g_vram_pages;\n    std::memset(g_vram_pages,0,sizeof g_vram_pages);',2)
    replace('    std::memset(g_palette_valid,0,sizeof g_palette_valid);',
            '    ::sm64ds_work[21]+=sizeof g_palette_valid;\n    std::memset(g_palette_valid,0,sizeof g_palette_valid);',2)
    return 'unsigned long long sm64ds_work[22]{};\n'+source


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt',type=Path,default=ROOT/'build/native-runtime-azahar/attempt-1791613748756720000')
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-performance-plan/frame-work.json')
    args=parser.parse_args()
    hashes={name:sha((ROOT/name).read_bytes()) for name in SOURCES}
    snapshots=[Snapshot(args.attempt/(prefix+'.ram.bin')) for prefix in ('ram-title-180','ram-final')]
    names=symbols(args.attempt/'sm64ds-native.elf')
    geometry=names['_ZN6sm64ds12_GLOBAL__N_1L8geometryE']
    rows=[]
    state_keys=[]
    for snap in snapshots:
        arrays=snap.read(geometry,524288+98304)
        fields=struct.unpack('<8I3B',snap.read(geometry+524288+98304,35))
        rows.append(dict(tick=snap.metadata['tick'],sha256=sha(snap.raw),geometry_address=hex(geometry),
            geometry_metadata=dict(zip((*gx.FIELDS,*gx.FLAGS),fields)),
            texture_bytes_sha256=sha(arrays[:524288]),palette_bytes_sha256=sha(arrays[524288:])))
        key=conservative_2d_inputs(snap);state_keys.append(key)
        rows[-1]['conservative_2D_inputs_bytes']=len(key)
        rows[-1]['conservative_2D_inputs_sha256']=sha(key)
    with tempfile.TemporaryDirectory(prefix='sm64ds-frame-work-') as directory:
        temp=Path(directory)
        probe=temp/'2d.cpp';probe.write_text(video.PROBE)
        original=ROOT/'source/game_port/native_2d.cpp'
        tapped=temp/'2d-tapped.cpp';tapped.write_text(tap_2d(original.read_text()))
        tap_probe=temp/'2d-probe-tapped.cpp'
        site='    const unsigned header[]={frame->main_on_top,frame->has_3d};'
        statement='    std::fprintf(stderr,"[");for(unsigned i=0;i<22;++i)std::fprintf(stderr,"%s%llu",i?",":"",::sm64ds_work[i]);std::fprintf(stderr,"]\\n");\n'
        tap_probe.write_text('extern unsigned long long sm64ds_work[22];\n'+video.PROBE.replace(site,statement+site))
        binaries={}
        for mode,source,p in (('intact',original,probe),('tap',tapped,tap_probe)):
            executable=temp/('2d-'+mode)
            subprocess.run(['c++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(p),str(source),str(ROOT/'source/game_port/native_video.cpp'),'-o',str(executable)],check=True)
            binaries[mode]=executable
        for snap,row in zip(snapshots,rows):
            views=[snap.video_view(a) for a in (0x04000000,0x05000000,0x06800000,0x07000000)]
            data=b''.join(snap.read(v['address'],v['size']) for v in views)
            results={mode:subprocess.run([str(exe)],input=data,check=True,capture_output=True) for mode,exe in binaries.items()}
            assert results['intact'].stdout==results['tap'].stdout,'Counters changed pixel buffers'
            counts=json.loads(results['tap'].stderr)
            assert len(counts)==len(COUNTERS)
            row['CPU_2D_operation_counts']=dict(zip(COUNTERS,counts))
            # The host tap's page-pointer array uses 64-bit pointers. Pixel
            # structures are identical; report the ARM32 cache clear explicitly.
            host_cache=row['CPU_2D_operation_counts'].pop('pointer_palette_cache_memset_bytes')
            row['CPU_2D_operation_counts']['host_pointer_palette_cache_memset_bytes']=host_cache
            row['CPU_2D_operation_counts']['ARM32_pointer_palette_cache_memset_bytes']=2304*4+1024
            row['CPU_2D_known_logical_write_bytes_ARM32']=(sum(counts[18:21])+2304*4+1024+
                (counts[15]+counts[16])*4+counts[17]*4)
            row['CPU_2D_logical_read_bytes']=counts[0]+counts[1]*2+counts[2]*4
            row['2D_intact_equals_instrumented']=True
            row['scanout_sha256']=sha(results['intact'].stdout[8:])
            if row['tick']==180:assert results['intact'].stdout[8:]==(args.attempt/'scene-1-visible-scanout.bin').read_bytes()
        meta=temp/'geometry.bin';meta.write_bytes(gx.encode_metadata(json.loads((args.attempt/'scene-1-visible-geometry.json').read_text())))
        gx_probe=temp/'gx.cpp';gx_probe.write_text(GX_PROBE)
        executable=temp/'gx'
        subprocess.run(['c++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(gx_probe),*[str(ROOT/name) for name in ('source/vendor/ntr/gx.cpp','source/vendor/ntr/texture.cpp','source/core/game_textures.cpp','source/core/game_geometry.cpp')],'-o',str(executable)],check=True)
        output=subprocess.run([str(executable),str(args.attempt/'scene-1-visible-gx.bin'),str(meta),str(args.attempt/'scene-1-visible-textures.bin'),str(args.attempt/'scene-1-visible-palettes.bin')],check=True,capture_output=True)
        gx_work=json.loads(output.stdout)
    for name,expected in hashes.items():assert sha((ROOT/name).read_bytes())==expected,'Production source changed during audit'
    gx_work.update(screen_layer_CRC_bytes_token_zero_or_changed_revision=3*256*256*4,
        screen_layer_CRC_bytes_stable_nonzero_revision=0,
        screen_CRC_includes_padding_bytes=3*256*64*4,
        renderer_vertex_bytes=(gx_work['triangle_vertices']+18)*24,
        ARM32_triangle_copy_bytes=gx_work['triangles']*204,
        stable_texture_CRC_bytes_token_zero_or_changed_revision=gx_work['distinct_triangle_texture_CRC_bytes_per_draw']+3*256*256*4,
        screen_layer_reuploads_required_by_unchanged_bytes=0)
    report=dict(status='passed',scope=__doc__,attempt=str(args.attempt.resolve()),
        elf_sha256=sha((args.attempt/'sm64ds-native.elf').read_bytes()),checkpoints=rows,
        tick180_GX_work=gx_work,source_sha256=hashes,
        same_CPU_scanout_between_checkpoints=rows[0]['scanout_sha256']==rows[1]['scanout_sha256'],
        conservative_2D_inputs_equal_between_checkpoints=state_keys[0]==state_keys[1],
        conservative_2D_inputs_changed_bytes_between_checkpoints=sum(a!=b for a,b in zip(*state_keys)),
        equality_applies_only_to_captured_endpoints=True,
        logical_byte_counts_are_not_DRAM_traffic_or_bandwidth=True,
        GPU_executed=False,production_changed=False,Azahar_launched=False,physical_performance_measured=False)
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='passed',report=str(args.report),same_CPU_scanout=report['same_CPU_scanout_between_checkpoints'],tick180_GX_work=gx_work,CPU_2D_operation_counts=rows[0]['CPU_2D_operation_counts']),indent=2))


if __name__=='__main__':main()
