#include "dialogue_hooks.hpp"
#ifdef SM64DS_FRESH_NATIVE_GAME
#include "model_components_hooks.hpp"
#include "dialogue_window_view.hpp"
#include <algorithm>
#include <cstdlib>
extern "C" {
void __real_func_0201eaac();
void __real_func_0201f32c(int);
void __real__ZN7Message7AddCharEc(unsigned char);
void __real_func_0201b388(int);
void __real_func_0201adfc();
extern unsigned char data_0209d660,data_0209d698,data_0209d6a4,data_0209d678,data_0209d6b0,data_0209d6c4;
extern unsigned char data_0209d6c0,data_0209d65c,data_0209d6a0,data_0209d6a8,data_0209f2d8;
extern unsigned char data_0209d650,data_0209d6cc,data_0209d658,data_0209d64c,data_0209d6d0,data_0209d6c8;
extern unsigned char data_0209d710[],data_0208f074[];
extern int data_0209d704;
extern unsigned char data_0209d45c;
extern short data_0209d6d4;
extern const unsigned char* data_0209d6f0;
}
namespace sm64ds::render::game_hooks {
namespace {
DialogueText text;
std::array<DialogueCharacter,256> line;
DialogueStatistics dialogue_counters;
bool available=false,starting=false,active=false;
bool native_window=false;
DialogueWindow window;
bool owns_text() noexcept {return active&&(starting||data_0209d660);}
void failed(Error error) noexcept {if(error!=Error::none){++dialogue_counters.failures;report_image_failure();}}
}
void configure_dialogue(bool font_available) noexcept {
    const auto* option=std::getenv("SM64DS_NATIVE_DIALOGUE");available=font_available&&(!option||*option!='0');
    const auto* owned_window=std::getenv("SM64DS_NATIVE_DIALOGUE_WINDOW");native_window=!owned_window||*owned_window!='0';
}
const DialogueStatistics& dialogue_statistics() noexcept {return dialogue_counters;}
DialogueWindow dialogue_window() noexcept {return window;}
Error capture_dialogue(SceneFrame& frame,Screen screen) noexcept {
    window={};
    // Only the ordinary MAIN Message contract is admitted: BG0 geometry,
    // native BG3 text and OBJ HUD. Other visible backgrounds retain their
    // existing composition until they have their own native producers.
    if(native_window&&active&&data_0209d660&&data_0209d698==0&&dialogue_window_view::accepts_main_layers(data_0209d45c)){
        window.owned=true;window.brightness=data_0209d6d4>=0x2a&&data_0209d6d4<0x83?7:-8;
        const unsigned origin=screen==Screen::top?40:0;
        const int left=data_0209d658,top=data_0209d64c;
        const int right=std::uint8_t(2*data_0209d6d0-data_0209d658+1);
        const int bottom=std::min(192,int(std::uint8_t(2*data_0209d6c8-data_0209d64c+1)));
        if(left<right&&top<bottom)window.clip={std::uint16_t(origin+(5*left+1)/4),std::uint16_t((5*top+1)/4),
            std::uint16_t(origin+(5*right+1)/4),std::uint16_t((5*bottom+1)/4)};
    }
    dialogue_window_view::owned.store(window.owned,std::memory_order_release);
    if(!active||!data_0209d660)return Error::none;
    const int x=int(data_0209d650)+8,y=int(data_0209d6cc)+(data_0209f2d8==2?0:4);
    const int width=int(data_0209d6a8)*8,height=int(data_0209d6f0[6])*16;
    // These bounds belong to the Message window animation, not emulated GPU
    // registers. Intersect the retained page with its current source window.
    const int left=std::max(x,int(data_0209d658)),top=std::max(y,int(data_0209d64c));
    const int right=std::min({x+width,256,int(std::uint8_t(2*data_0209d6d0-data_0209d658+1))});
    const int bottom=std::min({y+height,192,int(std::uint8_t(2*data_0209d6c8-data_0209d64c+1))});
    if(left>=right||top>=bottom)return Error::none;
    const unsigned origin=screen==Screen::top?40:0;
    const ScreenClip clip{std::uint16_t(origin+(5*left+1)/4),std::uint16_t((5*top+1)/4),
        std::uint16_t(origin+(5*right+1)/4),std::uint16_t((5*bottom+1)/4)};
    const auto error=text.capture(frame,screen,float(origin)+1.25f*x,1.25f*y,clip,unsigned(data_0209d6f0[6]),unsigned(width));
    if(error==Error::none)++dialogue_counters.frames;else failed(error);
    return error;
}
}
extern "C" void __wrap_func_0201f32c(int id){
    using namespace sm64ds::render::game_hooks;
    starting=available&&data_0209d698==0;
    __real_func_0201f32c(id);starting=false;
}
extern "C" void __wrap_func_0201eaac(){
    __real_func_0201eaac();using namespace sm64ds::render::game_hooks;
    active=starting;if(active){text.reset();++dialogue_counters.starts;}
}
extern "C" void __wrap__ZN7Message7AddCharEc(unsigned char code){
    using namespace sm64ds::render::game_hooks;
    if(!owns_text()){__real__ZN7Message7AddCharEc(code);return;}
    const auto advance=data_0208f074[code];
    line[data_0209d6a4]={code,advance,data_0209d6c4==0};
    data_0209d704+=16;data_0209d678=std::uint8_t((data_0209d678+advance)&7);
    data_0209d710[data_0209d6a4]=advance;data_0209d6b0=std::uint8_t(data_0209d6b0+advance);++data_0209d6a4;
    ++dialogue_counters.characters;
}
extern "C" void __wrap_func_0201b388(int stride){
    using namespace sm64ds::render::game_hooks;
    if(!owns_text()){__real_func_0201b388(stride);return;}
    failed(text.line(line.data(),data_0209d6a4,data_0209d65c,data_0209d6c0));
    ++data_0209d6c0;++dialogue_counters.lines;
}
extern "C" void __wrap_func_0201adfc(){
    using namespace sm64ds::render::game_hooks;
    if(!owns_text()){__real_func_0201adfc();return;}
    failed(text.scroll(line.data(),data_0209d6a4,data_0209d65c,data_0209d6f0[6],data_0209d6a0));
    data_0209d6a0=std::uint8_t(data_0209d6a0+3);++dialogue_counters.scroll_steps;
}
#else
namespace sm64ds::render::game_hooks {
void configure_dialogue(bool) noexcept {}
Error capture_dialogue(SceneFrame&,Screen) noexcept {return Error::none;}
DialogueWindow dialogue_window() noexcept {return {};}
const DialogueStatistics& dialogue_statistics() noexcept {static const DialogueStatistics dialogue_counters;return dialogue_counters;}
}
#endif
