#!/usr/bin/env python3
"""Build the isolated N64 controller; never modifies the pinned source checkout."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from devkitpro_tools import arm_tool, sdk_tool
from port_source import import_port,functions,PORT
import subprocess, re, hashlib, json, argparse, concurrent.futures, shutil, fcntl, os
ROOT=Path(__file__).resolve().parents[2]
VENDOR=ROOT/'third_party/libsm64-motion'
LIB=ROOT/'build/movement-lab/core/source'
OUT=ROOT/'build/movement-lab/core'
ANIMS=Path(os.environ.get('SM64DS_MOTION_ANIMS',str(PORT/'assets/anims'))).expanduser().resolve()
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True)
 if p.returncode: raise RuntimeError(' '.join(map(str,cmd))+'\n'+p.stderr[-5000:])
 return p.stdout

def prepare():
 OUT.mkdir(parents=True,exist_ok=True)
 shutil.copytree(VENDOR/'src',LIB/'src',dirs_exist_ok=True)
 (LIB/'src/decomp/mario').mkdir(exist_ok=True)
 (LIB/'src/decomp/mario/model.inc.h').write_text('#pragma once\n')
 path=LIB/'src/decomp/include/types.h';raw=path.read_text().replace('//  /*0x94*/ struct PlayerCameraState *statusForCamera;','    struct PlayerCameraState *statusForCamera;');path.write_text(raw)
 import_port(LIB,OUT)
 text=(LIB/'src/libsm64.c').read_text()
 text=text.replace('#include "decomp/mario/geo.inc.h"','extern void motion_animation_init(void);')
 text=text.replace('    load_mario_textures_from_rom( rom, outTexture );\n    load_mario_anims_from_rom( rom );','    (void)rom; (void)outTexture; motion_animation_init();')
 text=text.replace('        s_mario_graph_node = process_geo_layout( s_mario_geo_pool, mario_geo_ptr );','        s_mario_graph_node = NULL; // DS supplies the rendered character.')
 text=text.replace('    gfx_adapter_bind_output_buffers( outBuffers );\n\n    geo_process_root_hack_single_node( s_mario_graph_node );','''    (void)outBuffers;
    // The original renderer advances animation after behavior. Retain that
    // timing, including root translation data, without emitting N64 geometry.
    struct AnimInfo *ai = &gMarioState->marioObj->header.gfx.animInfo;
    if (ai->curAnim) {
        ai->animFrame = geo_update_animation_frame(ai, &ai->animFrameAccelAssist);
        ai->animTimer = gAreaUpdateCounter;
    }''')
 text=text.replace('    gAreaUpdateCounter++;','    gAreaUpdateCounter++;\n    gGlobalTimer++;')
 adjust=functions((PORT/'src/game/game_init.c').read_text())['adjust_analog_stick'][2]
 prefix='#include "decomp/shim.h"\nstatic int motion_raw_active,motion_raw_x,motion_raw_y;\nstatic short motion_raw_camera;\n'+adjust+'\n'
 text=prefix+text
 begin=text.index('    gMarioState->area->camera->yaw = atan2s( inputs->camLookZ, inputs->camLookX );')
 end=text.index('    apply_mario_platform_displacement();',begin)
 old=text[begin:end]
 text=text[:begin]+'''    if(motion_raw_active){
        gMarioState->area->camera->yaw=motion_raw_camera;
        gController.rawStickX=motion_raw_x/2;
        gController.rawStickY=motion_raw_y/2;
        adjust_analog_stick(&gController);
    }else{
'''+old+'    }\n'+text[end:]
 text += '''
void motion64_tick_circle(int id,int x,int y,short camera,int jump,int attack,int crouch,struct SM64MarioState* out){
    struct SM64MarioInputs input={0,1,0,0,jump!=0,attack!=0,crouch!=0};
    motion_raw_active=1;motion_raw_x=x;motion_raw_y=y;motion_raw_camera=camera;
    sm64_mario_tick(id,&input,out,NULL);motion_raw_active=0;
}
void motion64_detail(unsigned* timer,unsigned* phase){*timer=gMarioState->actionTimer;*phase=gMarioState->actionState;}
'''

 text=text.replace('    init_mario_from_save_file();','    init_mario_from_save_file();\n    gMarioState->statusForCamera=calloc(1,sizeof(struct PlayerCameraState));')
 text=text.replace('    free( gMarioObject );','    free(gMarioState->statusForCamera);\n    free( gMarioObject );')
 text += '\nfloat motion64_speed=1.f,motion64_jump=1.f;\nvoid motion64_profile(float speed,float jump){motion64_speed=speed;motion64_jump=jump;}\n'
 text+='\n#include "solver_contacts.inc"\n'
 (OUT/'headless.c').write_text(text)
 # DS owns playback. Match play_sound's initialization guard before any
 # linked-list walk in the optional, uninitialized N64 sound mixer.
 path=LIB/'src/decomp/audio/external.c';raw=path.read_text()
 raw='#include "../../load_audio_data.h"\n'+raw
 for signature in ['void stop_sound(u32 soundBits, f32 *pos) {',
                   'void stop_sounds_from_source(f32 *pos) {',
                   'void stop_sounds_in_continuous_banks(void) {']:
  assert raw.count(signature)==1
  raw=raw.replace(signature,signature+'\n    if (!g_is_audio_initialized) return;',1)
 path.write_text(raw)
 # Optional original-DS collision query owner. Unregistered providers preserve
 # the existing mesh behavior; registered misses/errors never use a second mesh.
 for name in ['collision_provider.h','collision_provider.c','solver_contacts.h','solver_contacts.inc']:
  shutil.copyfile(ROOT/'source/integrated_native/movement'/name,LIB/'src'/name)
 collision=LIB/'src/decomp/engine/surface_collision.c';body=collision.read_text()
 body='#include "../../collision_provider.h"\n'+body
 anchor='f32 find_floor(f32 xPos, f32 yPos, f32 zPos, struct SM64SurfaceCollisionData **pfloor)\n{'
 assert body.count(anchor)==1
 body=body.replace(anchor,anchor+'\n    f32 provided_height;\n    if(motion64_query_floor(xPos,yPos,zPos,&provided_height,pfloor))return provided_height;')
 anchor='f32 find_floor_height(f32 x, f32 y, f32 z)\n{'
 assert body.count(anchor)==1
 body=body.replace(anchor,anchor+'\n    f32 provided_height;struct SM64SurfaceCollisionData* provided_floor;\n    if(motion64_query_floor(x,y,z,&provided_height,&provided_floor))return provided_height;')
 anchor='f32 find_ceil(f32 posX, f32 posY, f32 posZ, struct SM64SurfaceCollisionData **pceil)\n{'
 assert body.count(anchor)==1
 body=body.replace(anchor,anchor+'\n    f32 provided_height;\n    if(motion64_query_ceiling(posX,posY,posZ,&provided_height,pceil))return provided_height;')
 anchor='s32 find_wall_collisions(struct SM64WallCollisionData *colData)\n{'
 assert body.count(anchor)==1
 body=body.replace(anchor,anchor+'\n    int provided_count;\n    if(motion64_query_walls(colData,&provided_count))return provided_count;')
 collision.write_text(body)
 platform=LIB/'src/decomp/game/platform_displacement.c';body=platform.read_text()
 body='#include "../../collision_provider.h"\n'+body
 anchor='    get_mario_pos(&x, &y, &z);'
 assert body.count(anchor)==1
 body=body.replace(anchor,anchor+'''
    float provided_position[3]={x,y,z};int16_t provided_yaw=0;
    int provided=motion64_query_platform(platform,provided_position,&provided_yaw);
    if(provided) {
        if(provided>0) {
            set_mario_pos(provided_position[0],provided_position[1],provided_position[2]);
            if(isMario)gMarioState->faceAngle[1]+=provided_yaw;
        }
        return;
    }
''')
 platform.write_text(body)

 path=LIB/'src/decomp/game/mario.c'; raw=path.read_text()
 raw='extern float motion64_speed,motion64_jump;\n'+raw
 raw=raw.replace('    if (m->intendedMag > 0.0f) {','    m->intendedMag *= motion64_speed;\n    if (m->intendedMag > 0.0f) {',1)
 raw=raw.replace('    m->vel[1] = initialVelY + get_additive_y_vel_for_jumps() + m->forwardVel * multiplier;', '    m->vel[1] = (initialVelY + get_additive_y_vel_for_jumps() + m->forwardVel * multiplier) * motion64_jump;',1)
 path.write_text(raw)
 # The library's object allocator left graph/animation fields indeterminate
 # on non-Windows targets. A reset must reproduce the same initial state.
 path=LIB/'src/decomp/game/object_stuff.c';raw=path.read_text()
 assert raw.count('malloc(sizeof(struct Object))')==1
 path.write_text(raw.replace('malloc(sizeof(struct Object))','calloc(1,sizeof(struct Object))'))
 # Use complete local animation values: some original actions consume root
 # translation. These private generated assets never enter source control.
 chunks=['#include "load_anim_data.h"\n#include <stdlib.h>\n#define ANIMINDEX_NUMPARTS(x) (sizeof(x)/sizeof((x)[0])/6-1)\n']
 names=[]
 for file in sorted(ANIMS.glob('anim_*.inc.c'),key=lambda p:int(p.stem.split('_')[1].split('.')[0],16)):
  raw=file.read_text();matches=list(re.finditer(r'static const struct Animation (anim_[A-Fa-f0-9]+)\[\] = \{.*?\};',raw,re.S))
  assert matches,file
  headers=[m[0] for m in matches]; names.extend(m[1] for m in matches)
  for m in reversed(matches):raw=raw[:m.start()]+raw[m.end():]
  chunks.extend([raw,*headers])
 assert len(names)==209
 chunks.append('static const struct Animation *anims[]={'+','.join(names)+'};\n')
 chunks.append('void motion_animation_init(void) {}\nvoid unload_mario_anims(void) {}\nvoid load_mario_animation(struct MarioAnimation *a,u32 i) { if(i>=sizeof(anims)/sizeof(anims[0])) abort(); a->currentAnimAddr=i+1; a->targetAnim=(struct Animation*)anims[i]; }\n')
 (OUT/'animations.c').write_text('\n'.join(chunks))
 return [OUT/'headless.c',OUT/'animations.c']

def _build(arm=False):
 generated=prepare(); dst=OUT/('arm' if arm else 'host');dst.mkdir(exist_ok=True)
 cc=arm_tool('gcc') if arm else 'clang'
 sources=[]
 for folder in ['src','src/decomp','src/decomp/engine','src/decomp/game','src/decomp/audio','src/decomp/tools','src/decomp/include/PR','src/decomp/pc']:
  sources.extend((LIB/folder).glob('*.c'))
 sources=[p for p in sources if p.name not in ['libsm64.c','load_anim_data.c','load_tex_data.c','gfx_adapter.c','rendering_graph_node.c','behavior_actions.c']]+generated
 flags=['-std=gnu11','-O2','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-DGBI_FLOATS','-DVERSION_US','-DNO_SEGMENTED_MEMORY','-I'+str(LIB/'src'),'-I'+str(LIB/'src/decomp/include'),'-Wno-incompatible-pointer-types','-Wno-int-conversion','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast']
 if arm:flags+=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mfpu=vfp','-mtp=soft','-D__3DS__']
 def compile(p):
  obj=dst/(hashlib.sha256(str(p).encode()).hexdigest()[:12]+'.o')
  run([cc,*flags,'-c',p,'-o',obj])
  if arm:run([arm_tool('objcopy'),'--redefine-sym','osGetTime=motion64_osGetTime',obj])
  return obj
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile,sources))
 ar=arm_tool('ar') if arm else 'ar'
 archive=dst/'libmotion64.a';archive.unlink(missing_ok=True);run([ar,'rcs',archive,*objects])
 evidence=sources+[LIB/'src'/name for name in ['collision_provider.h','solver_contacts.h','solver_contacts.inc']]
 (dst/'sources.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in evidence},indent=2))
 print(archive)
 return archive

def build(arm=False):
 # Host gates and the ARM packager share the generated source directory.
 # Serialize preparation through archive creation so neither can overwrite
 # a source while the other compiles it or applies its one-time adapters.
 OUT.mkdir(parents=True,exist_ok=True)
 with (OUT/'.build.lock').open('a+b') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX)
  return _build(arm)
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--arm',action='store_true');args=parser.parse_args();build(args.arm)
