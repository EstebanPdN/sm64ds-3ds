#!/usr/bin/env python3
"""Production ARM HUD/Minimap video sequences, verified original file bytes.

LoadFile/deallocation and scene-active query are declared CPU boundaries.
No FS read, GPU, Stage owner/dispatch, emulator or physical device executes.
All video memory is compared; DS bus windows are deliberately not CPU mapped.
"""
from collections import Counter
import json,struct,subprocess,re
from pathlib import Path
import ndspy.narc
from unicorn.arm_const import UC_ARM_REG_PC,UC_ARM_REG_LR
import test_native_file_video_arm as files

ROOT,TOOL=files.ROOT,files.TOOL
OUT=ROOT/'build/native-file-video/hud-minimap'
OBJ,PLAYER,CAMERA,LEVEL=0x12020000,0x12021000,0x12023000,0x12100000
HUD='_ZN3HUD13InitResourcesEv';MINIMAP='_ZN7Minimap13InitResourcesEv';BEHAVIOR='_ZN7Minimap8BehaviorEv'
SUPPORT=['func_02018ac4','func_0202a980','func_0202a96c','func_0202a958',
 'GetMinimapID','GetMinimapScale','SublevelToLevel','NumStars','IsStarCollected','func_0203da4c',
 'func_0202b044','func_0202af80','UpdateMinimap','SetSubBg2Offset','Vec3_Sub','AddVec3',
 'Vec3_MulScalarInPlace','Vec3_DivScalarInPlace','Vec3_RotateYAndTranslate']
SUPPORT_CPP=['_ZN7Minimap19UpdateLevelSpecificEv','_ZN7Minimap21FixTHIPaintingRoomPosER7Vector3',
 '_ZN7Minimap15GetPosOnMinimapER7Vector3S1_5Fix12IiEsS1_',
 '_ZN7Minimap20GetPosFromMinimapPosER7Vector3S1_5Fix12IiEsS1_',
 '_ZN6Player7IsStateERNS_5StateE',
 '_Z22LoadMinimapTileObjectsRN11LVL_Overlay11ObjSubTableEij',
 '_Z23LoadMinimapScaleObjectsRN11LVL_Overlay11ObjSubTableEij']

