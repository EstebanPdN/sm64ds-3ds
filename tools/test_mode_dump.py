#!/usr/bin/env python3
"""Read a deep dump of the SELECT+START test mode (BUILD131+).

Usage: python3 tools/test_mode_dump.py PATH [--dibujos] [--comandos OUT.txt]

PATH is a dump folder (LL-level/volcado or volcado-zonaN), a level folder or a
whole run; every dump below it is summarized. A dump holds:

  comandos_gpu.bin  one frame's PICA200 command list and a record per draw
  dibujos.csv       the same draw records as text
  escena.json       the semantic scene the renderer drew (parts, materials)
  texturas.csv      the renderer's textures and render targets
  gpu_estado.txt    renderer counters, GPU memory, the captured frame
  gpu_registros.bin GSP reads: display controllers, transfer engine and,
                    when present, the PICA internal registers
  vram.bin          the 6 MiB of VRAM (level dumps)
  ds_video.bin      DS I/O registers, palettes, OAM and VRAM windows
  planos_2d.bin     the three 256x192 planes the 2D bridge composed last
  actores.csv       every live actor of the level
  memoria.vm        whole process memory (first level, or prueba/ram.txt):
                    read it with tools/inspect_integrated_vm.py

Nothing here measures speed: the command list says what the CPU sent to the
GPU for one image (how many words, state changes and uniform uploads per
draw), which is the cost the renderer can still cut.
"""
from pathlib import Path
import argparse,collections,csv,struct,sys

# PICA200 internal registers (3dbrew GPU/Internal_Registers), grouped for totals.
GROUPS=[(0x000,0x03f,'varios'),(0x040,0x07f,'rasterizador'),(0x080,0x0ff,'texturas y TEV'),(0x100,0x13f,'framebuffer y fragmentos'),
        (0x140,0x1ff,'luces de fragmento'),(0x200,0x227,'atributos y bufferes'),(0x228,0x27f,'dibujo'),(0x280,0x2af,'shader de geometria'),
        (0x2b0,0x2df,'shader de vertices'),(0x2e0,0x2ff,'otros')]
NAMES={0x010:'finalizar',0x040:'cara',0x041:'viewport_ancho',0x043:'viewport_alto',0x04d:'depth_scale',0x04e:'depth_offset',0x06e:'viewport_xy',
       0x080:'unidades_textura',0x082:'tex0_tamano',0x083:'tex0_parametros',0x085:'tex0_direccion',0x08e:'tex0_formato',
       0x0c0:'tev0_fuente',0x0c8:'tev1_fuente',0x100:'modo_color',0x101:'mezcla',0x103:'color_mezcla',0x104:'prueba_alfa',0x105:'stencil',
       0x107:'profundidad_y_mascara',0x110:'vaciar_framebuffer',0x111:'limpiar_cache_framebuffer',0x116:'formato_profundidad',0x117:'formato_color',
       0x11c:'direccion_profundidad',0x11d:'direccion_color',0x11e:'tamano_framebuffer',0x200:'base_atributos',0x201:'formatos_bajo',0x202:'formatos_alto',
       0x227:'indices',0x228:'num_vertices',0x22a:'desplazamiento_vertices',0x22e:'dibujar_arrays',0x22f:'dibujar_elementos',0x231:'limpiar_post_vertice',
       0x245:'modo_primitiva',0x25e:'primitiva',0x25f:'reiniciar_primitiva',0x2b0:'vsh_booleanos',0x2ba:'vsh_entrada',0x2bf:'vsh_iniciar_codigo',
       0x2c0:'vsh_uniforme_indice',0x2c1:'vsh_uniforme_datos',0x2cb:'vsh_codigo_indice',0x2cc:'vsh_codigo_datos',0x2d5:'vsh_operandos_indice',0x2d6:'vsh_operandos_datos'}
TEX_FORMATS=['RGBA8','RGB8','RGBA5551','RGB565','RGBA4','LA8','HILO8','L8','A8','LA4','L4','A4','ETC1','ETC1A4']

def group(reg):
    for low,high,name in GROUPS:
        if low<=reg<=high:return name
    return 'fuera de rango'

