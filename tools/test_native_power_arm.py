#!/usr/bin/env python3
"""Real ARM production LCD/IRQ versus ROM; real Citro3D getter and callbacks.

GSP registration is an observed CPU boundary and callbacks are invoked as
explicit test events. Hardware notifications, physical sleep and ARM7 replies
are not claimed. No original HUD/file tests or complete game frames execute.
"""
import json,struct,subprocess
from pathlib import Path
import test_native_file_video_arm as files
import test_native_power_contract_arm as contract

ROOT,TOOL=files.ROOT,files.TOOL
OUT=ROOT/'build/native-stage-plan/power/candidate'
LIB=Path('/opt/devkitpro/libctru/lib/libcitro3d.a')
ROOTS=['func_0205f650','func_0205f66c','func_0205f68c','_ZN3IRQ7SetIRQsEj',
 'sm64ds_native_vblank_count','C3D_FrameCounter','C3Di_RenderQueueEnableVBlank',
 'test_power_setup','test_power_state_size','test_power_mask_offset','test_power_state_copy','test_power_state_info',
 '_ZN6sm64ds18native_video_resetEv','_ZN6sm64ds20native_video_pointerEmj',
 '_ZN6sm64ds15native_video_okEv']

def build():
 OUT.mkdir(parents=True,exist_ok=True)
 flags=['-O2','-D__3DS__','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
  '-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-Wa,--noexecstack',
  '-I'+str(ROOT/'include'),'-I'+str(ROOT/'build/native-core/generated/include'),'-I/opt/devkitpro/libctru/include']
 sources=[ROOT/'tests/test_native_power_arm.cpp',ROOT/'source/game_port/native_platform_abi.cpp',
  ROOT/'source/game_port/native_video.cpp',
  *[ROOT/'build/native-runtime/scene-video'/(n+'.c') for n in ['func_0205f650','func_0205f68c']],
  *[ROOT/'build/native-core/generated/src'/(n+'.c') for n in ['func_0205f66c','func_0205fa78',
   'func_0205fab4','func_0205fbd0','func_0205ff20','func_0205f8b0','func_0205ff08','func_0205ff00']]]
 objects=[]
 for i,path in enumerate(sources):
  scene='scene-video' in path.parts;c=path.suffix=='.c' and not scene
  cmd=[TOOL+('gcc' if c else 'g++'),*flags]
  if scene:cmd+=['-x','c++']
  cmd+=['-std=c11' if c else '-std=gnu++17','-c',str(path),'-o',str(OUT/('source-'+str(i)+'.o'))]
  result=subprocess.run(cmd,capture_output=True,text=True);(OUT/('compile-'+str(i)+'.log')).write_text(result.stdout+result.stderr)
  result.check_returncode();objects.append(str(OUT/('source-'+str(i)+'.o')))
 elf=OUT/'power-arm.elf'
 result=subprocess.run([TOOL+'g++',*flags,'-nostartfiles','-specs=nosys.specs',
  '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x01000000,-e,func_0205f650',
  *['-Wl,-u,'+n for n in ROOTS],*objects,str(LIB),'-o',str(elf)],capture_output=True,text=True)
 (OUT/'link.log').write_text(result.stdout+result.stderr);result.check_returncode()
 (OUT/'disassembly.txt').write_text(subprocess.check_output([TOOL+'objdump','-d',str(elf)],text=True))
 # Platform is included unchanged so its private state can be copied exactly.
 all_sources=[*sources,ROOT/'source/game_port/native_platform.cpp',ROOT/'include/sm64ds/native_platform.hpp',
  ROOT/'include/sm64ds/native_register.hpp',ROOT/'include/sm64ds/native_video.hpp',Path(__file__),
  Path('/opt/devkitpro/libctru/include/c3d/renderqueue.h'),Path('/opt/devkitpro/libctru/include/3ds/services/gspgpu.h'),LIB]
 return elf,{str(p):files.sha(p.read_bytes()) for p in all_sources}

