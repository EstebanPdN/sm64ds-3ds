#!/usr/bin/env python3
"""Trace retail label pixels exactly; extend the alphabet on the same 8x11 grid."""
from pathlib import Path
from collections import deque
import json
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'assets/ui/retail-pause/label-font';OUT.mkdir(exist_ok=True)
src=Image.open(ROOT/'assets/ui/retail-pause/reference/fonts/menu.png').convert('RGB')
a,b,c,d=102,57,233,75;outside=set();q=deque([(x,y) for x in range(a,c) for y in (b,d-1)]+[(x,y) for y in range(b,d) for x in (a,c-1)])
while q:
 x,y=q.popleft()
 if (x,y) in outside or not(a<=x<c and b<=y<d) or src.getpixel((x,y))!=(255,255,255):continue
 outside.add((x,y));q.extend([(x-1,y),(x+1,y),(x,y-1),(x,y+1)])
# Exact ink bounds measured from the original word sprites. Overlapping black
# outlines are intentional: advance is one pixel less than the stored width.
starts=dict(zip('BACKLIGHTSOUND',[105,114,123,132,141,150,154,163,172,185,194,203,212,221]))
faces={};rasters={};widths={}
for ch,x in starts.items():
 iw=3 if ch=='I' else 8;w=iw+2;widths[ch]=w
 face=[];pixels=[]
 for yy in range(11):face.append(''.join('1' if (x+xx,60+yy) not in outside and src.getpixel((x+xx,60+yy))!=(0,0,0) else '0' for xx in range(iw)))
 candidates={(xx,yy) for yy,row in enumerate(face) for xx,v in enumerate(row) if v=='1'};groups=[]
 while candidates:
  seed=candidates.pop();pending=[seed];group={seed}
  while pending:
   xx,yy=pending.pop()
   for dx in (-1,0,1):
    for dy in (-1,0,1):
     point=xx+dx,yy+dy
     if point in candidates:candidates.remove(point);group.add(point);pending.append(point)
  groups.append(group)
 ink=max(groups,key=len)
 face=[''.join('1' if (xx,yy) in ink else '0' for xx in range(iw)) for yy in range(11)]
 faces[ch]=face
 # Rasterize each glyph from its own face. Never copy a rectangular black
 # crop: that imports outlines belonging to neighbouring letters (notably L/I).
 rasters[ch]=[]
# New letters: explicit final-resolution strokes, matched cap height/stems.
extra={
'E':['11111111','10000000','10000000','10000000','10000000','11111100','10000000','10000000','10000000','11111111','11111111'],
'F':['11111111','10000000','10000000','10000000','10000000','11111100','10000000','10000000','10000000','10000000','10000000'],
'J':['00011111','00000001','00000001','00000001','00000001','00000001','00000001','10000001','10000001','01111110','00111100'],
'M':['10000001','11000011','10100101','10011001','10000001','10000001','10000001','10000001','10000001','10000001','10000001'],
'P':['11111110','10000001','10000001','10000001','10000001','11111110','10000000','10000000','10000000','10000000','10000000'],
'Q':['00111100','01000010','10000001','10000001','10000001','10000001','10001001','10000101','01000010','00111101','00000001'],
'R':['11111110','10000001','10000001','10000001','10000001','11111110','10010000','10001000','10000100','10000010','10000001'],
'V':['10000001','10000001','10000001','10000001','10000001','10000001','01000010','01000010','00100100','00111100','00011000'],
'W':['10000001','10000001','10000001','10000001','10000001','10000001','10011001','10100101','11000011','11000011','10000001'],
'X':['10000001','01000010','01000010','00100100','00011000','00011000','00011000','00100100','01000010','01000010','10000001'],
'Y':['10000001','10000001','01000010','01000010','00100100','00011000','00010000','00010000','00010000','00010000','00010000'],
'Z':['11111111','00000001','00000010','00000100','00001000','00010000','00100000','01000000','10000000','11111111','11111111']}
faces.update(extra)
for ch,face in faces.items():
 assert len(face)==11 and len(set(map(len,face)))==1
 w=len(face[0])+2;widths[ch]=w;pixels=[0]*(w*14)
 ink={(x+1,y+1) for y,row in enumerate(face) for x,v in enumerate(row) if v=='1'}
 for x,y in ink:
  for dx in (-1,0,1):
   for dy in (-1,0,1,2):
    if 0<=x+dx<w and 0<=y+dy<14:pixels[(y+dy)*w+x+dx]=1
 for x,y in ink:pixels[y*w+x]=2
 # All visible pixels must belong to this glyph's silhouette and shadow.
 expected={(x+dx,y+dy) for x,y in ink for dx in (-1,0,1) for dy in (-1,0,1,2)
           if 0<=x+dx<w and 0<=y+dy<14}
 assert {(x,y) for y in range(14) for x in range(w) if pixels[y*w+x]}==expected
 rasters[ch]=pixels
lines=['#pragma once','#include <cstdint>','namespace option_label_art {'];atlas=Image.new('RGBA',(260,40))
for i,ch in enumerate('ABCDEFGHIJKLMNOPQRSTUVWXYZ'):
 w=widths[ch];pixels=rasters[ch];im=Image.new('RGBA',(w,14));im.putdata([(0,0,0,0) if v==0 else (0,0,0,255) if v==1 else (255,255,255,255) for v in pixels]);im.save(OUT/f'{ch}.png');atlas.alpha_composite(im,((i%13)*20,(i//13)*20))
 lines.append(f'inline constexpr uint8_t glyph_{ch}[]={{'+','.join(map(str,pixels))+'};')
lines+=['inline constexpr const uint8_t* glyphs[]={'+','.join('glyph_'+c for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')+'};','inline constexpr unsigned widths[]={'+','.join(str(widths[c]) for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')+'};','}']
(ROOT/'source/integrated_native/options/option_label_art.hpp').write_text('\n'.join(lines)+'\n');atlas.save(OUT/'alphabet.png');(OUT/'pixels.json').write_text(json.dumps({'cap_height':11,'rows':faces,'traced_letters':list(starts),'extended_letters':list(extra),'advance':'stored width minus one; shared black outline'},indent=2)+'\n')
# Review sample with the original menu ink palette.
def render(word):
 out=Image.new('RGBA',(sum(widths[c]-1 for c in word)+1,14));x=0
 for ch in word:
  for y in range(14):
   for u in range(widths[ch]):
    v=rasters[ch][y*widths[ch]+u]
    if v:out.putpixel((x+u,y),(0,0,0,255) if v==1 else (255,255,255,255) if y<4 else (255,255,99,255) if y<7 else (255,198,90,255) if y<10 else (255,132,82,255))
  x+=widths[ch]-1
 return out
render('SOUND').resize((460,140),Image.Resampling.NEAREST).save(OUT/'sound-detail.png')
