"""Execute actual normal-UV shader instructions against a wide-integer oracle."""
from pathlib import Path
import math,random,re,subprocess,sys,unittest
from test_fixed_uv_shader import f24
ROOT=Path(__file__).resolve().parents[2]
def kernel():
    source=(ROOT/'source/native_renderer/rigid.v.pica').read_text()
    constants={name:[float(v) for v in values.split(',')] for name,values in re.findall(r'^\.constf (\w+)\(([^)]+)\)',source,re.M)}
    lines=[]
    for line in source.split('ifu use_normal_uv',1)[1].split('.else',1)[0].splitlines():
        line=line.split(';',1)[0].strip()
        if line:lines.append(line.split(None,1))
    return constants,lines

def execute(program,n,raw,inherit,dimensions,packed):
    constants,instructions=program;w,h,sw,sh=dimensions
    registers={**constants,**{f'r{i}':[0.]*4 for i in range(16)},'uv':[0.]*4,
        'normal':[*n,inherit],'texcoord':[f24(raw[0]/(w*16)),f24(raw[1]/(h*16)),0,0]}
    registers.update({f'integer_uv[{i}]':[f24(v) for v in packed[i*4:i*4+4]] for i in range(16)})
    def operand(text):
        match=re.fullmatch(r'(-?)(\w+(?:\[\d+\])?)(?:\.([xyzw]+))?',text)
        if not match:raise ValueError(text)
        sign,name,swizzle=match.groups();swizzle=swizzle or 'xyzw';swizzle+=swizzle[-1]*(4-len(swizzle))
        if name=='uv':raise ValueError('GPU output registers are write-only')
        return [(-1 if sign else 1)*registers[name]['xyzw'.index(c)] for c in swizzle]
    for op,args in instructions:
        fields=[f.strip() for f in args.split(',')];target=fields[0].split('.');name=target[0];mask=target[1] if len(target)>1 else 'xyzw'
        a=[operand(v) for v in fields[1:]]
        if op=='mov':value=a[0]
        elif op=='add':value=[x+y for x,y in zip(*a)]
        elif op=='mul':value=[x*y for x,y in zip(*a)]
        elif op=='mad':value=[x*y+z for x,y,z in zip(*a)]
        elif op=='flr':value=[math.floor(x) for x in a[0]]
        elif op=='sge':value=[float(x>=y) for x,y in zip(*a)]
        elif op=='dp3':value=[sum(a[0][i]*a[1][i] for i in range(3))]*4
        else:raise ValueError(op)
        for channel in mask:
            i='xyzw'.index(channel);registers[name][i]=f24(value[i])
    return tuple(round(registers['uv'][i]*dimensions[i+2]*16) for i in range(2))

class NormalShaderTests(unittest.TestCase):
    def test_actual_kernel_full_signed_coefficients(self):
        rng=random.Random(0x4e555633);cases=[]
        extremes=(-2147483648,-2147483647,-2097153,-2097152,-1,0,1,2097151,2097152,2147483647)
        for i in range(4096):
            c=tuple(rng.choice(extremes) if i<1024 else rng.randrange(-2147483648,2147483648) for _ in range(6))
            n=tuple(rng.choice((-512,-511,-33,-32,-1,0,1,31,32,510,511)) if i<1024 else rng.randrange(-512,512) for _ in range(3))
            raw=tuple(rng.randrange(-32768,32768) for _ in range(2));offset=tuple(rng.randrange(-32768,32768) for _ in range(2))
            dimensions=tuple(8<<rng.randrange(8) for _ in range(4));inherit=i%3
            cases.append((c,n,raw,offset,dimensions,inherit))
        data=''.join(' '.join(map(str,(*c,*d,*o)))+'\n' for c,n,raw,o,d,inherit in cases)
        lines=subprocess.run([PACKER],input=data,text=True,check=True,stdout=subprocess.PIPE).stdout.splitlines()
        self.assertEqual(len(lines),len(cases));program=kernel()
        for i,((c,n,raw,offset,dimensions,inherit),line) in enumerate(zip(cases,lines)):
            packed=list(map(float,line.split()));self.assertEqual(len(packed),64)
            expected=tuple(((((sum(n[k]*c[row*3+k] for k in range(3))>>21) if inherit!=2 else 0)+raw[row]+offset[row]*int(inherit==1)+32768)&65535)-32768 for row in range(2))
            self.assertEqual(execute(program,n,raw,inherit,dimensions,packed),expected,(i,c,n,raw,offset,dimensions,inherit))
if __name__=='__main__':PACKER=sys.argv.pop(1);unittest.main()
