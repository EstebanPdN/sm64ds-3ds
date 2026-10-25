#include "diagnostic_panel.hpp"
#include <cassert>
#include <initializer_list>
int main(){
 enhance::DiagnosticPanel panel;
 panel.update(true,4);assert(panel.visible);
 for(uint32_t key:{1u,2u,8u,16u,32u,64u,128u,256u,512u,1024u,2048u}){
  panel.update(true,key);assert(panel.visible);assert(enhance::DiagnosticPanel::game_keys(true,key)==key);
 }
 panel.update(true,4);assert(!panel.visible);
 panel.update(true,4);assert(panel.visible);panel.update(false,0);assert(!panel.visible);
 assert(enhance::DiagnosticPanel::game_keys(false,4)==4);
 assert(enhance::DiagnosticPanel::game_keys(true,2|4|16)==(2|16));
}
