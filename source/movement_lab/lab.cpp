#include "lab.hpp"
#include "Player.h"
#include "SaveData.h"
#include "Stage.h"
#include "platform/3ds/enhance_overlay.h"
#include "platform/3ds/enhance_settings.h"
#include "libsm64.h"
#include "controls.hpp"
#include "presentation.hpp"
#include "ModelAnim.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
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
extern unsigned char data_0209f2f8,data_0209d660;
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
void movement_lab_finish_audio();
void motion64_profile(float speed,float jump);
void motion64_tick_circle(int,int,int,short,int,int,int,SM64MarioState*);
void motion64_detail(unsigned*,unsigned*);
void _ZN5Sound13PlayCharVoiceEjjRK7Vector3(unsigned,unsigned,const void*);
void _ZN5Sound9PlayBank0EjRK7Vector3(unsigned,const void*);
extern Player::State data_ov002_0211019c; // ordinary jump; verified generated state map
extern Player::State data_ov002_021101e4; // freefall
int func_ov002_020e2664(void*);
}
namespace movement_lab {
namespace {
Mode mode=Mode::Original;
unsigned character=0,requested_character=0;
uint32_t keys=0,pressed=0;
bool touch_latched=false,started=false,initialized=false,owned=false,resync=true;
bool ds_jump=false,ds_action=false;
unsigned jump_held=0,steps=0;
unsigned replay_tick=0;
unsigned trace_tick=0;
bool replay_enabled(){static const bool on=std::getenv("SM64DS_LAB_REPLAY")!=nullptr;return on;}
bool trace_enabled(){static const bool on=std::getenv("SM64DS_LAB_TRACE")!=nullptr;return on||replay_enabled();}
bool presentation_replay(){static const bool on=std::getenv("SM64DS_LAB_PRESENTATION_REPLAY")!=nullptr;return on;}
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
  if(group>=0 || voice>=0){++voices;std::printf("MOVEMENT_LAB voice group=%d voice=%d char=%u n64=%08x total=%u\n",group,voice,unsigned(p->mCharacter),bits,voices);}
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
 if(trace_enabled())std::printf("MOVEMENT_PRESENT %u %u %u %u %d %d %u %u %d\n",trace_tick,unsigned(p->mCharacter),p->mCharFileBase/4,a->numFramesAndFlags&0xc0000000,before,int(a->speed),a->numFramesAndFlags&0x3fffffff,unsigned(contact),int(a->currFrame));
}
void release_core(){
 owned=false;
 if(initialized){world_session.close();world_source.reset_after_session_closed();initialized=false;}
 mario=-1;resync=true;
}
void native_fault(const char* reason){
 std::printf("MOVEMENT_LAB_NATIVE_FAULT %s faults=%u\n",reason,unsigned(world_session.faults()));
 if(replay_enabled())std::printf("FATAL: native movement replay: %s\n",reason);
 release_core();mode=Mode::Original;status="Original: native adapter fault";
}
void reset_core(Player* p){
 release_core();
 if(!world_source.prepare()){native_fault("scene geometry preparation");return;}
 if(!world_session.open(world_source.callbacks())){native_fault("world session open");return;}
 initialized=true;sm64_register_play_sound_function(sound_event);
 ++core_resets;
 std::printf("MOVEMENT_LAB reset=%u pos=%.3f,%.3f,%.3f dsAir=%u\n",core_resets,unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ),unsigned(p->mIsAirborne));
 mario=world_session.create(unfixed(p->mPosX),unfixed(p->mPosY)+2,unfixed(p->mPosZ),p);
 if(mario<0){native_fault("native spawn/contact validation");return;}
 sm64_set_mario_position(mario,unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ));
 sm64_set_mario_faceangle(mario,float(p->mAngleY)*3.14159265358979323846f/32768.f);
 sm64_set_mario_forward_velocity(mario,unfixed(p->mHorzSpeed));
 sm64_set_mario_velocity(mario,0,unfixed(p->mVertSpeed),0);
 sm64_set_mario_action(mario,p->mIsAirborne?0x0100088c:0x0c400201); // ACT_FREEFALL / ACT_IDLE
 const uint32_t previous_held=keys&~pressed;
 if(trace_enabled())std::printf("MOVEMENT_INPUT_SYNC previous=%08lx current=%08lx down=%08lx\n",(unsigned long)previous_held,(unsigned long)keys,(unsigned long)pressed);
 if(!world_session.seed_buttons(mario,previous_held&KEY_B,previous_held&KEY_A,previous_held&(KEY_L|KEY_ZL))){native_fault("controller history sync");return;}
 if(trace_enabled())std::printf("MOVEMENT_TRACE RESET %.9g %.9g %.9g %.9g %.9g %.9g %u\n",unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ),float(p->mAngleY)*3.14159265358979323846f/32768.f,unfixed(p->mHorzSpeed),unfixed(p->mVertSpeed),unsigned(p->mIsAirborne));
 resync=false;visual_anim=-1;
}
void set_ds_air_state(Player* p){
 const int vx=p->mHorzSpeed,vy=p->mVertSpeed,x=p->mPosX,y=p->mPosY,z=p->mPosZ;
 const short yaw=p->mAngleY;
 p->ChangeState(data_ov002_0211019c);
 p->mHorzSpeed=vx;p->mVertSpeed=vy;p->mPosX=x;p->mPosY=y;p->mPosZ=z;p->mAngleY=yaw;
 p->mPrevAngleY=yaw;p->mJumpComboStage=0;
}
void box(uint32_t* px,int x,int y,int w,int h,uint32_t color){for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)px[j*320+i]=color;}
void text(uint32_t* px,int x,int y,const char* s){enhance::draw_game_text(px,320,240,x,y,s,false);}
}
uint32_t input(uint32_t held,uint32_t down){
 keys=held&~uint32_t(KEY_TOUCH|KEY_START|KEY_SELECT);pressed=down;
 // Capture uses raw HID in game_main. Keep that chord out of both movement
 // owners so finishing a dump cannot trigger a crouch, attack or fresh press.
 constexpr uint32_t dump_chord=KEY_L|KEY_R|KEY_A;
 if(enhance::live().developer_dumps&&(held&dump_chord)==dump_chord){
  keys&=~dump_chord;pressed&=~dump_chord;
 }
 if(!(held&KEY_TOUCH)){touch_latched=false;return keys;}
 if(touch_latched)return keys;
 touch_latched=true;touchPosition t{};hidTouchRead(&t);
 if(t.py>=47 && t.py<91 && t.px>=8 && t.px<312){
  unsigned i=std::min(2u,unsigned(t.px-8)/104);mode=Mode(i);resync=true;ds_jump=ds_action=false;jump_held=0;
  std::printf("MOVEMENT_LAB mode=%s character=%s\n",mode_names[i],character_names[character]);
 }else if(t.py>=120 && t.py<166 && t.px>=8 && t.px<312){requested_character=std::min(3u,unsigned(t.px-8)/76);}
 return keys;
}
void frame(unsigned scene,unsigned updates){
 if(!started && scene==1 && updates>=90){
  SaveData::SetCharacterIntro(7);StartFile(1,0);started=true;
  std::puts("MOVEMENT_LAB castle-only start; private in-memory save fixture; no save write");
 }
 if(scene!=3){release_core();return;}
 Player* p=data_0209f394[0];if(!p){release_core();return;}
 if(data_0209f2f8!=1){release_core();status="Returning to castle...";StartFile(1,0);resync=true;return;}
 if(data_0209d660 || data_0209fc48 || p->mIsControlDisabled){release_core();return;}
 if(presentation_replay())requested_character=std::min(3u,replay_tick/240);
 if(p->mCharacter!=requested_character){
  p->SetRealCharacter(requested_character);character=requested_character;resync=true;ds_jump=ds_action=false;
  std::printf("MOVEMENT_LAB character=%s verified=%u\n",character_names[character],unsigned(p->mCharacter));
 }
 character=p->mCharacter;
 if(p->mPosY < -2000*4096){p->mPosX=p->mSpawnPosX;p->mPosY=p->mSpawnPosY;p->mPosZ=p->mSpawnPosZ;resync=true;}
}
bool step_impl(Player* p){
 owned=false;
 if(replay_enabled())mode=Mode::N64;
 if(mode==Mode::Original){resync=true;status="DS physics and abilities";return false;}
 if(p->mPlayerNo || data_0209f2f8!=1 || data_0209d660 || data_0209fc48 || p->mIsControlDisabled || p->mIsNoControl){resync=true;return false;}
 if(replay_enabled()){
  const unsigned t=replay_tick;
  keys=0;
  if(t>=20 && t<140)keys|=KEY_DUP;
  if(t>=60 && t<64)keys|=KEY_B;
  if(t>=170 && t<210)keys|=KEY_DRIGHT;
  if(t>=250 && t<310)keys|=KEY_DLEFT;
  if(t>=330 && t<360)keys|=KEY_B;
  if(t>=420 && t<470)keys|=KEY_L;
  if(t>=435 && t<448)keys|=KEY_B;
  if(t>=540 && t<620)keys|=KEY_DUP;
  if(t>=565 && t<585)keys|=KEY_L;
  if(t>=570 && t<582)keys|=KEY_B;
  if(t>=700 && t<720)keys|=KEY_B;
  if(t>=710 && t<715)keys|=KEY_L;
  if(presentation_replay()){
   const unsigned t=replay_tick%240;
   keys=0;
   if(t==0){p->mPosX=-1200*4096;p->mPosY=254*4096;p->mPosZ=6800*4096;p->mHorzSpeed=p->mVertSpeed=0;p->mIsAirborne=0;resync=true;}
   if(t>=20 && t<190)keys|=KEY_DUP;
   if(t>=95 && t<105)keys|=KEY_B;
   if(t>=205 && t<223)keys|=KEY_B;
  }
  ++replay_tick;
 }
 // Transformations keep their complete DS actions in every mode.
 if(p->mPowerupTimer || p->mBalloonTimer || p->mIsBalloon || p->mIsMetal || p->mIsVanish || p->mHasWings || p->mIsMega){resync=true;status="DS flower / transformation";return false;}
 if(keys&KEY_B)++jump_held;else jump_held=0;
 if(ds_jump){
  if(!p->mIsAirborne || !(keys&KEY_B)){ds_jump=false;resync=true;}else{status="DS held-jump ability";return false;}
 }
 if(mode==Mode::Hybrid && (p->mCharacter==1 || p->mCharacter==3) && p->mIsAirborne && jump_held>=3 && ordinary(p)){
  set_ds_air_state(p);ds_jump=true;resync=true;status="DS held-jump ability";return false;
 }
 // Yoshi's tongue/eggs and DS actor interactions remain reachable in N64 mode.
 if(p->mCharacter==3 && ((pressed&KEY_A) || p->mObjInMouth || p->mHeldObj)){
  if(!ds_action){if(p->mIsAirborne)set_ds_air_state(p);else p->ChangeState(Player::ST_WAIT);ds_action=true;}
  resync=true;status="DS tongue / egg action";return false;
 }
 if(ds_action){if(!ordinary(p)){resync=true;return false;}ds_action=false;}
 if(!ordinary(p)){resync=true;status="DS world interaction";return false;}
 if(resync)reset_core(p);if(mario<0)return false;
 // DS water/terrain sensing remains separate from its position solver.
 // The N64 controller owns collision response while it owns movement.
 sm64_set_mario_water_level(mario,p->unk_650==int(0x80000000)?-10000:p->unk_650/4096);
 const bool wario=p->mCharacter==2;
 motion64_profile(wario?float(data_ov002_020ff170[2])/data_ov002_020ff170[0]:1.f,
                  wario?float(data_ov002_020ff140[2])/data_ov002_020ff140[0]:1.f);
 circlePosition pad{};hidCircleRead(&pad);
 int x=pad.dx,y=pad.dy;
 if(keys&KEY_DLEFT)x=-156;if(keys&KEY_DRIGHT)x=156;if(keys&KEY_DUP)y=156;if(keys&KEY_DDOWN)y=-156;
 if(presentation_replay() && ((replay_tick-1)%240)>=20 && ((replay_tick-1)%240)<90)y=80;
 extern short camera_yaw(unsigned char) asm("GetAngleToCamera");
 const short camera=presentation_replay()?0:camera_yaw(0);
 pending_count=0;sound_player=p;
 const bool advanced=world_session.tick_circle(mario,x,y,camera,keys&KEY_B,keys&KEY_A,keys&(KEY_L|KEY_ZL),state);
 sound_player=nullptr;
 if(!advanced){pending_count=0;native_fault("native simulation step");return false;}
 ++trace_tick;
 if(trace_enabled()){
  unsigned timer,phase;motion64_detail(&timer,&phase);
  std::printf("MOVEMENT_TRACE TICK %u %d %d %d %u %u %u %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %lu %u %u %ld %d\n",trace_tick,x,y,int(camera),unsigned(bool(keys&KEY_B)),unsigned(bool(keys&KEY_A)),unsigned(bool(keys&(KEY_L|KEY_ZL))),p->unk_650==int(0x80000000)?-10000:p->unk_650/4096,state.position[0],state.position[1],state.position[2],state.velocity[0],state.velocity[1],state.velocity[2],state.forwardVelocity,state.faceAngle,(unsigned long)state.action,timer,phase,(long)state.animID,int(state.animFrame));
 }
 owned=true;commit(p);
 dispatch_sounds(p);
 animate(p);
 if(replay_enabled() && replay_tick==960){
  movement_lab_finish_audio();
  std::printf("MOVEMENT_LAB REPLAY complete steps=%u resets=%u voices=%u terrain=%u footsteps=%u\n",steps+1,core_resets,voices,terrain_sounds,footsteps);
 }
 status=wario?"N64 + original Wario ratios":"N64 controller active";
 if((++steps%120)==0)std::printf("MOVEMENT_LAB step=%u mode=%s char=%u pos=%.3f,%.3f,%.3f vel=%.3f action=%08lx ratios=%.5f,%.5f\n",steps,mode_names[unsigned(mode)],unsigned(p->mCharacter),state.position[0],state.position[1],state.position[2],state.forwardVelocity,(unsigned long)state.action,wario?float(data_ov002_020ff170[2])/data_ov002_020ff170[0]:1.f,wario?float(data_ov002_020ff140[2])/data_ov002_020ff140[0]:1.f);
 return true;
}
bool step(Player* p){
 const bool result=step_impl(p);
 if(trace_enabled()) {
  static unsigned serial=0;static int last_mode=-1,last_char=-1,last_owner=-1;static const char* last_status=nullptr;
  ++serial;
  if(last_mode!=int(mode)||last_char!=int(p->mCharacter)||last_owner!=int(result)||last_status!=status||serial%30==0)
   std::printf("MOVEMENT_OWNERSHIP update=%u mode=%u char=%u native=%u keys=%08lx air=%u pos=%.6f,%.6f,%.6f vy=%.6f state=%08lx reason=%s\n",serial,unsigned(mode),unsigned(p->mCharacter),unsigned(result),(unsigned long)keys,unsigned(p->mIsAirborne),unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ),unfixed(p->mVertSpeed),(unsigned long)(uintptr_t)p->mState,status);
  last_mode=int(mode);last_char=p->mCharacter;last_owner=int(result);last_status=status;
 }
 if(!result)release_core();
 return result;
}
void commit(Player* p){if(!owned)return;
 p->mPosX=fixed(state.position[0]);p->mPosY=fixed(state.position[1]);p->mPosZ=fixed(state.position[2]);
 p->mHorzSpeed=fixed(state.forwardVelocity);p->mVertSpeed=fixed(state.velocity[1]);
 p->mAngleY=short(state.faceAngle*32768.f/3.14159265358979323846f);p->mPrevAngleY=p->mAngleY;
 p->mIsAirborne=(state.action&0x800)!=0;p->mIsInAirState=p->mIsAirborne;
 if(trace_enabled())std::printf("MOVEMENT_TRACE DS %u %.9g %.9g %.9g %.9g %.9g %d\n",trace_tick,unfixed(p->mPosX),unfixed(p->mPosY),unfixed(p->mPosZ),unfixed(p->mHorzSpeed),unfixed(p->mVertSpeed),int(p->mAngleY));
}
bool render(uint32_t* px){
 static bool painted=false;
 static Mode last_mode=Mode::Original;
 static unsigned last_character=0;
 static const char* last_status=nullptr;
 if(!px || (painted && mode==last_mode && character==last_character && status==last_status))return false;
 std::fill(px,px+320*240,0xff182332);
 text(px,54,12,"MOVEMENT LAB");text(px,10,30,"Control Mode");
 for(unsigned i=0;i<3;++i){box(px,8+i*104,47,98,44,unsigned(mode)==i?0xff407ba7:0xff344354);text(px,17+i*104,60,mode_names[i]);}
 text(px,10,101,"Character");
 for(unsigned i=0;i<4;++i){box(px,8+i*76,120,70,46,character==i?0xff407ba7:0xff344354);text(px,12+i*76,134,character_names[i]);}
 text(px,10,177,"B: jump / hold   A: attack");text(px,10,195,"L: crouch   Circle Pad: move");text(px,10,216,status);
 last_mode=mode;last_character=character;last_status=status;painted=true;
 return true;
}
}
extern "C" bool movement_lab_owns_position(){return movement_lab::owned;}
