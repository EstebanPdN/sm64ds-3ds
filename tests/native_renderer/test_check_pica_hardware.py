#!/usr/bin/env python3
"""check_pica_hardware.py: a draw state the console never ran is reported.

Builds two synthetic captures in the BUILD131 comandos_gpu.bin layout: a
proven one whose draw uses the plain lit shader branch, and a suspect one
whose draw selects the toon branch (vertex shader bool 4), the state that
hung the Old 3DS GPU on the file selector in BUILD 137.
"""
from pathlib import Path
import struct,subprocess,sys,tempfile

TOOL=Path(__file__).resolve().parents[2]/'tools'/'check_pica_hardware.py'

def write(path,state):
    words=[]
    for reg,value in state:words+=[value,(0xf<<16)|reg]
    words+=[1,(0xf<<16)|0x22f]  # draw elements
    header=struct.pack('<8s8I',b'SM64PICA',1,40,len(words),len(words),0,0,struct.calcsize('<5I2H6BH'),1)
    path.write_bytes(header+struct.pack(f'<{len(words)}I',*words))

def base(bools):
    return [(0x2b0,bools),(0x228,6),(0x200,0x3000000),(0x202,0x40000000),(0x242,4),(0x2b9,0xa0000004),(0x11d,0x1000)]

with tempfile.TemporaryDirectory() as folder:
    folder=Path(folder);proven=folder/'proven';suspect=folder/'suspect';proven.mkdir();suspect.mkdir()
    write(proven/'comandos_gpu.bin',base(0x7fff0043))
    write(suspect/'comandos_gpu.bin',base(0x7fff0013))
    same=subprocess.run([sys.executable,str(TOOL),str(proven),'--proven',str(proven)],capture_output=True,text=True)
    assert same.returncode==0,same.stdout+same.stderr
    new=subprocess.run([sys.executable,str(TOOL),str(suspect),'--proven',str(proven)],capture_output=True,text=True)
    assert new.returncode==1 and '0x2b0' in new.stdout,new.stdout+new.stderr
    write(suspect/'comandos_gpu.bin',[(r,0 if r==0x228 else v) for r,v in base(0x7fff0043)])
    empty=subprocess.run([sys.executable,str(TOOL),str(suspect)],capture_output=True,text=True)
    assert empty.returncode==1 and 'num_vertices=0' in empty.stdout,empty.stdout
print('check_pica_hardware: OK')
