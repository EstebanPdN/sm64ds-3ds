#include "intro_skip.hpp"
#include <cstdint>
extern "C" {
extern uintptr_t data_0209fc48;
void EndKuppaScript();
void StartFile(signed char,unsigned char);
void _ZN8SaveData17SetCharacterIntroEi(int);
}
namespace { enhance::IntroSkip opening; }
extern "C" void sm64ds_intro_skip_begin(){opening.begin();}
extern "C" bool sm64ds_intro_skip_hint(){return opening.active&&opening.hint;}
extern "C" bool sm64ds_intro_skip_input(bool held,bool pressed){
 if(!opening.active)return false;
 if(!data_0209fc48){opening.end();return false;}
 if(opening.update(held,pressed)){
  // Use the original script teardown and original white scene transition.
  // Only mark the opening as seen in RAM; normal save ownership is unchanged.
  EndKuppaScript();
  _ZN8SaveData17SetCharacterIntroEi(7);
  StartFile(1,0);
 }
 return true;
}
