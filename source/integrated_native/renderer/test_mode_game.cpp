// Game-state side of the SELECT+START test mode. The decomp headers define
// their own integer types, incompatible with libctru's, so this file never
// includes <3ds.h>; test_mode.cpp owns everything platform-specific.
#if defined(SM64DS_FRESH_NATIVE_GAME)
#include "test_mode.h"
#include "Player.h"
#include "Stage.h"
#include "LVL_Overlay.h"
#include "SaveData.h"
#include "dActor_c.h"
#include "dBgCh_Gnd.h"
#include <cstring>
extern "C" {
void StartFile(signed char level,unsigned char entrance);
extern const unsigned char* data_02092208[52];
void _ZN6Player16SetRealCharacterEj(void*,unsigned int);
extern Player* data_0209f394[];
extern unsigned char data_0209f2f8;   // current level
extern unsigned char data_0209d660;   // message/global control lock
extern int data_0209fc48;             // cutscene
extern unsigned char data_0209f2c4;   // pause
}
extern "C" void sm64ds_test_game_observe(unsigned scene,Sm64dsTestPlayer* out){
    *out={};out->level=data_0209f2f8;out->dialogue=data_0209d660!=0;out->paused=data_0209f2c4!=0;
    Player* p=scene==3?data_0209f394[0]:nullptr;
    if(!p)return;
    out->present=1;out->character=p->mCharacter;out->area=p->mAreaId;
    out->x=p->mPosX;out->y=p->mPosY;out->z=p->mPosZ;out->angle=p->mAngleY;
    // Render() selects mBodyModelId, which can still be the old character
    // during the original transformation (same check as native_benchmark).
    const unsigned id=p->GetBodyModelID(p->mBodyModelId,true);
    out->body=p->mBodyModelId==p->mCharacter&&id<4&&p->mBodyModels[id]!=nullptr;
    out->control=!data_0209d660&&!data_0209fc48&&!data_0209f2c4;
    out->pause_locked=Stage::IsPauseDisabled()!=0;
}
extern "C" int sm64ds_test_game_teleport(unsigned scene,int area,std::int32_t x,std::int32_t y,std::int32_t z,int angle){
    Player* p=scene==3?data_0209f394[0]:nullptr;
    // Same area only: another area can still be hidden or unloaded.
    if(!p||(area>=0&&p->mAreaId!=area))return 0;
    p->mPosX=p->mPrevPosX=x;p->mPosY=p->mPrevPosY=y;p->mPosZ=p->mPrevPosZ=z;
    p->mAngleY=p->mPrevAngleY=static_cast<short>(angle);p->mHorzSpeed=0;p->mVertSpeed=0;
    return 1;
}
// StartFile chooses the visible areas from the retail entrance. Enter the
// checkpoint's area before restoring a position beyond an internal door.
static unsigned char checkpoint_entrance(int level,int area){
    if(level<0||level>=52||area<=0||area>7)return 0;
    const auto* root=data_02092208[level];
    if(!root)return 0;
    const LVL_Overlay::ObjTable* table=nullptr;
    std::memcpy(&table,root+4,sizeof table);
    if(!table||!table->entries)return 0;
    const auto* groups=static_cast<const LVL_Overlay::ObjSubTable*>(table->entries);
    for(unsigned i=0;i<table->count;++i){
        if(groups[i].unk_000!=1||!groups[i].entries)continue;
        const auto* entrances=static_cast<const LVL_Overlay::StandardEntry*>(groups[i].entries);
        for(unsigned j=0;j<groups[i].count;++j)
            if((entrances[j].param&7)==area)return static_cast<unsigned char>(j);
    }
    return 0;
}
extern "C" void sm64ds_test_game_start_file(int level,int area){
    // As the verified benchmark: mark only the opening as seen, no save write.
    SaveData::SetCharacterIntro(7);
    StartFile(static_cast<signed char>(level),checkpoint_entrance(level,area));
}
extern "C" unsigned sm64ds_test_game_actors(Sm64dsTestActor* out,unsigned capacity){
    unsigned total=0;
    // The game's own list walk (dActor_c::Next); bounded against a damaged list.
    for(dActor_c* a=dActor_c::Next(nullptr);a&&total<4096;a=dActor_c::Next(a),++total){
        if(!out||total>=capacity)continue;
        bool player=false;for(unsigned i=0;i<4;++i)player|=static_cast<dActor_c*>(data_0209f394[i])==a;
        out[total]={a->uniqueID,a->actorID,a->mAreaId,a->aliveState,std::uint8_t(player),a->mPosX,a->mPosY,a->mPosZ};
    }
    return total;
}
extern "C" int sm64ds_test_game_floor(std::int32_t x,std::int32_t y,std::int32_t z,std::int32_t* floor_y){
    static_assert(sizeof(Vector3)==12,"three 20.12 words");
    Vector3 v;const std::int32_t raw[3]={x,y,z};std::memcpy(&v,raw,sizeof raw);
    dBgCh_Gnd ground;ground.SetObjAndPos(v,nullptr);
    if(!ground.DetectClsn())return 0;
    std::int32_t hit;std::memcpy(&hit,&ground.clsnY,sizeof hit);*floor_y=hit;return 1;
}
extern "C" int sm64ds_test_game_player_state(unsigned scene,Sm64dsTestPlayerState* out){
    *out={};Player* p=scene==3?data_0209f394[0]:nullptr;if(!p)return 0;
    out->horizontal_speed=p->mHorzSpeed;out->vertical_speed=p->mVertSpeed;out->health=p->GetHealth();
    out->underwater=p->mIsUnderwater;out->shallow_water=p->mIsInShallowWater;out->in_air=p->mIsInAirState;out->floor_class=p->mFloorClass;
    out->state_flags=p->mStateFlags;out->cap_flags=p->mCapFlags;out->powerup_timer=p->mPowerupTimer;
    out->state=reinterpret_cast<std::uintptr_t>(p->mState);
    return 1;
}
extern "C" void sm64ds_test_game_set_character(int character){
    if(Player* p=data_0209f394[0])_ZN6Player16SetRealCharacterEj(p,unsigned(character));
}
#endif
