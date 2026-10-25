#include "../../source/integrated_native/options/native_benchmark.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace native_benchmark;
int main(){
 unsigned checks=0;
 for(unsigned scenario=0;scenario<scenario_count;++scenario)for(bool newer:{false,true}){
  Profile p;const auto text=std::string("SM64DS_BENCHMARK_V1\nscenario=")+scenarios[scenario].name+"\nmodel="+(newer?"new":"old")+"\n";
  assert(parse(text.data(),text.size(),p)&&p.scenario==scenario&&p.new3ds==newer);++checks;
  std::string crlf;for(char c:text){if(c=='\n')crlf+='\r';crlf+=c;}
  assert(parse(crlf.data(),crlf.size(),p));++checks;
  Model off;assert(!off.enable(p,false,newer));++checks;
  Model wrong;assert(!wrong.enable(p,true,!newer));++checks;
  Model m;assert(m.enable(p,true,newer));++checks;
  Observation o;o.scene=1;o.updates=899;o.ticks=tick_hz;
  assert(m.observe(o)==Action::None);++checks;
  o.updates=900;assert(m.observe(o)==Action::StartFile);++checks;
  for(unsigned i=0;i<100;++i){assert(m.observe(o)==Action::None);++checks;}
  o.scene=3;o.level=scenarios[scenario].level;o.player=true;o.control=false;
  assert(m.observe(o)==Action::None);++checks;
  o.control=true;o.character=(scenarios[scenario].character+1)%4;o.model=true;
  assert(m.observe(o)==Action::ChangeCharacter);++checks;
  for(unsigned i=0;i<50;++i){assert(m.observe(o)==Action::None);++checks;}
  o.character=scenarios[scenario].character;o.model=false;
  assert(m.observe(o)==Action::None);++checks;
  o.model=true;assert(m.observe(o)==Action::None &&m.state()==State::Settle);++checks;
  o.updates+=119;assert(m.observe(o)==Action::None &&m.state()==State::Settle);++checks;
  ++o.updates;o.ticks=10*tick_hz;assert(m.observe(o)==Action::None&&m.state()==State::Measure);++checks;
  const unsigned moments[]={10,30,60};uint64_t excluded=0;
  m.exclude_external_capture(11*tick_hz,13*tick_hz);excluded+=2*tick_hz;
  assert(m.elapsed(15*tick_hz)==3*tick_hz);++checks;
  for(unsigned dump=0;dump<3;++dump){
   o.ticks=(10+moments[dump])*tick_hz+excluded-1;
   assert(m.observe(o)==Action::None);++checks;
   ++o.ticks;assert(m.observe(o)==Action::Dump);++checks;
   const uint64_t requested=o.ticks;
   o.ticks+=100*tick_hz;assert(m.observe(o)==Action::None);++checks;
   assert(m.completed()==dump);++checks;
   assert(m.captured(true,o.ticks));++checks;
   excluded+=o.ticks-requested;
   assert(m.completed()==dump+1);++checks;
   assert(!m.captured(true,o.ticks));++checks;
  }
  assert(m.state()==State::Complete);++checks;
  assert(m.observe(o)==Action::None);++checks;
 }
 Profile unchanged{4,true};
 for(const std::string s:{"", "SM64DS_BENCHMARK_V1\nscenario=bad\nmodel=old\n", "SM64DS_BENCHMARK_V1\nscenario=bob-mario\nmodel=NEW\n", "SM64DS_BENCHMARK_V1\nscenario=bob-mario\nmodel=old\nextra=1\n", "SM64DS_BENCHMARK_V1\nmodel=old\nscenario=bob-mario\n", "SM64DS_BENCHMARK_V1\nscenario=bob-mario\nmodel=old\n\n", "SM64DS_BENCHMARK_V1\rscenario=bob-mario\nmodel=old"}){
  assert(!parse(s.data(),s.size(),unchanged));assert(unchanged.scenario==4&&unchanged.new3ds);checks+=2;
 }
 std::string embedded="SM64DS_BENCHMARK_V1\nscenario=bob-mario\nmodel=old\n";embedded[3]=0;
 assert(!parse(embedded.data(),embedded.size(),unchanged));++checks;
 assert(!parse(nullptr,50,unchanged));++checks;
 Model failed;assert(failed.enable({0,false},true,false));++checks;
 Observation o{1,900,255,255,false,false,false,0};assert(failed.observe(o)==Action::StartFile);++checks;
 o={3,1000,1,3,true,true,true,0};failed.observe(o);o.updates+=120;failed.observe(o);
 o.ticks=10*tick_hz;assert(failed.observe(o)==Action::Dump);++checks;
 assert(!failed.captured(false,o.ticks+tick_hz)&&failed.completed()==0&&failed.state()==State::Aborted);++checks;
 // A real cutscene/control interruption does not imply a missing body model.
 // Preserve completed captures and exclude the interruption plus re-settling.
 Model interrupted;assert(interrupted.enable({0,false},true,false));++checks;
 o={1,900,255,255,false,false,false,0};interrupted.observe(o);
 o={3,1000,1,3,true,true,true,0};interrupted.observe(o);
 o.updates+=120;interrupted.observe(o);
 o.ticks=10*tick_hz;assert(interrupted.observe(o)==Action::Dump);++checks;
 assert(interrupted.captured(true,o.ticks));++checks;
 o.ticks=11*tick_hz;o.control=false;
 assert(interrupted.observe(o)==Action::None&&interrupted.state()==State::Settle);++checks;
 o.ticks=60*tick_hz;o.updates+=300;
 assert(interrupted.observe(o)==Action::None&&interrupted.completed()==1);++checks;
 o.control=true;o.updates+=119;
 assert(interrupted.observe(o)==Action::None&&interrupted.state()==State::Settle);++checks;
 ++o.updates;o.ticks=61*tick_hz;
 assert(interrupted.observe(o)==Action::None&&interrupted.state()==State::Measure);++checks;
 assert(interrupted.elapsed(o.ticks)==11*tick_hz&&interrupted.completed()==1);checks+=2;
 o.ticks=80*tick_hz-1;assert(interrupted.observe(o)==Action::None);++checks;
 ++o.ticks;assert(interrupted.observe(o)==Action::Dump);++checks;
 assert(interrupted.captured(true,o.ticks)&&interrupted.completed()==2);++checks;
 std::printf("Native benchmark strict parser/state model: %u checks, ten pairedprofiles/three successful captures each simulated; no game execution claim\n",checks);
}
