#!/usr/bin/env python3
"""Link a separate castle movement laboratory using existing reviewed DS objects."""
from pathlib import Path
import sys,re,shlex,json,hashlib,struct,math,subprocess,argparse,time,shutil
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'build/integrated-native/render60-source'
OUT=ROOT/'build/movement-lab'
CC='/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
sys.path.insert(0,str(Path(__file__).parent))
from build_core import build,run

def castle():
 b=(ROOT/'build/player-motion-isolated-diagnostic/castle/castle.kcl').read_bytes()
 p,n,t,o=struct.unpack_from('<4I',b)
 dot=lambda a,b:sum(x*y for x,y in zip(a,b))
 def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
 tris=[]
 for i in range(1,(o-t)//16):
  length,vi,ni,e1,e2,e3,attr=struct.unpack_from('<i6H',b,t+i*16)
  a=tuple(x/64 for x in struct.unpack_from('<3i',b,p+12*vi))
  normals=[tuple(x/1024 for x in struct.unpack_from('<3h',b,n+6*j)) for j in (ni,e1,e2,e3)]
  face,x,y,z=normals;v=cross(face,x);w=cross(face,y)
  dv,dw=dot(v,z),dot(w,z)
  assert abs(dv)>1e-7 and abs(dw)>1e-7
  # KCL extent is a dot product of 1/64 positions and 1/1024 normals.
  v=tuple(a[j]+v[j]*(length/65536)/dv for j in range(3))
  w=tuple(a[j]+w[j]*(length/65536)/dw for j in range(3))
  if dot(cross(tuple(v[j]-a[j] for j in range(3)),tuple(w[j]-a[j] for j in range(3))),face)<0:v,w=w,v
  verts=[tuple(round(c) for c in point) for point in (a,v,w)]
  assert all(abs(c)<100000 for point in verts for c in point)
  tris.append(verts)
 path=OUT/'castle_surfaces.inc'
 path.write_text('static const struct SM64Surface castle_surfaces[]={\n'+''.join('{0,0,0,{'+','.join('{'+','.join(map(str,v))+'}' for v in tri)+'}},\n' for tri in tris)+'};\n')
 return {'triangles':len(tris),'source_sha256':hashlib.sha256(b).hexdigest(),'note':'Static KCL reconstructed triangles; CLPS mapping pending validation'}

def main():
 global OUT
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path);options=parser.parse_args()
 OUT=(options.output or OUT/('native-world-'+str(time.time_ns()))).resolve()
 assert not OUT.exists(),'Use a fresh output directory; earlier laboratory artifacts are immutable'
 OUT.mkdir(parents=True)
 geometry={'source':'original live DS registry via WorldSession/DsWorldSource','converted_static_mesh_fallback':False,'scope':'castle movement lab; not main-menu physics acceptance'}
 archive=build(True)
 flags=(BASE/'build3ds/cmake/CMakeFiles/smoke_player.dir/flags.make').read_text();args=[]
 for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:args+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
 args+=['-I'+str(BASE/'port/platform/3ds'),'-I'+str(ROOT/'source/movement_lab'),'-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(ROOT/'build/movement-lab/core/source/src'),'-I'+str(OUT)]
 def compile(source,name):
  obj=OUT/(name+'.o');run([CC,*args,'-c',source,'-o',obj]);return obj
 command=json.loads((BASE/'build3ds/menu-compile/game_main-link-command.json').read_text())
 # Compile the original recovered behavior with a single state-dispatch seam.
 path=BASE/'sm64ds-decomp/src/_ZN6Player8BehaviorEv.cpp';text=path.read_text()
 text='#include "lab.hpp"\n'+text
 anchor='        if (*(void **)&st->mMain != 0)\n            (this->*(st->mMain))();'
 assert text.count(anchor)==1
 text=text.replace(anchor,'        if (!movement_lab::step(this)) { st=mState; if (*(void **)&st->mMain != 0) (this->*(st->mMain))(); }')
 # Native controller owns displacement; retain DS collision/interactions, then
 # commit the authoritative N64 result after DS background probing.
 text=text.replace('    func_ov002_020bf36c(((char *)this), ((char *)this) + 0x2d4);','    if (!movement_lab_owns_position()) func_ov002_020bf36c(((char *)this), ((char *)this) + 0x2d4);')
 text='extern "C" bool movement_lab_owns_position();\n'+text
 text='extern "C" void func_ov002_020c14b8(void*);\n'+text
 anchor='    func_ov002_020c2db8(((char *)this));'
 assert text.count(anchor)==1
 text=text.replace(anchor,'    if (movement_lab_owns_position()) func_ov002_020c14b8(this);\n    else func_ov002_020c2db8(((char *)this));')
 text=text.replace('    return 1;\n}', '    movement_lab::commit(this);\n    return 1;\n}')
 behavior=OUT/'player_behavior.cpp';behavior.write_text(text)
 main=BASE/'port/platform/3ds/game_main.cpp';text=main.read_text();text='#include "lab.hpp"\n'+text
 anchor=' const uint32_t pause_requests=sm64ds_native_pause_take_requests();';assert text.count(anchor)==1
 text=text.replace(anchor,' movement_lab::frame(current_scene,updates);\n'+anchor)
 entry=OUT/'lab_main.cpp';entry.write_text(text)
 path=BASE/'port/platform/3ds/enhance_runtime.cpp';text=path.read_text();text='#include "lab.hpp"\n'+text
 anchor='uint32_t filter_input(uint32_t held, uint32_t down, bool& touch_allowed) {';assert text.count(anchor)==1
 text=text.replace(anchor,anchor+'''
    touch_allowed=false;
    if(g_channels[0].allocate(320,240))if(auto* px=g_channels[0].writable()){
        if(movement_lab::render(px))g_channels[0].publish();
    }
    return movement_lab::input(held,down);
''')
 runtime=OUT/'lab_overlay.cpp';runtime.write_text(text)
 path=BASE/'port/platform/3ds/game_entry.cpp';text=path.read_text()
 # The lab always uses its own settings/save/log; env fixture is retained only
 # for bounded test runs and renderer setup, never to select the main save.
 text=text.replace(' chdir("sdmc:/3ds/sm64ds");',' setenv("SM64DS_SAVE_PATH","sdmc:/3ds/sm64ds/movement-lab.sav",1);\n chdir("sdmc:/3ds/sm64ds");')
 text=text.replace('sdmc:/3ds/sm64ds/log.txt','sdmc:/3ds/sm64ds/movement-lab.log')
 boot=OUT/'lab_entry.cpp';boot.write_text(text)
 replacements={
  '/game_entry.o':compile(boot,'lab_entry'),
  '/menu-compile/game_main.o':compile(entry,'lab_main'),
  '/_ZN6Player8BehaviorEv.o':compile(behavior,'player_behavior'),
  '/platform/3ds/enhance_runtime.o':compile(runtime,'lab_overlay'),
 }
 counts={s:0 for s in replacements}
 for i,token in enumerate(command):
  for suffix,new in replacements.items():
   if token.endswith(suffix):command[i]=str(new);counts[suffix]+=1
 assert all(n==1 for n in counts.values()),counts
 obj=compile(ROOT/'source/movement_lab/lab.cpp','lab');command.insert(1,str(obj))
 command.insert(1,str(compile(ROOT/'source/integrated_native/movement/ds_world_query.cpp','ds_world_query')))
 command.insert(1,str(compile(ROOT/'source/movement_lab/audio_capture.cpp','audio_capture')))
 command.insert(1,'-Wl,--wrap=ndspChnWaveBufAdd')
 command.insert(1,'-Wl,--wrap=_ZN5Sound13PlayCharVoiceEjjRK7Vector3')
 # Static library follows the game object list, before system libraries.
 idx=next(i for i,x in enumerate(command) if x.startswith('-l'))
 command.insert(idx,str(archive))
 elf=OUT/'sm64ds-movement-lab.elf';command[command.index('-o')+1]=str(elf)
 (OUT/'link-command.json').write_text(json.dumps(command,indent=2))
 run(command)
 run(['/opt/devkitpro/tools/bin/3dsxtool',elf,OUT/'sm64ds-movement-lab.3dsx'])
 evidence=OUT/'source-evidence';sources=[]
 for folder in ['source/movement_lab','source/integrated_native/movement']:
  sources.extend(p for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in {'.cpp','.c','.h','.hpp','.inc'})
 sources.extend([Path(__file__),ROOT/'tools/movement_lab/build_core.py'])
 for source in sources:
  target=evidence/source.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
 source_hashes={str(source.relative_to(ROOT)):hashlib.sha256(source.read_bytes()).hexdigest() for source in sources}
 shutil.copyfile(ROOT/'build/movement-lab/core/arm/sources.json',OUT/'core-sources.json')
 (OUT/'build.json').write_text(json.dumps({'source_sha256':source_hashes,'core_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'geometry':geometry,'libsm64_commit':run(['git','-C',ROOT/'third_party/libsm64-motion','rev-parse','HEAD']).strip(),'status':'native DS-world laboratory built; emulator and gameplay acceptance pending','movement_source':json.loads((ROOT/'build/movement-lab/core/port-source.json').read_text()),'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.glob('sm64ds-movement-lab.*')}},indent=2))
 print(OUT/'sm64ds-movement-lab.3dsx')
if __name__=='__main__':main()