def decode_commands(words):
    """Yield (word_index, register, values, mask) for every command."""
    i=0
    while i+1<len(words):
        parameter,header=words[i],words[i+1]
        reg,mask,extra,consecutive=header&0xffff,(header>>16)&0xf,(header>>20)&0xff,header>>31
        values=[parameter]+list(words[i+2:i+2+extra])
        yield i,reg,values,mask,consecutive
        n=2+extra;i+=n+(n&1)

def read_commands(path):
    data=path.read_bytes()
    magic,version,header_bytes,words,total,draws,draws_total,draw_bytes,passes=struct.unpack_from('<8s8I',data)
    if magic!=b'SM64PICA' or version!=1:raise SystemExit(f'{path}: not a BUILD131 command capture')
    fields='<5I2H6BH';assert struct.calcsize(fields)==draw_bytes,(struct.calcsize(fields),draw_bytes)
    records=[struct.unpack_from(fields,data,header_bytes+i*draw_bytes) for i in range(draws)]
    start=header_bytes+draws*draw_bytes
    return {'words':list(struct.unpack_from(f'<{words}I',data,start)),'total':total,'draws':records,'draws_total':draws_total,'passes':passes}

def command_summary(capture,lines,listing=None):
    words=capture['words'];per_group=collections.Counter();writes=collections.Counter();regs=[0]*0x300
    draws=[];since=collections.Counter();uniform_words=code_words=0;problems=[]
    for index,reg,values,mask,consecutive in decode_commands(words):
        for k,value in enumerate(values):
            r=reg+k if consecutive else reg
            if r>=0x300:problems.append(f'palabra {index}: registro {r:#x} fuera de rango');continue
            full=sum(0xff<<(8*b) for b in range(4) if mask>>b&1);regs[r]=(regs[r]&~full)|(value&full)
            per_group[group(r)]+=1;writes[r]+=1;since[group(r)]+=1
            if r in(0x2c1,0x2c2,0x2c3,0x2c4,0x2c5,0x2c6,0x2c7,0x2c8):uniform_words+=1
            if r in(0x2cc,0x2cd,0x2ce,0x2cf,0x2d0,0x2d1,0x2d2,0x2d3,0x29c):code_words+=1
            if r in(0x22e,0x22f) and value&1:
                tex=regs[0x080]&1;size=regs[0x082];fmt=regs[0x08e]&0xf
                draws.append({'palabra':index,'tipo':'elementos' if r==0x22f else 'arrays','vertices':regs[0x228],'textura':tex,
                    'tex_ancho':size>>16,'tex_alto':size&0x7ff,'tex_formato':TEX_FORMATS[fmt] if fmt<len(TEX_FORMATS) else fmt,
                    'atributos':(regs[0x202]>>28)+1,'cambios':dict(since)})
                if tex and (size>>16<8 or size&0x7ff<8 or (size>>16)&((size>>16)-1) or (size&0x7ff)&((size&0x7ff)-1)):
                    problems.append(f"dibujo {len(draws)-1}: textura {size>>16}x{size&0x7ff}")
                if not regs[0x228]:problems.append(f'dibujo {len(draws)-1}: 0 vertices')
                since=collections.Counter()
        if listing is not None:
            listing.append(f"{index:6d}  {reg:03x} {NAMES.get(reg,''):24s} mascara={mask:x} {'consecutivo ' if consecutive else ''}"+' '.join(f'{v:08x}' for v in values[:8])+(' ...' if len(values)>8 else ''))
    truncated=capture['total']>len(words)
    lines.append(f"- lista de comandos: {len(words)} palabras ({len(words)*4/1024:.1f} KiB){' TRUNCADA de '+str(capture['total']) if truncated else ''}, "
                 f"{capture['passes']} pasadas, {len(draws)} dibujos en la lista, {capture['draws_total']} registrados por el render")
    if draws:
        lines.append(f"- por dibujo: {len(words)/len(draws):.0f} palabras, {uniform_words/len(draws):.1f} palabras de uniformes float; "
                     f"codigo de shader reenviado: {code_words} palabras")
    lines.append('- escrituras por grupo: '+', '.join(f'{name} {count}' for name,count in per_group.most_common()))
    lines.append('- registros mas escritos: '+', '.join(f"{NAMES.get(r,hex(r))} {c}" for r,c in writes.most_common(8)))
    if draws:
        heavy=sorted(draws,key=lambda d:-sum(d['cambios'].values()))[:5]
        lines.append('- dibujos con mas estado previo: '+'; '.join(f"#{draws.index(d)} ({sum(d['cambios'].values())} escrituras, {d['vertices']} vertices)" for d in heavy))
    lines+=[f'- AVISO {p}' for p in problems[:20]]
    return draws

