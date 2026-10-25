#!/usr/bin/env python3
"""Read a captured native decoded-texture cache using its ARM32 C++ layout.

The helper is an explicitly separate read-only ABI observer. It executes the
captured ELF's real tree increment, with exact RAM and saved header versions;
it never resumes the game or imports old RAM into a new game ELF. The private
fixture contains acquired texture/palette inputs and original cache bindings.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess

import test_native_audio_deferred_arm as shared
from test_native_title_fade_arm import Snapshot

base, ROOT = shared.base, shared.ROOT
HEADERS=('native_frame.hpp','game_renderer.hpp','game_textures.hpp')
SOURCE=r'''
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include <memory>
#include <string>
#define private public
#include "sm64ds/native_frame.hpp"
#undef private
extern "C" unsigned observe_texture_cache(sm64ds::NativeFrame* frame, unsigned* out, unsigned max) {
  auto& cache=frame->textures_.cache_;
  if(cache.size()>max)return ~0u;
  unsigned count=0;
  for(auto& entry:cache) {
    auto* row=out+count++*8;
    row[0]=std::get<0>(entry.first);row[1]=std::get<1>(entry.first);
    row[2]=std::get<2>(entry.first);row[3]=std::get<3>(entry.first);
    row[4]=reinterpret_cast<std::uintptr_t>(entry.second.pixels.data());
    row[5]=entry.second.width;row[6]=entry.second.height;row[7]=entry.second.pixels.size();
  }
  return count;
}
extern "C" const unsigned observed_layout[] = {
  sizeof(sm64ds::NativeFrame), sizeof(sm64ds::GameTextures), sizeof(ntr::GxTriangle),
  offsetof(sm64ds::NativeFrame,textures_), offsetof(sm64ds::NativeFrame,renderer_),
  sizeof(sm64ds::GameRenderer), sizeof(sm64ds::GameGeometry)};
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt',type=Path,required=True)
    parser.add_argument('--snapshot',default='ram-final.ram.bin')
    parser.add_argument('--directory',type=Path,required=True)
    args=parser.parse_args();args.directory.mkdir(parents=True,exist_ok=True)
    assert not (args.directory/'report.json').exists(), 'Preserve completed observer evidence'
    result=json.loads((args.attempt/'result.json').read_text())
    elf=args.attempt/'sm64ds-native.elf'
    assert base.sha(elf.read_bytes())==result['elf_sha256']
    binary=args.attempt/args.snapshot;expected=result['ram_snapshot_files'][binary.name]
    assert base.sha(binary.read_bytes())==expected['sha256']
    snap=Snapshot(binary);probe=shared.Probe('captured C12 read-only cache observer',elf=elf)
    names=probe.names;snap.install(probe.cpu)
    active=probe.u32(names['_ZN12_GLOBAL__N_1L12active_frameE'])
    assert active
    overlay=args.directory/'include/sm64ds';overlay.mkdir(parents=True,exist_ok=True)
    for name in HEADERS:
        if not (overlay/name).exists():shutil.copy2(ROOT/'include/sm64ds'/name,overlay/name)
    source=args.directory/'observer.cpp';source.write_text(SOURCE)
    helper=args.directory/'observer.elf'
    flags=['-std=c++17','-march=armv6k','-mfloat-abi=hard','-mtp=soft','-O2',
           '-fno-exceptions','-fno-rtti','-fno-pic','-D__3DS__',
           '-Wno-invalid-offsetof','-Wa,--noexecstack',
           '-I'+str(args.directory/'include'),'-I'+str(ROOT/'include'),
           '-I/opt/devkitpro/libctru/include','-I/opt/devkitpro/portlibs/3ds/include']
    increment='_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base'
    subprocess.run([base.TOOL+'g++',*flags,'-nostdlib','-nostartfiles',
        '-Wl,-e,observe_texture_cache','-Wl,-Ttext=0x01000000',
        '-Wl,--defsym='+increment+'='+hex(names[increment]),str(source),'-o',str(helper)],check=True)
    data=helper.read_bytes();at=struct.unpack_from('<I',data,28)[0]
    size,count=struct.unpack_from('<HH',data,42)
    for i in range(count):
        kind,offset,address,_,filesz,memsz,_,_=struct.unpack_from('<8I',data,at+i*size)
        if kind==1 and memsz:
            begin=address&~0xfff;end=(address+memsz+0xfff)&~0xfff
            probe.cpu.mem_map(begin,end-begin);probe.cpu.mem_write(address,data[offset:offset+filesz])
    observer=base.symbols(helper);probe.names.update(observer)
    layout=struct.unpack('<7I',probe.cpu.mem_read(observer['observed_layout'],28))
    assert layout[2]==204
    records=shared.WORK+0x100;probe.cpu.mem_write(shared.WORK,bytes([0xa5])*0x10000)
    frame_before=bytes(probe.cpu.mem_read(active,layout[0]))
    count=probe.call('observe_texture_cache',[active,records,1024]);assert 0<count<=1024
    assert bytes(probe.cpu.mem_read(active,layout[0]))==frame_before
    assert bytes(probe.cpu.mem_read(records+count*32,32))==bytes([0xa5])*32
    rows=[]
    fixture=bytearray(b'TEXRAM01'+struct.pack('<I',count))
    geometry=names['_ZN6sm64ds12_GLOBAL__N_1L8geometryE']
    caps=struct.unpack('<2I',snap.read(geometry+512*1024+96*1024,8))
    fixture+=struct.pack('<2I',*caps)
    fixture+=snap.read(geometry,caps[0])+snap.read(geometry+512*1024,caps[1])
    for i in range(count):
        image,palette,tg,pg,pixels,width,height,length=struct.unpack('<8I',probe.cpu.mem_read(records+i*32,32))
        assert width*height==length and 8<=width<=1024 and 8<=height<=1024
        raw=snap.read(pixels,length*4)
        rows.append(dict(image=hex(image),palette=palette,texture_generation=tg,
            palette_generation=pg,pixels=hex(pixels),width=width,height=height,
            bytes=len(raw),sha256=base.sha(raw)))
        fixture+=struct.pack('<5I',image,palette,width,height,len(raw))+raw
    path=args.directory/'captured-textures.bin';path.write_bytes(fixture)
    report=dict(status='passed',scope=__doc__,ELF_sha256=result['elf_sha256'],
        snapshot_sha256=base.sha(snap.raw),helper_sha256=base.sha(helper.read_bytes()),
        saved_header_sha256={n:base.sha((overlay/n).read_bytes()) for n in HEADERS},
        native_layout=list(layout),active_frame=hex(active),bindings=rows,
        full_frame_record_unchanged=True,output_canaries_checked=True,
        fixture=dict(path=str(path.resolve()),bytes=len(fixture),sha256=base.sha(fixture)),
        physical_performance_measured=False)
    (args.directory/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='passed',bindings=count,pixel_bytes=sum(r['bytes'] for r in rows),
        report=str(args.directory/'report.json'))))


if __name__=='__main__':main()
