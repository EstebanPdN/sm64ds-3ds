#!/usr/bin/env python3
"""Resolve DSMT's first observed SPA texture bindings with the real host bridge.

Reads the verified user's pack, supplies SPA bytes to a temporary host harness
through stdin and records hashes only. The production GameGeometry,
GameTextures and texture decoder run unchanged. This is host texture evidence,
not an original rendered game frame or console/performance verification.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import test_native_menu_callbacks as base
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['source/core/game_geometry.cpp','source/core/game_textures.cpp',
         'source/vendor/ntr/texture.cpp']
ASSET='data/DSMT/mdst_kira1_spa.bin'
ASSET_SHA='6407bacff4838b43343468ad7507a5735e33dda8e0cb9c74edf4bcdeeba82596'
IMAGE=[0x689072b0,0x689072a8]
PALETTE=[0xb8,0xb9]
SOURCE=[(0x90,0xd0),(0xf8,0x138)]
CPP=r'''#include "sm64ds/game_geometry.hpp"
#include "sm64ds/game_textures.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
void word(std::uint32_t value) {
 unsigned char bytes[4];for(unsigned i=0;i<4;++i)bytes[i]=static_cast<unsigned char>(value>>(i*8));
 assert(std::fwrite(bytes,1,4,stdout)==4);
}
int main() {
 unsigned char spa[320];assert(std::fread(spa,1,320,stdin)==320);
 auto storage=std::make_unique<sm64ds::GameGeometry>();auto& geometry=*storage;
 // Original bd5a8's real masks: A/B texture banks and F palette bank.
 assert(geometry.set_texture_banks(3) && geometry.set_palette_banks(0x20));
 assert(geometry.begin(false));
 assert(geometry.upload(false,spa+0x90,0x39580,64));
 assert(geometry.upload(false,spa+0xf8,0x39540,64));
 assert(geometry.end(false) && geometry.begin(true));
 assert(geometry.upload(true,spa+0xd0,0x5c0,8));
 assert(geometry.upload(true,spa+0x138,0x5c8,8));
 assert(geometry.end(true));
 sm64ds::GameTextures textures(geometry);textures.begin_frame();
 const unsigned images[]={0x689072b0,0x689072a8},palettes[]={0xb8,0xb9};
 for(unsigned index=0;index<2;++index) {
  const std::uint32_t* pixels=nullptr;int width=0,height=0;
  assert(textures.resolve(images[index],palettes[index],pixels,width,height));
  assert(pixels && width==16 && height==16);
  word(index);word(static_cast<unsigned>(width));word(static_cast<unsigned>(height));
  for(unsigned pixel=0;pixel<256;++pixel)word(pixels[pixel]);
 }
 const std::uint32_t* pixels=nullptr;int width=0,height=0;
 assert(!textures.resolve(images[0],0x1fff,pixels,width,height));
 assert(!textures.resolve(images[0],0x2000,pixels,width,height));
 assert(geometry.begin(false));
 assert(!textures.resolve(images[0],palettes[0],pixels,width,height));
 assert(geometry.end(false));
 auto short_storage=std::make_unique<sm64ds::GameGeometry>();
 assert(short_storage->set_texture_banks(1) && short_storage->set_palette_banks(0x20));
 sm64ds::GameTextures short_texture(*short_storage);
 assert(!short_texture.resolve(images[0],palettes[0],pixels,width,height));
 word(4); // Four explicit invalid-window/transaction controls were rejected.
}
'''


def expected_pixels(spa,index):
    texture,palette=SOURCE[index]
    colours=struct.unpack_from('<4H',spa,palette)
    pixels=[]
    for position in range(256):
        colour=(spa[texture+position//4]>>((position&3)*2))&3
        if colour==0:
            pixels.append(0)
            continue
        value=colours[colour]
        red,green,blue=value&31,(value>>5)&31,(value>>10)&31
        # BGR555 expansion repeats its high three bits in the low bits.
        pixels.append(0xff000000|((red*8+red//4)<<16)|((green*8+green//4)<<8)|(blue*8+blue//4))
    return struct.pack('<256I',*pixels)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack',type=Path,default=ROOT/'build/europe.pak')
    parser.add_argument('--compiler',default='c++')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-runtime/menu-texture-binding-test.json')
    args=parser.parse_args()
    cart=base.Cartridge(args.pack)
    _,indices=cart.handles()
    assert cart.entries[indices[658]][4]==ASSET
    spa=cart.file(ASSET)
    assert len(spa)==320 and base.sha(spa)==ASSET_SHA
    with tempfile.TemporaryDirectory(prefix='sm64ds-menu-binding-') as directory:
        temp=Path(directory)
        source,executable=temp/'binding.cpp',temp/'binding'
        source.write_text(CPP)
        command=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-UNDEBUG',
                 '-I'+str(ROOT/'include')]
        if args.sanitize:
            command+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
        subprocess.run([*command,str(source),*[str(ROOT/s) for s in SOURCES],'-o',str(executable)],check=True)
        checked=subprocess.run([str(executable)],input=spa,capture_output=True,check=True)
        assert not checked.stderr,checked.stderr
        raw=checked.stdout
    assert len(raw)==2*(12+1024)+4
    results=[]
    for index in range(2):
        at=index*(12+1024)
        actual_index,width,height=struct.unpack_from('<3I',raw,at)
        assert (actual_index,width,height)==(index,16,16)
        pixels=raw[at+12:at+12+1024]
        assert pixels==expected_pixels(spa,index),'Decoded palette or texel indices differ'
        opaque=sum(bool(value>>24) for value in struct.unpack('<256I',pixels))
        assert 0<opaque<256
        results.append({'image':hex(IMAGE[index]),'palette':hex(PALETTE[index]),
                        'texture_offset':hex((IMAGE[index]&0xffff)*8),
                        'palette_offset':hex(PALETTE[index]*8),
                        'width':width,'height':height,'opaque_pixels':opaque,
                        'rgba_sha256':base.sha(pixels)})
    assert struct.unpack_from('<I',raw,len(raw)-4)[0]==4
    report={'status':'passed','scope':'Host GameGeometry/GameTextures/texture_decode contracts only; no game frame, Azahar, physical console or performance proof',
            'asset':{'path':ASSET,'handle':658,'size':len(spa),'sha256':ASSET_SHA},
            'actual_menu_bank_masks':{'textures':3,'palettes':0x20},
            'results':results,'rejection_controls':4,
            'sanitizers':['address','undefined'] if args.sanitize else [],
            'source_sha256':{s:base.sha((ROOT/s).read_bytes()) for s in SOURCES},
            'cartridge_bytes_written_to_disk':False}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
