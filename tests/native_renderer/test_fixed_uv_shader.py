"""Execute the ACTUAL shader arithmetic with a 16-bit mantissa model.

Independent oracle: Python wide integer dot products, arithmetic shift and
signed16 reduction. No game resources or original rendering implementation.
"""
from pathlib import Path
import math
import random
import re
import struct
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]


def f24(value):
    # All intended arithmetic is exactly representable. Truncating conversion
    # also catches accidental intermediates requiring more than 17 significant
    # bits. Exact cases are independent of the hardware's rounding choice.
    bits=struct.unpack('<I',struct.pack('<f',value))[0]&~127
    return struct.unpack('<f',struct.pack('<I',bits))[0]


def kernel():
    source=(ROOT/'source/native_renderer/rigid.v.pica').read_text()
    constants={name:[float(v) for v in values.split(',')] for name,values in
        re.findall(r'^\.constf (\w+)\(([^)]+)\)',source,re.M)}
    instructions=[]
    for line in source.split('ifu use_fixed_uv',1)[1].split('.else',1)[0].splitlines():
        line=line.split(';',1)[0].strip()
        if line:instructions.append(line.split(None,1))
    return constants,instructions


def execute(constants,instructions,s,t,width,height,packed):
    registers={**constants,**{f'r{i}':[0.0]*4 for i in range(16)},'uv':[0.0]*4,
        'texcoord':[f24(s/(width*16)),f24(t/(height*16)),0,0]}
    registers.update({f'integer_uv[{i}]':[f24(x) for x in packed[i*4:i*4+4]] for i in range(7)})
    def operand(text):
        match=re.fullmatch(r'(-?)(\w+(?:\[\d+\])?)(?:\.([xyzw]+))?',text)
        if not match:raise ValueError(text)
        sign,name,swizzle=match.groups();swizzle=swizzle or 'xyzw';swizzle+=swizzle[-1]*(4-len(swizzle))
        if name=='uv':raise ValueError('PICA output registers are not readable sources')
        return [(-1 if sign else 1)*registers[name]['xyzw'.index(c)] for c in swizzle]
    for op,arguments in instructions:
        fields=[v.strip() for v in arguments.split(',')];target=fields[0].split('.')
        name=target[0];mask=target[1] if len(target)>1 else 'xyzw';args=[operand(v) for v in fields[1:]]
        if op=='mov':value=args[0]
        elif op=='add':value=[a+b for a,b in zip(*args)]
        elif op=='mul':value=[a*b for a,b in zip(*args)]
        elif op=='mad':value=[a*b+c for a,b,c in zip(*args)]
        elif op=='flr':value=[math.floor(a) for a in args[0]]
        else:raise ValueError('Unhandled actual shader instruction '+op)
        # PICA destination masks select the corresponding result lanes.
        for channel in mask:
            i='xyzw'.index(channel);registers[name][i]=f24(value[i])
    return registers['uv'][:2]


def reference(s,t,coefficients):
    def signed(value):return ((value+32768)&65535)-32768
    return tuple(signed((s*coefficients[i]+t*coefficients[i+1]+coefficients[i+2])>>12) for i in (0,3))


class FixedShaderTests(unittest.TestCase):
    def test_actual_shader_against_integer_oracle(self):
        rng=random.Random(0x55564658);cases=[]
        extremes=(-2147483648,-268435457,-65537,-4097,-1,0,1,4095,4096,65535,268435455,2147483647)
        coordinates=(-32768,-32767,-257,-256,-17,-16,-1,0,1,15,16,255,256,32766,32767)
        for i in range(2048):
            coefficients=tuple(rng.choice(extremes) if i<512 else rng.randrange(-2147483648,2147483648) for _ in range(6))
            s,t=(rng.choice(coordinates) if i<512 else rng.randrange(-32768,32768) for _ in range(2))
            dimensions=tuple(8<<rng.randrange(8) for _ in range(4))
            cases.append((s,t,coefficients,dimensions))
        data=''.join(' '.join(str(v) for v in (*c,*dimensions))+'\n' for s,t,c,dimensions in cases)
        output=subprocess.run([PACKER],input=data,text=True,check=True,stdout=subprocess.PIPE).stdout.splitlines()
        self.assertEqual(len(output),len(cases));constants,instructions=kernel()
        for number,((s,t,c,dimensions),line) in enumerate(zip(cases,output)):
            w,h,sw,sh=dimensions;packed=[float(v) for v in line.split()];self.assertEqual(len(packed),28)
            u,v=execute(constants,instructions,s,t,w,h,packed)
            actual=(round(u*sw*16),round(v*sh*16))
            self.assertEqual(actual,reference(s,t,c),(number,s,t,c,dimensions,actual))


if __name__=='__main__':
    PACKER=sys.argv.pop(1);unittest.main()
