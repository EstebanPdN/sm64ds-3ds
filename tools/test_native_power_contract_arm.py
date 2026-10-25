#!/usr/bin/env python3
"""Bounded original PM arithmetic and current fail-fast provider contracts.

This does not validate a new PM replacement. ROM-only registers/shared words
are explicit inputs. No ARM7 response, sleep completion, FS, HUD or GPU is
fabricated. Nonzero-arg LCD commands stop at their unexecuted PXI wrapper.
"""
import hashlib,json,struct
from pathlib import Path
import test_native_file_video_arm as files
import audit_native_power_contract as audit
from unicorn.arm_const import UC_ARM_REG_PC

ROOT,ELF=files.ROOT,audit.ELF
OUT=ROOT/'build/native-stage-plan/power'

class Boundary(Exception):pass

class Probe(files.Probe):
 def __init__(self,elf=None,cart=None):
  self.boundaries={};self.boundary=None;self.native=elf is not None
  # Avoid the file regression's FS opening/initialization entirely.
  self.next=files.ARENA;self.allocations={};self.events=[];self.calls={}
  files.shared.Probe.__init__(self,'native PM boundary' if elf else 'original LCD contract',elf=elf,cart=cart)
 def code(self,cpu,address,size,_):
  if address in self.boundaries:
   name=self.boundaries[address]
   self.boundary=dict(name=name,GP=[cpu.reg_read(r) for r in files.shared.REGS],PC=hex(address))
   if name=='sm64ds_native_missing':
    p=self.boundary['GP'][0]
    self.boundary['diagnostic']=bytes(cpu.mem_read(p,256)).split(b'\0')[0].decode()
   raise Boundary(name)

