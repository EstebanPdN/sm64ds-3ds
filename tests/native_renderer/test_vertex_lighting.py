"""Native CPU colour versus the independent wide-integer and f24 GPU oracles."""
import subprocess
import sys
from test_lighting_shader import cases,reference,program,execute,pack_lines

records=cases(8192)
for i,c in enumerate(records):
    c['toon']=bool(i%7==0 and c['mask']);c['grey']=bool(i%7==1 and c['mask'])
    if c['toon'] or c['grey']:c['table']=False
    c['toon_table']=[(j*7%32)|((31-j)<<5)|((j*11%32)<<10) for j in range(32)]
lines=[]
for c in records:
    values=[*c['matrix'],*c['material'],c['mask'],int(c['table']),int(c['toon']),int(c['grey']),
        *[v for d,r,k in c['lights'] for v in (*d,*r,k)],*c['shininess'],*c['toon_table'],*c['normal']]
    lines.append(' '.join(map(str,values)))
actual=subprocess.run([sys.argv[1]],input='\n'.join(lines)+'\n',text=True,capture_output=True,check=True).stdout.splitlines()
assert len(actual)==len(records)
packed=subprocess.run([sys.argv[2]],input=pack_lines(records),text=True,capture_output=True,check=True).stdout.splitlines()
shader=program()
for i,(c,line,gpu) in enumerate(zip(records,actual,packed)):
    expected=reference(c)
    if c['toon']:
        word=c['toon_table'][expected[0]>>3];channels=[word>>(j*5)&31 for j in range(3)]
        expected=tuple((v<<3)|(v>>2) for v in channels)
    elif c['grey']:expected=(expected[0],)*3
    assert tuple(map(int,line.split()))==(*expected,255),(i,c,line,expected)
    if i<1024:
        uniforms=list(map(float,gpu.split()))
        if c['toon']:
            uniforms[-128:]=[v for word in c['toon_table'] for v in (*[word>>(j*5)&31 for j in range(3)],0)]
        assert execute(shader,c['normal'],uniforms,toon=c['toon'],grey=c['grey'])==expected,(i,c)
print('PASS:8192 native integer colours, including extremes, wrap, specular/table/toon/grey;1024 f24 shader comparisons')
