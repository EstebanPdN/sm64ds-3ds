"""Original 320x240 DS loading-screen painter, scaled only with nearest pixels."""
from pathlib import Path
import json
from PySide6.QtCore import QRect
from PySide6.QtGui import QImage, QPainter, QColor


class PixelArt:
    def __init__(self):
        root = Path(__file__).with_name('assets')
        self.images = {name: QImage(str(root / (name + '.png'))) for name in
                       ('pattern', 'star', 'frame', 'title_font', 'body_font', 'fill', 'percent')}
        if any(image.isNull() for image in self.images.values()):
            raise ValueError('Missing loading screen artwork')
        self.ink_cache = {}
        self.fonts = json.loads((root / 'fonts.json').read_text())
        self.frame = self.images['frame'].copy(self.images['frame'].createAlphaMask().rect())
        # Original 24x24 tile contains transparent margins; crop their exact bounds.
        points = [(x, y) for y in range(self.frame.height()) for x in range(self.frame.width())
                  if self.frame.pixelColor(x, y).alpha()]
        left, top = min(x for x,y in points), min(y for x,y in points)
        right, bottom = max(x for x,y in points), max(y for x,y in points)
        self.frame = self.frame.copy(left, top, right-left+1, bottom-top+1)
    def advance(self, char, title=False):
        code = ord(char) if ord(char) < 256 else ord('?')
        return self.fonts['advances16' if title else 'advances8'][code] or (16 if title else 8)
    def ink_bounds(self, text, title=False):
        """Opaque glyph bounds after applying advances; excludes blank cell margins."""
        image = self.images['title_font' if title else 'body_font']
        cw = 16 if title else 8
        cursor = 0
        result = None
        for char in text:
            code = ord(char) if ord(char) < 256 else ord('?')
            cell = self.fonts['glyphs'][code]
            if cell == 65535 and char != ' ':
                cell = self.fonts['glyphs'][ord('?')]
            key = (title, cell, char == '%')
            if key not in self.ink_cache:
                glyph = self.images['percent'] if char == '%' else image.copy(cell%16*cw,cell//16*16,cw,16) if cell != 65535 else QImage()
                pixels = [(x,y+(2 if char=='%' else 0)) for y in range(glyph.height()) for x in range(glyph.width()) if glyph.pixelColor(x,y).alpha()]
                self.ink_cache[key] = ((min(x for x,y in pixels),min(y for x,y in pixels),max(x for x,y in pixels)+1,max(y for x,y in pixels)+1) if pixels else None)
            box = self.ink_cache[key]
            if box:
                left,top,right,bottom = box
                box = (cursor+left,top,cursor+right,bottom)
                if result:
                    result = (min(result[0],box[0]),min(result[1],box[1]),max(result[2],box[2]),max(result[3],box[3]))
                else:
                    result = box
            cursor += self.advance(char,title)
        return result or (0,0,0,0)
    def width(self, text, title=False):
        left,top,right,bottom = self.ink_bounds(text,title)
        return right-left
    def text(self, painter, text, x, y, title=False, centered=False, center_y=None):
        if centered:
            left,top,right,bottom = self.ink_bounds(text,title)
            x -= (left+right)//2
        if center_y is not None:
            left,top,right,bottom = self.ink_bounds(text,title)
            y = center_y-(top+bottom)//2
        image = self.images['title_font' if title else 'body_font']
        cell_width = 16 if title else 8
        for char in text:
            code = ord(char) if ord(char) < 256 else ord('?')
            cell = self.fonts['glyphs'][code]
            if char == '%':
                painter.drawImage(x, y+2, self.images['percent'])
            elif cell != 65535:
                painter.drawImage(QRect(x, y, cell_width, 16), image,
                                  QRect(cell % 16 * cell_width, cell // 16 * 16, cell_width, 16))
            elif char != ' ':
                cell = self.fonts['glyphs'][ord('?')]
                painter.drawImage(QRect(x, y, cell_width, 16), image,
                                  QRect(cell % 16 * cell_width, cell // 16 * 16, cell_width, 16))
            x += self.advance(char, title)
    def fit(self, text, width, title=False):
        if self.width(text, title) <= width:
            return text
        while text and self.width(text + '...', title) > width:
            text = text[:-1]
        return text + '...'
    def panel(self, painter, x, y, width, height):
        f = self.frame
        fw, fh = f.width(), f.height()
        corner = min(fw, fh) // 3
        c = min(corner, 8)
        for dx,dy,dw,dh,sx,sy,sw,sh in (
            (x+c,y+c,width-2*c,height-2*c,corner,corner,fw-2*corner,fh-2*corner),
            (x+c,y,width-2*c,c,corner,0,fw-2*corner,corner),
            (x+c,y+height-c,width-2*c,c,corner,fh-corner,fw-2*corner,corner),
            (x,y+c,c,height-2*c,0,corner,corner,fh-2*corner),
            (x+width-c,y+c,c,height-2*c,fw-corner,corner,corner,fh-2*corner),
            (x,y,c,c,0,0,corner,corner),
            (x+width-c,y,c,c,fw-corner,0,corner,corner),
            (x,y+height-c,c,c,0,fh-corner,corner,corner),
            (x+width-c,y+height-c,c,c,fw-corner,fh-corner,corner,corner)):
            painter.drawImage(QRect(dx,dy,dw,dh), f, QRect(sx,sy,sw,sh))
    def render(self, phase='DROP ROM HERE', percent=0, busy=False, ready=False, filename='', cancelled=False):
        image = QImage(320, 240, QImage.Format.Format_ARGB32)
        image.fill(QColor('#79bdf7'))
        painter = QPainter(image)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
        pattern = self.images['pattern']
        for y in range(0,240,pattern.height()):
            for x in range(0,320,pattern.width()):
                painter.drawImage(x,y,pattern)
        painter.drawImage(QRect(144,8,32,32),self.images['star'])
        self.text(painter,'SM64DS PACKER',160,50,True,True,center_y=58)
        self.panel(painter,28,82,264,34)
        self.text(painter,self.fit(phase.upper(),240,True),160,91,True,True,center_y=99)
        hint = 'Almost there!' if busy and percent > 90 else 'Preparing your game...' if busy else 'Your game data is ready!' if ready else 'Drop a ROM or choose below.'
        if cancelled:
            hint = 'Cancelled safely. Try again!'
        self.text(painter,hint,160,122,False,True,center_y=130)
        self.panel(painter,28,146,264,28)
        painter.fillRect(36,154,248,12,QColor(126,80,25))
        if percent:
            painter.drawImage(QRect(36,154,248*min(100,percent)//100,12),self.images['fill'])
        self.text(painter,str(percent)+'%',160,152,True,True,center_y=160)
        detail = 'Copy to SD:/3ds/sm64ds/' if ready else 'Original European ROM required' if not busy else filename
        self.text(painter,self.fit(detail,288),160,182,False,True,center_y=190)
        self.panel(painter,80,204,160,28)
        self.text(painter,'CANCEL' if busy else 'CHOOSE ROM',160,210,True,True,center_y=218)
        painter.end()
        return image
