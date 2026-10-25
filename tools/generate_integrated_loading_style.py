#!/usr/bin/env python3
"""Author tiny boot UI accents pixel by pixel; no Nintendo image is embedded."""
import argparse
import hashlib
import json
from pathlib import Path

def pixel(r,g,b):return 0x8000|((r>>3)<<10)|((g>>3)<<5)|(b>>3)

def generate(output):
    # Original small percent sign. Gold face, warm shadow, red/brown outline
    # follow the menu palette; the actual alphabet still comes from the ROM.
    mask=['11000000','11000001','00000010','00000100','00001000','00010000',
          '00100000','01000000','10000011','00000011']
    width,height=10,12
    percent=[0]*(width*height)
    for y in range(10):
        for x in range(8):
            if mask[y][x]!='1':continue
            for dy in (-1,0,1):
                for dx in (-1,0,1):
                    percent[(y+1+dy)*width+x+1+dx]=pixel(111,39,15)
    for y in range(10):
        for x in range(8):
            if mask[y][x]=='1':percent[(y+1)*width+x+1]=pixel(255,232 if y<4 else 189,77 if y<4 else 40)
    # A compact bevelled gold fill, stretched only in the horizontal direction.
    shades=[(255,249,185),(255,237,137),(255,218,91),(255,207,61),(250,195,43),
            (242,181,26),(234,166,22),(225,151,20),(214,137,18),(201,116,15),
            (174,89,14),(133,62,12)]
    fill=[pixel(*shades[y]) for y in range(12) for _ in range(8)]
    lines=['#pragma once','#include <cstdint>','namespace loading_style {']
    for name,pixels,w,h in [('percent',percent,width,height),('fill',fill,8,12)]:
        lines.append(f'inline constexpr unsigned {name}_width={w}, {name}_height={h};')
        lines.append(f'inline constexpr uint16_t {name}_pixels[]={{')
        lines.extend('    '+','.join(f'0x{v:04x}' for v in pixels[y*w:(y+1)*w])+',' for y in range(h))
        lines.append('};')
    lines.append('}')
    data=('\n'.join(lines)+'\n').encode('ascii')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(data)
    return dict(status='generated_original_pixel_art',bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
                copied_game_pixels=0,pixel_storage_bytes=(len(percent)+len(fill))*2)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(generate(a.output),indent=2))
