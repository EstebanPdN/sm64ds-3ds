#!/usr/bin/env python3
"""Contact sheets of the test tour's screenshots (no imaging library needed).

  python3 tools/test_mode_sheet.py SD/3ds/sm64ds/prueba OUT [--abajo] [--columnas N] [--nivel NN]

Writes OUT/NN-level.png: every report picture of that level in report order,
top screen by default. Reads both report layouts (a folder per report up to
BUILD130, flat NN-step.arriba.bmp files from BUILD131).
"""
from pathlib import Path
import argparse,struct,sys,zlib

def read_bmp(path):
    """24/32-bit uncompressed BMP -> (width, height, rows of RGB bytes, top first)."""
    data=path.read_bytes()
    if data[:2]!=b'BM':raise ValueError('not a BMP')
    offset=struct.unpack_from('<I',data,10)[0]
    width,height,planes,bits,compression=struct.unpack_from('<iiHHI',data,18)
    if bits not in(24,32) or compression not in(0,3):raise ValueError(f'unsupported BMP {bits} bits, compression {compression}')
    step=bits//8;stride=(width*step+3)&~3;flip=height>0;height=abs(height);rows=[]
    for y in range(height):
        source=offset+(height-1-y if flip else y)*stride
        line=data[source:source+width*step]
        rows.append(bytes(b for x in range(width) for b in (line[x*step+2],line[x*step+1],line[x*step])))
    return width,height,rows

def write_png(path,width,height,rows):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    raw=b''.join(b'\0'+row for row in rows)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,6))+chunk(b'IEND',b''))

def pictures(level,screen):
    flat=sorted(level.glob(f'*.{screen}.bmp'))
    if flat:return [(p.name.split('.')[0],p) for p in flat]
    return [(p.parent.name,p) for p in sorted(level.glob(f'*/pantalla_{screen}.bmp'))]

def sheet(level,out,screen,columns):
    shots=pictures(level,screen)
    if not shots:return None
    decoded=[]
    for name,path in shots:
        try:decoded.append(read_bmp(path))
        except (ValueError,struct.error,IndexError) as error:print(f'{path}: {error}',file=sys.stderr)
    if not decoded:return None
    width=max(d[0] for d in decoded);height=max(d[1] for d in decoded);gap=4
    columns=min(columns,len(decoded));lines=(len(decoded)+columns-1)//columns
    total_width=columns*width+(columns-1)*gap;rows=[]
    for line in range(lines):
        cells=decoded[line*columns:(line+1)*columns]
        for y in range(height):
            row=bytearray()
            for index in range(columns):
                if index:row+=b'\x30\x30\x30'*gap
                if index<len(cells) and y<cells[index][1]:row+=cells[index][2][y]+b'\0\0\0'*(width-cells[index][0])
                else:row+=b'\0\0\0'*width
            rows.append(bytes(row))
        if line+1<lines:rows+=[b'\x30\x30\x30'*total_width]*gap
    target=out/f'{level.name}.png';write_png(target,total_width,len(rows),rows)
    return target,len(decoded)

def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('path',type=Path);parser.add_argument('out',type=Path)
    parser.add_argument('--abajo',action='store_true',help='bottom screen instead of the top one')
    parser.add_argument('--columnas',type=int,default=6);parser.add_argument('--nivel',help='only levels whose folder starts with this')
    args=parser.parse_args()
    runs=[p for p in sorted(args.path.iterdir()) if p.is_dir() and p.name.isdigit()] or [args.path]
    args.out.mkdir(parents=True,exist_ok=True);made=0
    for run in runs:
        for level in sorted(p for p in run.iterdir() if p.is_dir()):
            if args.nivel and not level.name.startswith(args.nivel):continue
            result=sheet(level,args.out,'abajo' if args.abajo else 'arriba',args.columnas)
            if result:print(f'{result[0]}: {result[1]} imagenes');made+=1
    if not made:print('no report pictures below',args.path);return 1
    return 0

if __name__=='__main__':sys.exit(main())
