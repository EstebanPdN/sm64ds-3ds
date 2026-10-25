#!/usr/bin/env python3
"""Author the native volume slider pixel by pixel; reuse existing DS palette banks."""
import argparse,hashlib,json,struct
from pathlib import Path

def generate(output,preview=None,palette=None):
 def canvas(w,h):return [0]*(w*h)
 def put(a,w,x,y,c):a[y*w+x]=c
 track=canvas(113,16)
 for x in range(113):
  for y in range(5,11):
   edge=x in (0,112) or y in (5,10)
   put(track,113,x,y,1 if edge else 14)
  if x==56:
   for y in range(12,15):put(track,113,x,y,14)
 fill=canvas(1,4)
 for y,c in enumerate((3,4,11,12)):put(fill,1,0,y,c)
 handle=canvas(9,16)
 for y in range(16):
  for x in range(9):
   if (x in (0,8) and y in (0,15)):continue
   c=1 if x in (0,8) or y in (0,15) else 2 if y==1 or x==1 else 15 if y==14 or x==7 else 4
   if 4<=y<=11 and x in (3,5):c=14
   put(handle,9,x,y,c)
 def sign(plus):
  a=canvas(7,7)
  for y in range(7):
   for x in range(7):
    if y in (2,3,4) or (plus and x in (2,3,4)):put(a,7,x,y,1)
  for x in range(1,6):put(a,7,x,3,3)
  if plus:
   for y in range(1,6):put(a,7,3,y,3)
  return a
 minus,plus=sign(False),sign(True)
 percent=canvas(5,7)
 for y,line in enumerate(('11001','11010','00010','00100','01000','01011','10011')):
  for x,c in enumerate(line):
   if c=='1':put(percent,5,x,y,15)
 frame=canvas(192,56)
 for y in range(56):
  for x in range(192):
   if (x<2 or x>=190) and (y<2 or y>=54):continue
   c=1 if x in (0,191) or y in (0,55) else 15 if x==190 or y==54 else 12 if x==1 or y==1 else 14 if x==189 or y==53 else 10 if x==3 or y==3 else 11 if x==4 or y==4 else 8
   put(frame,192,x,y,c)
 images=[('track',113,16,track),('fill',1,4,fill),('handle',9,16,handle),('minus',7,7,minus),('plus',7,7,plus),('percent',5,7,percent),('frame',192,56,frame)]
 lines=['#pragma once','#include <cstdint>','// Original pixel art; indices select existing DS palette 1, not new global colors.','namespace native_volume_sprite {','inline constexpr unsigned tile_base=0x300,canvas_width=144,canvas_height=16;','inline constexpr int canvas_x=56,track_x=72,track_right=184,track_y=104;', 'inline constexpr unsigned frame_tile_base=0x340,frame_x=32,volume_frame_y=72,update_frame_y=56;','inline constexpr uint8_t percent_code=0x4f; // private BG0-only glyph extension; never passed to the BMG engine']
 for name,w,h,a in images:
  lines += [f'inline constexpr unsigned {name}_width={w},{name}_height={h};',f'inline constexpr uint8_t {name}[]={{']
  lines += ['    '+','.join(str(c) for c in a[y*w:(y+1)*w])+',' for y in range(h)]
  lines += ['};']
 lines+=['}'];output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(lines)+'\n')
 if preview:
  from PIL import Image,ImageDraw
  palraw=palette.read_bytes() if palette else b''
  if len(palraw)!=512:raise ValueError('preview requires original 256-color menu palette')
  colors=struct.unpack('<256H',palraw)
  def rgb(c):return tuple(((c>>s)&31)*255//31 for s in (0,5,10))
  im=Image.new('RGB',(208,192),(57,123,57));draw=ImageDraw.Draw(im)
  for row,volume in enumerate((0,100,200)):
   ybase=row*64+8;pixels=canvas(144,16)
   for yy in range(56):
    for xx in range(192):
     c=frame[yy*192+xx]
     if c:im.putpixel((xx+8,yy+ybase),rgb(colors[6*16+c]))
   def blit(a,w,h,x,y):
    for yy in range(h):
     for xx in range(w):
      c=a[yy*w+xx]
      if c and 0<=x+xx<144 and 0<=y+yy<16:pixels[(y+yy)*144+x+xx]=c
   blit(track,113,16,16,0)
   end=16+volume*112//200
   for x in range(17,end):blit(fill,1,4,x,6)
   blit(minus,7,7,0,4);blit(plus,7,7,137,4);blit(handle,9,16,end-4,0)
   for y in range(16):
    for x in range(144):
     c=pixels[y*144+x]
     if c:im.putpixel((x+32,y+ybase+32),rgb(colors[16+c]))
  preview.parent.mkdir(parents=True,exist_ok=True);im.resize((624,576),Image.Resampling.NEAREST).save(preview)
 return dict(format='DS4bpp palette indices',palette_bank=1,default_percent=100,range=[0,200],header_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),copied_game_pixels=0)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--preview',type=Path);p.add_argument('--palette',type=Path);a=p.parse_args();print(json.dumps(generate(a.output,a.preview,a.palette),indent=2))
