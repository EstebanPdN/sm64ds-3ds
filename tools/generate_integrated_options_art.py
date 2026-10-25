#!/usr/bin/env python3
"""Reconstruct original Standard/Touch/Dual-hand DS diagrams from pinned ROM assets."""
import argparse, hashlib, json, struct
from pathlib import Path
import ndspy.lz10

def generate(extracted,output):
 root=extracted/'dsd/archives'; hashes={}
 def read(relative):
  p=root/relative;b=p.read_bytes();hashes[relative]=hashlib.sha256(b).hexdigest()
  try:return ndspy.lz10.decompress(b)
  except Exception:return b
 tiles=read('cee/data/2D_cad/EUR/ENG/d_2d_upr_play_mode_select_E_ncg.bin')
 palette=read('c2d/data/2D_cad/d_2d_upr_play_mode_select_ncl.bin');pal=struct.unpack('<208H',palette)
 assert len(tiles)==16384
 images=[]
 for mode in 'ABC':
  m=read(f'c2d/data/2D_cad/d_2d_upr_play_mode{mode}_select_nsc.bin');assert len(m)==2048
  pixels=[]
  for y in range(56,164):
   for x in range(40,216):
    e=struct.unpack_from('<H',m,((y//8)*32+x//8)*2)[0]
    u=7-x%8 if e&0x400 else x%8;v=7-y%8 if e&0x800 else y%8
    n=(e&1023)*64+v*8+u;assert n//2<len(tiles)
    i=(tiles[n//2]>>(4*(n%2)))&15;c=pal[(e>>12)*16+i]
    pixels.append((0x8000|((c&31)<<10)|(c&0x3e0)|((c>>10)&31)) if i else 0)
  images.append(pixels)
 output.parent.mkdir(parents=True,exist_ok=True)
 source=['#pragma once','#include <cstdint>','namespace options_art {','constexpr unsigned width=176,height=108;','inline constexpr uint16_t modes[3][width*height]={']
 for pixels in images:
  source.append('{');source.extend(','.join(hex(c) for c in pixels[i:i+24])+',' for i in range(0,len(pixels),24));source.append('},')
 source.extend(['};','}']);output.write_text('\n'.join(source)+'\n')
 output.with_suffix('.json').write_text(json.dumps(dict(schema=1,owner='Nintendo',source_assets=hashes,width=176,height=108,modes=['Standard','Touch','Dual-hand'],pixel_format='A1R5G5B5',header_sha256=hashlib.sha256(output.read_bytes()).hexdigest()),indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--extracted',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();generate(a.extracted,a.output)
