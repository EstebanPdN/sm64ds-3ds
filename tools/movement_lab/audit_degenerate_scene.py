#!/usr/bin/env python3
"""Validate the complete real Whomp registry/cache and its degenerate prism in saved RAM."""
from pathlib import Path
import sys,argparse,ctypes,subprocess,json,struct,time,hashlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from inspect_movement_colliders import inspect
from read_mapped_capture import MappedCapture
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import *
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--elf',required=True,type=Path);a=p.parse_args();inventory=inspect(a.capture,a.elf);vm=MappedCapture(a.capture)
 assert inventory['ELF_anchor_bias']==0 and vm.complete
 with a.elf.open('rb') as f:e=ELFFile(f);sy={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for r in vm.regions:
  if r['captured_bytes']==r['size'] and r['size']:cpu.mem_map(r['address'],r['size']);cpu.mem_write(r['address'],vm.read(r['address'],r['size']))
 scratch=0xe000000;cpu.mem_map(scratch,0x10000);HIT=scratch;PRISM=scratch+256;STOP=scratch+4096;STACK=scratch+0xf000
 cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);cpu.reg_write(UC_ARM_REG_FPEXC,1<<30);calls={};errors=[]
 def call(name,*args):
  calls[name]=calls.get(name,0)+1;cpu.reg_write(UC_ARM_REG_CPSR,0x10);cpu.reg_write(UC_ARM_REG_SP,STACK);cpu.reg_write(UC_ARM_REG_LR,STOP)
  for i,r in enumerate((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)):cpu.reg_write(r,(args[i] if i<len(args) else 0)&0xffffffff)
  cpu.emu_start(sy[name],STOP,count=3000000);assert cpu.reg_read(UC_ARM_REG_PC)==STOP and cpu.reg_read(UC_ARM_REG_SP)==STACK,name
  v=cpu.reg_read(UC_ARM_REG_R0);return v if v<0x80000000 else v-0x100000000
 B=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint)
 @B
 def bridge(tag,inp,n,out,m):
  try:
   data=ctypes.string_at(inp,n);payload=b''
   if tag==1:return call('sm64ds_world_prism_count',struct.unpack('<I',data)[0])
   if tag==2:
    slot,tri=struct.unpack('<2I',data);status=call('sm64ds_world_prism_at',slot,tri,HIT,PRISM)
    if status==1:payload=bytes(cpu.mem_read(HIT,40))+bytes(cpu.mem_read(PRISM,112))
   elif tag==3:
    cpu.mem_write(HIT,data[:40]);return call('sm64ds_contact_pass_through',HIT,struct.unpack_from('<I',data,40)[0],data[44],data[45])
   else:raise ValueError(tag)
   if payload:assert len(payload)==m;ctypes.memmove(out,payload,m)
   return status
  except Exception as error:errors.append(repr(error));return -1
 out=ROOT/'build/movement-adapter'/('degenerate-scene-'+str(time.time_ns()));out.mkdir(parents=True);target=out/'fixture.dylib'
 fixture=ROOT/'tests/movement_lab/degenerate_scene.cpp';command=['clang++','-std=c++17','-O1','-g','-dynamiclib','-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(ROOT/'build/movement-lab/core/source/src'),str(fixture),'-o',str(target)]
 subprocess.run(command,check=True);lib=ctypes.CDLL(str(target));lib.fixture_bridge.argtypes=[B];lib.fixture_bridge(bridge);lib.fixture_decode.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.POINTER(ctypes.c_double)];lib.fixture_contact.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.POINTER(ctypes.c_float)]
 statuses={};special=[]
 for c in inventory['colliders']:
  if 'triangles' not in c:continue
  for tri in range(1,c['triangles']+1):
   assert call('sm64ds_world_prism_at',c['slot'],tri,HIT,PRISM)==1
   hit=bytes(cpu.mem_read(HIT,40));prism=bytes(cpu.mem_read(PRISM,112));normal=hit[4:16]
   if tri==375:print('tri375',struct.unpack('<i3i12h2H',prism[:44]),'normal',struct.unpack('<3i',normal),flush=True)
   if abs(struct.unpack_from('<i',normal,4)[0])>40:continue
   points=(ctypes.c_double*9)();status=lib.fixture_decode(prism,normal,points);statuses[status]=statuses.get(status,0)+1;assert status in (0,6),(tri,status)
   if status==6:
    center=[sum(points[i+j] for i in (0,3,6))/3 for j in range(3)];near=(ctypes.c_float*3)(*center);far=(ctypes.c_float*3)(center[0]+10000,center[1],center[2]+10000)
    assert lib.fixture_contact(prism,normal,near)==-2 and lib.fixture_contact(prism,normal,far)==0
    special.append(dict(slot=c['slot'],triangle=tri,vertices=list(points),near_original_DS_owner=True,far_no_delegation=True))
 result=lib.fixture_build()
 print("diagnostic",statuses,"special",len(special),"build",result,"size",lib.fixture_size(),"errors",errors,flush=True)
 assert special and result==1 and not errors
 report=dict(status='complete_real_registry_cached_with_explicit_degenerate_DS_ownership',capture_sha256=sha(a.capture),ELF_sha256=sha(a.elf),wall_decode_counts=statuses,cache_entries=lib.fixture_size(),special_prisms=special,original_ARM_calls=calls,errors=errors,source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),fixture]+list((ROOT/'source/integrated_native/movement').glob('*.hpp'))},limits='Offline complete saved process RAM and original ARM query calls, host wall cache. No live movement, physical FPS or all-world acceptance.')
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));print(out)
if __name__=='__main__':main()
