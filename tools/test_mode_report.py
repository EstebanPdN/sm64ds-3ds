#!/usr/bin/env python3
"""Summarize SELECT+START hardware test runs copied from the SD card.

Usage: python3 tools/test_mode_report.py PATH [PATH ...] [--markdown OUT.md] [--detalle]

PATH is a copy of sdmc:/3ds/sm64ds/prueba (or one prueba/NN run folder).
A tour run (SELECT+START) holds one LL-level/ folder per level; a spot run
(R+SELECT+START) holds informes/. A report is a summary (device text), a CSV
with one row per presented image and two BMP screenshots. Until BUILD130
each report was a folder SS-situation/ with resumen.txt, cuadros.csv,
pantalla_arriba.bmp and pantalla_abajo.bmp; BUILD131 writes them flat as
SS-situation.resumen.txt, .cuadros.csv, .arriba.bmp, .abajo.bmp and, when
models were not drawn, .faltan.csv (the missing-model census). Deep dumps
(volcado/, volcado-zonaN/) are read by tools/test_mode_dump.py. Ticks are
268,111,856 Hz. Phase scopes nest (update contains 2D preparation and
producers; render work contains composition, upload and submission); they
are never added up.
"""
from pathlib import Path
import argparse,collections,csv,re,statistics,sys

TICK_HZ=268111856
PHASES=[('update_ticks','juego'),('source_2d_ticks','2D CPU'),('producer_ticks','productores'),
        ('bridge_ticks','composicion'),('draw_ticks','dibujo'),('upload_ticks','subida'),
        ('submit_ticks','envio'),('gpu_wait_ticks','espera GPU')]
# modelos_faltantes: model draws with no native bank (not drawn): catalog gaps.
HEALTH=['modelos_faltantes','fallos_productor','fallos_vida_modelo','fallos_particulas','imagenes_rechazadas']
# secas_por_render_ocupado: BUILD121+ (Old 3DS: no capture while the shared-core worker draws).
SKIPS=['secas_por_retraso','secas_por_tope','saltadas_tarde','descartadas_render_ocupado','secas_por_render_ocupado']

def runs(paths):
    for p in paths:
        p=Path(p)
        if (p/'resultado.txt').exists():yield p
        else:yield from sorted(d for d in p.iterdir() if (d/'resultado.txt').exists())

