// Compile the production bridge and real imported core. Only the DS actor,
// animation/audio/particle services, HID and contact queries are host stand-ins.
#include "../../source/integrated_native/movement/native_motion.cpp"
#include <cassert>

Player::State Player::ST_WAIT{&Player::St_Wait_Main};
circlePosition test_pad{};
static unsigned dust=0,second_prism_reads=0;
static int body=0;
extern "C" {
Player* data_0209f394[4]{};
unsigned char data_0209f2f8=1,data_0209d660=0,data_020a0e40=0;
unsigned char data_0209f49c[4*0x18]{},data_0209f49e[4*0x18]{};
int data_0209fc48=0;
unsigned short data_ov002_020ff170[]={100,100,80},data_ov002_020ff140[]={100,100,80};
Player::State data_ov002_0211019c{&Player::St_Jump_Main};
Player::State data_ov002_021101e4{&Player::St_Fall_Main};
void _ZN6Player7SetAnimEji5Fix12IiEj(void* actor,unsigned clip,int flags,int rate,unsigned){
 auto* p=static_cast<Player*>(actor);auto* a=p->mBodyModels[body];
 if(a->clip!=clip||!a->file){a->currFrame=0;a->clip=clip;a->file=a;}
 a->numFramesAndFlags=32|unsigned(flags);a->speed=rate;p->mCharFileBase=clip*4;
}
void Player_AdvanceAnims(void* actor){
 auto* a=static_cast<Player*>(actor)->mBodyModels[body];
 a->currFrame+=a->speed;
 if(a->currFrame>=32*4096)a->currFrame=a->numFramesAndFlags&0x40000000?32*4096:a->currFrame%(32*4096);
}
int _ZNK6Player14GetBodyModelIDEjb(void*,unsigned,int){return body;}
int _ZNK9Animation12WillHitFrameEi(void* anim,int frame){
 auto* a=static_cast<Animation*>(anim);int before=a->currFrame,next=before+a->speed;
 return next<32*4096?before<=frame*4096&&frame*4096<next:
  before<=frame*4096||frame*4096<next%(32*4096);
}
void func_ov002_020e25f0(void*,int){}
void func_ov002_020c2f64(void*){}
void func_ov002_020d4c30(void*){}
void _ZN5Sound13PlayCharVoiceEjjRK7Vector3(unsigned,unsigned,const void*){}
void _ZN5Sound9PlayBank0EjRK7Vector3(unsigned,const void*){}
}
void Player::func_ov002_020bf88c(){if(!mIsInShallowWater)++dust;}
short test_camera(unsigned char) asm("GetAngleToCamera");
short test_camera(unsigned char){return 0;}

static DsWorldGroundHit ground(){
 DsWorldGroundHit hit{};hit.normal[1]=4096;hit.collider=0x1000;hit.owner_id=-1;hit.triangle=1;return hit;
}
extern "C" int sm64ds_world_prism_count(unsigned slot){return slot?0:2;}
extern "C" int sm64ds_contact_prism(const DsWorldGroundHit*,DsWorldPrism* prism){
 *prism={};prism->extent=100*65536;
 const int16_t normals[4][3]={{0,1024,0},{1024,0,0},{0,0,1024},{1024,0,-1024}};
 std::memcpy(prism->normal,normals,sizeof normals);prism->kcl=0x2000;prism->scale_y=4096;
 prism->transform[0]=prism->transform[4]=prism->transform[8]=4096;return 1;
}
extern "C" int sm64ds_world_prism_at(unsigned slot,unsigned index,DsWorldGroundHit* hit,DsWorldPrism* prism){
 if(slot||!index||index>2)return 0;
 if(index==2)++second_prism_reads;
 *hit=ground();hit->triangle=uint16_t(index);return sm64ds_contact_prism(hit,prism);
}
extern "C" int sm64ds_motion_ground(const int32_t* pos,void*,DsWorldGroundHit* out){
 if(pos[1]+78<0)return 0;*out=ground();return 1;
}
extern "C" int sm64ds_motion_ceiling(const int32_t*,void*,DsWorldGroundHit*){return 0;}
extern "C" int sm64ds_contact_pass_through(const DsWorldGroundHit*,void*,uint8_t,uint8_t){return 0;}
extern "C" int sm64ds_platform_displace(const DsWorldPlatformRef*,const int32_t*,int32_t*){return 0;}

