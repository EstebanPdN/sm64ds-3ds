"""Actual lighting assembly versus independent wide-integer vertex equations."""
from pathlib import Path
import math,random,re,subprocess,sys,unittest
from test_fixed_uv_shader import f24
ROOT=Path(__file__).resolve().parents[2]

def program(source_path=None):
    source=(source_path or ROOT/'source/native_renderer/rigid.v.pica').read_text()
    constants={name:[float(v) for v in values.split(',')] for name,values in re.findall(r'^\.constf (\w+)\(([^)]+)\)',source,re.M)}
    lines=[line.split(';',1)[0].strip() for line in source.splitlines()];lines=[v for v in lines if v]
    def block(at):
        out=[]
        while at<len(lines):
            text=lines[at];at+=1
            if text in ('.end','.else'):return out,at,text
            if text.startswith('ifu '):
                a,at,ending=block(at);b=[]
                if ending=='.else':b,at,ending=block(at)
                out.append(('if',text.split()[1],a,b))
            elif text.startswith('for '):
                a,at,ending=block(at);out.append(('for',a))
            else:out.append(('op',*text.split(None,1)))
        raise ValueError('Unclosed shader block')
    functions={}
    for i,line in enumerate(lines):
        if line.startswith('.proc '):functions[line.split()[1]]=block(i+1)[0]
    return constants,functions

def execute(parsed,normal,line,toon=False,grey=False,normal_only=False,basis=None,basis_slot=0):
    count,specular,table=map(int,line[:3]);data=line[3:];assert len(data)==224
    constants,functions=parsed;regs={**constants,**{f'r{i}':[0.]*4 for i in range(16)},'normal':[*normal,0]}
    offset=0
    for name,n in [('light_normal',12),('light_direction',4),('light_colour',4),('light_material',4),('shininess',32)]:
        for i in range(n):regs[f'{name}[{i}]']=[f24(v) for v in data[offset+i*4:offset+i*4+4]]
        offset+=n*4
    if basis is not None:
        regs['rows']=[basis_slot*4+i for i in range(4)]
        for j in range(3):regs[f'shininess[{basis_slot*3+j}]']=[basis[k*3+j] for k in range(3)]+[0]
    flags={'has_lights':bool(count),'use_specular':bool(specular),'use_shininess':bool(table),'use_toon':toon,'use_grey':grey};indices={'aL':0,'a0.x':0,'a0.y':0}
    def operand(text):
        match=re.fullmatch(r'(-?)(\w+)(?:\[([^]]+)\])?(?:\.([xyzw]+))?',text)
        if not match:raise ValueError(text)
        sign,name,index,swizzle=match.groups()
        if index is not None:name+='['+str(int(index) if index.isdigit() else indices[index])+']'
        swizzle=swizzle or 'xyzw';swizzle+=swizzle[-1]*(4-len(swizzle))
        return [(-1 if sign else 1)*regs[name]['xyzw'.index(c)] for c in swizzle]
    transformed=None
    def run(nodes):
        nonlocal transformed
        for node in nodes:
            if node[0]=='if':run(node[2] if flags[node[1]] else node[3]);continue
            if node[0]=='for':
                transformed=tuple(int(v) for v in regs['r2'][:3])
                if normal_only:return
                for i in range(count):indices['aL']=i;run(node[1])
                continue
            _,op,arguments=node
            if op=='call':run(functions[arguments]);continue
            fields=[v.strip() for v in arguments.split(',')]
            if op=='mova':indices[fields[0]]=int(operand(fields[1])[0]);continue
            target=fields[0].split('.');name=target[0];mask=target[1] if len(target)>1 else 'xyzw';a=[operand(v) for v in fields[1:]]
            if op=='mov':v=a[0]
            elif op=='mul':v=[x*y for x,y in zip(*a)]
            elif op=='add':v=[x+y for x,y in zip(*a)]
            elif op=='mad':v=[x*y+z for x,y,z in zip(*a)]
            elif op=='flr':v=[math.floor(x) for x in a[0]]
            elif op=='min':v=[min(x,y) for x,y in zip(*a)]
            elif op=='max':v=[max(x,y) for x,y in zip(*a)]
            elif op=='slt':v=[float(x<y) for x,y in zip(*a)]
            elif op=='sge':v=[float(x>=y) for x,y in zip(*a)]
            elif op in ('dp3','dp4'):v=[sum(a[0][i]*a[1][i] for i in range(3 if op=='dp3' else 4))]*4
            else:raise ValueError(op)
            for channel in mask:
                i='xyzw'.index(channel);regs[name][i]=f24(v[i])
    run(functions['calculate_lighting' if basis is None else 'batch_lighting'])
    return transformed if normal_only else tuple(round(v*255) for v in regs['r1'][:3])

