#!/usr/bin/env python3
"""Real ARM Fog/FogTable and Player::CanPause caller versus original ROM.

Native video backing is real production code. Original ROM uses an explicitly
mapped DS register window as its reference memory. Native CPU never maps DS
addresses. CPU fixture globals provide input/state identities; no FS/GPU/game
lifecycle executes and the closed HUD/file regressions are not run.
"""
from collections import Counter
import hashlib,json,random,re,struct,subprocess
from pathlib import Path
import test_native_hud_video_arm as hud
from unicorn.arm_const import UC_ARM_REG_PC,UC_ARM_REG_LR

files=hud.files;ROOT,TOOL=files.ROOT,files.TOOL
OUT=ROOT/'build/native-file-video/fog-canpause'
FOG='_ZN3G3X6SetFogEbiii';TABLE='_ZN3G3X11SetFogTableEPv';PAUSE='_ZN6Player8CanPauseEv'
REGS=files.shared.REGS

def build():
 OUT.mkdir(parents=True,exist_ok=True)
 flags=['-O2','-D__3DS__','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
  '-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-Wa,--noexecstack',
  '-I'+str(ROOT/'include'),'-I'+str(ROOT/'build/native-core/generated/include')]
 sources=[ROOT/'tests/test_native_file_video_fog_arm.cpp',
  ROOT/'source/game_port/native_video.cpp',ROOT/'source/game_port/native_video_services.cpp',
  ROOT/'source/ctr/game_memory_primitives.S',
  *[ROOT/'build/native-runtime/scene-video'/(n+suffix) for n,suffix in [(FOG,'.cpp'),(TABLE,'.cpp'),('func_02029408','.c')]],
  ROOT/'build/native-core/generated/src/actors/Player.cpp.parts/000.cpp',
  ROOT/'build/native-core/generated/src/_ZN6Player7IsStateERNS_5StateE.cpp',
  ROOT/'build/native-core/generated/src/Copy32Bytes.c']
 hashes={str(p.relative_to(ROOT)):files.sha(p.read_bytes()) for p in sources}
 objects=[]
 for i,path in enumerate(sources):
  target=OUT/('source-'+str(i)+'.o');scene='scene-video' in path.parts
  asm=path.suffix=='.S' or path.read_bytes().startswith(b'.syntax unified')
  c=path.suffix=='.c' and not scene and not asm
  command=[TOOL+('gcc' if c or asm else 'g++'),*flags]
  if asm:command+=['-x','assembler-with-cpp']
  if scene:command+=['-x','c++']
  if not asm:command+=['-std=c11' if c else '-std=gnu++17']
  result=subprocess.run([*command,'-c',str(path),'-o',str(target)],capture_output=True,text=True,cwd=ROOT)
  (OUT/('compile-'+str(i)+'.log')).write_text(result.stdout+result.stderr);result.check_returncode();objects.append(str(target))
 roots=[FOG,TABLE,'func_02029408',PAUSE,
  '_ZN6sm64ds18native_video_resetEv','_ZN6sm64ds20native_video_pointerEmj',
  '_ZN6sm64ds18native_video_writeEmyj','_ZN6sm64ds15native_video_okEv',
  '_ZN6sm64ds24native_video_bad_addressEv']
 elf=OUT/'fog-canpause-arm.elf'
 result=subprocess.run([TOOL+'g++',*flags,'-nostartfiles','-specs=nosys.specs',
  '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x01000000,-e,'+FOG,
  *['-Wl,-u,'+n for n in roots],*objects,'-o',str(elf)],capture_output=True,text=True,cwd=ROOT)
 (OUT/'link.log').write_text(result.stdout+result.stderr);result.check_returncode()
 subprocess.run([TOOL+'objdump','-d',str(elf)],check=True,stdout=(OUT/'disassembly.txt').open('w'))
 return elf,hashes

