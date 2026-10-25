#include "native_motion.hpp"
#include "motion_buttons.hpp"
#include "retry_gate.hpp"
#include "Player.h"
#include "SaveData.h"
#include "Stage.h"
#include "platform/3ds/enhance_overlay.h"
#include "platform/3ds/enhance_settings.h"
#include "platform/3ds/developer_keys.hpp"
#include "libsm64.h"
#include "controls.hpp"
#include "presentation.hpp"
#include "ModelAnim.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#define u32 CtrU32
#define s32 CtrS32
#define vu32 CtrVU32
#define vs32 CtrVS32
#define Profile CtrProfile
#include <3ds.h>
#undef Profile
#undef vs32
#undef vu32
#undef s32
#undef u32
#include "ds_world_source.hpp"
extern "C" {
extern Player* data_0209f394[];
extern unsigned char data_0209f2f8,data_0209d660,data_020a0e40;
extern unsigned char data_0209f49c[],data_0209f49e[];
extern int data_0209fc48;
extern unsigned short data_ov002_020ff170[],data_ov002_020ff140[];
void StartFile(signed char,unsigned char);
void _ZN6Player7SetAnimEji5Fix12IiEj(void*,unsigned,int,int,unsigned);
void Player_AdvanceAnims(void*);
void func_ov002_020e25f0(void*,int);
void func_ov002_020c2f64(void*);
void func_ov002_020d4c30(void*);
int _ZNK6Player14GetBodyModelIDEjb(void*,unsigned,int);
int _ZNK9Animation12WillHitFrameEi(void*,int);

void motion64_profile(float speed,float jump);
void motion64_tick_circle(int,int,int,short,int,int,int,SM64MarioState*);
void motion64_detail(unsigned*,unsigned*);
void _ZN5Sound13PlayCharVoiceEjjRK7Vector3(unsigned,unsigned,const void*);
void _ZN5Sound9PlayBank0EjRK7Vector3(unsigned,const void*);
extern Player::State data_ov002_0211019c; // ordinary jump; verified generated state map
extern Player::State data_ov002_021101e4; // freefall
int func_ov002_020e2664(void*);
}
namespace native_motion {
namespace {
Mode mode=Mode::Original;
uint32_t keys=0,pressed=0,commands=0,commands_pressed=0;
unsigned buttons_mode=~0u;
RetryGate terrain_retry;
bool initialized=false,owned=false,resync=true,faulted=false;
Player* owner=nullptr;unsigned owner_character=~0u,owner_id=0;
Player::State* owned_state=nullptr;
bool sync_buttons=false,gameplay=false;
unsigned scene_level=~0u;
bool ds_jump=false,ds_action=false;
unsigned jump_held=0,steps=0;
unsigned trace_tick=0;
bool trace_enabled(){static const bool on=std::getenv("SM64DS_MOTION_TRACE")!=nullptr;return on;}
int mario=-1,visual_anim=-1;
SM64MarioState state{};
movement_geometry::DsWorldSource<1024,32> world_source;
movement_geometry::WorldSession<256,4> world_session;
Player* sound_player=nullptr;
unsigned voices=0,terrain_sounds=0,core_resets=0,footsteps=0;
uint32_t pending_sounds[64];unsigned pending_count=0;
const char* status="Preparing castle...";
const char* mode_names[]={"Original","Hybrid","N64"};
const char* character_names[]={"Mario","Luigi","Wario","Yoshi"};
float unfixed(int x){return float(x)/4096.f;}
int fixed(float x){return int(std::lround(x*4096.f));}
bool ordinary(Player* p){
 auto f=p->mState->mMain;
 return f==&Player::St_Wait_Main || f==&Player::St_Walk_Main || f==&Player::St_Jump_Main ||
 f==&Player::St_Fall_Main || f==&Player::St_Land_Main || f==&Player::St_Crouch_Main ||
 f==&Player::St_Crawl_Main || f==&Player::St_TurnAround_Main ||
 f==&Player::St_LongJump_Main || f==&Player::St_WallJump_Main || f==&Player::St_SideFlip_Main;
}
bool transformed(const Player* p){
 return p->mPowerupTimer||p->mBalloonTimer||p->mIsBalloon||p->mIsMetal||p->mIsVanish||p->mHasWings||p->mIsMega;
}
bool valid_owner(const Player* p){
 return p==owner&&p->uniqueID==owner_id&&p->mCharacter==owner_character&&p->mState==owned_state&&
  !p->mIsControlDisabled&&!p->mIsNoControl&&!p->mIsUnderwater&&!p->mIsTakingDamage&&!transformed(p)&&
  !p->mHeldObj&&!(p->mCharacter==3&&p->mObjInMouth);
}
void sound_event(uint32_t bits,float*) {
 if(!sound_player)return;
 if(pending_count<64)pending_sounds[pending_count++]=bits;
 else {std::fputs("FATAL: movement sound event overflow\n",stderr);std::abort();}
}
void dispatch_sounds(Player* p){
 bool takeoff=false;
 for(unsigned i=0;i<pending_count;++i){
  const unsigned bits=pending_sounds[i],bank=bits>>28,id=(bits>>16)&255;
  if(bank!=2)continue;
  const int group=jump_voice_group(id,state.action);
  int voice=-1;
  if(group>=0){func_ov002_020e25f0(p,group);takeoff=true;++terrain_sounds;}
  else if(id<=4 || (id>=0x2b && id<=0x2f)){
   if(state.action==0x03000886)voice=0xc;
   else if(state.action==0x03000888)voice=4;
  }else if(id==0x22)voice=0x15;
  if(voice>=0)_ZN5Sound13PlayCharVoiceEjjRK7Vector3(p->mCharacter,voice,&p->mCamSpacePosX);
  if(group>=0 || voice>=0){++voices;if(trace_enabled())std::printf("NATIVE_MOTION voice group=%d voice=%d char=%u n64=%08x total=%u\n",group,voice,unsigned(p->mCharacter),bits,voices);}
 }
 for(unsigned i=0;i<pending_count;++i){
  const unsigned bits=pending_sounds[i],bank=bits>>28,id=(bits>>16)&255;
  if(bank!=0)continue;
  if(id<8){
   p->mLandSoundPlayed=0;
   if(!takeoff){_ZN5Sound9PlayBank0EjRK7Vector3(p->mIsMetal?0xa0:0x30+p->mGroundSoundType,&p->mCamSpacePosX);++terrain_sounds;takeoff=true;}
  }else if(id<0x10){func_ov002_020c2f64(p);++terrain_sounds;}
  else if(id>=0x18 && id<0x20 && state.action==0x0080023c){
   _ZN5Sound9PlayBank0EjRK7Vector3(p->mIsInShallowWater?0xd3:0x90+p->mGroundSoundType,&p->mCamSpacePosX);++terrain_sounds;
  }
  // N64 footstep events describe N64 bone timing. The DS skeleton's
  // actual contact frames below own footstep audio, avoiding double steps.
 }
 pending_count=0;
}
void animate(Player* p){
 const auto v=presentation(state.action,state.animID,state.forwardVelocity);
 const int model=_ZNK6Player14GetBodyModelIDEjb(p,p->param1&255,0);
 // Resource/model switches can temporarily leave no renderable body. Physics
 // and ownership remain valid; do not dereference an unavailable animation.
 if(model<0||model>=4||!p->mBodyModels[model]){visual_anim=-1;return;}
 Animation* a=static_cast<Animation*>(p->mBodyModels[model]);
 const bool gait_before=visual_anim==0x48 || visual_anim==0x3f;
 const bool gait_after=v.animation==0x48 || v.animation==0x3f;
 const int old_frame=a->currFrame;
 const unsigned old_length=a->numFramesAndFlags&0x3fffffff;
 // A freshly switched body can carry the clip pointer with an uninitialized
 // frame controller. Bypass ModelAnim's same-file fast path in that case.
 if(!old_length)p->mBodyModels[model]->file=nullptr;
 _ZN6Player7SetAnimEji5Fix12IiEj(p,v.animation,v.flags,v.rate,0);
 // Preserve normalized phase when exchanging walk/run clips of different
 // lengths. Updating the same clip uses ModelAnim's original fast path.
 if(gait_before && gait_after && visual_anim!=int(v.animation) && old_length)
  a->currFrame=int((int64_t(old_frame)*(a->numFramesAndFlags&0x3fffffff))/old_length);
 visual_anim=v.animation;
 bool contact=v.footsteps && (_ZNK9Animation12WillHitFrameEi(a,4) || _ZNK9Animation12WillHitFrameEi(a,19));
 if(contact){func_ov002_020d4c30(p);++footsteps;}
 const int before=a->currFrame;
 Player_AdvanceAnims(p);
 if(trace_enabled())std::printf("NATIVE_MOTION_PRESENT %u %u %u %u %d %d %u %u %d\n",trace_tick,unsigned(p->mCharacter),p->mCharFileBase/4,a->numFramesAndFlags&0xc0000000,before,int(a->speed),a->numFramesAndFlags&0x3fffffff,unsigned(contact),int(a->currFrame));
}
void release_core(bool invalidate_scene=true){
 owned=false;owned_state=nullptr;
 if(initialized){world_session.close();initialized=false;}
 if(invalidate_scene)world_source.reset_after_session_closed();
 else world_source.reset_contacts_after_session_closed();
 mario=-1;resync=true;visual_anim=-1;
}
void native_fault(const char* reason){
 std::printf("NATIVE_MOTION_NATIVE_FAULT %s faults=%u begin=%u registryChanged=%u\n",reason,unsigned(world_session.faults()),world_session.begin_failure_stage(),unsigned(world_source.generation_changed()));

 release_core();faulted=true;status="DS fallback: native adapter fault";
}
void reset_core(Player* p){
 release_core(false);
 if(!world_source.prepare()){native_fault("scene geometry preparation");return;}
 if(!world_session.open(world_source.callbacks())){native_fault("world session open");return;}
 initialized=true;sm64_register_play_sound_function(sound_event);
 ++core_resets;
 if(trace_enabled())std::printf("NATIVE_MOTION reset=%u pos=%.3f,%.3f,%.3f dsAir=%u\n",core_resets,unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ),unsigned(p->mIsAirborne));
 mario=world_session.create(unfixed(p->mPosX),unfixed(p->mPosY)+2,unfixed(p->mPosZ),p);
 if(mario<0){if(world_source.delegation()!=movement_geometry::MaterialUse::Solid){status="DS spawn terrain";release_core(false);return;}native_fault("native spawn/contact validation");return;}
 sm64_set_mario_position(mario,unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ));
 sm64_set_mario_faceangle(mario,float(p->mAngleY)*3.14159265358979323846f/32768.f);
 sm64_set_mario_forward_velocity(mario,unfixed(p->mHorzSpeed));
 sm64_set_mario_velocity(mario,0,unfixed(p->mVertSpeed),0);
 sm64_set_mario_action(mario,p->mIsAirborne?0x0100088c:0x0c400201); // ACT_FREEFALL / ACT_IDLE
 const uint32_t previous_held=commands&~commands_pressed;
 if(trace_enabled())std::printf("NATIVE_MOTION_INPUT_SYNC previous=%08lx current=%08lx down=%08lx\n",(unsigned long)previous_held,(unsigned long)keys,(unsigned long)pressed);
 if(!world_session.seed_buttons(mario,previous_held&Jump,previous_held&Attack,previous_held&Crouch)){native_fault("controller history sync");return;}
 if(trace_enabled())std::printf("NATIVE_MOTION_TRACE RESET %.9g %.9g %.9g %.9g %.9g %.9g %u\n",unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ),float(p->mAngleY)*3.14159265358979323846f/32768.f,unfixed(p->mHorzSpeed),unfixed(p->mVertSpeed),unsigned(p->mIsAirborne));
 resync=false;sync_buttons=false;visual_anim=-1;
}
bool set_ds_air_state(Player* p,Player::State& target=data_ov002_0211019c){
 const int vx=p->mHorzSpeed,vy=p->mVertSpeed,x=p->mPosX,y=p->mPosY,z=p->mPosZ;
 const short yaw=p->mAngleY;
 // Cleanup can veto a transition, or an initializer can redirect it. In
 // either case the resulting DS state keeps its own velocity and position.
 if(!p->ChangeState(target) || p->mState!=&target)return false;
 p->mHorzSpeed=vx;p->mVertSpeed=vy;p->mPosX=x;p->mPosY=y;p->mPosZ=z;p->mAngleY=yaw;
 p->mPrevAngleY=yaw;
 if(&target==&data_ov002_0211019c)p->mJumpComboStage=0;
 return true;
}
}
uint32_t input(uint32_t held,uint32_t down){
 keys=held&~uint32_t(KEY_TOUCH|KEY_START|KEY_SELECT);pressed=down;
 // Same Developer chord rule as ordinary game input (developer_keys.hpp).
 keys=developer_keys::game_keys(keys,held);pressed=developer_keys::game_keys(pressed,held);
 return developer_keys::game_keys(held,held);
}
void frame(unsigned scene,unsigned){
 const unsigned requested=enhance::live().physics_mode.load(std::memory_order_relaxed)%3;
 const unsigned buttons=enhance::live().controller_mode.load(std::memory_order_relaxed);
 // Logical DS actions already use the selected table. Changing the table,
 // or Hybrid <-> N64, must not destroy landing history/doubleJumpTimer.
 if(buttons_mode!=buttons){terrain_retry.clear();buttons_mode=buttons;sync_buttons=true;}
 if(unsigned(mode)!=requested){
  terrain_retry.clear();faulted=false;mode=Mode(requested);sync_buttons=true;
  if(mode==Mode::Original)release_core(false);
  ds_jump=ds_action=false;jump_held=0;
 }
 if(gameplay!=(scene==3)||scene_level!=data_0209f2f8){terrain_retry.clear();release_core();ds_jump=ds_action=false;jump_held=0;scene_level=data_0209f2f8;faulted=false;owner=nullptr;}
 gameplay=scene==3;
}
bool step_impl(Player* p){
 owned=false;
 const unsigned slot=data_020a0e40;
 if(slot>=4)return false;
 uint16_t logical_held=0,logical_pressed=0;
 std::memcpy(&logical_held,data_0209f49c+slot*0x18,2);std::memcpy(&logical_pressed,data_0209f49e+slot*0x18,2);
 // input() already removes the physical dump chord before DS remapping.
 // Preserve other logical buttons (e.g. held jump) and their history.
 commands=motion_buttons(logical_held);commands_pressed=motion_buttons(logical_pressed);

 if(mode==Mode::Original||faulted){resync=true;status="DS physics and abilities";return false;}
 if(owner!=p||owner_id!=p->uniqueID||owner_character!=p->mCharacter){terrain_retry.clear();release_core();owner=p;owner_id=p->uniqueID;owner_character=p->mCharacter;ds_jump=ds_action=false;jump_held=0;}
 if(!p->mState)return false;
 if(p->mPlayerNo || data_0209d660 || data_0209fc48 || p->mIsControlDisabled || p->mIsNoControl){resync=true;return false;}
 // Transformations keep their complete DS actions in every mode.
 if(transformed(p)){resync=true;status="DS flower / transformation";return false;}
 if(p->mIsUnderwater || p->mIsTakingDamage){resync=true;status="DS water / damage";return false;}
 if(commands&Jump)jump_held=std::min(jump_held+1,3u);else jump_held=0;
 if(ds_jump){
  if(!p->mIsAirborne || !(commands&Jump)){ds_jump=false;resync=true;}else{status="DS held-jump ability";return false;}
 }
 if(mode==Mode::Hybrid && (p->mCharacter==1 || p->mCharacter==3) && p->mIsAirborne && jump_held>=3 && ordinary(p)){
  ds_jump=set_ds_air_state(p);resync=true;status="DS held-jump ability";return false;
 }
 // Attacks, grabs, tongue/eggs use real DS actors/hitboxes in both native
 // modes. The imported controller cannot collide with DS enemies itself.
 if((commands_pressed&Attack) || p->mHeldObj || (p->mCharacter==3 && p->mObjInMouth)){
  if(!ds_action){if(p->mIsAirborne)set_ds_air_state(p);else p->ChangeState(Player::ST_WAIT);ds_action=true;}
  resync=true;status="DS attack / carried object";return false;
 }
 if(ds_action){if(!ordinary(p)){resync=true;return false;}ds_action=false;}
 if(!ordinary(p)){resync=true;status="DS world interaction";return false;}
 // Hazard/warp friction remains owned by the DS state machine. Retry native
 // movement only after original sensing reports ordinary terrain again.
 if(p->mSurfaceType || p->mFloorClass>=6){resync=true;status="DS terrain effect";return false;}
 const RetryGate::Signature terrain_signature{{p->mPosX,p->mPosY,p->mPosZ,p->mFloorClass,p->mSurfaceType,std::int64_t(reinterpret_cast<std::intptr_t>(p->mState))}};
 if(resync){
  if(!terrain_retry.ready(terrain_signature,commands_pressed!=0)){status="DS terrain retry pending";return false;}
  reset_core(p);
 }
 if(mario<0){if(!faulted)terrain_retry.defer(terrain_signature);return false;}
 if(sync_buttons){
  const uint32_t previous=commands&~commands_pressed;
  if(!world_session.seed_buttons(mario,previous&Jump,previous&Attack,previous&Crouch)){native_fault("controller history sync");return false;}
  sync_buttons=false;
 }
 // DS water/terrain sensing remains separate from its position solver.
 // The N64 controller owns collision response while it owns movement.
 sm64_set_mario_water_level(mario,p->unk_650==int(0x80000000)?-10000:p->unk_650/4096);
 const bool wario=p->mCharacter==2;
 motion64_profile(wario?float(data_ov002_020ff170[2])/data_ov002_020ff170[0]:1.f,
                  wario?float(data_ov002_020ff140[2])/data_ov002_020ff140[0]:1.f);
 circlePosition pad{};hidCircleRead(&pad);
 const unsigned sensitivity=enhance::live().move_sensitivity.load(std::memory_order_relaxed);
 int x=int(pad.dx)*int(sensitivity)/100,y=int(pad.dy)*int(sensitivity)/100;
 if(buttons_mode==0){if(keys&KEY_DLEFT)x=-156;if(keys&KEY_DRIGHT)x=156;if(keys&KEY_DUP)y=156;if(keys&KEY_DDOWN)y=-156;}

 extern short camera_yaw(unsigned char) asm("GetAngleToCamera");
 const short camera=camera_yaw(0);
 pending_count=0;sound_player=p;
 const bool advanced=world_session.tick_circle(mario,x,y,camera,commands&Jump,commands&Attack,commands&Crouch,state);
 sound_player=nullptr;
 if(!advanced){
  pending_count=0;
  if(world_source.generation_changed()){resync=true;status="DS scene geometry refresh";return false;}
  if(world_source.delegation()!=movement_geometry::MaterialUse::Solid){terrain_retry.defer(terrain_signature);resync=true;status="DS terrain effect";return false;}
  native_fault("native simulation step");return false;
 }
 ++trace_tick;
 if(trace_enabled()){
  unsigned timer,phase;motion64_detail(&timer,&phase);
  std::printf("NATIVE_MOTION_TRACE TICK %u %d %d %d %u %u %u %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %lu %u %u %ld %d\n",trace_tick,x,y,int(camera),unsigned(bool(commands&Jump)),unsigned(bool(commands&Attack)),unsigned(bool(commands&Crouch)),p->unk_650==int(0x80000000)?-10000:p->unk_650/4096,state.position[0],state.position[1],state.position[2],state.velocity[0],state.velocity[1],state.velocity[2],state.forwardVelocity,state.faceAngle,(unsigned long)state.action,timer,phase,(long)state.animID,int(state.animFrame));
 }
 owned=true;owned_state=p->mState;commit(p);
 dispatch_sounds(p);
 // The solver reports these every tick; skipping the original DS locomotion
 // mains also skipped their dust calls. Keep water effects with DS sensing.
 if(state.particleFlags&1u)p->func_ov002_020bf88c(); // PARTICLE_DUST
 animate(p);
 status=wario?"N64 + original Wario ratios":"N64 controller active";
 if((++steps%120)==0&&trace_enabled())std::printf("NATIVE_MOTION step=%u mode=%s char=%u pos=%.3f,%.3f,%.3f vel=%.3f action=%08lx ratios=%.5f,%.5f\n",steps,mode_names[unsigned(mode)],unsigned(p->mCharacter),state.position[0],state.position[1],state.position[2],state.forwardVelocity,(unsigned long)state.action,wario?float(data_ov002_020ff170[2])/data_ov002_020ff170[0]:1.f,wario?float(data_ov002_020ff140[2])/data_ov002_020ff140[0]:1.f);
 return true;
}
bool step(Player* p){
 const bool previously_owned=owned&&owner==p&&owner_id==p->uniqueID&&owner_character==p->mCharacter;
 const bool result=step_impl(p);
 if(trace_enabled()) {
  static unsigned serial=0;static int last_mode=-1,last_char=-1,last_owner=-1;static const char* last_status=nullptr;
  ++serial;
  if(last_mode!=int(mode)||last_char!=int(p->mCharacter)||last_owner!=int(result)||last_status!=status||serial%30==0)
   std::printf("NATIVE_MOTION_OWNERSHIP update=%u mode=%u char=%u native=%u keys=%08lx air=%u pos=%.6f,%.6f,%.6f vy=%.6f state=%08lx reason=%s\n",serial,unsigned(mode),unsigned(p->mCharacter),unsigned(result),(unsigned long)keys,unsigned(p->mIsAirborne),unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ),unfixed(p->mVertSpeed),(unsigned long)(uintptr_t)p->mState,status);
  last_mode=int(mode);last_char=p->mCharacter;last_owner=int(result);last_status=status;
 }
 if(!result){
  // Destroy contacts before the next DS update, retaining the decoded static
  // scene for prepare() to revalidate when ordinary movement resumes.
  release_core(false);
  if(previously_owned&&p->mState&&p->mIsAirborne&&ordinary(p)&&!ds_jump&&!ds_action&&!p->mIsUnderwater&&!p->mIsTakingDamage&&!p->mIsControlDisabled&&!p->mIsNoControl&&!transformed(p)){
   set_ds_air_state(p,data_ov002_021101e4);
  }
 }
 return result;
}
void commit(Player* p){if(!owned)return;
 // Sensing/body interactions later in Behavior can enter damage, water,
 // scripts or another DS state. Never overwrite that transition's result.
 if(!valid_owner(p)){release_core(false);return;}
 p->mPosX=fixed(state.position[0]);p->mPosY=fixed(state.position[1]);p->mPosZ=fixed(state.position[2]);
 p->mHorzSpeed=fixed(state.forwardVelocity);p->mVertSpeed=fixed(state.velocity[1]);
 p->mAngleY=short(state.faceAngle*32768.f/3.14159265358979323846f);p->mPrevAngleY=p->mAngleY;
 p->mIsAirborne=(state.action&0x800)!=0;p->mIsInAirState=p->mIsAirborne;
 if(trace_enabled())std::printf("NATIVE_MOTION_TRACE DS %u %.9g %.9g %.9g %.9g %.9g %d\n",trace_tick,unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ),unfixed(p->mHorzSpeed),unfixed(p->mVertSpeed),int(p->mAngleY));
}
}
extern "C" bool native_motion_owns_position(){
 // Behavior asks before its collision/sensing work as well as committing at
 // the end. A post-tick DS transition must receive that collision work now.
 if(native_motion::owned&&!native_motion::valid_owner(native_motion::owner))native_motion::release_core(false);
 return native_motion::owned;
}
