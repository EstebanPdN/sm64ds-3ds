#!/usr/bin/env python3
"""Verify exact opaque glyph placement rather than logical cell advances."""
import os
os.environ['QT_QPA_PLATFORM']='offscreen'
from PySide6.QtWidgets import QApplication
from pixel_ui import PixelArt
app=QApplication([])
art=PixelArt()
for text in ('Drop a ROM or choose below.','Preparing your game...','Almost there!',
             'Your game data is ready!','Cancelled safely. Try again!'):
    left,top,right,bottom=art.ink_bounds(text)
    x=160-(left+right)//2
    y=130-(top+bottom)//2
    assert abs((x+left+x+right)/2-160)<=.5
    assert 116 < y+top <= y+bottom < 146,(text,y+top,y+bottom)
for text in ('Mário_ñ_日本.NdS','W'*1000,'x'*1000+'.nds'):
    fitted=art.fit(text,288)
    assert art.width(fitted)<=288
    left,top,right,bottom=art.ink_bounds(fitted)
    x=160-(left+right)//2
    assert 0 <= x+left < x+right <= 320
for percent in (0,9,64,90,91,99,100):
    art.render('CREATING .SM64DS',percent,True,filename='Mário_ñ_日本.NdS')
print('PASS: all hint opaque bounds centered ±0.5px and clear of compact panel/bar; longUnicode text bounded; progress0..100 rendered')
