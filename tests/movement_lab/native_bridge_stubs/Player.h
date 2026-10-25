#pragma once
#include "ModelAnim.h"
struct Player {
 struct State {int (Player::*mMain)();};
 static State ST_WAIT;
 unsigned uniqueID=1,param1=0,mCharacter=0,mPlayerNo=0,mCharFileBase=0;
 int mPosX=0,mPosY=0,mPosZ=0,mHorzSpeed=0,mVertSpeed=0;
 short mAngleY=0,mPrevAngleY=0;
 int mIsAirborne=0,mIsInAirState=0,mJumpComboStage=0;
 int mIsControlDisabled=0,mIsNoControl=0,mPowerupTimer=0,mBalloonTimer=0;
 int mIsBalloon=0,mIsMetal=0,mIsVanish=0,mHasWings=0,mIsMega=0;
 int mIsUnderwater=0,mIsTakingDamage=0,mIsInShallowWater=0;
 int mLandSoundPlayed=0,mGroundSoundType=0,mFloorClass=0,mSurfaceType=0;
 int mCamSpacePosX=0,unk_650=int(0x80000000);
 void* mHeldObj=nullptr;void* mObjInMouth=nullptr;
 ModelAnim* mBodyModels[4]{};
 State* mState=&ST_WAIT;
 bool veto=false;
 void func_ov002_020bf88c();
 int ChangeState(State& s){if(veto)return 0;mState=&s;mHorzSpeed=0;mVertSpeed=0;return 1;}
 int St_Wait_Main(){return 1;} int St_Walk_Main(){return 1;}
 int St_Jump_Main(){return 1;} int St_Fall_Main(){return 1;}
 int St_Land_Main(){return 1;} int St_Crouch_Main(){return 1;}
 int St_Crawl_Main(){return 1;} int St_TurnAround_Main(){return 1;}
 int St_LongJump_Main(){return 1;} int St_WallJump_Main(){return 1;}
 int St_SideFlip_Main(){return 1;} int special(){return 1;}
};