def percentile(values,p):
    if not values:return 0.0
    values=sorted(values);return values[min(len(values)-1,max(0,(len(values)*p+99)//100-1))]

class Report:
    """One situation: a folder (until BUILD130) or a file stem (BUILD131)."""
    def __init__(self,base,flat):
        self.base=base;self.flat=flat;self.name=base.name
    def file(self,kind):
        names={'resumen':('resumen.txt','.resumen.txt'),'cuadros':('cuadros.csv','.cuadros.csv'),'arriba':('pantalla_arriba.bmp','.arriba.bmp'),
               'abajo':('pantalla_abajo.bmp','.abajo.bmp'),'faltan':('faltan.csv','.faltan.csv')}[kind]
        return self.base.parent/(self.base.name+names[1]) if self.flat else self.base/names[0]

def report_rows(folder,summary):
    path=folder.file('cuadros')
    if not path.exists():return []
    with path.open(newline='') as f:rows=list(csv.DictReader(f))
    m=re.search(r'ventana_inicio_tick=(\d+)',summary)
    if m:
        start=int(m.group(1));rows=[r for r in rows if int(r['start_tick'])>=start]
    else:
        # The window is the CSV tail: "== imagenes (N, E intermedias, R rechazadas) ==".
        m=re.search(r'== imagenes \((\d+), \d+ intermedias, (\d+) rechazadas\) ==',summary)
        if m:rows=rows[-(int(m.group(1))+int(m.group(2))):] if int(m.group(1))+int(m.group(2)) else []
    return [r for r in rows if r['error']=='0']

def counters(summary):
    out={}
    for name in HEALTH+SKIPS+['actualizaciones']:
        m=re.search(r'^'+name+r'=(\d+)',summary,re.M)
        out[name]=int(m.group(1)) if m else 0
    m=re.search(r'^app_libre=(\d+)',summary,re.M);out['app_libre']=int(m.group(1)) if m else 0
    m=re.search(r'^linear_libre=(\d+)',summary,re.M);out['linear_libre']=int(m.group(1)) if m else 0
    m=re.search(r'^heap_libre=(\d+)',summary,re.M);out['heap_libre']=int(m.group(1)) if m else 0  # BUILD115+
    return out

def missing_models(folder):
    """BUILD131 census rows: models the native route did not draw."""
    path=folder.file('faltan')
    if not path.exists():return []
    with path.open(newline='') as f:return list(csv.DictReader(f))

def analyse(folder):
    summary=folder.file('resumen').read_text(errors='replace') if folder.file('resumen').exists() else ''
    rows=report_rows(folder,summary)
    out={'situacion':folder.name,'imagenes':len(rows),'capturas':int(folder.file('arriba').exists())+int(folder.file('abajo').exists())}
    out['faltan']=missing_models(folder);out['zona_nivel']='zona' in folder.name
    m=re.search(r'situacion: (.*)',summary);out['titulo']=m.group(1).strip() if m else folder.name
    ends=[int(r['end_tick']) for r in rows]
    intervals=[(b-a)*1000/TICK_HZ for a,b in zip(ends,ends[1:]) if b>a]
    out['fps']=len(intervals)*1000/sum(intervals) if intervals else 0
    out['p50']=percentile(intervals,50);out['p95']=percentile(intervals,95);out['max']=max(intervals,default=0)
    for key,name in PHASES:
        values=[int(r[key])*1000/TICK_HZ for r in rows if key in r and (r.get('endpoint')=='0' or key not in('update_ticks','source_2d_ticks','producer_ticks'))]
        out[name]=statistics.fmean(values) if values else 0;out[name+' max']=max(values,default=0)
    out['gpu']=statistics.fmean(int(r['previous_gpu_us'])/1000 for r in rows) if rows else 0
    for key in ('draws','triangles','matrices','light_rows'):
        out[key]=statistics.fmean(int(r[key]) for r in rows if key in r) if rows and key in rows[0] else 0
    out['subida_kb']=statistics.fmean(int(r['upload_bytes'])/1024 for r in rows) if rows and 'upload_bytes' in rows[0] else 0
    # BUILD121+: render CPU without the game updates that ran on the same core.
    # BUILD124: a worker on its own core (render_comparte_nucleo=0) keeps its work.
    m=re.search(r'^render_comparte_nucleo=(\d)',summary,re.M);shared=not m or m.group(1)=='1'
    m=re.search(r'^nucleo_render=(-?\d+)',summary,re.M);out['nucleo_render']=int(m.group(1)) if m else -1
    out['render_cpu']=statistics.fmean(max(0,int(r['end_tick'])-int(r['start_tick'])-(int(r['game_overlap_ticks']) if shared else 0))*1000/TICK_HZ
                                       for r in rows) if rows and 'game_overlap_ticks' in rows[0] else 0
    out['cpu2d']=sum(1 for r in rows if int(r.get('route_2d','0'))&2)
    out.update(counters(summary))
    out['sin_imagen']=sum(out[k] for k in ('secas_por_retraso','saltadas_tarde','descartadas_render_ocupado','secas_por_render_ocupado'))
    out['problemas']=sum(out[k] for k in HEALTH)
    # 2026-11-09+: mixer time of the window (0 for older reports).
    m=re.search(r'^carga_audio=([\d.]+)%',summary,re.M);out['audio%']=float(m.group(1)) if m else 0.0
    m=re.search(r'^mezcla_ms_por_s=([\d.]+)',summary,re.M);out['mezcla_ms_s']=float(m.group(1)) if m else 0.0
    m=re.search(r'^juego_espera_ms total=([\d.]+) max=([\d.]+)',summary,re.M)
    out['espera_juego_max']=float(m.group(2)) if m else 0.0
    return out

def situations(folder):
    old=[Report(d,False) for d in sorted(folder.iterdir()) if d.is_dir() and re.match(r'\d\d-',d.name) and (d/'resumen.txt').exists()]
    flat=[Report(f.parent/f.name[:-len('.resumen.txt')],True) for f in sorted(folder.glob('[0-9][0-9]-*.resumen.txt'))]
    return sorted(old+flat,key=lambda r:r.name)

def table(lines,cols,items,label):
    lines.append('| '+' | '.join(cols)+' |');lines.append('|'+'---|'*len(cols))
    for a in items:
        lines.append('| '+' | '.join(str(a[label]) if c==label else (f"{a.get(c,0):.1f}" if isinstance(a.get(c,0),float) else str(a.get(c,0))) for c in cols)+' |')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths',nargs='+');parser.add_argument('--markdown',type=Path)
    parser.add_argument('--detalle',action='store_true',help='one table row per situation of every level')
    args=parser.parse_args()
    lines=[];everything=[];levels=[];problems=[]
    census=collections.OrderedDict()  # model signature -> [calls, levels]
    cols=['situacion','fps','p95','max','juego','2D CPU','productores','dibujo','render_cpu','nucleo_render','subida','envio','gpu','draws','subida_kb','cpu2d','audio%','mezcla_ms_s','espera_juego_max','sin_imagen','problemas']
    for run in runs(args.paths):
        result=(run/'resultado.txt').read_text(errors='replace').splitlines()
        lines+=['','## '+run.name+': '+(result[0] if result else ''),'']
        verdicts=[l for l in result if l.startswith(('== ','RECORRIDO','COMPLETA','INCOMPLETA','CANCELADA','NO TERMINO'))]
        if (run/'informes').exists():  # spot run
            items=[analyse(f) for f in situations(run/'informes')]
            for a in items:a['zona']=run.name
            everything+=items;table(lines,cols,items,'situacion')
            lines+=['']+['_'+v+'_' for v in verdicts[-1:]]
            continue
        title=Report(run/'00-titulo',not (run/'00-titulo').is_dir())
        if title.file('resumen').exists():
            a=analyse(title);lines.append(f"Titulo: {a['fps']:.1f} FPS, p95 {a['p95']:.1f} ms, juego {a['juego']:.2f} ms, 2D {a['2D CPU']:.2f} ms, render {a['dibujo']:.2f} ms")
            lines.append('')
        for folder in sorted(d for d in run.iterdir() if d.is_dir() and re.match(r'\d\d-',d.name) and d.name!='00-titulo'):
            items=[analyse(f) for f in situations(folder)]
            if not items:continue
            for a in items:a['zona']=f"{run.name}/{folder.name}"
            everything+=items
            for a in items:
                for m in a['faltan']:
                    key=(m['motivo'],m['fichero'],m['huesos'],m['materiales'],m['texturas'],m['paletas'],m['grupos'],m['material0'],m['textura0'])
                    entry=census.setdefault(key,[0,[]]);entry[0]+=int(m['llamadas'])
                    if folder.name not in entry[1]:entry[1].append(folder.name)
            level={'situacion':folder.name,'informes':len(items),'zonas':sum(1 for a in items if a['zona_nivel'])}
            for c in cols[1:]:
                values=[a[c] for a in items]
                if not all(isinstance(v,(int,float)) for v in values):continue
                level[c]=max(values) if c in('p95','max','sin_imagen','problemas','espera_juego_max') else sum(values) if c=='cpu2d' else statistics.fmean(values)
            level['sin_imagen']=sum(a['sin_imagen'] for a in items);level['problemas']=sum(a['problemas'] for a in items)
            levels.append(level)
            # One line per level: totals over its situations (--detalle: each one).
            totals={k:sum(a[k] for a in items) for k in HEALTH}
            missing_shots=[a['situacion'] for a in items if a['capturas']<2]
            bad=[f"{k}={v}" for k,v in totals.items() if v]
            if bad:problems.append(f"{run.name}/{folder.name}: "+', '.join(bad)+f" (en {sum(1 for a in items if a['problemas'])} de {len(items)} situaciones)")
            if missing_shots:problems.append(f"{run.name}/{folder.name}: faltan capturas en "+', '.join(missing_shots))
            if args.detalle:
                for a in items:
                    bad=[f"{k}={a[k]}" for k in HEALTH if a[k]]
                    if bad:problems.append(f"   {a['situacion']}: "+', '.join(bad))
        lines.append('### Por nivel (media de sus situaciones; p95/max = peor situacion)');lines.append('')
        table(lines,['situacion','informes','zonas']+cols[1:],[l for l in levels if True],'situacion')
        lines.append('')
        lines+=['### Resultado de cada nivel','']+['- '+v for v in verdicts]
        kept=sorted(p.parent.name for p in run.glob('*/registro_cuelgue.txt'))
        if kept:
            lines+=['','Registros de arranques que no terminaron: '+', '.join(kept)]
            for name in kept:
                fatal=[l for l in (run/name/'registro_cuelgue.txt').read_text(errors='replace').splitlines() if l.startswith(('FATAL','WARNING','Native exit','PPU cache way 2'))]
                lines+=[f'- {name}: '+(' | '.join(fatal[:6]) if fatal else 'sin linea FATAL (corte de corriente, HOME o fallo sin aviso)')]
        if args.detalle:
            lines+=['','### Detalle por situacion','']
            table(lines,['zona']+cols,everything,'zona')
    if everything:
        lines+=['','## Resumen','']
        worst=sorted(everything,key=lambda a:a['fps'] if a['fps'] else 1e9)[:10]
        lines+=['Situaciones mas lentas: '+', '.join(f"{a['zona']}/{a['situacion']} {a['fps']:.1f} FPS (p95 {a['p95']:.1f} ms)" for a in worst),'']
        if levels:
            slow=sorted(levels,key=lambda l:l['fps'])[:10]
            lines+=['Niveles mas lentos: '+', '.join(f"{l['situacion']} {l['fps']:.1f} FPS" for l in slow),'']
        for name in [n for _,n in PHASES]+['gpu']:
            lines.append(f"- {name}: media {statistics.fmean(a[name] for a in everything):.2f} ms, peor {max(a[name] for a in everything):.2f} ms")
        free=[a['app_libre'] for a in everything if a['app_libre']];linear=[a['linear_libre'] for a in everything if a['linear_libre']]
        if linear:lines.append(f"- memoria lineal libre minima: {min(linear)/1048576:.1f} MiB")
        if free:lines.append(f"- memoria de aplicacion libre minima: {min(free)/1048576:.1f} MiB")
        heap=[(a['heap_libre'],a) for a in everything if a.get('heap_libre')]
        if heap:
            low,where=min(heap,key=lambda x:x[0])
            lines.append(f"- montón libc libre mínimo: {low/1048576:.1f} MiB ({where['zona']}/{where['situacion']})")
        lines+=['','### Problemas del render','']+(['- '+p for p in problems] if problems else ['- ninguno registrado'])
        if census:
            reasons={'1':'carga no vista (sin identidad)','2':'sin banco nativo','3':'identidad doble','0':'otros'}
            lines+=['','### Modelos que el render nativo no dibujo (BUILD131+)','',
                    'Cada fila es un modelo distinto (huesos, materiales, texturas y los nombres de su primer material y textura lo identifican en la ROM).','',
                    '| llamadas | motivo | fichero | huesos | materiales | texturas | material0 | textura0 | niveles |','|---|---|---|---|---|---|---|---|---|']
            for key,(calls,where) in sorted(census.items(),key=lambda kv:-kv[1][0]):
                lines.append(f"| {calls} | {reasons.get(key[0],key[0])} | {key[1]} | {key[2]} | {key[3]} | {key[4]} | {key[7] or '-'} | {key[8] or '-'} | {len(where)}: {', '.join(where[:6])}{' ...' if len(where)>6 else ''} |")
    text='\n'.join(lines).strip()+'\n'
    print(text)
    if args.markdown:args.markdown.write_text('# Informes del modo de prueba\n\n'+text)
    return 0 if everything else 1

if __name__=='__main__':sys.exit(main())
