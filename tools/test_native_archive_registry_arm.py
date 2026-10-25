#!/usr/bin/env python3
"""Fresh bounded CPU gate for the real ROM anchor and native NARC lifecycle.

No snapshots, Stage, UI, GPU or card-DMA initialization. Raw pack reads and
guarded allocations are explicit seams; registration/parser/open/read/close/
retirement execute production ARM. Original ROM registers identical NARC bytes
at identical fixture addresses, permitting complete record-byte comparison.
"""
import json,struct,subprocess
from pathlib import Path
import test_native_file_video_arm as files
shared=files.shared
ROOT,TOOL=shared.ROOT,shared.TOOL
OUT=ROOT/'build/native-stage-plan/archive-registry-gate'
INPUT,PATH,NAME,FILE,OUTPUT,INFO=(files.WORK+0x1000,files.WORK+0x100,
 files.WORK+0x300,files.WORK+0x700,files.WORK+0x20000,files.WORK+0x800)
CARD_SHA='d76b198cc7ac4ab1502a6534100f964dacf478163e3d0696461388ff99c6427b'
SDK=['func_0204ee40','func_0204ee10','func_0204ef9c','func_0205cd34',
 'func_0205cc80','func_0205cbe4','func_0205d304','func_0205d23c',
 'func_0205cb68','func_0205caa8','func_0205c864','func_0205d2dc','func_0205d2b0','FS_InitFile']
ROOTS=['test_archive_initialize','test_archive_set_payload','test_archive_observe',
 'sm64ds_native_register_rom_archive','func_02018934','func_02018908',
 'func_0205d568','FS_ReadFile','FS_CloseFile','func_0205d23c','FS_InitFile']

def build():
 OUT.mkdir(parents=True,exist_ok=True)
 assert files.sha((ROOT/'source/game_port/native_card.cpp').read_bytes())==CARD_SHA
 flags=['-O2','-D__3DS__','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
  '-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-Wa,--noexecstack',
  '-I'+str(ROOT/'include'),'-I'+str(ROOT/'build/native-core/generated/include'),
  '-I/opt/devkitpro/libctru/include']
 sources=[ROOT/'tests/test_native_archive_registry_arm.cpp',ROOT/'source/ctr/game_files.cpp',
  ROOT/'source/core/narc.cpp',ROOT/'source/game_port/native_platform_abi.cpp',ROOT/'source/game_port/native_platform.cpp',
  ROOT/'source/game_port/native_video.cpp',ROOT/'source/ctr/game_memory_primitives.S',
  *[ROOT/'build/native-core/generated/src'/(n+'.c') for n in SDK]]
 objects=[]
 for i,p in enumerate(sources):
  c=p.suffix=='.c';asm=p.suffix=='.S';o=OUT/('source-'+str(i)+'.o')
  cmd=[TOOL+('gcc' if c or asm else 'g++'),*flags]
  if not asm:cmd+=['-std=c11' if c else '-std=gnu++20']
  result=subprocess.run([*cmd,'-c',str(p),'-o',str(o)],capture_output=True,text=True)
  (OUT/('compile-'+str(i)+'.log')).write_text(result.stdout+result.stderr)
  result.check_returncode();objects.append(str(o))
 elf=OUT/'archive-registry-arm.elf'
 result=subprocess.run([TOOL+'g++',*flags,'-nostartfiles','-specs=nosys.specs',
  '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x01000000,-e,test_archive_initialize',
  *['-Wl,-u,'+n for n in ROOTS],*objects,'-o',str(elf)],capture_output=True,text=True)
 (OUT/'link.log').write_text(result.stdout+result.stderr);result.check_returncode()
 inputs=list(dict.fromkeys([*sources,ROOT/'source/game_port/native_card.cpp',ROOT/'source/game_port/native_platform.cpp',
  Path(__file__),Path(files.__file__),Path(shared.__file__),Path(shared.base.__file__),
  ROOT/'tools/test_native_menu_constructors.py',ROOT/'include/sm64ds/game_files.hpp',ROOT/'include/sm64ds/assets.hpp']))
 hashes={str(p.relative_to(ROOT)):files.sha(p.read_bytes()) for p in inputs}
 frozen=OUT/'frozen-sources'
 for p in inputs:
  dst=frozen/p.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(p.read_bytes())
 return elf,hashes,flags