class Probe(contract.Probe):
 def __init__(self,elf):
  self.callbacks={};super().__init__(elf=elf)
  self.boundaries={self.names[n]:n for n in ['sm64ds_native_missing','test_power_unexpected'] if n in self.names}
  self.cpu.mem_map(files.WORK+0x10000,0x30000)
  self.video=None
 def code(self,cpu,address,size,_):
  if address==self.names.get('gspSetEventCallback'):
   args=[cpu.reg_read(r) for r in files.shared.REGS]
   assert args[0] in (2,3) and args[1]!=0 and args[2:]==[0,0]
   self.callbacks[args[0]]=args[1]
   cpu.reg_write(files.UC_ARM_REG_PC,cpu.reg_read(files.UC_ARM_REG_LR));return
  super().code(cpu,address,size,_)
 def configure(self,old,count,saved):
  self.call('test_power_setup',[0,0,0x80,1]);self.call('_ZN6sm64ds18native_video_resetEv',[])
  self.video={name:(self.pointer(address,size),size) for name,address,size in
   [('registers',0x04000000,0x2000),('palette',0x05000000,0x800),('vram',0x06800000,0xa4000),('oam',0x07000000,0x800)]}
  for p,size in self.video.values():self.cpu.mem_write(p,b'\xa5'*size)
  self.cpu.mem_write(self.video['registers'][0]+0x304,struct.pack('<H',old))
  self.words(self.names['frameCounter'],count,0x13579bdf)
  self.words(self.names['data_020a8110'],saved);self.words(self.names['data_020a8114'],0,0,0,0)
  self.boundary=None