def build():
 OUT.mkdir(parents=True,exist_ok=True)
 flags=['-O2','-D__3DS__','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
  '-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-Wa,--noexecstack',
  '-I'+str(ROOT/'include'),'-I'+str(ROOT/'build/native-core/generated/include')]
 sources=[ROOT/'tests/test_native_hud_video_arm.cpp',
  *[ROOT/'source/game_port'/p for p in ['native_video.cpp','native_video_services.cpp','native_boot_services.cpp','native_file_video.cpp']],
  ROOT/'source/ctr/game_math.cpp',ROOT/'source/core/fixed_math.cpp',ROOT/'source/ctr/game_memory_primitives.S',
  *[ROOT/'build/native-runtime/scene-video'/(p+'.cpp') for p in [HUD,MINIMAP,BEHAVIOR]],
  *[ROOT/'build/native-core/generated/src'/(p+'.c') for p in SUPPORT],
  *[ROOT/'build/native-core/generated/src'/(p+'.cpp') for p in SUPPORT_CPP],
  *[ROOT/'build/native-core/generated/src/actors/Player.cpp.parts'/(p+'.cpp') for p in ['045','227']]]
 hashes={str(p.relative_to(ROOT)):files.sha(p.read_bytes()) for p in sources}
 objects=[]
 for i,p in enumerate(sources):
  target=OUT/('source-'+str(i)+'.o');c=p.suffix=='.c';assembly=p.suffix=='.S'
  command=[TOOL+('gcc' if c or assembly else 'g++'),*flags]
  if not assembly:command+=['-std=c11' if c else '-std=gnu++20']
  result=subprocess.run([*command,'-c',str(p),'-o',str(target)],cwd=ROOT,capture_output=True,text=True)
  (OUT/('compile-'+str(i)+'.log')).write_text(result.stdout+result.stderr)
  if result.returncode:print(result.stderr)
  result.check_returncode()
  objects.append(str(target))
 elf=OUT/'hud-minimap-arm.elf'
 roots=[HUD,MINIMAP,BEHAVIOR,'sm64ds_decompress_lz16_video','test_hud_scene_active',
  '_ZN6sm64ds18native_video_resetEv','_ZN6sm64ds20native_video_pointerEmj',
  '_ZN6sm64ds18native_video_writeEmyj','_ZN6sm64ds15native_video_okEv',
  '_ZN6sm64ds24native_video_bad_addressEv','_ZN2GX12SetBankForBGEt','_ZN2GX13SetBankForOBJEt',
  '_ZN2GX15SetBankForSubBGEt','_ZN2GX16SetBankForSubOBJEt','_ZN2GX22SetBankForSubBGExtPlttEt',
  '_Z22LoadMinimapTileObjectsRN11LVL_Overlay11ObjSubTableEij','_Z23LoadMinimapScaleObjectsRN11LVL_Overlay11ObjSubTableEij']
 command=[TOOL+'g++',*flags,'-nostartfiles','-specs=nosys.specs',
  '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x01000000,-e,'+HUD,
  '-Wl,--defsym=data_0209f3c8=data_0209f3c4+4',
  *['-Wl,-u,'+p for p in roots],*objects,'-o',str(elf)]
 result=subprocess.run(command,text=True,capture_output=True,cwd=ROOT)
 (OUT/'link.log').write_text(result.stdout+result.stderr);result.check_returncode()
 assert hashes=={str(p.relative_to(ROOT)):files.sha(p.read_bytes()) for p in sources}
 (OUT/'sources.json').write_text(json.dumps(hashes,indent=2)+'\n')
 subprocess.run([TOOL+'objdump','-d',str(elf)],check=True,stdout=(OUT/'disassembly.txt').open('w'))
 return elf,hashes

class Probe(files.Probe):
 def __init__(self,elf,assets):
  self.assets=assets;self.allocations={};self.events=[];self.calls=Counter();self.next=files.ARENA
  files.shared.Probe.__init__(self,'HUD/Minimap production ARM11',elf=elf)
  self.cpu.mem_map(files.WORK+0x10000,0x30000)
  self.cpu.mem_map(LEVEL,0x20000)
  self.cpu.mem_map(files.ARENA,files.ARENA_END-files.ARENA)
  self.observed={self.names[p]:p for p in [HUD,MINIMAP,BEHAVIOR,'LoadFile','Deallocate','DecompressLZ16',
   'sm64ds_decompress_lz16_video','_ZN3GXS18BeginLoadBGExtPlttEv','_ZN3GXS16EndLoadBGExtPlttEv',
   '_ZN3GXS13LoadBGExtPlttEPKvjj']}
  self.video=None
 def code(self,cpu,address,size,_):
  name=getattr(self,'observed',{}).get(address)
  if name:self.calls[name]+=1
  if name=='LoadFile':
   handle=cpu.reg_read(files.shared.REGS[0]);assert handle in self.assets,hex(handle)
   data=self.assets[handle]['data'];p=(self.next+63)&~31;self.next=p+len(data)+64
   assert self.next<files.ARENA_END
   cpu.mem_write(p-32,b'\xa5'*32);cpu.mem_write(p,data);cpu.mem_write(p+len(data),b'\x5a'*32)
   self.allocations[p]=len(data);self.events.append(dict(operation='LoadFile_buffer_boundary',handle=hex(handle),pointer=hex(p),bytes=len(data)))
   cpu.reg_write(files.shared.REGS[0],p);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
  elif name=='Deallocate':
   p=cpu.reg_read(files.shared.REGS[0]);self.check_allocation(p)
   self.events.append(dict(operation='Deallocate_boundary',pointer=hex(p),bytes=self.allocations.pop(p)))
   cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
  elif address==self.names.get('test_hud_unexpected'):
   p=cpu.reg_read(files.shared.REGS[0]);message=bytes(cpu.mem_read(p,160)).split(b'\0')[0].decode(errors='replace')
   raise AssertionError('Unreached boundary: '+message)
 def write(self,name,value,fmt='<I'):self.cpu.mem_write(self.names[name],struct.pack(fmt,value))
 def configure_video(self,banks=()):
  self.call('_ZN6sm64ds18native_video_resetEv',[]);self.write('test_hud_failures',0)
  self.cpu.mem_write(self.names['data_020a6088'],bytes(26))
  for name,mask in [('_ZN2GX12SetBankForBGEt',8),('_ZN2GX13SetBankForOBJEt',0x40),
   ('_ZN2GX15SetBankForSubBGEt',4),('_ZN2GX16SetBankForSubOBJEt',0x100),('_ZN2GX22SetBankForSubBGExtPlttEt',0x80)]:self.call(name,[mask])
  self.video={name:(self.pointer(address,size),size) for name,address,size in
   [('registers',0x04000000,0x2000),('palette',0x05000000,0x800),('vram',0x06800000,0xa4000),('oam',0x07000000,0x800)]}
  for name,(pointer,size) in self.video.items():
   if name!='registers':self.cpu.mem_write(pointer,b'\xa5'*size)
  assert self.okay()