class Probe(files.Probe):
 def __init__(self,elf=None,cart=None):
  self.native=elf is not None;self.next=files.ARENA;self.allocations={};self.events=[]
  shared.Probe.__init__(self,'fresh native archive' if elf else 'ROM archive',elf=elf,cart=cart)
  self.cpu.mem_map(files.WORK+0x10000,0x30000);self.cpu.mem_map(files.ARENA,files.ARENA_END-files.ARENA)
  if self.native:
   for old,new in [('allocate','allocate'),('free','free'),('unexpected','unexpected')]:
    self.names['test_file_video_'+old]=self.names['test_archive_'+new]
   self.call('test_archive_initialize',[])
  else:
   self.cpu.mem_map(0x04000000,0x3000) # Original IRQ code only; native bus stays unmapped.
   self.words(self.names['data_020a8048'],0);self.words(self.names['data_020a804c'],0,0,0)
   self.cpu.mem_write(self.anchor,bytes(0x60))
  self.observed={};self.steps=0;self.entries_count={}
 def code(self,cpu,address,size,unused):
  self.steps=getattr(self,'steps',0)+1
  for n in ('OS_SleepThread','func_0205c788'):
   if address==self.names.get(n):raise AssertionError('External SDK boundary reached: '+n)
  super().code(cpu,address,size,unused)
 @property
 def anchor(self):return self.names['data_020a8074']
 def bootstrap(self):
  if self.native:return self.call('sm64ds_native_register_rom_archive',[])
  self.call('func_0205cd34',[self.anchor]);return self.call('func_0205cc80',[self.anchor,self.names['data_0209a048'],3])
 def text(self,address,text):self.cpu.mem_write(address,text.encode()+b'\0')
 def observe(self,record=0):
  self.call('test_archive_observe',[INFO,record]);return list(struct.unpack('<6I',self.cpu.mem_read(INFO,24)))
 def mount(self,name,path,data):
  self.text(NAME,name);self.text(PATH,path);self.cpu.mem_write(INPUT,data)
  self.call('test_archive_set_payload',[PATH,INPUT,len(data)])
  return self.call('func_02018934',[NAME,PATH,0x12345678])
 def open(self,record,id):
  self.cpu.mem_write(FILE,b'\x9b'*0x44);self.call('FS_InitFile',[FILE])
  return self.call('func_0205d568',[FILE,record,id])
 def fingerprint(self,records=()):
  return (bytes(self.cpu.mem_read(self.names['data_020a8048'],4)),
   bytes(self.cpu.mem_read(self.names['data_020a804c'],12)),
   bytes(self.cpu.mem_read(self.anchor,0x60)),
   tuple(bytes(self.cpu.mem_read(p,0x60)) for p in records))

