#!/usr/bin/env python3
"""Compare converted N64 mesh queries with the original DS ARM collision code."""
from pathlib import Path
import ctypes,struct,subprocess,json,random,math
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import *
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/movement-lab';DS=ROOT/'build/native-collision'
nm=subprocess.check_output(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm','-g','--defined-only',DS/'runtime.elf'],text=True)
syms={p[2]:int(p[0],16) for line in nm.splitlines() if len(p:=line.split())==3}
cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);cpu.mem_map(0x100000,0x100000);cpu.mem_write(0x100000,(DS/'runtime.bin').read_bytes());cpu.mem_map(0x200000,0x100000);cpu.mem_map(0x300000,0x10000);cpu.mem_map(0x400000,0x1000)
COLLIDER,KCL,CLPS,POS,RESULT=0x200000,0x210000,0x230000,0x240000,0x240020
cpu.mem_write(KCL,(DS/'castle.kcl').read_bytes());cpu.mem_write(CLPS,(DS/'castle.clps').read_bytes())
def call(name,*args):
 cpu.reg_write(UC_ARM_REG_CPSR,0x10);cpu.reg_write(UC_ARM_REG_SP,0x308000);cpu.reg_write(UC_ARM_REG_LR,0x400000)
 for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):cpu.reg_write(reg,value&0xffffffff)
 cpu.emu_start(syms[name],0x400000,count=2000000)
 assert cpu.reg_read(UC_ARM_REG_PC)==0x400000
 return cpu.reg_read(UC_ARM_REG_R0)
assert call('native_collision_init',COLLIDER,KCL,CLPS)==1
lib=ctypes.CDLL(str(OUT/'collision-oracle.dylib'));lib.oracle_init();lib.oracle_floor.argtypes=[ctypes.c_float]*3+[ctypes.POINTER(ctypes.c_float)];out=(ctypes.c_float*4)()
rng=random.Random(6403);points=[(-1200,6800)]+[(x,z) for x in range(-6000,6001,500) for z in range(-6000,7501,500)]+[(rng.randrange(-4000,2000),rng.randrange(2500,7000)) for _ in range(250)]
counts={'both_hit':0,'both_miss':0,'missing_in_mesh':0,'extra_in_mesh':0,'height_error_over_2':0};errors=[];max_error=0
for x,z in points:
 cpu.mem_write(POS,struct.pack('<3i',x*4096,10000*4096,z*4096));found=call('native_collision_ground',COLLIDER,POS,(-20000*4096),RESULT)
 ds=struct.unpack('<4iH2x',cpu.mem_read(RESULT,20)) if found else None
 mesh=bool(lib.oracle_floor(x,10000,z,out))
 if ds and mesh:
  counts['both_hit']+=1;error=abs(ds[0]/4096-out[0]);max_error=max(max_error,error)
  if error>2:counts['height_error_over_2']+=1;errors.append({'x':x,'z':z,'ds':ds[0]/4096,'mesh':out[0],'error':error,'triangle':ds[4]})
 elif ds:counts['missing_in_mesh']+=1;errors.append({'x':x,'z':z,'ds':ds[0]/4096,'mesh':None})
 elif mesh:counts['extra_in_mesh']+=1;errors.append({'x':x,'z':z,'ds':None,'mesh':out[0]})
 else:counts['both_miss']+=1
report={'samples':len(points),'counts':counts,'max_height_error':max_error,'first_errors':errors[:40],'scope':'Static floor query conversion only; not a movement or wall collision pass','status':'differences_detected' if errors else 'sampled_floor_queries_match_within_two_units'}
(OUT/'collision-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='first_errors'},indent=2))
