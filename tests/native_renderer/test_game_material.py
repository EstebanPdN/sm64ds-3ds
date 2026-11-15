"""Compare native material lowering with independent original integer math."""
import random
import subprocess
import sys
import unittest


def word(n):return ((n+(1<<31))&0xffffffff)-(1<<31)
def trunc(n,d):return (1 if n>=0 else -1)*(abs(n)//d)


def reference(sx,sy,tx,ty,w,h,bw,bh,sine,cosine):
    C=word((sx*sine)>>12);B=word((sx*cosine)>>12)
    A=word((sy*sine)>>12);D=word((sy*cosine)>>12)
    # func_02044b30: W and H multiply the whole bracket (decomp src/func_02044b30.c).
    return [B,trunc(word(C*w),h),word(word(w*word(sx-word(C+B)))*8-w*word((sx*tx)>>8)),
            trunc(word(A*(-h)),w),D,word(word(h*word(word(word(A-D)-sy)+8192))*8+h*word((sy*ty)>>8))]


class MaterialTests(unittest.TestCase):
    def test_original_matrix_equation(self):
        rng=random.Random(0x4d41544c);cases=[]
        for i in range(4096):
            extent=tuple(8<<rng.randrange(8) for _ in range(4))
            scalars=tuple(rng.randrange(-8192,8193) if i<1024 else rng.randrange(-(1<<31),1<<31) for _ in range(4))
            trig=tuple(rng.randrange(-32768,32768) for _ in range(2))
            cases.append((*scalars,*extent,*trig))
        text=''.join(' '.join(str(v) for v in case)+'\n' for case in cases)
        result=subprocess.run([ADAPTER],input=text,text=True,stdout=subprocess.PIPE,check=True).stdout.splitlines()
        self.assertEqual(len(result),len(cases))
        for i,(case,line) in enumerate(zip(cases,result)):
            self.assertEqual([int(v) for v in line.split()],reference(*case),(i,case))


if __name__=='__main__':ADAPTER=sys.argv.pop(1);unittest.main()
