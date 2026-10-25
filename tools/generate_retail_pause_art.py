#!/usr/bin/env python3
"""Generate retail menu frames pixel by pixel from the approved SURROUND reference."""
from pathlib import Path
import struct, json, hashlib
import ndspy.lz10
from PIL import Image, ImageOps
from collections import deque
ROOT=Path(__file__).resolve().parents[1]
def generate():
 root=ROOT/'third_party/sm64ds-3ds/sm64ds-decomp/extracted/dsd/archives'
 hashes={}
 def read(name):
  b=(root/name).read_bytes();hashes[name]=hashlib.sha256(b).hexdigest()
  try:return ndspy.lz10.decompress(b)
  except Exception:return b
 tiles=read('cee/data/2D_cad/EUR/ENG/d_2d_touch_select_4bit_E_ncg.bin')
 palette=read('c2d/data/2D_cad/d_2d_touch_select_4bit_ncl.bin')
 layout=read('cee/data/2D_cad/EUR/ENG/d_2d_btm_framework_play_mode_select_E_nsc.bin')
 im=Image.new('RGBA',(256,192))
 for y in range(192):
  for x in range(256):
   e=struct.unpack_from('<H',layout,(y//8*32+x//8)*2)[0]
   u=7-x%8 if e&1024 else x%8;v=7-y%8 if e&2048 else y%8
   a=(e&1023)*32+v*4+u//2;i=tiles[a]>>(u%2*4)&15
   c=struct.unpack_from('<H',palette,(e>>12)*32+i*2)[0]
   im.putpixel((x,y),((c&31)*255//31,(c>>5&31)*255//31,(c>>10&31)*255//31,255 if i else 0))
 # Crop exact retail frames at 1x. Each box includes the whole control: the
 # two-row black bottom outline of OK/return sits at DS rows 184-185.
 crops={'selected':(48,32,208,64),'normal':(48,72,208,104),
        'ok':(96,152,160,186),'back':(216,152,248,186),
        'action':(0,0,1,1),'shortcut_plus':(0,0,1,1),'confirm':(0,0,1,1),'selection_star':(0,0,1,1),'arrow_left':(0,0,1,1),'arrow_right':(0,0,1,1),'page_down':(0,0,1,1)}
 out=ROOT/'assets/ui/retail-pause';out.mkdir(parents=True,exist_ok=True)
 header=['#pragma once','#include <cstdint>','namespace retail_pause_art {']
 for name,box in crops.items():
  sprite=im.crop(box)
  if name in ('ok','back'):
   assert sprite.size==((64,34) if name=='ok' else (32,34))
   assert all(sprite.getpixel((x,sprite.height-1))[3]==0 or sum(sprite.getpixel((x,sprite.height-1))[:3])<60 for x in range(sprite.width))
  if name in ('selected','normal'):
   reference=out/'reference/surround.png'
   source=Image.open(reference).convert('RGBA')
   hashes[str(reference.relative_to(ROOT))]=hashlib.sha256(reference.read_bytes()).hexdigest()
   # Keep the exact 12-pixel chamfer and bevel at each end. Extend only
   # the flat middle: neither the corner pixels nor the font are stretched.
   sprite=Image.new('RGBA',(200,40))
   for y in range(40):
    for x in range(200):
     u=x if x<12 else 120-200+x if x>=188 else 18
     v=y if y<10 else 32-40+y if y>=30 else 16
     sprite.putpixel((x,y),source.getpixel((u,v)))
   # Selection is already indicated by the original star. Both states retain
   # the approved cream and gold artwork rather than inventing a gray skin.

  if name=='shortcut_plus':
   # Single-pixel strokes, same brown palette and1x grid as the value font.
   sprite=Image.new('RGBA',(7,7))
   for y in range(7):
    for x in range(7):
     if x==3 or y==3:sprite.putpixel((x,y),(141,67,0,255))
  if name in ('action','confirm','selection_star','arrow_left','arrow_right','page_down'):
   reference=out/'reference/fonts/menu-elements.png'
   hashes[str(reference.relative_to(ROOT))]=hashlib.sha256(reference.read_bytes()).hexdigest()
   boxes={'action':(14,657,254,691),'confirm':(124,900,192,937),'selection_star':(12,790,28,808),
          'arrow_left':(69,853,82,875),'arrow_right':(69,853,82,875),'page_down':(256,657,278,672)}
   sheet=Image.open(reference).convert('RGBA');key=sheet.getpixel((0,0))[:3]
   sprite=sheet.crop(boxes[name])
   sprite.putdata([(0,0,0,0) if p[:3]==key else p for p in sprite.get_flattened_data()])
   sprite=sprite.crop(sprite.getbbox())
   if name=='action':
    assert sprite.size==(240,34)
    # TOUCH TO START: retain the complete original bevel at 1x. Replace
    # only its lettered flat middle with the untouched column before the text.
    for y in range(sprite.height):
     for x in range(12,sprite.width-12):sprite.putpixel((x,y),sprite.getpixel((12,y)))
   elif name=='selection_star':
    assert sprite.size==(11,12)
    # Native11x12 selection marker; never enlarge it relative to labels.
   elif name=='confirm':assert sprite.size==(64,32)
   elif name=='page_down':assert sprite.size==(22,15)
   else:
    assert sprite.size==(10,14)
    if name=='arrow_right':sprite=ImageOps.mirror(sprite)
  sprite.save(out/(name+'.png'))
  header.append(f'inline constexpr unsigned {name}_width={sprite.width},{name}_height={sprite.height};')
  rgba=[(a<<24)|(r<<16)|(g<<8)|b for r,g,b,a in sprite.get_flattened_data()]
  header.append(f'inline constexpr uint32_t {name}[]={{'+','.join(hex(c)+'u' for c in rgba)+'};')
 header.append('}')
 (ROOT/'source/integrated_native/options/retail_pause_art.hpp').write_text('\n'.join(header)+'\n')
 (out/'manifest.json').write_text(json.dumps({'sources':hashes,'scale':'all menu controls 1x (no fractional resize); frames extend only the flat interior','crops':crops},indent=2)+'\n')
if __name__=='__main__':generate()