def assets(cart):
 _,indices=cart.handles();base,_,arm9=cart.image(0xffffffff)
 archives=[]
 for i in range(13):
  _,_,begin,end,_,path=struct.unpack_from('<IIHHII',arm9,0x0208ecf4-base+i*20)
  name=arm9[path-base:path-base+160].split(b'\0')[0].decode().lstrip('/')
  archives.append((begin,end,name))
 result={}
 def read(handle):
  if handle in result:return result[handle]
  if handle<0x8000:
   e=cart.entries[indices[handle]];data=cart.file(e[4]);path=e[4];member=None;archive_sha=None
  else:
   matches=[(b,e,p) for b,e,p in archives if b<=handle<e];assert len(matches)==1,hex(handle)
   b,e,path=matches[0];raw=cart.file(path);archive_sha=files.sha(raw);member=handle-b
   narc=ndspy.narc.NARC(raw);data=narc.files[member]
  item=dict(handle=hex(handle),path=path,narc_member=member,archive_sha256=archive_sha,raw_bytes=len(data),raw_sha256=files.sha(data),data=data)
  # These selected graphics resources have raw 0x10 LZ headers; palettes do not.
  if data[:1]==b'\x10':item['decoded'],item['lz_audit']=files.lz_audit(data)
  result[handle]=item;return item
 for h in [0x269,0x26a,0x26c,0x26e,0x270,0x229,0x22a,0x980f,0x8000,0x8001,0x8002,0x8003,0x798,0x799,0x796]:read(h)
 for b in [0xa000,0xa400,0xa800,0xac00,0xb000]:
  for index in [3,7,10,14]:read(b+index)
 return result