def main():
 OUT.mkdir(parents=True,exist_ok=True)
 assert files.sha(ELF.read_bytes())==audit.stage.HASH
 cart=files.shared.base.Cartridge(ROOT/'build/europe.pak');rom=Probe(cart=cart)
 rom.cpu.mem_map(0x04000000,0x2000);rom.cpu.mem_map(0x027ff000,0x1000)
 rom.boundaries={rom.names[n]:n for n in ('func_0205fa78','func_0205fab4','func_0205ba3c')}
 def initialize(old,saved,current):
  rom.cpu.mem_write(0x04000000,b'\xa5'*0x2000)
  rom.cpu.mem_write(0x04000304,struct.pack('<H',old))
  rom.words(rom.names['data_020a8110'],saved);rom.words(0x027ffc3c,current)
  rom.boundary=None
 results=[]
 for saved,current in [(0,0),(0,7),(0,8),(0,9),(0xffffffff,0),
  (0xfffffff8,0),(0xfffffffe,6),(1,0),(0,0xffffffff)]:
  for bypass in (0,1):
   for old in (0x820e,0x820f):
    initialize(old,saved,current);before=bytes(rom.cpu.mem_read(0x04000000,0x2000))
    expected=int(bool(bypass) or ((current-saved)&0xffffffff)>7)
    actual=rom.call('func_0205f68c',[1,0,bypass,1]);assert actual==expected
    after=bytearray(before)
    struct.pack_into('<H',after,0x304,old|1 if expected else old)
    assert bytes(rom.cpu.mem_read(0x04000000,0x2000))==bytes(after)
    assert rom.u32(rom.names['data_020a8110'])==saved
    results.append(dict(mode=1,bypass=bypass,power_before=hex(old),saved=hex(saved),current=hex(current),
     unsigned_delta=hex((current-saved)&0xffffffff),result=actual,power_after=hex(old|1 if expected else old),
     full_registers_checked=True,ABI_preserved=True))
 for old,current in [(0,0),(0x820f,8),(0xffff,0xffffffff)]:
  initialize(old,0x87654321,current);before=bytes(rom.cpu.mem_read(0x04000000,0x2000))
  assert rom.call('func_0205f68c',[0,0,0,1])==1
  after=bytearray(before);struct.pack_into('<H',after,0x304,old&~1)
  assert bytes(rom.cpu.mem_read(0x04000000,0x2000))==bytes(after)
  assert rom.u32(rom.names['data_020a8110'])==current
  results.append(dict(mode=0,current=hex(current),recorded_current=True,power_after=hex(old&~1),
   full_registers_checked=True,ABI_preserved=True))
 for mode in (2,0xffffffff):
  initialize(0x820e,5,100);before=bytes(rom.cpu.mem_read(0x04000000,0x2000))
  assert rom.call('func_0205f68c',[mode,3,1,1])==1
  assert bytes(rom.cpu.mem_read(0x04000000,0x2000))==before and rom.u32(rom.names['data_020a8110'])==5
  results.append(dict(mode=hex(mode),result=1,no_callback=True,registers_unchanged=True,ABI_preserved=True))
 wrapper_boundaries=[]
 for mode in (0,1):
  for sync in (0,1):
   initialize(0x820f,1,100)
   try:rom.call('func_0205f68c',[mode,2,1,sync]);raise AssertionError('PXI wrapper boundary absent')
   except Boundary:pass
   assert rom.boundary['name']==('func_0205fa78' if sync else 'func_0205fab4')
   assert rom.boundary['GP'][0]==2
   power=struct.unpack('<H',rom.cpu.mem_read(0x04000304,2))[0]
   assert power==(0x820e if mode==0 else 0x820f)
   assert rom.u32(rom.names['data_020a8110'])==(100 if mode==0 else 1)
   wrapper_boundaries.append(dict(mode=mode,synchronous=sync,boundary=rom.boundary,
    power_before_wrapper=hex(power),wrapper_executed=False,returned_result_claimed=False))
 rom.boundaries={rom.names['func_0205ba3c']:'func_0205ba3c',rom.names['func_0205b858']:'func_0205b858'}
 # First boot PM init is inspected up to its real PXI initialization call.
 rom.cpu.mem_write(rom.names['data_020a8104'],b'\0\0');rom.words(rom.names['data_020a8114'],0xdeadbeef,0xfacefeed)
 try:rom.call('func_0205fde8',[]);raise AssertionError('PM init PXI boundary absent')
 except Boundary:pass
 assert rom.boundary['name']=='func_0205b858'
 assert bytes(rom.cpu.mem_read(rom.names['data_020a8104'],2))==b'\1\0'
 assert bytes(rom.cpu.mem_read(rom.names['data_020a8114'],8))==b'\0'*8
 initialization=dict(boundary=rom.boundary,initialized_flag_set_before_PXI=True,transaction_fields_cleared=True,
  ARM7_readiness_and_success_not_fabricated=True)
 # The guarded second invocation returns without reaching the missing service.
 rom.boundary=None;rom.call('func_0205fde8',[])
 assert rom.boundary is None # Void return; r0 is not an initialization result.
 sleep_filters=[]
 rom.boundaries={rom.names['func_0205fc44']:'func_0205fc44'}
 for boot in (0,1,2):
  initialize(0x820f,0,10);rom.words(0x027ffc40,boot)
  rom.cpu.mem_write(0x04000208,struct.pack('<H',1));rom.words(0x04000210,0x123456)
  try:rom.call('func_0205f77c',[0xc,0x1234,0xa500]);raise AssertionError('Sleep handshake boundary absent')
  except Boundary:pass
  assert rom.boundary['GP'][:2]==[4 if boot==2 else 12,0xb734]
  assert struct.unpack('<H',rom.cpu.mem_read(0x04000208,2))[0]==1
  assert rom.u32(0x04000210)==0x40000
  sleep_filters.append(dict(boot_indicator=boot,boundary=rom.boundary,IME=1,IE='0x40000',
   sleep_or_ACK_executed=False,no_return_claimed=True))
 # The busy transaction result is genuine original code; free transaction is
 # stopped at its first send, before any response or LCD-off operation.
 rom.boundaries={rom.names['func_0205f8b0']:'func_0205f8b0'}
 rom.words(rom.names['data_020a8114'],1)
 rom.boundary=None;assert rom.call('func_0205fc44',[0xc,0])==1 and rom.boundary is None
 initialize(0x820f,0,10);rom.words(rom.names['data_020a8114'],0)
 rom.words(rom.names['data_020a8108'],0x12345678);rom.words(rom.names['data_020a810c'],0x87654321)
 try:rom.call('func_0205fc44',[0xc,0]);raise AssertionError('Sleep first send boundary absent')
 except Boundary:pass
 assert rom.boundary['GP'][0]==0x03006000 and rom.u32(rom.names['data_020a8108'])==0
 assert rom.u32(rom.names['data_020a810c'])==0x87654321
 assert struct.unpack('<H',rom.cpu.mem_read(0x04000304,2))[0]==0x820f
 first_send=dict(boundary=rom.boundary,transaction_busy_flag_set=rom.u32(rom.names['data_020a8114'])==1,
  command60_response_flag_cleared=True,LCD_still_on=True,ACK_never_fabricated=True,
  busy_transaction_return=1)
 native=Probe(elf=ELF);native.boundaries={native.names['sm64ds_native_missing']:'sm64ds_native_missing'}
 failures=[]
 for name,args,diagnostic in [('func_0205ba3c',[8,1],'ARM7 readiness requested'),
  ('IPCSend',[8,0x3006000,0],'no ARM7 acknowledgement generated'),
  ('func_0205ba3c',[32,0],'invalid readiness query'),
  ('func_0205ba3c',[8,2],'invalid readiness query')]:
  native.boundary=None
  try:native.call(name,args);raise AssertionError('Native missing-service boundary absent')
  except Boundary:pass
  assert native.boundary['name']=='sm64ds_native_missing' and diagnostic in native.boundary['diagnostic']
  failures.append(dict(function=name,args=args,boundary=native.boundary,no_result_returned=True))
 # Local callback readiness has a real implementation and no ARM7 claim.
 local=[]
 for callback in (0,files.STOP):
  native.words(native.names['data_020a7fc8']+8*4,callback)
  result=native.call('func_0205ba3c',[8,0]);assert result==int(bool(callback))
  local.append(dict(processor=0,callback=hex(callback),result=result,ABI_preserved=True))
 assert not rom.faults and not native.faults
 report=dict(status='pass',scope=__doc__,frozen_native_ELF_sha256=audit.stage.HASH,
  original_pin=files.PIN,original_LCD_cases=results,PXI_wrapper_boundaries=wrapper_boundaries,
  original_initialization=initialization,original_sleep_mode_filters=sleep_filters,original_sleep_first_send=first_send,
  native_failure_cases=failures,native_local_readiness=local,
  native_DS_MMIO_mapped=False,native_CPU_faults=native.faults,original_CPU_faults=rom.faults,
  source_sha256={str(Path(__file__).relative_to(ROOT)):files.sha(Path(__file__).read_bytes())},
  limits=['Reference-only PM arithmetic gate; no replacement PM provider is claimed tested.',
   'PXI wrappers, DS ARM7 readiness/sleep and physical APT notifications are never completed or simulated.'],
  HUD_or_file_tests_rerun=False,production_changed=False,UI_launched=False)
 (OUT/'arm-contract.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(dict(status='pass',LCD_cases=len(results),PXI_boundaries=len(wrapper_boundaries),
  sleep_filters=len(sleep_filters),native_explicit_failures=len(failures),local_readiness_cases=len(local))))

if __name__=='__main__':main()