def draw_records(capture,lines,detail):
    records=capture['draws']
    if not records:return
    keys=['malla','textura','primer','indices','palabra','ancho','alto','formato','banderas','mezcla','cara','profundidad','pasada','corte']
    rows=[dict(zip(keys,r)) for r in records]
    triangles=sum(r['indices']//3 for r in rows);lit=sum(r['banderas']&1 for r in rows);batch=sum(r['banderas']>>1&1 for r in rows)
    textures=len({r['textura'] for r in rows if r['textura']!=0xffffffff});meshes=len({r['malla'] for r in rows})
    switches=sum(1 for a,b in zip(rows,rows[1:]) if a['textura']!=b['textura']);mesh_switches=sum(1 for a,b in zip(rows,rows[1:]) if a['malla']!=b['malla'])
    lines.append(f"- dibujos del render: {len(rows)} ({triangles} triangulos; {lit} iluminados, {batch} en lote), {meshes} mallas, {textures} texturas, "
                 f"{switches} cambios de textura y {mesh_switches} de malla entre dibujos seguidos")
    small=sum(1 for r in rows if r['indices']<=12)
    lines.append(f"- dibujos de 4 triangulos o menos: {small} de {len(rows)} (candidatos a agrupar)")
    if detail:
        lines.append('');lines.append('| # | pasada | malla | textura | tam | indices | luz | lote | mezcla | prof |');lines.append('|---|---|---|---|---|---|---|---|---|---|')
        for i,r in enumerate(rows):
            lines.append(f"| {i} | {r['pasada']} | {r['malla']} | {'-' if r['textura']==0xffffffff else r['textura']} | {r['ancho']}x{r['alto']} | {r['indices']} | "
                         f"{r['banderas']&1} | {r['banderas']>>1&1} | {r['mezcla']} | {r['profundidad']} |")

def registers(path,lines):
    data=path.read_bytes()
    magic,version,header_bytes,blocks,chunk,failed,internal=struct.unpack_from('<8s6I',data)
    if magic!=b'SM64GPUR':return
    at=header_bytes;regs={}
    for _ in range(blocks):
        offset,size,bad=struct.unpack_from('<3I',data,at);at+=12
        for i in range(0,size,4):regs[offset+i]=struct.unpack_from('<I',data,at+i)[0]
        at+=size
    lines.append(f"- registros GPU: {blocks} bloques, {failed} lecturas fallidas, internos PICA {'incluidos' if internal else 'no leidos'}")
    for name,base in(('superior',0x400400),('inferior',0x400500)):
        if base+0x70 in regs:
            fmt=regs[base+0x70];size=regs.get(base+0x5c,0)
            lines.append(f"  - pantalla {name}: {size>>16}x{size&0xffff}, formato {fmt&7}, framebuffers {regs.get(base+0x68,0):08x}/{regs.get(base+0x6c,0):08x}, activo {regs.get(base+0x78,0)&1}")
    if internal and 0x401000+0x11e*4 in regs:
        g=lambda r:regs.get(0x401000+r*4,0)
        lines.append(f"  - PICA: framebuffer color {g(0x11d)*8:08x} profundidad {g(0x11c)*8:08x} tamano {g(0x11e)&0x7ff}x{(g(0x11e)>>12&0x3ff)+1}, "
                     f"formato color {g(0x117)&7} profundidad {g(0x116)&3}, cara {g(0x040)&3}, prueba alfa {g(0x104):08x}, mezcla {g(0x101):08x}")

SIZES=(((8,8),(16,16),(32,32),(64,64)),((16,8),(32,8),(32,16),(64,32)),((8,16),(8,32),(16,32),(32,64)),((8,8),)*4)
def sprites(oam,base):
    """OAM entries of one engine that overlap its 256x192 screen."""
    out=[]
    for i in range(128):
        if len(oam)<base+i*8+8:break
        a0,a1,a2=struct.unpack_from('<3H',oam,base+i*8)
        affine=bool(a0&0x100);double=affine and bool(a0&0x200)
        if not affine and a0&0x200:continue  # disabled
        w,h=SIZES[a0>>14][a1>>14];y=a0&0xff;x=a1&0x1ff
        if y>=192:y-=256
        if x>=256:x-=512
        bw,bh=(w*2,h*2) if double else (w,h)
        if x+bw<=0 or y+bh<=0 or x>=256 or y>=192:continue
        out.append(dict(i=i,x=x,y=y,w=w,h=h,tile=a2&0x3ff,palette=a2>>12,priority=(a2>>10)&3,mode=(a0>>10)&3,affine=affine,double=double,
                        hflip=not affine and bool(a1&0x1000),vflip=not affine and bool(a1&0x2000),colors256=bool(a0&0x2000)))
    return out

def write_png(path,width,height,words):
    """ARGB words to an RGBA PNG (no imaging library needed)."""
    import zlib
    raw=bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            v=words[y*width+x];raw+=bytes(((v>>16)&255,(v>>8)&255,v&255,v>>24))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw),6))+chunk(b'IEND',b''))

