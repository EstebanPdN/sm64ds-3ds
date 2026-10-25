#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace native_benchmark {
constexpr uint64_t tick_hz=268111856;
constexpr const char* marker_path="sdmc:/3ds/sm64ds/benchmark-profile.txt";
// star_select: the file opens the course's star select (its preload is
// measured when a star is chosen) instead of entering the course.
struct Scenario {const char* name;uint8_t level,entrance,star,character;bool star_select=false;};
inline constexpr Scenario scenarios[]={
 {"castle-yoshi",1,0,1,3},{"bob-mario",6,0,1,0},{"whomp-luigi",7,0,1,1},
 {"bay-wario",8,0,1,2},{"clock-wario",27,0,1,2},{"bob-select",6,0,1,0,true}};
constexpr unsigned scenario_count=sizeof(scenarios)/sizeof(scenarios[0]);
struct Profile {unsigned scenario=0;bool new3ds=false;};
inline bool parse(const char* bytes,size_t size,Profile& result){
 if(!bytes || !size || size>200)return false;
 char lines[3][80]{};unsigned line=0,column=0;
 for(size_t i=0;i<size;++i){
  const unsigned char c=bytes[i];
  if(c=='\r'){if(i+1>=size || bytes[i+1]!='\n')return false;continue;}
  if(c=='\n'){if(!column || line>=3)return false;++line;column=0;continue;}
  if(c<32 || c>126 || line>=3 || column>=79)return false;
  lines[line][column++]=char(c);
 }
 if(column)++line;
 if(line!=3 || std::strcmp(lines[0],"SM64DS_BENCHMARK_V1"))return false;
 if(std::strncmp(lines[1],"scenario=",9) || std::strncmp(lines[2],"model=",6))return false;
 unsigned selected=scenario_count;
 for(unsigned i=0;i<scenario_count;++i)if(!std::strcmp(lines[1]+9,scenarios[i].name))selected=i;
 if(selected==scenario_count)return false;
 bool model=false;
 if(!std::strcmp(lines[2]+6,"new"))model=true;
 else if(std::strcmp(lines[2]+6,"old"))return false;
 result={selected,model};return true;
}
enum class State {Disabled,WaitTitle,WaitStage,WaitCharacter,Settle,Measure,Capture,Complete,Aborted};
enum class Action {None,StartFile,ChangeCharacter,Dump,Abort};
struct Observation {
 unsigned scene=0,updates=0;uint8_t level=255,character=255;
 bool player=false,control=false,model=false;uint64_t ticks=0;
};
class Model {
 Profile profile_{};State state_=State::Disabled;
 unsigned stable_update_=0,completed_=0;
 uint64_t started_=0,capture_started_=0,excluded_=0;
 bool suspended_=false;
public:
 const char* reason="none";
 bool enable(Profile p,bool diagnostics,bool actual_new){
  if(state_!=State::Disabled || !diagnostics || p.scenario>=scenario_count || p.new3ds!=actual_new)return false;
  profile_=p;state_=State::WaitTitle;return true;
 }
 State state()const{return state_;}
 unsigned completed()const{return completed_;}
 const Scenario& scenario()const{return scenarios[profile_.scenario];}
 uint64_t elapsed(uint64_t now)const{return now>=started_+excluded_?now-started_-excluded_:0;}
 Action abort(const char* why){reason=why;state_=State::Aborted;return Action::Abort;}
 Action observe(const Observation& o){
  if(state_==State::Disabled || state_==State::Complete || state_==State::Aborted || state_==State::Capture)return Action::None;
  const auto& target=scenario();
  if(state_==State::WaitTitle){
   if(o.scene==1 && o.updates>=900){state_=State::WaitStage;return Action::StartFile;}
   return Action::None;
  }
  const bool stage=o.scene==3 && o.level==target.level && o.player;
  if(state_==State::WaitStage){
   if(!stage || !o.control)return Action::None;
   state_=State::WaitCharacter;
   if(o.character!=target.character)return Action::ChangeCharacter;
  }
  if(state_==State::WaitCharacter){
   if(!stage || !o.control || o.character!=target.character || !o.model)return Action::None;
   stable_update_=o.updates;state_=State::Settle;return Action::None;
  }
  if(!stage || o.character!=target.character)return abort("stage-character-changed");
  if(state_==State::Settle){
   if(!o.control || !o.model){stable_update_=o.updates;return Action::None;}
   if(o.updates<stable_update_ || o.updates-stable_update_<120)return Action::None;
   if(suspended_){excluded_+=o.ticks-capture_started_;suspended_=false;}
   else{started_=o.ticks;excluded_=0;}
   state_=State::Measure;return Action::None;
  }
  if(!o.model)return abort("body-model-unavailable-during-measurement");
  if(!o.control){
   // Original cutscenes/dialogues may resume after the first controllable
   // frames. Exclude the interruption and a fresh settling period; never
   // capture them as steady gameplay or discard already completed captures.
   capture_started_=o.ticks;suspended_=true;stable_update_=o.updates;
   state_=State::Settle;return Action::None;
  }
  constexpr unsigned moments[]={10,30,60};
  if(elapsed(o.ticks)>=uint64_t(moments[completed_])*tick_hz){
   capture_started_=o.ticks;state_=State::Capture;return Action::Dump;
  }
  return Action::None;
 }
 bool captured(bool success,uint64_t now){
  if(state_!=State::Capture)return false;
  if(!success || now<capture_started_){abort("capture-failed");return false;}
  excluded_+=now-capture_started_;++completed_;
  state_=completed_==3?State::Complete:State::Measure;return true;
 }
 void exclude_external_capture(uint64_t begin,uint64_t end){
  if(state_==State::Measure && end>=begin)excluded_+=end-begin;
 }
};
// Game-thread only. Absent marker / disabled diagnostics / wrong model => no-op.
// True requests one full existing diagnostic capture; call capture_completed
// after its RAM+GPU work, so capture stalls never advance the sample clock.
bool frame(unsigned scene,unsigned updates,bool diagnostics_enabled,bool actual_new3ds,uint64_t now_ticks);
void capture_completed(bool success,uint64_t now_ticks);
void external_capture_completed(uint64_t begin_ticks,uint64_t end_ticks);
}
