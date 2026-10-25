#include "native_benchmark.h"
#include "enhance_settings.h"
#include "Player.h"
#include "Stage.h"
#include "SaveData.h"
#include <cstdio>
#include <cstring>
extern "C" {
void StartFile(signed char level,unsigned char entrance);
void _ZN8dScene_c14StartSceneFadeEjjt(unsigned scene,unsigned arg,unsigned short color);
void _ZN6Player16SetRealCharacterEj(void*,unsigned int);
extern Player* data_0209f394[];
extern unsigned char data_0209f2f8;
extern unsigned char data_0209d660;
extern short data_0209d6d4;
extern unsigned char data_0209d6bc;
extern int data_0209fc48;
extern unsigned char data_0209f2c4;
}
namespace native_benchmark {
static Model model;
static bool checked=false;
static Profile profile;
static unsigned last_updates=0;
static unsigned last_wait_log=0;
static uint64_t dialogue_since=0;
static bool interactive=false;
static unsigned observed_body_character=255;
static Observation last;
static void log(const char* event,uint64_t ticks){
 const auto& s=scenarios[profile.scenario];
 std::printf("BENCHMARK event=%s scenario=%s model=%s scene=%u level=%u expectedLevel=%u entrance=%u star=%u character=%u expectedCharacter=%u player=%u bodyModel=%u area=%d updates=%u ticks=%llu sampledTicks=%llu dumpsCompleted=%u state=%u reason=%s provenance=original_StartFile_SetRealCharacter_no_RAM_patches\n",
 event,s.name,profile.new3ds?"new":"old",last.scene,unsigned(last.level),unsigned(s.level),unsigned(s.entrance),unsigned(s.star),unsigned(last.character),unsigned(s.character),unsigned(last.player),unsigned(last.model),last.player?int(data_0209f394[0]->mAreaId):-1,last_updates,(unsigned long long)ticks,(unsigned long long)model.elapsed(ticks),model.completed(),unsigned(model.state()),model.reason);
 std::printf("BENCHMARK modelIdentity logicalCharacter=%u renderBodyCharacter=%u renderBodyMatches=%u\n",unsigned(last.character),observed_body_character,unsigned(last.model));
}
bool frame(unsigned scene,unsigned updates,bool diagnostics_enabled,bool actual_new,uint64_t ticks){
 if(!diagnostics_enabled){
  if(model.state()!=State::Disabled && model.state()!=State::Complete && model.state()!=State::Aborted){model.abort("diagnostics-disabled");log("aborted",ticks);}
  return false;
 }
 if(!checked){
  checked=true;
  FILE* f=std::fopen(marker_path,"rb");if(!f)return false;
  char text[202]{};const size_t n=std::fread(text,1,sizeof(text),f);
  const bool read_ok=!std::ferror(f);const bool close_ok=std::fclose(f)==0;
  if(!read_ok || !close_ok || !parse(text,n,profile) || !model.enable(profile,true,actual_new)){
   std::puts("BENCHMARK rejected marker: malformed or model mismatch; no game API called");return false;
  }
  std::printf("BENCHMARK enabled scenario=%s model=%s marker=%s productionDefault=noop\n",scenarios[profile.scenario].name,actual_new?"new":"old",marker_path);
  // A bounded manual session needs time for accessibility attachment and
  // real multi-page dialogue input. Unattended runs keep the30s watchdog.
  if(FILE* ui=std::fopen("sdmc:/3ds/sm64ds/benchmark-interactive.enabled","rb")){
   char marker[64]{};const size_t bytes=std::fread(marker,1,sizeof(marker),ui);
   const bool good=!std::ferror(ui);const bool closed=std::fclose(ui)==0;
   constexpr char expected[]="SM64DS_BENCHMARK_INTERACTIVE_V1\n";
   interactive=good && closed && bytes==sizeof(expected)-1 && !std::memcmp(marker,expected,bytes);
  }
  if(interactive)std::puts("BENCHMARK manual dialogue watchdog=300s real_HID_required no_auto_dismissal");
 }
 if(model.state()==State::Disabled || model.state()==State::Complete || model.state()==State::Aborted)return false;
 if(!diagnostics_enabled)return false;
 {  // star_select: the star select keeps star 1 (StartFile chose it); after
    // five seconds the course starts as a confirmed star would.
  static unsigned select_since=0;static bool confirmed=false;
  if(model.scenario().star_select && scene==4 && !confirmed){
   if(!select_since)select_since=updates;
   else if(updates-select_since>=300){confirmed=true;log("star-select-confirmed",ticks);_ZN8dScene_c14StartSceneFadeEjjt(3,0,0x7fff);}
  }
 }
 last_updates=updates;
 Player* p=scene==3?data_0209f394[0]:nullptr;
 bool body=false;
 const bool control=p && !data_0209d660 && !data_0209fc48 && !data_0209f2c4 && !Stage::IsPauseDisabled() &&
  !enhance::live().overlay_open.load(std::memory_order_relaxed);
 // A cutscene can disable control while retaining a perfectly valid model.
 // Observe those independently so a control transition is not misreported as
 // a missing model (and the model can suspend the measurement clock).
 observed_body_character=p?unsigned(p->mBodyModelId):255u;
 if(p){
  // Render() selects mBodyModelId, which can still be the old character
  // during the original transformation. A loaded target and updated HUD
  // alone do not establish the model actually being drawn.
  const unsigned id=p->GetBodyModelID(p->mBodyModelId,true);
  body=p->mBodyModelId==p->mCharacter && id<4 && p->mBodyModels[id]!=nullptr;
 }
 last={scene,updates,data_0209f2f8,p?p->mCharacter:uint8_t(255),p!=nullptr,control,body,ticks};
 if(scene==3 && p && data_0209d660 && !control){
  if(!dialogue_since)dialogue_since=ticks;
  if(ticks-dialogue_since>=(interactive?300u:30u)*tick_hz){
   // Do not auto-dismiss dialogue or fake HID input while the UI is unavailable.
   // Preserve one full diagnostic instead of waiting/looping for ten minutes.
   model.abort("interactive-dialogue-needs-input");
   std::printf("BENCHMARK blockedMessage=%d messagePhase=%u waitingTicks=%llu\n",int(data_0209d6d4),unsigned(data_0209d6bc),(unsigned long long)(ticks-dialogue_since));
   log("aborted-diagnostic-capture-not-campaign",ticks);return true;
  }
 }else dialogue_since=0;
 const State before=model.state();const Action action=model.observe(last);
 if(action==Action::StartFile){
  // The retail menu only starts a NEW file in the castle. Entering a course
  // with a virgin intro flag instead runs the castle opening script there;
  // its Lakitu resources belong to overlay85, not that course's overlay.
  // This private diagnostic fixture marks ONLY the opening as seen through
  // the original API. It unlocks no characters/stars and never writes a save.
  SaveData::SetCharacterIntro(7);
  std::puts("BENCHMARK fixture=original_SaveData_SetCharacterIntro7 opening_seen_only character_unlocks_unchanged no_save_write");
  log("request-original-StartFile",ticks);
  StartFile(static_cast<signed char>(model.scenario().level),model.scenario().entrance);
  if(model.scenario().star_select)_ZN8dScene_c14StartSceneFadeEjjt(4,0,0x7fff); // star select instead of the course
 }else if(action==Action::ChangeCharacter){
  log("request-original-SetRealCharacter",ticks);
  _ZN6Player16SetRealCharacterEj(p,model.scenario().character);
 }else if(action==Action::Dump){log("request-full-RAM-dump",ticks);return true;}
 else if(action==Action::Abort){log("aborted-diagnostic-capture-not-campaign",ticks);return true;}
 else if(before!=model.state())log(model.state()==State::Measure?"measurement-start":"player-model-validated-settling",ticks);
 else if((model.state()==State::WaitStage || model.state()==State::WaitCharacter || model.state()==State::Settle) && updates-last_wait_log>=300){
  last_wait_log=updates;log("waiting-for-stable-control",ticks);
  std::printf("BENCHMARK control=%u globalControlDisabled=%u cutscene=%d pauseDisabled=%d\n",unsigned(control),unsigned(data_0209d660),int(data_0209fc48),p?Stage::IsPauseDisabled():-1);
 }
 return false;
}
void capture_completed(bool success,uint64_t ticks){
 if(model.state()!=State::Capture)return;
 model.captured(success,ticks);
 log(model.state()==State::Complete?"complete-three-dumps":success?"dump-completed-clock-resumed":"capture-failed-aborted",ticks);
}
void external_capture_completed(uint64_t begin,uint64_t end){model.exclude_external_capture(begin,end);}
}