class Probe(files.Probe):
 def __init__(self,elf):
  self.next=files.ARENA;self.allocations={};self.events=[];self.calls=Counter();self.native=True;self.trace=[]
  files.shared.Probe.__init__(self,'production ARM11 Fog/CanPause',elf=elf)
  self.cpu.mem_map(files.WORK+0x10000,0x30000)
  self.observed={self.names[n]:n for n in [FOG,TABLE,'Copy32Bytes','func_02029408',PAUSE,'_ZN6Player7IsStateERNS_5StateE']}
  self.video=None
 def code(self,cpu,address,size,_):
  name=getattr(self,'observed',{}).get(address)
  if name:
   self.calls[name]+=1;self.trace.append(dict(function=name,GP=[cpu.reg_read(r) for r in REGS]))
  if address==self.names.get('test_hud_unexpected'):
   p=cpu.reg_read(REGS[0]);message=bytes(cpu.mem_read(p,160)).split(b'\0')[0].decode(errors='replace')
   raise AssertionError('Unexpected boundary '+message)
 def configure(self):
  self.call('_ZN6sm64ds18native_video_resetEv',[]);self.words(self.names['test_hud_failures'],0)
  self.video={name:(self.pointer(address,size),size) for name,address,size in
   [('registers',0x04000000,0x2000),('palette',0x05000000,0x800),('vram',0x06800000,0xa4000),('oam',0x07000000,0x800)]}
  for name,(p,size) in self.video.items():self.cpu.mem_write(p,b'\xa5'*size)