def planes(path,lines,out):
    data=path.read_bytes()
    magic,version,width,height,count,valid=struct.unpack_from('<8s5I',data)
    if magic!=b'SM64PL2D':return
    names=('detras-del-3d','delante-del-3d','otra-pantalla');parts=[]
    for k in range(count):
        words=struct.unpack_from(f'<{width*height}I',data,28+k*width*height*4)
        used=sum(1 for v in words if v>>24);rows=[y for y in range(height) if any(words[y*width+x]>>24 for x in range(width))]
        parts.append(f"{names[k]} {'valido' if valid>>k&1 else 'sin usar'} {used} px"+(f" filas {rows[0]}-{rows[-1]}" if rows else ''))
        if out is not None:
            target=out/f"{path.parent.parent.name}-{path.parent.name}-{names[k]}.png";write_png(target,width,height,words)
    lines.append('- planos 2D del puente: '+'; '.join(parts)+(f' (PNG en {out})' if out is not None else ''))

def ds_video(path,lines,detail=False):
    data=path.read_bytes()
    magic,version,count=struct.unpack_from('<8s2I',data)
    if magic!=b'SM64DSV1':return
    table=[struct.unpack_from('<2I',data,16+8*i) for i in range(count)];at=16+8*count;memory={}
    for address,size in table:memory[address]=data[at:at+size];at+=size
    io=memory.get(0x04000000,b'')
    for engine,base in(('A',0),('B',0x1000)):
        if len(io)>=base+4:
            dispcnt=struct.unpack_from('<I',io,base)[0]
            lines.append(f"  - motor 2D {engine}: DISPCNT {dispcnt:08x} modo {dispcnt&7} capas BG {(dispcnt>>8)&0xf:04b} OBJ {(dispcnt>>12)&1} ventanas {(dispcnt>>13)&7:03b} 3D {(dispcnt>>3)&1 if engine=='A' else '-'}")
    oam=memory.get(0x07000000,b'')
    for engine,base in(('A',0),('B',0x400)):
        shown=sprites(oam,base)
        lines.append(f"  - OAM {engine}: {len(shown)} sprites en pantalla de 128")
        if detail:
            for s in shown:
                lines.append(f"    - #{s['i']:3d} x={s['x']:4d} y={s['y']:4d} {s['w']}x{s['h']} tile={s['tile']:4d} paleta={s['palette']:2d} prioridad={s['priority']} "
                             f"modo={('normal','semitransparente','ventana','bitmap')[s['mode']]}{' afin' if s['affine'] else ''}{' doble' if s['double'] else ''}"
                             f"{' volteoH' if s['hflip'] else ''}{' volteoV' if s['vflip'] else ''}{' 256col' if s['colors256'] else ''}")
    used=sum(1 for address,size in table if address>=0x06000000 for i in range(0,size,4096) if any(memory[address][i:i+4096]))
    lines.append(f"  - VRAM DS: {used*4} KiB con datos en las ventanas mapeadas")

