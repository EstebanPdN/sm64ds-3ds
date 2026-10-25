#!/usr/bin/env python3
"""Check N64 floor entry points routed to the original recovered DS ARM query.

CPU-only differential: no live actor, emulator, GPU or hardware FPS acceptance.
The callback fixture handles one original static collider, not production
moving-platform lifetime or surface-property mapping.
"""
from pathlib import Path
import ctypes,hashlib,json,math,random,struct,subprocess,time
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_CPSR
ROOT=Path(__file__).resolve().parents[2];CORE=ROOT/'build/movement-lab/core';DS=ROOT/'build/native-collision';OUT=ROOT/'build/movement-adapter'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 OUT.mkdir(parents=True,exist_ok=True);directory=OUT/('floor-provider-'+str(time.time_ns()));directory.mkdir()
 manifest=json.loads((CORE/'host/sources.json').read_text())
 for source,digest in manifest.items():assert sha(Path(source))==digest,source
 for name in ['collision_provider.h','collision_provider.c']:
  assert (CORE/'source/src'/name).read_bytes()==(ROOT/'source/integrated_native/movement'/name).read_bytes()
 library=directory/'floor-provider-oracle.dylib'
 command=['clang','-dynamiclib','-O2','-std=gnu11','-I'+str(CORE/'source/src'),'-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(ROOT/'build/movement-lab'),str(ROOT/'tests/movement_lab/floor_provider_oracle.c'),str(CORE/'host/libmotion64.a'),'-lm','-o',str(library)]
 r=subprocess.run(command,capture_output=True,text=True);(directory/'build.log').write_text(r.stdout+r.stderr);r.check_returncode()
 binary=(DS/'runtime.bin').read_bytes();nm=subprocess.check_output(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm','-g','--defined-only',DS/'runtime.elf'],text=True)
 sy={p[2]:int(p[0],16) for line in nm.splitlines() if len(p:=line.split())==3}
 cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);cpu.mem_map(0x100000,0x100000);cpu.mem_write(0x100000,binary);cpu.mem_map(0x200000,0x100000);cpu.mem_map(0x300000,0x10000);cpu.mem_map(0x400000,0x1000)
 COLLIDER,KCL,CLPS,POS,RESULT=0x200000,0x210000,0x230000,0x240000,0x240020
 cpu.mem_write(KCL,(DS/'castle.kcl').read_bytes());cpu.mem_write(CLPS,(DS/'castle.clps').read_bytes())
 def call(name,*args):
  cpu.reg_write(UC_ARM_REG_CPSR,0x10);cpu.reg_write(UC_ARM_REG_SP,0x308000);cpu.reg_write(UC_ARM_REG_LR,0x400000)
  for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):cpu.reg_write(reg,value&0xffffffff)
  cpu.emu_start(sy[name],0x400000,count=2000000);assert cpu.reg_read(UC_ARM_REG_PC)==0x400000
  return cpu.reg_read(UC_ARM_REG_R0)
 assert call('native_collision_init',COLLIDER,KCL,CLPS)==1
 def ground_raw(x,y,z):
  cpu.mem_write(POS,struct.pack('<3i',x,y,z))
  return struct.unpack('<4iH2x',cpu.mem_read(RESULT,20)) if call('native_collision_ground',COLLIDER,POS,-20000*4096,RESULT) else None
 def ground(x,y,z):return ground_raw(round(x*4096),round(y*4096),round(z*4096))
 lib=ctypes.CDLL(str(library));lib.oracle_init();floats=[ctypes.c_float]*3
 lib.oracle_floor.argtypes=floats+[ctypes.POINTER(ctypes.c_float)];lib.oracle_floor.restype=ctypes.c_int
 lib.oracle_height.argtypes=floats;lib.oracle_height.restype=ctypes.c_float
 lib.oracle_details.argtypes=floats+[ctypes.POINTER(ctypes.c_int)];lib.oracle_details.restype=ctypes.c_float
 out=(ctypes.c_float*4)();rng=random.Random(6403)
 points=[(-1200,10000,6800)]+[(x,10000,z) for x in range(-6000,6001,500) for z in range(-6000,7501,500)]+[(rng.randrange(-4000,2000),10000,rng.randrange(2500,7000)) for _ in range(250)]
 baseline=[];old_missing=old_bad=0;old_max=0
 for x,y,z in points:
  expected=ground(x,y,z);found=bool(lib.oracle_floor(x,y,z,out));baseline.append((expected,found,tuple(out)))
  if bool(expected)!=found:old_missing+=1
  if expected and found:
   error=abs(expected[0]/4096-out[0]);old_max=max(old_max,error);old_bad+=error>2
 historical=json.loads((ROOT/'build/movement-lab/collision-audit.json').read_text())
 assert len(points)==historical['samples']==951
 assert old_missing==historical['counts']['missing_in_mesh']+historical['counts']['extra_in_mesh']==1
 assert old_bad==historical['counts']['height_error_over_2']==5 and abs(old_max-historical['max_height_error'])<1e-6
 callback_errors=[];DSQUERY=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_int32,ctypes.c_int32,ctypes.c_int32,ctypes.POINTER(ctypes.c_int32))
 @DSQUERY
 def query(x,y,z,result):
  try:
   hit=ground_raw(x,y,z)
   if hit:
    for i,v in enumerate(hit):result[i]=v
   return int(hit is not None)
  except Exception as error:callback_errors.append(str(error));return 0
 lib.oracle_provider.argtypes=[DSQUERY];lib.oracle_provider(query)
 mismatch=0;maximum=0
 for point,(expected,_,__) in zip(points,baseline):
  found=bool(lib.oracle_floor(*point,out));assert not callback_errors
  if bool(expected)!=found:mismatch+=1;continue
  if expected:
   error=abs(expected[0]/4096-out[0]);maximum=max(maximum,error)
   assert error<=1/1024 and tuple(out[1:])==tuple(v/4096 for v in expected[1:4])
 assert not mismatch and not lib.oracle_faults()
 # All three internal helpers must honor the provider, including the old miss.
 indexes=[0,next(i for i,(expected,found,_) in enumerate(baseline) if expected and not found),len(points)-1]
 helper_checks=0
 for index in indexes:
  point=points[index];expected=baseline[index][0];valid=ctypes.c_int()
  for height in [lib.oracle_height(*point),lib.oracle_details(*point,ctypes.byref(valid))]:assert abs(height-expected[0]/4096)<=1/1024;helper_checks+=1
  assert valid.value==1
 @DSQUERY
 def missing(x,y,z,result):return 0
 lib.oracle_provider(missing);valid=ctypes.c_int(1)
 assert not lib.oracle_floor(*points[0],out) and lib.oracle_height(*points[0])==-11000
 assert lib.oracle_details(*points[0],ctypes.byref(valid))==-11000 and valid.value==0
 assert not lib.oracle_faults()
 lib.oracle_provider(DSQUERY())
 for index in indexes:
  point=points[index];expected,found,old=baseline[index]
  assert bool(lib.oracle_floor(*point,out))==found
  if found:assert tuple(out)==old
 assert not callback_errors;lib.oracle_shutdown()
 report={'status':'original_DS_ARM_floor_provider_routing_validated','points':len(points),'baseline_mesh_hit_mismatches':old_missing,'baseline_height_differences_over_two':old_bad,'baseline_max_height_error':old_max,'provider_hit_mismatches':mismatch,'provider_max_height_error':maximum,'helper_checks':helper_checks,'missing_provider_never_falls_back':True,'unregister_restores_mesh':True,'sources_sha256':{str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),ROOT/'tools/movement_lab/build_core.py',ROOT/'source/integrated_native/movement/collision_provider.h',ROOT/'source/integrated_native/movement/collision_provider.c',ROOT/'tests/movement_lab/floor_provider_oracle.c']},'original_runtime_elf_sha256':sha(DS/'runtime.elf'),'castle_kcl_sha256':sha(DS/'castle.kcl'),'castle_clps_sha256':sha(DS/'castle.clps'),'core_sources':manifest,'library_sha256':sha(library),'directory':str(directory),'limits':'CPU-only floor-routing fixture, not production collider lookup/lifetimes/CLPS properties, wall/ceiling behavior, live N64/Hybrid movement, hardware or FPS proof.'}
 (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n');(OUT/'floor-provider-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:v for k,v in report.items() if k not in ['core_sources','sources_sha256']},indent=2))
if __name__=='__main__':main()