def signed(n,bits):return ((n+(1<<(bits-1)))&((1<<bits)-1))-(1<<(bits-1))
def reference(c):
    normal=[signed(sum(c['normal'][k]*c['matrix'][k*3+j] for k in range(3))>>12,11) for j in range(3)]
    colours=[[v>>(i*5)&31 for i in range(3)] for v in c['material']]
    diffuse,ambient,specular,emission=colours;acc=[v<<14 for v in emission]
    for i,(direction,colour,reciprocal) in enumerate(c['lights']):
        if not c['mask']&(1<<i):continue
        dot=sum((direction[k]*normal[k])>>9 for k in range(3));shine=0
        if dot>0:
            d=signed(dot,11)
            for j in range(3):acc[j]+=(diffuse[j]*colour[j]*d)&0xfffff
            half=signed(dot+normal[2],11);square=(half*half>>10)&1023
            shine=(square*reciprocal>>8)-512
            if shine<0:shine=0
            else:shine=max(0,min(511,signed(shine,14)))
        if c['table']:shine=c['shininess'][shine>>2]*2
        for j in range(3):acc[j]+=(specular[j]*shine+(ambient[j]<<9))*colour[j]
    result=[min(31,v>>14) for v in acc]
    return tuple((v<<3)|(v>>2) for v in result)

def cases(count=1024):
    rng=random.Random(0x4c495447);out=[]
    extreme=(-2147483648,-2147483647,-8193,-4096,-1,0,1,4095,8192,2147483647)
    directions=(-1024,-1023,-512,-1,0,1,511,512,1023)
    for i in range(count):
        matrix=[rng.choice(extreme) if i<256 else rng.randrange(-2147483648,2147483648) for _ in range(9)]
        if i%4==3:matrix=[4096,0,0,0,4096,0,0,0,4096]
        normal=[rng.choice((-512,-511,-1,0,1,510,511)) if i<256 else rng.randrange(-512,512) for _ in range(3)]
        material=[sum(rng.randrange(4 if i%4==2 else 32)<<(j*5) for j in range(3)) for _ in range(4)]
        if i%4 in (0,3):material[1]=material[3]=0
        if i%4==3:material[0]=0
        if i%19==0:material[2]=0
        table=bool(i%2);mask=i%16;lights=[]
        for _ in range(4):
            d=[rng.choice(directions) if i<256 else rng.randrange(-1024,1024) for _ in range(3)]
            colour=[rng.randrange(16 if i%4==2 else 32) for _ in range(3)];denominator=512-signed(-d[2],11)
            r=0 if denominator==0 else int(262144/denominator);lights.append((d,colour,r))
        shininess=[rng.randrange(256) for _ in range(128)]
        out.append(dict(matrix=matrix,normal=normal,material=material,mask=mask,table=table,lights=lights,shininess=shininess))
    return out

def pack_lines(records):
    return ''.join(' '.join(map(str,[*c['matrix'],*c['material'],c['mask'],int(c['table']),1,*[v for d,r,k in c['lights'] for v in (*d,*r,k)],*c['shininess']]))+'\n' for c in records)
class LightingShaderTests(unittest.TestCase):
    def test_actual_assembly_integer_lighting(self):
        records=cases();out=subprocess.run([PACKER],input=pack_lines(records),text=True,stdout=subprocess.PIPE,check=True).stdout.splitlines()
        self.assertEqual(len(out),len(records));shader=program()
        for i,(c,line) in enumerate(zip(records,out)):
            actual=execute(shader,c['normal'],list(map(float,line.split())))
            self.assertEqual(actual,reference(c),(i,c,actual,reference(c)))
    def test_normal_transform_before_colour_clamping(self):
        # Compare the actual f24 assembly result before saturated colours could
        # hide a normal error. Exercise negative carries and the signed11 wrap.
        records=cases(4096)
        for c in records:c['mask']=1;c['table']=False
        lines=subprocess.run([PACKER],input=pack_lines(records),text=True,stdout=subprocess.PIPE,check=True).stdout.splitlines()
        shader=program()
        for i,(c,line) in enumerate(zip(records,lines)):
            expected=tuple(signed(sum(c['normal'][k]*c['matrix'][k*3+j] for k in range(3))>>12,11) for j in range(3))
            self.assertEqual(execute(shader,c['normal'],list(map(float,line.split())),normal_only=True),expected,(i,c))

    def test_toon_lookup_after_lighting(self):
        records=cases(128)
        for c in records:c['table']=False;c['material'][2]=0
        lines=subprocess.run([PACKER],input=pack_lines(records),text=True,stdout=subprocess.PIPE,check=True).stdout.splitlines()
        shader=program();table=[(i,31-i,(i*7)%32) for i in range(32)]
        for c,line in zip(records,lines):
            packed=list(map(float,line.split()));packed[-128:]=[v for rgb in table for v in (*rgb,0)]
            red=reference(c)[0]>>3
            expected=tuple((v<<3)|(v>>2) for v in table[red])
            self.assertEqual(execute(shader,c['normal'],packed,toon=True),expected)
            self.assertEqual(execute(shader,c['normal'],packed,grey=True),(reference(c)[0],)*3)
if __name__=='__main__':PACKER=sys.argv.pop(1);unittest.main()
