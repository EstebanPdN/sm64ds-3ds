"""Execute the actual two short entries with f24 inputs and each palette slot."""
import random
import re
from pathlib import Path
from test_fixed_uv_shader import f24
from test_lighting_shader import program

root=Path(__file__).resolve().parents[2]/'source/native_renderer'
# The third entry calls helpers compiled in the first entry. Picasso allocates
# constants from c95 downward separately per entry; names alone do not share
# GPU locations. Every shared helper constant must use identical ordering/data.
assert re.findall(r'^\.constf .+$',(root/'rigid.v.pica').read_text(),re.M)==re.findall(r'^\.constf .+$',(root/'batch.v.pica').read_text(),re.M)
reference=program(root/'affine.v.pica');candidate=program(root/'batch.v.pica')


def execute(parsed,entry,values):
    constants,functions=parsed
    registers={k:[f24(v) for v in row] for k,row in {**constants,**values}.items()}
    for name in ['r0','r1','clip','uv','tint']:registers[name]=[0.]*4
    address=0
    def operand(text):
        name,index,swizzle=re.fullmatch(r'(\w+)(?:\[([^]]+)\])?(?:\.([xyzw]+))?',text).groups()
        if index is not None:name+='['+str(address if index=='a0.x' else int(index))+']'
        swizzle=swizzle or 'xyzw';swizzle+=swizzle[-1]*(4-len(swizzle))
        return [registers[name]['xyzw'.index(c)] for c in swizzle]
    def unlit(nodes):
        for node in nodes:
            if node[0]=='if':yield from unlit(node[3])
            else:yield node
    for _,op,*argument in unlit(functions[entry]):
        if op=='end':break
        fields=[v.strip() for v in argument[0].split(',')]
        if op=='mova':address=int(operand(fields[1])[0]);continue
        name,*mask=fields[0].split('.');mask=mask[0] if mask else 'xyzw'
        args=[operand(v) for v in fields[1:]]
        if op=='mov':value=args[0]
        elif op=='mul':value=[a*b for a,b in zip(*args)]
        elif op in ('dp3','dp4'):value=[sum(args[0][i]*args[1][i] for i in range(3 if op=='dp3' else 4))]*4
        else:raise ValueError(op)
        for c in mask:registers[name]['xyzw'.index(c)]=f24(value['xyzw'.index(c)])
    return tuple(tuple(registers[name]) for name in ['clip','uv','tint'])


rng=random.Random(0x50414c45545445)
for sample in range(256):
    palette=[[rng.uniform(-1e4,1e4) for _ in range(4)] for _ in range(32)]
    position=[rng.uniform(-4096,4096) for _ in range(3)]+[1]
    texcoord=[rng.uniform(-64,64),rng.uniform(-64,64),0,0]
    colour=[rng.randrange(256) for _ in range(4)]
    uv=[[rng.uniform(-2,2) for _ in range(3)]+[0] for _ in range(2)]
    tint=[rng.randrange(256)/255 for _ in range(4)]
    for slot in range(8):
        shared={'position':position,'texcoord':texcoord,'colour':colour}
        old={**shared,**{f'mvp[{i}]':palette[slot*4+i] for i in range(4)},
            'uv_transform[0]':uv[0],'uv_transform[1]':uv[1],'material_tint':tint}
        new={**shared,**{f'mvp[{i}]':row for i,row in enumerate(palette)},
            'light_normal[9]':uv[0],'light_normal[10]':uv[1],'light_normal[11]':tint,'rows':[slot*4+i for i in range(4)]}
        assert execute(reference,'affine_main',old)==execute(candidate,'batch_main',new),(sample,slot)
print('PASS:2048 actual shader executions, all8 matrix selectors, perspective/UV/tint f24 parity')

# Execute the actual signed16 uniform-palette transform and shared lighting body.
import subprocess,sys
from test_lighting_shader import cases,pack_lines,execute as lighting_execute,reference as lighting_reference,signed
records=cases(4096)
for i,c in enumerate(records):
    c['matrix']=[signed(v,16) for v in c['matrix']];c['table']=False
    if i<16:c['matrix']=[(-32768,-32767,-4097,-4096,-65,-64,-1,0,1,63,64,4095,4096,32766,32767)[(i+j)%15] for j in range(9)]
lines=subprocess.run([sys.argv[1]],input=pack_lines(records),text=True,stdout=subprocess.PIPE,check=True).stdout.splitlines()
shader=program();attrs=program(root/'batch.v.pica')
combined=({**shader[0],**attrs[0]},{**shader[1],**attrs[1]})
for i,(c,line) in enumerate(zip(records,lines)):
    packed=list(map(float,line.split()))
    expected=tuple(signed(sum(c['normal'][k]*c['matrix'][k*3+j] for k in range(3))>>12,11) for j in range(3))
    if c['mask']:assert lighting_execute(combined,c['normal'],packed,normal_only=True,basis=c['matrix'],basis_slot=i%8)==expected,(i,c)
    assert lighting_execute(combined,c['normal'],packed,basis=c['matrix'],basis_slot=i%8)==lighting_reference(c),(i,c)
    if i<128 and c['mask']:
        assert lighting_execute(combined,c['normal'],packed,grey=True,basis=c['matrix'],basis_slot=i%8)==(lighting_reference(c)[0],)*3
print('PASS:4096 signed16 uniform-palette f24 normals/colours, all8 selectors and grey; table/toon retain general shader')
