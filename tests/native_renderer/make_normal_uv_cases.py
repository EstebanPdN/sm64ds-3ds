"""Independent signed-normal dot/offset/wrap fixtures, including exact boundaries."""
import argparse,json,random
from pathlib import Path

def cases():
    rng=random.Random(0x4e475055);out=[]
    for i in range(240):
        n=[rng.choice((-512,-511,-32,-1,0,1,31,510,511)) for _ in range(3)]
        c=[rng.randrange(-2147483648,2147483648) for _ in range(6)]
        w,h=8<<rng.randrange(8),8<<rng.randrange(8);inherit=i%3
        dot=[(sum(n[k]*c[row*3+k] for k in range(3))>>21) if inherit!=2 else 0 for row in range(2)]
        target=[(i%24-4)*16+(0,1,15)[i%3],(i//3%24-4)*16+(0,1,15)[i//3%3]]
        if i%13==0:target=[-32768,32767]
        raw=[rng.randrange(-32768,32768) for _ in range(2)] if inherit==1 else [((target[j]-dot[j]+32768)&65535)-32768 for j in range(2)]
        offset=[((target[j]-dot[j]-raw[j]+32768)&65535)-32768 for j in range(2)]
        result=[((dot[j]+raw[j]+offset[j]*int(inherit==1)+32768)&65535)-32768 for j in range(2)];assert result==target
        expected={}
        for mode in ('clamp','repeat','mirror'):
            address=[]
            for q in result:
                texel=q//16
                if mode=='clamp':v=max(0,min(15,texel))
                elif mode=='repeat':v=texel%16
                else:v=texel%32;v=31-v if v>=16 else v
                address.append(v)
            x,y=address;expected[mode]=[x*17,y*17,(x^y)*17]
        out.append(dict(normal=n,s=raw[0],t=raw[1],offset=offset,inherit=inherit,width=w,height=h,coefficient=c,raw_result=result,expected=expected))
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('header',type=Path);a=p.parse_args();records=cases();rows=[]
    for c in records:
        rows.append('{'+f'{c["s"]},{c["t"]},{c["width"]},{c["height"]},'+'{{'+','.join(map(str,c['coefficient']))+'}},{{'+','.join(map(str,c['normal']))+'}},{{'+','.join(map(str,c['offset']))+'}},'+str(c['inherit'])+'}')
    a.header.write_text('#pragma once\n#include <array>\n#include <cstdint>\nstruct NormalCase{std::int16_t s,t;std::uint16_t width,height;std::array<std::int32_t,6> coefficient;std::array<std::int16_t,3> normal;std::array<std::int16_t,2> offset;std::int16_t inherit;};\ninline constexpr NormalCase normal_cases[]={\n'+',\n'.join(rows)+'\n};\n')
    a.header.with_suffix('.json').write_text(json.dumps(dict(scope='Authored native normal UV cases',cases=records),indent=2)+'\n')
if __name__=='__main__':main()
