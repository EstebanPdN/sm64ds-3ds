#!/usr/bin/env python3
"""Author native lower-screen UI pixels; retain original bitmap glyph silhouettes.

No antialiasing or fractional runtime sprite scaling. Backgrounds are not assets
of this atlas: the original castle/map remains owned by the game.
"""
from pathlib import Path
import json
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/ui/native-lower';OUT.mkdir(parents=True,exist_ok=True)
palette=[0x00000000,0xff191008,0xffffec9a,0xffffc34e,0xffd98900,0xff8d4300,0xfffffbd9,0xffc4b898]
def card(w,h,cream=False):
 im=Image.new('RGBA',(w,h))
 for y in range(h):
  for x in range(w):
   edge=min(x,y,w-1-x,h-1-y)
   role=1 if edge==0 else 2 if edge==1 and x+y<w else 5 if edge==1 else 3 if edge<4 and x+y<w else 5 if edge<4 else 6 if cream else 4
   if min(x,w-1-x)<2 and min(y,h-1-y)<2:role=0
   c=palette[role];im.putpixel((x,y),((c>>16)&255,(c>>8)&255,c&255,c>>24))
 return im
assets={}
for name,w,h,cream in [('button',288,36,False),('selected',288,36,True),('footer',88,30,False),('audio',288,76,True),('card',288,112,True),('bar',256,18,False)]:
 im=card(w,h,cream);im.save(OUT/(name+'.png'));assets[name]=(w,h)
# Each atlas edge/corner is pixel-authored; runtime nine-slicing copies pixels.
tile=card(24,24,True);tile.save(OUT/'frame.png')
root=ROOT/'tools/sm64ds_packer/assets';meta=json.loads((root/'fonts.json').read_text())
font=Image.open(root/'body_font.png').convert('RGBA');title=Image.open(root/'title_font.png').convert('RGBA')
body=[];big=[]
for code in range(32,127):
 cell=meta['glyphs'][code]
 body.append([sum(1<<x for x in range(8) if cell!=65535 and font.getpixel((cell%16*8+x,cell//16*16+y))[3]) for y in range(16)])
 big.append([sum(1<<x for x in range(16) if cell!=65535 and title.getpixel((cell%16*16+x,cell//16*16+y))[3]) for y in range(16)])
# Missing punctuation is authored at native body-glyph resolution.
for code,rows in {
 ord('%'):["11001","11010","00010","00100","01000","01011","10011"],
 ord('+'):["00100","00100","11111","00100","00100"],
}.items():
 body[code-32]=[0]*16
 for y,row in enumerate(rows,4):body[code-32][y]=sum(1<<x for x,c in enumerate(row) if c=='1')
title_colors=sorted(set(title.get_flattened_data()))
assert len(title_colors)<=16
title_indices=[]
for code in range(32,127):
 cell=meta['glyphs'][code];indices=[]
 for y in range(16):
  for x in range(16):
   indices.append(title_colors.index(title.getpixel((cell%16*16+x,cell//16*16+y))) if cell!=65535 else next(i for i,c in enumerate(title_colors) if c[3]==0))
 title_indices.append([indices[i]|(indices[i+1]<<4) for i in range(0,256,2)])
pixels=[(a<<24)|(r<<16)|(g<<8)|b for r,g,b,a in tile.get_flattened_data()]
header=['#pragma once','#include <cstdint>','namespace native_lower_art {',
 'inline constexpr uint32_t frame[576]={'+','.join(hex(x)+'u' for x in pixels)+'};']
for name,typ,values in [('body','uint8_t',body),('title','uint16_t',big)]:
 header.append(f'inline constexpr {typ} {name}[95][16]={{'+','.join('{'+','.join(map(str,row))+'}' for row in values)+'};')
for name,key in [('body_advance','advances8'),('title_advance','advances16')]:
 header.append(f'inline constexpr uint8_t {name}[95]={{'+','.join(str(meta[key][i] or (8 if name=='body_advance' else 16)) for i in range(32,127))+'};')
header.append('inline constexpr uint32_t title_colors[]={'+','.join(hex((a<<24)|(r<<16)|(g<<8)|b)+'u' for r,g,b,a in title_colors)+'};')
header.append('inline constexpr uint8_t title_pixels[95][128]={'+','.join('{'+','.join(map(str,row))+'}' for row in title_indices)+'};')
header.append('}')
(ROOT/'source/integrated_native/options/native_lower_art.hpp').write_text('\n'.join(header)+'\n')
(OUT/'manifest.json').write_text(json.dumps(dict(canvas=[320,240],sprites=assets,backgrounds_replaced=False,glyph_source='Existing approved original bitmap glyph silhouettes, copied1:1 into native pixels; no fractional scaling',runtime_integration='dump screen first; complete menu/HUD integration pending'),indent=2)+'\n')
print(OUT)
