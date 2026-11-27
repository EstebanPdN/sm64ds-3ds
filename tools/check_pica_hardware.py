#!/usr/bin/env python3
"""Find the GPU state of a captured frame that the console has never run.

    check_pica_hardware.py SUSPECT [--proven DIR ...]

SUSPECT is a comandos_gpu.bin (or a folder holding one) captured in Azahar,
for example prueba/01/jugador/03-seleccion-gpu. Azahar draws command lists
that hang the real PICA200, so an emulator run cannot prove a list safe.
The console can: every comandos_gpu.bin under a --proven folder (dumps the
console wrote, such as dumps/prueba133, or Azahar captures of screens that
run on the console) is a list the hardware executed.

For every draw of SUSPECT the script replays the register file and reports
1. hard-rule violations (zero vertex counts, misaligned or out-of-range
   addresses, texture sizes the sampler cannot take, inconsistent attribute
   counts), and
2. configuration registers whose value at a draw never appears at any draw
   of the proven lists (addresses, uniforms and data ports excluded).
Exit status: 0 when nothing is reported, 1 otherwise.
"""
from pathlib import Path
import argparse,collections,sys

sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_mode_dump import NAMES,decode_commands,read_commands  # noqa: E402

# Registers that carry data or addresses rather than configuration.
PORTS=(set(range(0x2c0,0x2c9))|set(range(0x2cb,0x2d4))|set(range(0x29b,0x2a4))|set(range(0x2d5,0x2de))|set(range(0x232,0x236))
       |{0x22e,0x22f,0x231,0x111,0x110,0x010,0x063,0x1c8,0x1c9,0x1ca,0x1cb,0x1cc,0x1cd,0x1ce,0x1cf}|set(range(0x1d0,0x1d8)))
ADDRESSES={0x085,0x086,0x087,0x088,0x089,0x08a,0x08b,0x08c,0x095,0x09d,0x11c,0x11d,0x200,0x203,0x206,0x209,0x20c,0x20f,
           0x212,0x215,0x218,0x21b,0x21e,0x221,0x227}
COUNTS={0x228,0x22a,0x065,0x066,0x067}  # vertex count, first index and scissor rectangle vary per draw by nature
SKIP=PORTS|ADDRESSES|COUNTS

def replay(path):
    capture=read_commands(path)
    regs=[0]*0x300;draws=[]
    for at,reg,values,mask,consecutive in decode_commands(capture['words']):
        for k,value in enumerate(values):
            r=reg+(k if consecutive else 0)
            if r>=0x300:continue
            if r in (0x22e,0x22f) and k==0:draws.append((at,r,list(regs)))
            full=0
            for b in range(4):
                if mask&(1<<b):full|=0xff<<(8*b)
            regs[r]=(regs[r]&~full)|(value&full)
    return draws

def pot(n):return n>=8 and n<=1024 and n&(n-1)==0

def hard_rules(regs,kind):
    problems=[]
    if regs[0x228]==0:problems.append('num_vertices=0')
    if kind==0x22f:
        index=regs[0x227]
        if (index&0x0fffffff)%2:problems.append(f'indices desalineados 0x{index:08x}')
    attr_base=regs[0x200]
    if attr_base%16:problems.append(f'base_atributos no alineada a 16: 0x{attr_base:08x}')
    total_attrs=((regs[0x202]>>28)&0xf)+1
    vsh_attrs=(regs[0x242]&0xf)+1
    if total_attrs!=vsh_attrs:problems.append(f'atributos: formato={total_attrs} vs 0x242={vsh_attrs}')
    vsh_in=(regs[0x2b9]&0xf)+1
    if vsh_in!=total_attrs:problems.append(f'atributos: shader entradas (0x2b9)={vsh_in} vs formato={total_attrs}')
    units=regs[0x080]
    for unit,(size_reg,addr_reg,fmt_reg) in enumerate(((0x082,0x085,0x08e),(0x092,0x095,0x096),(0x09a,0x09d,0x09e))):
        if not units&(1<<unit):continue
        size=regs[size_reg];height,width=size&0x7ff,(size>>16)&0x7ff
        if not pot(width) or not pot(height):problems.append(f'textura {unit} tamano {width}x{height} (debe ser potencia de 2 entre 8 y 1024)')
        if regs[addr_reg]==0:problems.append(f'textura {unit} sin direccion')
        if (regs[fmt_reg]&0xf)>13:problems.append(f'textura {unit} formato {regs[fmt_reg]&0xf} invalido')
    # A colour-only pass has no depth buffer (0x11c=0): the console runs those.
    if regs[0x11d]==0:problems.append('direccion_color=0')
    return problems

def files(target):
    target=Path(target)
    return [target] if target.is_file() else sorted(target.rglob('comandos_gpu.bin'))

def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('suspect');parser.add_argument('--proven',action='append',default=[])
    args=parser.parse_args()
    seen=collections.defaultdict(set);proven_draws=0
    for folder in args.proven:
        for path in files(folder):
            for _,_,regs in replay(path):
                proven_draws+=1
                for r in range(0x300):
                    if r not in SKIP:seen[r].add(regs[r])
    reported=0
    for path in files(args.suspect):
        print(f'== {path} (comparado con {proven_draws} dibujos probados en consola)')
        for n,(at,kind,regs) in enumerate(replay(path)):
            lines=[f'regla: {p}' for p in hard_rules(regs,kind)]
            if proven_draws:
                for r in range(0x300):
                    if r in SKIP or regs[r] in seen[r]:continue
                    known=sorted(seen[r])[:6]
                    lines.append(f'nuevo: 0x{r:03x} {NAMES.get(r,"")} = 0x{regs[r]:08x} (en consola: {", ".join(f"0x{v:x}" for v in known)}{"..." if len(seen[r])>6 else ""})')
            if lines:
                reported+=1
                print(f'  dibujo {n} ({"elementos" if kind==0x22f else "arrays"}, palabra {at}, vertices {regs[0x228]}):')
                for line in lines:print('    '+line)
    print(f'dibujos con algo que senalar: {reported}')
    return 1 if reported else 0

if __name__=='__main__':sys.exit(main())