def main():
 elf,hashes=build();native=Probe(elf)
 cart=files.shared.base.Cartridge(ROOT/'build/europe.pak');rom=contract.Probe(cart=cart)
 rom.cpu.mem_map(0x04000000,0x2000);rom.cpu.mem_map(0x027ff000,0x1000)
 lcd=[]
 for mode,arg2,saved,current in [(m,b,s,c) for m in (0,1,2,0xffffffff) for b in (0,1)
  for s,c in [(0,0),(0,7),(0,8),(0,9),(0xffffffff,0),(0xfffffff8,0),(0xfffffffe,6),(1,0),(0,0xffffffff)]]:
  old=0x820e|(current&1);native.configure(old,current,saved);before=native.video_bytes()
  rom.cpu.mem_write(0x04000000,before['registers']);rom.words(0x027ffc3c,current);rom.words(rom.names['data_020a8110'],saved)
  args=[mode,0,arg2,1];actual=native.call('func_0205f68c',args);reference=rom.call('func_0205f68c',args)
  assert actual==reference
  expected=bytes(rom.cpu.mem_read(0x04000000,0x2000));files.verify_video(native,before,[('registers',0,expected)])
  assert native.u32(native.names['data_020a8110'])==rom.u32(rom.names['data_020a8110'])
  assert native.call('sm64ds_native_vblank_count',[])==current
  assert native.call('func_0205f650',[])==int(bool(struct.unpack_from('<H',expected,0x304)[0]&1))
  lcd.append(dict(mode=hex(mode),bypass=arg2,saved=hex(saved),current=hex(current),return_value=actual,
   ROM_equal=True,full_video_equal=True,ABI_preserved=True))
 toggles=[]
 for arg in (0,1,2,0xffffffff):
  native.configure(0x820f,8,0);before=native.video_bytes()
  rom.cpu.mem_write(0x04000000,before['registers']);rom.words(0x027ffc3c,8);rom.words(rom.names['data_020a8110'],0)
  actual=native.call('func_0205f66c',[arg]);reference=rom.call('func_0205f66c',[arg]);assert actual==reference==1
  expected=bytes(rom.cpu.mem_read(0x04000000,0x2000));files.verify_video(native,before,[('registers',0,expected)])
  assert native.u32(native.names['data_020a8110'])==rom.u32(rom.names['data_020a8110'])
  toggles.append(dict(argument=hex(arg),mode=1 if arg==1 else 0,ROM_equal=True,ABI_preserved=True))
 callbacks=[]
 native.call('C3Di_RenderQueueEnableVBlank',[]);assert set(native.callbacks)=={2,3}
 for index,event in [(0,2),(1,3)]:
  native.words(native.names['frameCounter'],0xffffffff,0xffffffff)
  alias='test_observed_callback_'+str(index);native.names[alias]=native.callbacks[event]
  native.call(alias,[0]);assert native.call('C3D_FrameCounter',[index])==0
  assert native.call('C3D_FrameCounter',[1-index])==0xffffffff
  if index==0:assert native.call('sm64ds_native_vblank_count',[])==0
  native.call(alias,[0]);assert native.call('C3D_FrameCounter',[index])==1
  if index==0:
   assert native.call('sm64ds_native_vblank_count',[])==1
   native.call('test_power_state_info',[files.WORK+0x2000])
   assert native.u32(files.WORK+0x2000+5*4)==0
  callbacks.append(dict(event=event,index=index,callback_address=hex(native.callbacks[event]),
   real_archive_callback=True,wrap_checked=True,logical_clock_unchanged=index==0,GSP_notifications_simulated=False,
   input='explicit CPU invocation of observed callback; no physical GSP event claimed'))
 state_size=native.call('test_power_state_size',[]);mask_offset=native.call('test_power_mask_offset',[])
 state=files.WORK+0x1000;irq=[]
 for master in (0,1):
  for cpsr in (0,0x40,0x80,0xc0):
   for old,pending,new in [(0,0,0x40000),(0x123456,0x234567,0),(0x3fffff,0x3fffff,0x123456),
    (0x123456,0x222222,0xffffffff)]:
    native.call('test_power_setup',[old,pending,cpsr,master]);native.call('test_power_state_copy',[state])
    before=bytes(native.cpu.mem_read(state,state_size))
    result=native.call('_ZN3IRQ7SetIRQsEj',[new]);assert result==old
    native.call('test_power_state_copy',[state]);after=bytes(native.cpu.mem_read(state,state_size))
    expected=bytearray(before);struct.pack_into('<I',expected,mask_offset,new&0x3fffff);assert after==bytes(expected)
    # Plain-ROM register reference uses valid effective bits to avoid assuming
    # behavior of hardware-reserved IE bits in an ordinary RAM observer.
    rom.cpu.mem_write(0x04000208,struct.pack('<H',master));rom.words(0x04000210,old)
    assert rom.call('_ZN3IRQ7SetIRQsEj',[new&0x3fffff])==old
    assert rom.u32(0x04000210)==new&0x3fffff
    assert struct.unpack('<H',rom.cpu.mem_read(0x04000208,2))[0]==master
    irq.append(dict(old=hex(old),pending=hex(pending),requested=hex(new),result=hex(result),
     master=master,cpsr=hex(cpsr),only_enabled_mask_changed=True,effective_mask_ROM_equal=True,ABI_preserved=True))
 failures=[]
 for mode,sync in [(0,0),(0,1),(1,0),(1,1)]:
  native.configure(0x820e,8,0)
  try:native.call('func_0205f68c',[mode,1,1,sync]);raise AssertionError('PXI failure absent')
  except contract.Boundary:pass
  assert native.boundary['name']=='sm64ds_native_missing'
  assert 'no ARM7 acknowledgement generated' in native.boundary['diagnostic']
  assert native.u32(native.names['data_020a8110'])==(8 if mode==0 else 0)
  assert struct.unpack('<H',native.cpu.mem_read(native.video['registers'][0]+0x304,2))[0]==0x820e
  failures.append(dict(mode=mode,synchronous=sync,missing=native.boundary,no_result_returned=True,no_ACK_fabricated=True))
 assert not native.faults and not rom.faults
 assert hashes=={name:files.sha(Path(name).read_bytes()) for name in hashes}
 for name,h in hashes.items():
  path=Path(name);relative=path.relative_to(ROOT) if path.is_relative_to(ROOT) else Path(str(path).lstrip('/'))
  dst=OUT/'frozen-inputs'/relative;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(path.read_bytes())
 report=dict(status='pass',scope=__doc__,ELF_sha256=files.sha(elf.read_bytes()),source_and_archive_sha256=hashes,
  LCD_cases=lcd,wrapper_cases=toggles,Citro3D_callback_cases=callbacks,IRQ_cases=irq,PXI_failures=failures,
  owned_video_bytes=684032,native_DS_MMIO_mapped=False,native_CPU_faults=native.faults,ROM_CPU_faults=rom.faults,
  seams=['svcGetSystemTick returns initialization input0; no timer conversion claim.',
   'GSP registration observed; retained original archive callbacks invoked as explicit CPU events.',
   'Native missing-service entry stops CPU before returning; no ARM7 feedback is supplied.',
   'Fixture white-box helpers only serialize real private Platform state and configure it via production APIs.'],
  limits=['No physical GSP/APT/sleep or complete Stage lifecycle execution.',
   'IRQ reserved bits: original RAM observer receives effective22-bit mask; native full32 input also tested.'],
  HUD_or_file_tests_rerun=False,production_changed=False,UI_launched=False)
 (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(dict(status='pass',ELF_sha256=report['ELF_sha256'],LCD_cases=len(lcd),wrapper_cases=len(toggles),
  IRQ_cases=len(irq),Citro3D_callback_cases=len(callbacks),PXI_failures=len(failures))))

if __name__=='__main__':main()
