#!/usr/bin/env python3
"""Real camera/runtime host checks, exact original PNG pixels and ARM object probes.

No emulator, main build, third_party edits, or runtime/physical acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
OPTIONS=ROOT/'source/integrated_native/options'
ASSETS=ROOT/'assets/proposals/lower-hud-2026-10-18'
VENDOR=ROOT/'third_party/sm64ds-3ds'

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--arm',action='store_true')
 parser.add_argument('--regressions',action='store_true')
 parser.add_argument('--output',type=Path,default=ROOT/'build/code-pass-2026-10-21/gameplay-camera')
 args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
 commands=[]
 def run(name,command):
  commands.append(command)
  result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
  (out/(name+'.log')).write_text(result.stdout+result.stderr)
  if result.returncode:print((result.stdout+result.stderr)[-4000:],file=sys.stderr)
  result.check_returncode();return result.stdout.strip()
 def compile_host(name,sources,extra=()):
  exe=out/name
  run(name+'-compile',['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
     '-fsanitize=address,undefined','-I'+str(OPTIONS),*extra,*map(str,sources),'-o',str(exe)])
  return str(exe)
 sources=[OPTIONS/'cstick_camera.cpp',OPTIONS/'enhance_settings.cpp']
 exe=compile_host('test-gameplay',[ROOT/'tests/options_camera/test_gameplay.cpp',*sources])
 host={'gameplay':run('gameplay',[exe,str(out/'overlay.raw')])}
 rgba=Image.frombytes('RGBA',(320,240),(out/'overlay.raw').read_bytes(),'raw','BGRA')
 expected=Image.new('RGBA',(320,240))
 for name,x in [('left',10),('right',278)]:
  sprite=Image.open(ASSETS/'sprites'/f'{name}.png').convert('RGBA')
  assert sprite.size==(32,32);expected.paste(sprite,(x,198))
 assert rgba.tobytes()==expected.tobytes(),'Exact original pixel differential failed'
 rgba.save(out/'overlay.png')
 preview=Image.open(ASSETS/'map-reference.png').convert('RGBA').resize((320,320),Image.Resampling.NEAREST).crop((0,40,320,280))
 preview.alpha_composite(rgba);preview.resize((960,720),Image.Resampling.NEAREST).save(out/'host-preview.png')
 regenerated=out/'native_gameplay_camera_art.hpp'
 run('generate',['%s'%sys.executable,str(ASSETS/'generate.py'),'--camera-header',str(regenerated)])
 assert regenerated.read_bytes()==(OPTIONS/regenerated.name).read_bytes()
 host['pixels']='76800 RGBA pixels exactly match two unscaled original PNGs; all other pixels zero; generated header reproducible'

 # Test production filter_input/overlay_image with real publication/camera
 # code. Only unavailable OS, font painting and pause-image services are stubbed.
 support=out/'host-support';support.mkdir(exist_ok=True)
 (support/'3ds.h').write_text('''#pragma once
#include <cstdint>
constexpr uint32_t KEY_A=1,KEY_SELECT=4,KEY_START=8,KEY_R=256,KEY_L=512,KEY_TOUCH=1u<<20;
constexpr uint64_t SYSCLOCK_ARM11=268111856;
struct touchPosition { unsigned short px=0,py=0; };
inline void hidTouchRead(touchPosition* p){*p={};}
inline uint64_t svcGetSystemTick(){return 1;}
inline unsigned linearSpaceFree(){return 0;}
inline int APT_CheckNew3DS(bool* p){*p=false;return 0;}
''')
 (support/'malloc.h').write_text('#pragma once\nstruct mallinfo { int fordblks; };\ninline struct mallinfo mallinfo(){return {0};}\n')
 exe=compile_host('test-runtime',[ROOT/'tests/options_camera/test_gameplay_runtime.cpp',*sources],
                  ['-I'+str(support),'-I'+str(ROOT/'source/integrated_native')])
 host['runtime']=run('runtime',[exe])

 # Execute the actual unchanged eight-direction command-consumer branch from
 # the pinned source, reading the very command record written by our hook.
 # Only L0's 32-bit pointer cast is widened for this host-only excerpt; the
 # geometry/collision tail is deliberately outside this isolated input test.
 retail=(VENDOR/'sm64ds-decomp/src/func_0200bb28.c').read_text()
 start=retail.index('        if (!(*(u32*)(c + 0x154) & 0x60)) {')
 end=retail.index('        _Z14ApproachLinearRsss(',start)
 branch=retail[start:end]
 prefix=(ROOT/'tests/options_camera/test_gameplay.cpp').read_text().split('int main(',1)[0]
 driver=out/'retail_consumer.cpp'
 driver.write_text(prefix+'''
using u8=unsigned char;using u16=unsigned short;using u32=unsigned;using s16=short;using s32=int;
u16* data_0209f49c=reinterpret_cast<u16*>(data_0209f498+4);
u16* data_0209f49e=reinterpret_cast<u16*>(data_0209f498+6);
#define L0(p) (p)
'''+(VENDOR/'sm64ds-decomp/src/DecIfAbove0_Short.c').read_text()+
 '\nvoid original_arrow_consumer(char* c){\n'+branch+'\n}\n'+r'''
int main(){
 flags(0x1000);sm64ds_cstick_inject(1,0,0);cstick_camera::set_mode(cstick_camera::Mode::Off);
 for(unsigned ctl:{0u,1u,2u})for(unsigned dir:{left,right}){
  release();std::memset(camera,0,sizeof camera);flags(0x1000);data_0209f498[0x16]=ctl;
  const int x=dir==left?20:290;const short step=dir==left?-0x2000:0x2000;
  touch(false,true,x,210);update();original_arrow_consumer(reinterpret_cast<char*>(camera));
  short offset;std::memcpy(&offset,camera+0x184,2);assert(offset==step);
  assert((camera[0x154]&0x60)==(dir==left?0x20:0x40));
  flags(0x1000); // Represents completion of the unrelated angle/geometry tail.
  for(int n=1;n<20;++n){
   touch(false,true,x,210);update();assert(!rd(6));original_arrow_consumer(reinterpret_cast<char*>(camera));
   std::memcpy(&offset,camera+0x184,2);assert(offset==step);
  }
  touch(false,true,x,210);update();original_arrow_consumer(reinterpret_cast<char*>(camera));
  std::memcpy(&offset,camera+0x184,2);assert(offset==short(step*2));
  flags(0x1000);release();original_arrow_consumer(reinterpret_cast<char*>(camera));
  short released;std::memcpy(&released,camera+0x184,2);assert(released==offset);
 }
 std::puts("PASS: actual pinned eight-direction branch consumes native command bits; both directions/three controls; press step 0x2000; repeat at 20 updates; release stops");
}
''')
 exe=compile_host('test-retail-consumer',[driver,*sources])
 host['retail_consumer']=run('retail-consumer',[exe])
 regressions={}
 if args.regressions:
  exe=compile_host('test-camera',[ROOT/'tests/options_camera/test.cpp',OPTIONS/'analog_pad.cpp',*sources])
  regressions['camera_analog']=run('regression-camera',[exe])
  for label,source in [('stylus','tests/options_camera/test_stylus.cpp'),('publication','tests/test_overlay_publication.cpp')]:
   exe=compile_host('test-'+label,[ROOT/source]);run('regression-'+label,[exe]);regressions[label]='passed ASan/UBSan'
  for label,source in [('prepare_contracts','tests/options_menu/test_prepare_contracts.py'),('pause_hooks','tests/options_menu/test_native_pause_hooks.py')]:
   run('regression-'+label,[sys.executable,str(ROOT/source)]);regressions[label]=(out/('regression-'+label+'.log')).read_text().strip()
 pinned=json.loads((ROOT/'reference-native.lock.json').read_text())['vendored_files_sha256']
 mismatches=[name for name,digest in pinned.items() if hashlib.sha256((VENDOR/name).read_bytes()).hexdigest()!=digest]
 if mismatches:raise ValueError('Vendor files differ from lock: '+str(mismatches))
 arm={}
 if args.arm:
  source=ROOT/'build/integrated-native/render60-source'
  flags=(source/'build3ds/cmake/CMakeFiles/smoke_player.dir/flags.make').read_text()
  arguments=[]
  for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
   match=re.search(r'^'+key+r' = (.*)$',flags,re.M)
   if not match:raise ValueError('Missing ARM flags '+key)
   arguments+=shlex.split(match[1])
  compiler=Path(os.environ.get('DEVKITARM',str(Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))/'devkitARM')))/'bin/arm-none-eabi-g++'
  # One isolated include tree avoids mixing two physical copies of pragma-once
  # settings/art headers through the configured generated include directories.
  arm_source=out/'arm-source';arm_source.mkdir(exist_ok=True)
  for path in OPTIONS.iterdir():
   if path.suffix in {'.cpp','.h','.hpp'}:shutil.copyfile(path,arm_source/path.name)
  shutil.copyfile(ROOT/'source/integrated_native/native_diagnostics.hpp',arm_source/'native_diagnostics.hpp')
  for name in ['cstick_camera.cpp','enhance_runtime.cpp']:
   obj=out/(name+'.o')
   run(name+'-arm',[str(compiler),'-I'+str(arm_source),*arguments,'-c',str(arm_source/name),'-o',str(obj)])
   assert (arm_source/name).read_bytes()==(OPTIONS/name).read_bytes()
   arm[name]={'status':'compiled current source in isolated include tree','sha256':hashlib.sha256(obj.read_bytes()).hexdigest()}
 changed=['source/integrated_native/options/'+name for name in ['native_gameplay_camera.hpp','native_gameplay_camera_art.hpp','cstick_camera.cpp','cstick_camera.h','enhance_runtime.cpp']]
 changed+=['assets/proposals/lower-hud-2026-10-18/generate.py','tests/options_camera/test.cpp','tests/options_camera/test_gameplay.cpp','tests/options_camera/test_gameplay_runtime.cpp','tests/options_camera/test_gameplay.py']
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 report={'status':'host_and_object_candidate_not_runtime_acceptance','host':host,'regressions':regressions,'ARM':arm,'vendor_hashes_matched':len(pinned),
   'changed_files_sha256':{name:sha(ROOT/name) for name in changed},
   'original_asset_sha256':{name:sha(ASSETS/'sprites'/f'{name}.png') for name in ['left','right']},
   'retail_consumer':{'source_sha256':sha(VENDOR/'sm64ds-decomp/src/func_0200bb28.c'),'branch_sha256':hashlib.sha256(branch.encode()).hexdigest(),
                      'scope':'original input/yaw-offset branch only; geometry/collision tail not executed'},
   'parent_pipeline':'No patch required: existing options header copy and existing cstick/runtime translation units include all additions; no CMake sources needed.',
   'static_overlay':'Two 32x32 images (8192 ROM bytes), no per-frame repaint or publication while content stays visible; reuses the existing pause overlay channel.',
   'limits':['No Azahar, physical console, final link, GPU execution, or screenshot of the running game.',
             'host-preview.png is the production overlay pixels composed over the reference map, without recreating the ROM HUD.',
             'Upper sprites in Wide and complete runtime acceptance remain owned by parent.'],
   'commands':commands}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({'host':host,'regressions':regressions,'ARM':arm,'vendor_hashes_matched':len(pinned),'report':str(out/'report.json')},indent=2))

if __name__=='__main__':main()
