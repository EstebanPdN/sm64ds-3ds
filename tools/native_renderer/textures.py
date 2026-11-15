"""Host-only source texture decoding shared by model and particle preparation.

Native runtime consumers receive tiled native pixels; none of this is linked
into the game. Containers supply explicit bounded spans and source semantics.
"""
import struct

try:
    from .audit_models import SourceError
    from .bank import dimensions
except ImportError:
    from audit_models import SourceError
    from bank import dimensions


def tex_rgb(colour):
    # Preserve the existing 3D texture's 5->6->8-bit expansion. This differs
    # from direct vertex colours, which have their own widening rule.
    def channel(v):
        six=2*v+1 if v else 0
        return (six<<2)|(six>>4)
    return tuple(channel((colour>>shift)&31) for shift in (0,5,10))


def decode_texture(source, palette, width, height, fmt, *, zero_transparent=False,
                   retain_zero_rgb=False, descriptor=None):
    if not dimensions(width,height):
        raise SourceError('Texture dimension bounds')
    if len(palette)%2:
        raise SourceError('Odd palette length')
    if not isinstance(zero_transparent,bool) or not isinstance(retain_zero_rgb,bool):
        raise SourceError('Texture alpha policy must be boolean')
    w,h=width,height
    pixels=w*h
    colours=[v[0] for v in struct.iter_unpack('<H',palette)]
    def colour(index,alpha=255):
        if not 0<=index<len(colours):
            raise SourceError('Texture references missing palette colour')
        return (*tex_rgb(colours[index]),alpha)
    rgba=bytearray(pixels*4)
    if fmt==5:
        if len(source)!=pixels//4:
            raise SourceError('Invalid compressed 4x4 payload size')
        if descriptor is None or len(descriptor)!=pixels//8:
            raise SourceError('Invalid compressed block descriptor size')
        def mixed(a,b,wa,wb):
            return sum(((((a>>s)&31)*wa+((b>>s)&31)*wb)//8)<<s for s in (0,5,10))
        for block in range(pixels//16):
            bits=struct.unpack_from('<I',source,block*4)[0]
            info=struct.unpack_from('<H',descriptor,block*2)[0]
            base,mode=(info&0x3fff)*2,info>>14
            needed=3 if mode==0 else 4 if mode==2 else 2
            if base+needed>len(colours):
                # PLAYABLE MODE approximation: DS reads neighbouring palette
                # VRAM here; pad with black instead of refusing the model.
                import os
                if os.environ.get('SM64DS_COOK_APPROXIMATE')!='1':raise SourceError('Compressed block palette outside source')
                colours.extend([0]*(base+needed-len(colours)))
            a,b=colours[base:base+2]
            extra=([colours[base+2],None] if mode==0 else
                   [mixed(a,b,4,4),None] if mode==1 else
                   colours[base+2:base+4] if mode==2 else [mixed(a,b,5,3),mixed(a,b,3,5)])
            choices=[a,b,*extra]
            bx,by=block%(w//4),block//(w//4)
            for texel in range(16):
                c=choices[(bits>>(texel*2))&3]
                value=(0,0,0,0) if c is None else (*tex_rgb(c),255)
                at=((by*4+texel//4)*w+bx*4+texel%4)*4
                rgba[at:at+4]=bytes(value)
    elif fmt in (1,2,3,4,6,7):
        need=(pixels*{1:8,2:2,3:4,4:8,6:8,7:16}[fmt]+7)//8
        if len(source)!=need:
            raise SourceError('Texture byte count does not match dimensions')
        for i in range(pixels):
            if fmt==7:
                c=struct.unpack_from('<H',source,i*2)[0]
                value=(*tex_rgb(c),255) if c&0x8000 else (0,0,0,0)
            elif fmt in (1,6):
                b=source[i]
                value=colour(b&(31 if fmt==1 else 7),(b>>(5 if fmt==1 else 3))*255//(7 if fmt==1 else 31))
            else:
                bits={2:2,3:4,4:8}[fmt]
                index=(source[i*bits//8]>>((i*bits)&7))&((1<<bits)-1)
                value=(colour(index,0) if retain_zero_rgb else (0,0,0,0)) if index==0 and zero_transparent else colour(index)
            rgba[i*4:i*4+4]=bytes(value)
    else:
        raise SourceError(f'Texture format {fmt}')
    return bytes(rgba)
