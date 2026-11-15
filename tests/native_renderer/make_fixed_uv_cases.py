"""Authored signed/fractional/wrapping UV cases; no ROM or graphics decoder."""
import argparse
import json
from pathlib import Path
import random


def cases():
    rng=random.Random(0x55564750);output=[]
    raw=(-32768,-32767,-257,-256,-17,-16,-1,0,1,15,16,255,256,32766,32767)
    signed=lambda n:((n+(1<<31))&0xffffffff)-(1<<31)
    for i in range(240):
        s,t=rng.choice(raw),rng.choice(raw);w,h=8<<rng.randrange(8),8<<rng.randrange(8)
        coefficient=[rng.randrange(-(1<<31),1<<31) for _ in range(6)]
        target=[((i%24)-4)*16+(0,1,15)[i%3],((i//3%24)-4)*16+(0,1,15)[(i//3)%3]]
        if i%13==0:target=[-32768,32767]
        for component,row in enumerate((0,3)):
            remainder=(0,1,4095)[(i+component)%3]
            coefficient[row+2]=signed(target[component]*4096+remainder-s*coefficient[row]-t*coefficient[row+1])
        result=[(((s*coefficient[j]+t*coefficient[j+1]+coefficient[j+2])>>12)+32768)%65536-32768 for j in (0,3)]
        assert result==target
        expected={}
        for mode in ('clamp','repeat','mirror'):
            address=[]
            for q in result:
                texel=q//16
                if mode=='clamp':v=max(0,min(15,texel))
                elif mode=='repeat':v=texel%16
                else:
                    v=texel%32;v=31-v if v>=16 else v
                address.append(v)
            x,y=address;expected[mode]=[x*17,y*17,(x^y)*17]
        output.append(dict(s=s,t=t,width=w,height=h,coefficient=coefficient,raw_result=result,expected=expected))
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('header',type=Path);args=parser.parse_args()
    records=cases();rows=[]
    for c in records:rows.append('{'+f'{c["s"]},{c["t"]},{c["width"]},{c["height"]},'+'{{'+','.join(str(v) for v in c['coefficient'])+'}}}')
    args.header.write_text('#pragma once\n#include <array>\n#include <cstdint>\nstruct FixedCase{std::int16_t s,t;std::uint16_t width,height;std::array<std::int32_t,6> coefficient;};\n'+
        'inline constexpr FixedCase fixed_cases[]={\n'+',\n'.join(rows)+'\n};\n')
    args.header.with_suffix('.json').write_text(json.dumps(dict(scope='Authored exact UV shader cases',cases=records),indent=2)+'\n')


if __name__=='__main__':main()