def setup_context(p,cart):
 base,_,arm9=cart.image(0xffffffff);ovbase,_,ov=cart.image(9)
 pointer=struct.unpack_from('<I',arm9,0x02092208+4-base)[0];assert ovbase<=pointer<ovbase+len(ov)
 p.cpu.mem_write(LEVEL,ov)
 def relocated(addr):assert ovbase<=addr<ovbase+len(ov);return LEVEL+addr-ovbase
 level=relocated(pointer);areas=struct.unpack_from('<I',ov,pointer-ovbase+0x10)[0]
 p.words(level+0x10,relocated(areas));p.write('data_0209f340',level)
 table=struct.unpack_from('<I',ov,pointer-ovbase+4)[0]
 count,entries=struct.unpack_from('<H2xI',ov,table-ovbase)
 for i in range(count):
  kind,n,at=struct.unpack_from('<BB2xI',ov,entries-ovbase+i*8)
  if kind&31 in [11,12]:
   t=relocated(entries+i*8);p.words(t+4,relocated(at))
   name='_Z22LoadMinimapTileObjectsRN11LVL_Overlay11ObjSubTableEij' if kind&31==11 else '_Z23LoadMinimapScaleObjectsRN11LVL_Overlay11ObjSubTableEij'
   assert kind>>5==0;p.call(name,[t,-1,0])
 p.cpu.mem_write(PLAYER,b'\0'*0x800);p.cpu.mem_write(CAMERA,b'\0'*0x200)
 p.cpu.mem_write(PLAYER+0x5c,struct.pack('<3i',-1200*4096,254*4096,6800*4096))
 p.cpu.mem_write(PLAYER+0xcc,b'\0');p.words(CAMERA+0x110,PLAYER)
 p.write('data_0209f394',PLAYER);p.write('data_0209f318',CAMERA);p.write('data_0209f2f8',1,'<b')
 p.cpu.mem_write(p.names['SUBLEVEL_LEVEL_TABLE'],arm9[0x02075298-base:0x02075298-base+0x40])
 p.cpu.mem_write(p.names['data_02082214'],arm9[0x02082214-base:0x02082214-base+0x4000])
 p.cpu.mem_write(p.names['data_ov002_0211116c'],cart.image(2)[2][0x0211116c-cart.image(2)[0]:0x0211116c-cart.image(2)[0]+12])
 p.write('data_02092144',0x800,'<h');p.words(p.names['data_0209caa0']+8,0x80)
 p.words(files.WORK+0x900,p.names['test_hud_scene_active']);p.words(files.WORK+0x800,files.WORK+0x900-20)
 p.write('data_0209f5bc',files.WORK+0x800)
 return dict(original_level=hex(pointer),level_image_sha256=files.sha(ov),native_level=hex(level),map_count=p.cpu.mem_read(p.names['data_0209f2e8'],1)[0])