static unsigned ticks=0,core_ticks=0,ds_ticks=0;
static uint32_t raw_keys=0;
static bool tick(Player& p,unsigned held=0,unsigned down=0){
 const uint16_t h=uint16_t(held),d=uint16_t(down);
 std::memcpy(data_0209f49c,&h,2);std::memcpy(data_0209f49e,&d,2);
 native_motion::input(raw_keys,0);native_motion::frame(3,1);++ticks;
 const bool result=native_motion::step(&p);native_motion::commit(&p);
 if(result)++core_ticks;else ++ds_ticks;return result;
}
static void land(Player& p){
 for(unsigned i=0;i<120;++i){assert(tick(p));if(!p.mIsAirborne)return;}
 assert(false&&"jump never landed");
}
static int actors(const Motion64Contacts*,void*){return 1;}
static void start(Player& p){
 native_motion::frame(1,1);p.mState=&Player::ST_WAIT;p.mIsAirborne=0;
 p.mPosX=p.mPosY=p.mPosZ=p.mHorzSpeed=p.mVertSpeed=0;
 enhance::live().physics_mode=2;enhance::live().controller_mode=0;
 test_pad={0,156};for(int i=0;i<40;++i)assert(tick(p));
 assert(p.mHorzSpeed>20*4096);
}
static void double_land(Player& p){
 assert(tick(p,2,2));assert(native_motion::state.action==0x03000880);
 for(int i=0;i<6;++i)assert(tick(p,2));land(p);
 assert(tick(p,2,2));assert(native_motion::state.action==0x03000881);
 for(int i=0;i<6;++i)assert(tick(p,2));land(p);
}
int main(){
 Player p;ModelAnim model;p.mBodyModels[0]=&model;
 start(p);double_land(p);
 const auto resets=native_motion::core_resets;
 enhance::live().physics_mode=1;enhance::live().controller_mode=1;
 assert(tick(p,2,2));
 assert(native_motion::core_resets==resets);
 assert(native_motion::state.action==0x01000882); // real triple, with original timer/prevAction
 std::puts("bridge: jump -> double -> triple survives Hybrid/N64 and button-table changes without recreation");

 // Positive causal control: recreating at exactly the same landing loses the
 // chain, even though DS position/velocity/held history are restored.
 start(p);double_land(p);native_motion::reset_core(&p);
 assert(tick(p,2,2));assert(native_motion::state.action==0x03000880);
 std::puts("bridge: recreating at double landing reproduces the lost chain in the real core");

 start(p);const auto reads=second_prism_reads;
 p.mIsControlDisabled=1;assert(!tick(p,2));p.mIsControlDisabled=0;
 p.mPosY=0;p.mVertSpeed=0;p.mIsAirborne=0;
 assert(tick(p,2));assert(!p.mIsAirborne); // held through DS does not become a new press
 assert(second_prism_reads==reads); // static prisms survive ordinary handoffs
 assert(tick(p));assert(tick(p,2,2));assert(p.mIsAirborne);
 const int x=p.mPosX,y=p.mPosY,v=p.mVertSpeed;p.veto=true;
 assert(!native_motion::set_ds_air_state(&p));
 assert(!native_motion::set_ds_air_state(&p,data_ov002_021101e4));
 assert(p.mPosX==x&&p.mPosY==y&&p.mVertSpeed==v);p.veto=false;
 std::puts("bridge: DS held history, fresh press, vetoed handoff and static-scene reuse passed");
 start(p);enhance::live().developer_dumps=true;raw_keys=KEY_L|KEY_R|KEY_A;
 assert(native_motion::input(raw_keys,raw_keys)==0); // physical chord removed before DS mapping
 assert(tick(p,2,2));assert(p.mIsAirborne); // Standard's other physical jump button survives
 for(int i=0;i<120&&p.mIsAirborne;++i)assert(tick(p,2));
 assert(!p.mIsAirborne);
 assert(tick(p,2));assert(!p.mIsAirborne); // continued hold does not create another press
 raw_keys=0;enhance::live().developer_dumps=false;
 std::puts("bridge: dump chord leaves unrelated logical jump and held history intact");

 // Interactions after the native tick must retain their DS pose, even if
 // another actor or a new lifetime occupies the same address next update.
 Player::State special{&Player::special};p.mState=&special;
 assert(!native_motion_owns_position()); // DS collision dispatch sees the transition before final commit
 p.mPosX=12345;p.mVertSpeed=-6789;native_motion::commit(&p);
 assert(p.mPosX==12345&&p.mVertSpeed==-6789&&!native_motion_owns_position());
 assert(motion64_visit_contacts(actors,nullptr)==0);
 start(p);p.mIsControlDisabled=1;p.mPosX=9876;p.mVertSpeed=-4321;
 native_motion::commit(&p); // no intervening owns-position query
 assert(p.mPosX==9876&&p.mVertSpeed==-4321&&!native_motion_owns_position());p.mIsControlDisabled=0;
 start(p);const auto before_lifetime=native_motion::core_resets;
 ++p.uniqueID;assert(tick(p));assert(native_motion::core_resets==before_lifetime+1);
 p.mIsUnderwater=1;assert(!tick(p));p.mIsUnderwater=0;
 assert(motion64_visit_contacts(actors,nullptr)==0);
 assert(tick(p));p.mIsTakingDamage=1;assert(!tick(p));p.mIsTakingDamage=0;
 assert(motion64_visit_contacts(actors,nullptr)==0);
 assert(tick(p));p.mIsAirborne=1;p.mIsBalloon=1;
 const auto ds_state=p.mState;assert(!tick(p));assert(p.mState==ds_state);p.mIsBalloon=0;
 std::puts("bridge: late DS transition, actor lifetime, water and damage ownership passed");

 start(p);const auto before_dust=dust;test_pad={0,-156};
 bool requested_dust=false;
 for(int i=0;i<20;++i){assert(tick(p));requested_dust|=(native_motion::state.particleFlags&1)!=0;}
 assert(requested_dust&&dust>before_dust);
 test_pad={0,156};for(int i=0;i<40;++i)assert(tick(p));
 p.mIsInShallowWater=1;test_pad={0,-156};const auto water_dust=dust;
 for(int i=0;i<20;++i)assert(tick(p));assert(dust==water_dust);p.mIsInShallowWater=0;
 body=4;assert(tick(p));body=0; // transient invalid body never dereferences a missing model
 std::puts("bridge: real solver skid dust reaches DS particle service; unavailable body is tolerated");

 start(p);test_pad={0,0};bool slept=false;
 const auto idle_resets=native_motion::core_resets;
 for(unsigned i=0;i<20000;++i){assert(tick(p));slept|=native_motion::state.action==0x0c000203;}
 assert(slept);test_pad={0,156};bool woke=false;
 for(int i=0;i<90;++i){assert(tick(p));woke|=native_motion::state.action==0x0c000204;}
 assert(woke&&p.mHorzSpeed>20*4096&&native_motion::core_resets==idle_resets);
 assert(tick(p,2,2));assert(p.mVertSpeed>0);
 native_motion::frame(1,1);assert(!native_motion_owns_position());
 assert(motion64_visit_contacts(actors,nullptr)==0);
 std::printf("bridge: %u updates (%u successful real-core ticks, %u DS handoffs), 20000 idle ticks through sleep, wake, walk and jump; no emulator/hardware acceptance\n",ticks,core_ticks,ds_ticks);
}
