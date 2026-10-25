#!/usr/bin/env python3
"""Pixel-authored 24x24 nine-slice for the DS-style telemetry cards."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
# No antialiasing: each pixel has an explicit palette role.
P=[0x0000,0x8000,0xfaa0,0xa500,0xf640,0xc540,0xf220,0xffb6]
pixels=[]
for y in range(24):
    for x in range(24):
        edge=min(x,y,23-x,23-y)
        role=1 if edge==0 else 7 if edge==1 and x+y<24 else 3 if edge==1 else 4 if edge==2 and x+y<24 else 5 if edge<4 else 6
        if (x<2 and y<2) or (x>21 and y<2) or (x<2 and y>21) or (x>21 and y>21):role=0
        pixels.append(P[role])
out=ROOT/'source/integrated_native/options/diagnostics_skin.hpp'
out.write_text('#pragma once\n#include <cstdint>\nnamespace diagnostics_skin {\ninline constexpr uint16_t frame[576]={\n'+''.join(','.join(hex(n) for n in pixels[y*24:y*24+24])+',\n' for y in range(24))+'};\n}\n')
try:
    from PIL import Image
    im=Image.new('RGBA',(24,24));im.putdata([(((c>>10)&31)*255//31,((c>>5)&31)*255//31,(c&31)*255//31,255 if c&0x8000 else 0) for c in pixels]);im.resize((240,240),Image.Resampling.NEAREST).save(ROOT/'assets/ui/diagnostics/frame-preview.png')
except ImportError:pass
print(out)