def normalized(p,data,native):
 values=list(struct.unpack('<'+'I'*(len(data)//4),data))
 aliases={p.anchor:0x7abc0000}
 for i,n in enumerate(('func_0205d2dc','func_0205d2b0')):aliases[p.names[n]]=0x7abc1000+i*4
 return struct.pack('<'+'I'*len(values),*[aliases.get(v,v) for v in values])

def compare(native,rom,records,label):
 a=native.fingerprint(records);b=rom.fingerprint(records)
 assert [normalized(native,x,True) for x in a[:3]]==[normalized(rom,x,False) for x in b[:3]],label
 for x,y in zip(a[3],b[3]):assert normalized(native,x,True)==normalized(rom,y,False),label
 native.check_allocations()

def members(data):
 at=16;fat=payload=0;count=0
 for _ in range(3):
  tag=data[at:at+4];size=struct.unpack_from('<I',data,at+4)[0]
  if tag==b'BTAF':fat=at+12;count=struct.unpack_from('<H',data,at+8)[0]
  if tag==b'GMIF':payload=at+8
  at+=size
 assert at==len(data) and fat and payload
 return [data[payload+a:payload+b] for a,b in [struct.unpack_from('<2I',data,fat+i*8) for i in range(count)]]

def main():
 elf,hashes,flags=build();cart=shared.base.Cartridge(ROOT/'build/europe.pak');results=[]
 assets=[]
 for name in ('c2d','cee','cef'):
  path='ARCHIVE/'+name+'.narc';data=cart.file(path)
  assets.append((name,path,data));results.append(dict(case='verified_input',path=path,size=len(data),
   sha256=files.sha(data),members=len(members(data))))
 native,rom=Probe(elf),Probe(cart=cart)
 assert native.bootstrap()==rom.bootstrap()==1;compare(native,rom,[],'fresh bootstrap')
 assert native.u32(native.anchor+0x10)==0 # No invented loaded flag/FAT callback.
 before=native.fingerprint();assert native.bootstrap()==1 and native.fingerprint()==before
 results.append(dict(case='fresh_anchor_and_idempotence',result='PASS'))
 for label,location,value in [('head',native.names['data_020a8048'],0x12345678),
   ('key',native.anchor,0x00646162),('previous',native.anchor+8,0x12345678)]:
  old=native.u32(location);native.words(location,value);before=native.fingerprint()
  assert native.bootstrap()==0 and native.fingerprint()==before
  native.words(location,old);results.append(dict(case='idempotent_corrupt_'+label,result='PASS',registry_unchanged=True))
 for label,location,value in [('stale_head','data_020a8048',0x12345678),('bad_name','data_0209a048',0x00646162)]:
  fresh=Probe(elf);fresh.words(fresh.names[location],value);before=fresh.fingerprint()
  name_before=bytes(fresh.cpu.mem_read(fresh.names['data_0209a048'],4))
  assert fresh.bootstrap()==0 and fresh.fingerprint()==before
  assert bytes(fresh.cpu.mem_read(fresh.names['data_0209a048'],4))==name_before
  results.append(dict(case='first_'+label,result='PASS',registry_unchanged=True))
 records=[]
 for name,path,data in assets:
  record=native.mount(name,path,data);assert record
  rom.cpu.mem_write(record,b'\xcd'*0x60);rom.cpu.mem_write(record+0x60,data);rom.text(NAME,name)
  assert rom.call('func_0204ee40',[record,NAME,record+0x60])==1
  records.append(record);compare(native,rom,records,'mount '+name)
  assert bytes(native.cpu.mem_read(record+0x60,len(data)))==data
  for p in (native,rom):
   p.text(NAME,name.upper());assert p.call('func_0205d23c',[NAME,3])==record
  before=native.fingerprint(records);assert native.bootstrap()==1 and native.fingerprint(records)==before
  results.append(dict(case='mount_'+name,result='PASS',record_bytes_compared=0x60,payload_bytes_compared=len(data)))
 before=native.fingerprint(records);assert native.mount(*assets[0])==0 and native.fingerprint(records)==before
 results.append(dict(case='duplicate_mount',result='PASS',registry_unchanged=True))
 # Removal in middle, tail, and first removable position always retains ROM.
 for index in (1,2,0):
  record=records[index]
  native.call('func_02018908',[record,0x12345678]);assert rom.call('func_0204ee10',[record])==1
  compare(native,rom,records,'unload '+assets[index][0]);assert native.u32(native.names['data_020a8048'])==native.anchor
  for p in (native,rom):
   p.text(NAME,assets[index][0]);assert p.call('func_0205d23c',[NAME,3])==0
  assert record not in native.allocations
  results.append(dict(case='unload_'+('middle' if index==1 else 'tail' if index==2 else 'first_removable'),result='PASS'))
 assert native.observe()[:2]==[0,0]
 # Real open buffers keep residency after unlink; retirement follows last close.
 name,path,data=assets[0];record=native.mount(name,path,data);assert record
 rom.cpu.mem_write(record,b'\xcd'*0x60);rom.cpu.mem_write(record+0x60,data);rom.text(NAME,name)
 assert rom.call('func_0204ee40',[record,NAME,record+0x60])==1;compare(native,rom,[record],'remount')
 expected=members(data)[0];assert expected and native.open(record,0)==1
 second=FILE+0x200;native.cpu.mem_write(second,b'\x9b'*0x44);native.call('FS_InitFile',[second])
 assert native.call('func_0205d568',[second,record,0])==1
 assert native.observe(record)[:4]==[1,2,2,0]
 native.call('func_02018908',[record,0x12345678]);assert rom.call('func_0204ee10',[record])==1
 compare(native,rom,[record],'retained unlink');assert native.observe(record)[:4]==[1,2,2,1]
 native.cpu.mem_write(OUTPUT,b'\xa5'*(len(expected)+64))
 assert native.call('FS_ReadFile',[FILE,OUTPUT+32,len(expected)+16])==len(expected)
 assert bytes(native.cpu.mem_read(OUTPUT,len(expected)+64))==b'\xa5'*32+expected+b'\xa5'*32
 # Separate object avoids resetting the still-open file before testing rejection.
 native.cpu.mem_write(FILE+0x100,b'\0'*0x44)
 assert native.call('func_0205d568',[FILE+0x100,record,0])==0
 assert native.call('FS_CloseFile',[FILE])==1 and record in native.allocations
 assert native.observe(record)[:4]==[1,1,1,1]
 assert native.call('FS_CloseFile',[second])==1 and record not in native.allocations
 assert native.observe()[:2]==[0,0];assert native.call('FS_CloseFile',[FILE])==0
 assert native.bootstrap()==1
 results.append(dict(case='remount_open_unlink_read_reject_new_open_retire_last_close',result='PASS',
  member_bytes_compared=len(expected),member_sha256=files.sha(expected),open_abi='FSFileID in r1/r2',
  simultaneous_open_references=2,first_close_retains_record=True,last_close_retires_record=True))
 report=dict(status='PASS',cases=results,unit_elf_sha256=files.sha(elf.read_bytes()),source_hashes=hashes,
  compiler_flags=flags,instructions=native.steps+rom.steps,allocator_events=native.events,
  verified_pack_sha256=files.sha(cart.raw),original_arm9_sha256=files.sha(cart.image(0xffffffff)[2]),
  production_general_elf_sha256='1e1eef06dff21c7a4ded69c93366f5c3d2718f8190d129abe06ffb94dec78ba0',
  seams=['raw AssetPack find/read from CRC-verified Europe.pak payloads','guarded CPU allocator; real list/vector ownership',
   'explicit initialization clock 0; no external wake/SDK active request cancellation'],
  limits=['CPU helper/lifecycle unit gate, not live Stage or hardware proof','No snapshot imported',
   'SDK card-DMA, FAT/FNT callbacks, active SDK queues and ARM7 initialization not exercised',
   'Production native_card included unchanged but recompiled with reported unit flags; no identity claim with CMake object'])
 (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(dict(status='PASS',cases=len(results),instructions=report['instructions'],report=str(OUT/'report.json'))))
if __name__=='__main__':main()