def main():
 assert subprocess.check_output(['git','-C',str(ROOT/'upstream'),'rev-parse','HEAD'],text=True).strip()==files.PIN
 elf,hashes=build();cart=files.shared.base.Cartridge(ROOT/'build/europe.pak');native=Probe(elf)
 original=files.Probe(cart=cart);base,_,ov=cart.image(2);original.cpu.mem_write(base,ov)
 for line in (ROOT/'upstream/config/arm9/overlays/ov002/symbols.txt').read_text().splitlines():
  match=re.match(r'(\S+) kind:\S+ addr:0x([0-9a-f]+)',line)
  if match:original.names[match[1]]=int(match[2],16)
 original.cpu.mem_map(0x04000000,0x2000)
 rng=random.Random(0x53544746);fog=[];tables=[]
 for i in range(48):
  native.configure();before=native.video_bytes();regs=bytearray(before['registers'])
  old=rng.randrange(65536);oldoffset=rng.randrange(65536)
  struct.pack_into('<H',regs,0x60,old);struct.pack_into('<H',regs,0x35c,oldoffset)
  native.cpu.mem_write(native.video['registers'][0],bytes(regs));original.cpu.mem_write(0x04000000,bytes(regs))
  # Fog shifts 0..10 avoid writing the DISP3DCNT W1C error bits. The ROM
  # reference is plain mapped RAM, not a DS register implementation: larger
  # shift values would falsely preserve W1C bits that native_video_write clears.
  enabled=i%2;a=(i//2)%2;b=(i*13)%11;c=[0,32767,65535,0x12345678][(i//4)%4]
  native.call(FOG,[enabled,a,b,c]);original.call(FOG,[enabled,a,b,c])
  expected=bytes(original.cpu.mem_read(0x04000000,0x2000))
  files.verify_video(native,{**before,'registers':bytes(regs)},[('registers',0,expected)])
  assert native.okay() and native.u32(native.names['test_hud_failures'])==0
  changed=[offset for offset,(x,y) in enumerate(zip(regs,expected)) if x!=y]
  assert set(changed)<=({0x60,0x61,0x35c,0x35d} if enabled else {0x60,0x61})
  fog.append(dict(enable=enabled,a=a,b=b,c=hex(c),incoming_DISP3DCNT=hex(old),
   expected_DISP3DCNT=hex(struct.unpack_from('<H',expected,0x60)[0]),full_video_compare=True,ABI_preserved=True))
 for i in range(16):
  native.configure();before=native.video_bytes();payload=bytes(rng.randrange(256) for _ in range(32))
  p=files.WORK+0x1000;native.cpu.mem_write(p-32,b'\x71'*32+payload+b'\x72'*32)
  original.cpu.mem_write(p-32,b'\x71'*32+payload+b'\x72'*32);original.cpu.mem_write(0x04000000,before['registers'])
  trace=len(native.trace);native.call(TABLE,[p]);original.call(TABLE,[p])
  expected=bytes(original.cpu.mem_read(0x04000000,0x2000))
  assert expected[0x360:0x380]==payload
  files.verify_video(native,before,[('registers',0,expected)])
  assert bytes(native.cpu.mem_read(p-32,96))==b'\x71'*32+payload+b'\x72'*32
  calls=native.trace[trace:];copy=[t for t in calls if t['function']=='Copy32Bytes'];assert len(copy)==1
  assert copy[0]['GP'][0]==p and copy[0]['GP'][1]==native.video['registers'][0]+0x360
  assert native.okay() and native.u32(native.names['test_hud_failures'])==0
  tables.append(dict(payload_sha256=files.sha(payload),all_32_bytes_equal=True,full_video_compare=True,
   source_and_canaries_unchanged=True,Copy32Bytes_target_native=True,ABI_preserved=True))
 pause=[];player=files.WORK+0x20000
 for index in range(4):
  for label,flags,state,expected in [('normal',(0,0,0,0),None,1),('airborne',(0,1,0,0),None,0),
   ('damage',(0,0,1,0),None,0),('no_control',(0,0,0,1),None,0),('underwater_other',(1,0,0,0),None,0),
   ('underwater_state_a',(1,1,1,1),'data_ov002_0211067c',1),('underwater_state_b',(1,1,1,1),'data_ov002_021106ac',1)]:
   for p in [native,original]:
    p.cpu.mem_write(player-32,b'\x91'*32+b'\0'*0x800+b'\x92'*32)
    for offset,value in zip([0x706,0x6de,0x708,0x709],flags):p.cpu.mem_write(player+offset,bytes([value]))
    p.words(player+0x370,p.names[state] if state else 0)
    p.cpu.mem_write(p.names['data_0209f250'],bytes([index]));p.words(p.names['data_0209f394']+index*4,player)
   initial=bytes(native.cpu.mem_read(player-32,0x840));trace=len(native.trace)
   actual=native.call('func_02029408',[0x13579bdf,0x2468ace0,0xfedcba98,0x01234567])
   reference=original.call('func_02029408',[0x13579bdf,0x2468ace0,0xfedcba98,0x01234567])
   assert actual==reference==expected
   assert bytes(native.cpu.mem_read(player-32,0x840))==initial
   selected=[t for t in native.trace[trace:] if t['function']==PAUSE];assert len(selected)==1 and selected[0]['GP'][0]==player
   pause.append(dict(index=index,case=label,flags=flags,state=state,result=actual,ROM_equal=True,
    real_member_called=True,selected_this=hex(player),observed_callee_GP=[hex(v) for v in selected[0]['GP']],
    player_and_canaries_unchanged=True,ABI_preserved=True))
 assert not native.faults and not original.faults
 assert hashes=={p:files.sha((ROOT/p).read_bytes()) for p in hashes}
 frozen=dict(hashes)
 for p in ['tests/test_native_hud_video_arm.cpp','tools/test_native_file_video_fog_arm.py',
  'tools/test_native_hud_video_arm.py','tools/test_native_file_video_arm.py','include/sm64ds/native_register.hpp',
  'include/sm64ds/native_video.hpp',*['build/native-core/generated/include/'+p for p in ['types.h','Player.h']]]:
  frozen[p]=files.sha((ROOT/p).read_bytes())
 for p,h in frozen.items():
  target=OUT/'frozen-sources'/p;target.parent.mkdir(parents=True,exist_ok=True);raw=(ROOT/p).read_bytes();assert files.sha(raw)==h;target.write_bytes(raw)
 report=dict(status='pass',scope=__doc__,ELF_sha256=files.sha(elf.read_bytes()),source_sha256=hashes,frozen_source_sha256=frozen,
  original_pin=files.PIN,Fog_cases=fog,FogTable_cases=tables,CanPause_caller_cases=pause,
  native_CPU_faults=native.faults,original_CPU_faults=original.faults,owned_video_bytes=684032,native_DS_bus_mapped=False,
  seams=['Fixture globals and Player state object identities are inputs; methods are real code.',
   'Unreached FS/GX/IRQ boundaries trap. The original reference alone maps its DS register window.'],
  limits=['Fog b=0..10. ROM reference register memory does not emulate W1C status behavior; out-of-range b values that write bits 12/13 are outside this equality gate.',
   'No DS GPU behavior, physical timing, exceptional invalid pointers or complete Stage lifecycle is established.'],
  FS_executed=False,HUD_rerun=False,GPU_executed=False,Azahar_launched=False,production_changed=False)
 (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(dict(status='pass',Fog_cases=len(fog),FogTable_cases=len(tables),CanPause_cases=len(pause),ELF_sha256=report['ELF_sha256'])))

if __name__=='__main__':main()