def dump(folder,args,lines):
    lines+=['',f'### {folder}','']
    capture=None
    if (folder/'comandos_gpu.bin').exists():
        capture=read_commands(folder/'comandos_gpu.bin');listing=[] if args.comandos else None
        command_summary(capture,lines,listing);draw_records(capture,lines,args.dibujos)
        if listing is not None:
            out=Path(str(args.comandos).replace('.txt','')+'-'+folder.parent.name+'-'+folder.name+'.txt');out.write_text('\n'.join(listing)+'\n')
            lines.append(f'- listado de comandos: {out}')
    else:lines.append('- sin comandos_gpu.bin (el cuadro no se capturo)')
    if (folder/'gpu_registros.bin').exists():registers(folder/'gpu_registros.bin',lines)
    if (folder/'texturas.csv').exists():
        with (folder/'texturas.csv').open(newline='') as f:rows=list(csv.DictReader(f))
        total=sum(int(r['bytes']) for r in rows);vram=sum(int(r['bytes']) for r in rows if r['vram']=='1')
        lines.append(f"- texturas: {len(rows)} ({total/1048576:.2f} MiB, {vram/1048576:.2f} MiB en VRAM), {sum(1 for r in rows if r['destino_render']=='1')} destinos de render")
    if (folder/'vram.bin').exists():
        data=(folder/'vram.bin').read_bytes();used=sum(1 for i in range(0,len(data),4096) if any(data[i:i+4096]))
        lines.append(f"- VRAM: {len(data)/1048576:.0f} MiB volcados, {used*4/1024:.2f} MiB con datos")
    if (folder/'ds_video.bin').exists():lines.append('- memoria de video DS:');ds_video(folder/'ds_video.bin',lines,args.sprites)
    if (folder/'planos_2d.bin').exists():planes(folder/'planos_2d.bin',lines,args.planos)
    if (folder/'actores.csv').exists():
        with (folder/'actores.csv').open(newline='') as f:rows=list(csv.DictReader(f))
        kinds=collections.Counter(r['id_actor'] for r in rows)
        lines.append(f"- actores: {len(rows)} vivos, {len(kinds)} tipos; mas frecuentes: "+', '.join(f'{k}x{c}' for k,c in kinds.most_common(8)))
    if (folder/'memoria.vm').exists():
        lines.append(f"- memoria.vm: {(folder/'memoria.vm').stat().st_size/1048576:.1f} MiB (tools/inspect_integrated_vm.py con el ELF de este BUILD)")
    if (folder/'gpu_estado.txt').exists():
        text=(folder/'gpu_estado.txt').read_text(errors='replace')
        keep=[l for l in text.splitlines() if l.startswith(('lineal_libre','vram_libre','fallos_carga','rechazados','ocurrencias_saltadas'))]
        lines.append('- render: '+', '.join(keep))

def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('path',type=Path);parser.add_argument('--dibujos',action='store_true',help='one table row per draw')
    parser.add_argument('--comandos',type=Path,help='write the decoded command list of each dump to OUT-level-dump.txt')
    parser.add_argument('--sprites',action='store_true',help='list every on-screen OAM entry')
    parser.add_argument('--planos',type=Path,help='write the 2D bridge planes of each dump as PNG into this folder')
    args=parser.parse_args()
    if args.planos:args.planos.mkdir(parents=True,exist_ok=True)
    folders=[args.path] if (args.path/'gpu_estado.txt').exists() else sorted(p.parent for p in args.path.rglob('gpu_estado.txt'))
    if not folders:print('no dump found below',args.path);return 1
    lines=[f'## Volcados del modo de prueba: {len(folders)}']
    for folder in folders:dump(folder,args,lines)
    print('\n'.join(lines));return 0

if __name__=='__main__':sys.exit(main())