def main():
 cart=files.shared.base.Cartridge(ROOT/'build/europe.pak');resource=assets(cart);elf,hashes=build();p=Probe(elf,resource)
 context=setup_context(p,cart);results=[]
 for mode in [0,1]:
  for language in range(6):
   p.configure_video();p.cpu.mem_write(OBJ-32,b'\xc7'*(0x258+64));p.cpu.mem_write(OBJ,b'\0'*0x258)
   p.write('data_0209f2d8',mode,'<B');p.write('test_hud_language',language)
   before=p.video_bytes();events=len(p.events);calls=p.calls.copy();assert p.call(HUD,[OBJ])==1
   langbase={2:0xa400,3:0xa800,4:0xac00,5:0xb000}.get(language,0xa000)
   icon=(langbase+3) if mode else {2:0x26a,3:0x26c,4:0x26e,5:0x270}.get(language,0x269)
   text=langbase+(7 if mode else 14);bottom=0x8000 if mode else 0x229
   common=0x8002 if mode else langbase+10;top=0x8003 if mode else 0x980f;palbottom=0x8001 if mode else 0x22a
   mutations=[('vram',offset,resource[h]['decoded']) for offset,h in
    [(0x94000,icon),(0xa0000,icon),(0x96000,text),(0xa2000,bottom)]]
   mutations+=[('palette',0x200,resource[common]['data'][:0x120]),('palette',0x600,resource[common]['data'][:0x120]),
    ('palette',0x320,resource[top]['data'][:0xe0]),('palette',0x720,resource[palbottom]['data'][:0xe0])]
   files.verify_video(p,before,mutations);assert not p.allocations and p.okay() and p.u32(p.names['test_hud_failures'])==0
   assert bytes(p.cpu.mem_read(OBJ-32,32))==b'\xc7'*32 and bytes(p.cpu.mem_read(OBJ+0x258,32))==b'\xc7'*32
   assert p.calls['DecompressLZ16']-calls['DecompressLZ16']==4
   assert p.calls['sm64ds_decompress_lz16_video']-calls['sm64ds_decompress_lz16_video']==4
   results.append(dict(mode=mode,language=language,result=1,full_video_compare=True,video_bytes=sum(s for _,s in p.video.values()),
    allocation_events=p.events[events:],LZ16_calls=4,ABI_preserved=True))
 minimap=[]
 for mode,save in [(0,0),(1,0),(0,0x80)]:
  p.configure_video();p.cpu.mem_write(OBJ-32,b'\xc7'*(0x258+64));p.cpu.mem_write(OBJ,b'\0'*0x258)
  p.write('data_0209f2d8',mode,'<B');p.words(p.names['data_0209caa0']+8,save)
  before=p.video_bytes();calls=p.calls.copy();events=len(p.events);assert p.call(MINIMAP,[OBJ])==1
  expected=[('vram',0x50000,resource[0x798]['decoded']),('vram',0x4f800,resource[0x796]['decoded']),
   ('vram',0x9e000,resource[0x799]['data']),('palette',0x400,resource[0x799]['data'][:2])]
  expected_regs=bytearray(before['registers']);struct.pack_into('<H',expected_regs,0x100e,0x1f12)
  expected.append(('registers',0,expected_regs));files.verify_video(p,before,expected)
  assert p.cpu.mem_read(p.names['data_020a6088']+22,2)==b'\x80\0' and p.u32(p.names['data_020a60a4'])==0
  assert not p.allocations and p.okay() and p.u32(p.names['test_hud_failures'])==0
  intro=mode==0 and save==0
  assert p.u32(OBJ+0x214)==3050 and p.u32(OBJ+0x218)==3050*(2 if intro else 1)
  assert p.cpu.mem_read(OBJ+0x255,1)[0]==int(intro)
  assert bytes(p.cpu.mem_read(OBJ-32,32))==b'\xc7'*32 and bytes(p.cpu.mem_read(OBJ+0x258,32))==b'\xc7'*32
  assert p.calls['DecompressLZ16']-calls['DecompressLZ16']==2
  assert p.calls['sm64ds_decompress_lz16_video']-calls['sm64ds_decompress_lz16_video']==2
  minimap.append(dict(mode=mode,save_intro_bit=hex(save),result=1,full_video_compare=True,bank_H_restored=True,palette_save_cleared=True,
   header_65536_span_ends_at_bank_C_end=True,allocation_events=p.events[events:],
   target_inverse_scale=3050,inverse_scale=p.u32(OBJ+0x218),in_intro=int(intro),LZ16_calls=2,ABI_preserved=True))
 behavior=[]
 for touch in [0,1,2,0]:
  before=p.video_bytes();p.write('data_0209f4ac',int(touch!=0),'<B');p.write('data_0209f4ae',touch,'<B')
  timer=p.cpu.mem_read(OBJ+0x254,1)[0]
  p.write('data_0209f4a8',128,'<B');p.write('data_0209f4a9',96,'<B');assert p.call(BEHAVIOR,[OBJ])==1
  expected_regs=bytearray(before['registers'])
  if touch or timer:
   struct.pack_into('<I',expected_regs,0x1050,0x0d042844)
   control=struct.unpack_from('<H',expected_regs,0x100c)[0]
   struct.pack_into('<H',expected_regs,0x100c,(control&0x43)|(0x5500 if touch==2 else 0x5300))
   assert p.cpu.mem_read(p.names['data_0209d454'],1)[0]&4
   assert p.cpu.mem_read(OBJ+0x254,1)[0]==(30 if touch else timer-1)
  else:
   display=struct.unpack_from('<I',expected_regs,0x1000)[0]
   struct.pack_into('<I',expected_regs,0x1000,(display&~0x1f00)|0x800)
   struct.pack_into('<H',expected_regs,0x1050,0)
  files.verify_video(p,before,[('registers',0,expected_regs)])
  after=p.video_bytes()
  assert p.okay() and p.u32(p.names['test_hud_failures'])==0
  behavior.append(dict(touch_kind=touch,result=1,ABI_preserved=True,BG2CNT=hex(struct.unpack_from('<H',after['registers'],0x100c)[0]),
   BLDCNT=hex(struct.unpack_from('<I',after['registers'],0x1050)[0]),BG3CNT=hex(struct.unpack_from('<H',after['registers'],0x100e)[0]),
   source_video_unchanged=True,object_sha256=files.sha(bytes(p.cpu.mem_read(OBJ,0x258)))))
 # Local helper rejects a DS span before running LZ16; a native destination
 # follows unchanged LZ16, with a guarded output and identical source bytes.
 boundaries=[]
 source=files.WORK+0x1000;raw=resource[0x269]['data'];decoded=resource[0x269]['decoded'];p.cpu.mem_write(source,raw)
 for target,valid in [(0x06403ff0,False),(files.WORK+0x28000,True)]:
  p.configure_video();before=p.video_bytes();calls=p.calls.copy()
  if valid:p.cpu.mem_write(target-32,b'\xa5'*(len(decoded)+64))
  p.call('sm64ds_decompress_lz16_video',[source,target])
  assert bytes(p.cpu.mem_read(source,len(raw)))==raw
  files.verify_video(p,before,[])
  assert p.calls['DecompressLZ16']-calls['DecompressLZ16']==int(valid)
  if valid:
   assert bytes(p.cpu.mem_read(target,len(decoded)))==decoded
   assert bytes(p.cpu.mem_read(target-32,32))==b'\xa5'*32 and bytes(p.cpu.mem_read(target+len(decoded),32))==b'\xa5'*32
   assert p.okay() and p.u32(p.names['test_hud_failures'])==0
  else:
   assert not p.okay() and p.u32(p.names['test_hud_failures'])==1
  boundaries.append(dict(destination=hex(target),native_pointer=valid,LZ16_calls=int(valid),all_video_bytes_preserved=True,ABI_preserved=True))
 assert not p.faults and hashes=={path:files.sha((ROOT/path).read_bytes()) for path in hashes}
 report=dict(status='pass',scope=__doc__,ELF_sha256=files.sha(elf.read_bytes()),source_sha256=hashes,
  upstream_pin=files.PIN,assets={hex(h):{k:v for k,v in r.items() if k not in ['data','decoded']} for h,r in resource.items()},
  context=context,HUD_cases=results,Minimap_cases=minimap,Minimap_behavior_cases=behavior,helper_boundary_cases=boundaries,CPU_faults=p.faults,
  owned_video_bytes=684032,DS_bus_mapped=False,FS_reads_executed=False,GPU_executed=False,Azahar_launched=False,production_changed=False,
  limits=['Already verified raw/NARC buffers are LoadFile seam; FS opening/reading is not rerun.',
   'Scene-active vtable slot is an explicit input query. Player health/state, minimap metadata/math/updates are original code.',
   'Only castle level1 entrance and controlled input; no Stage actor owner/lifecycle, GPU/HUD visual acceptance or physical performance.',
   'Native G2S pointers validate one byte. This fixture proves actual resource extents, not rejection of arbitrary native pointers.'])
 source_records=dict(hashes)
 for path in [Path(__file__).relative_to(ROOT),Path('tools/test_native_file_video_arm.py'),
              Path('tools/test_native_audio_deferred_arm.py'),Path('include/sm64ds/native_register.hpp'),
              Path('include/sm64ds/native_video.hpp'),Path('include/sm64ds/fixed_math.hpp'),
              *[p.relative_to(ROOT) for p in (ROOT/'build/native-core/generated/include').rglob('*.h')]]:
  source_records[str(path)]=files.sha((ROOT/path).read_bytes())
 for path,digest in source_records.items():
  destination=OUT/'frozen-sources'/path;destination.parent.mkdir(parents=True,exist_ok=True)
  data=(ROOT/path).read_bytes();assert files.sha(data)==digest;destination.write_bytes(data)
 report['frozen_source_sha256']=source_records
 (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(status='pass',HUD_cases=len(results),Minimap_cases=len(minimap),Minimap_behavior_cases=len(behavior),ELF_sha256=report['ELF_sha256'])))

if __name__=='__main__':main()
