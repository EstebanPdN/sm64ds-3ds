#include "menu_display_state.hpp"
#include "dialogue_window_view.hpp"
#include <cassert>
using namespace sm64ds::render;
using namespace sm64ds::render::game_hooks;
int main(){
    using namespace sm64ds::render::dialogue_window_view;
    assert(accepts_main_layers(0x19)&&!accepts_main_layers(0x1f)&&!accepts_main_layers(0x18));
    for(unsigned value=0;value<65536;++value){assert(dispcnt(value,false)==value);
        assert(dispcnt(value,true)==(value&~0xe800u));
        assert(control(0x40,std::uint16_t(value),false)==value&&control(0x40,std::uint16_t(value),true)==0);
        assert(control(0x54,std::uint16_t(value),true)==0&&control(0x6c,std::uint16_t(value),true)==value);}
    MenuDisplayState state;int tone=777;
    assert(state.unaffected()==Error::unsupported);
    assert(state.whole_tone(0,63,tone)==Error::unsupported&&tone==777);
    assert(state.brightness(0,63,-16)==Error::none&&state.brightness(1,47,-16)==Error::none);
    const auto generation=state.snapshot().generation;
    for(unsigned i=0;i<1000;++i)assert(state.brightness(1,47,-16)==Error::none);
    assert(state.snapshot().generation==generation&&state.whole_tone(0,63,tone)==Error::none&&tone==-16);
    // Upper title BGs/backdrop are selected, but an OBJ there must never be
    // darkened by lowering this masked effect to a whole-output fade.
    assert(state.whole_tone(1,44,tone)==Error::none&&tone==-16);tone=777;
    assert(state.whole_tone(1,60,tone)==Error::unsupported&&tone==777);
    assert(state.strength(1,8)==Error::none&&state.whole_tone(1,44,tone)==Error::none&&tone==8);
    SourceBackground bg;bg.resource=2;bg.layer=3;bg.priority=1;bg.width=32;bg.height=24;bg.has_content=true;bg.dirty=true;bg.x=-257;bg.y=511;
    assert(state.background(1,bg)==Error::none);bg.x=0;
    assert(state.snapshot().background[3].x==-257&&state.snapshot().background[3].dirty);
    const auto epoch=state.snapshot().background_epochs;
    assert(state.backgrounds_begin(2)==Error::none&&state.snapshot().background[3].seen);
    assert(state.snapshot().background_epochs[0]==epoch[0]&&state.snapshot().background_epochs[1]==epoch[1]+1);
    assert(state.background(3,bg)==Error::unsupported&&!state.snapshot().background[7].seen);
    assert(state.backgrounds_begin(3)==Error::none);assert(!state.snapshot().background[3].seen&&state.snapshot().effect[1].amount==8&&state.snapshot().failure);
    assert(state.whole_tone(1,44,tone)==Error::unsupported);state.reset();
    assert(state.disable(3)==Error::none&&state.strength(0,-8)==Error::none);
    assert(state.unaffected()==Error::none);
    assert(state.snapshot().effect[0].mode==DisplayEffect::none&&state.whole_tone(0,63,tone)==Error::none&&tone==0);
    assert(state.alpha(0,1,32,4,12)==Error::none&&state.strength(0,-8)==Error::none);
    assert(state.snapshot().effect[0].mode==DisplayEffect::alpha&&state.snapshot().effect[0].alpha_b==12);
    tone=777;assert(state.whole_tone(0,33,tone)==Error::unsupported&&tone==777);
    assert(state.whole_tone(0,32,tone)==Error::none&&tone==0);
    assert(state.brightness(0,1,-8)==Error::none&&state.whole_tone(0,32,tone)==Error::none&&tone==0);
    assert(state.brightness(0,1,0)==Error::none&&state.whole_tone(0,63,tone)==Error::none&&tone==0);
    assert(state.unaffected()==Error::none&&state.background(1,bg)==Error::none&&state.unaffected()==Error::unsupported);
    assert(state.backgrounds_begin(1)==Error::none&&state.unaffected()==Error::none);
    assert(state.whole_tone(2,63,tone)==Error::invalid_data&&state.whole_tone(0,31,tone)==Error::invalid_data);
    assert(state.alpha(0,1,32,31,31)==Error::none&&state.snapshot().effect[0].alpha_a==16&&state.snapshot().effect[0].alpha_b==16);
    assert(state.alpha(0,1,32,32,0)==Error::unsupported&&state.snapshot().failure);state.reset();
    assert(state.brightness(2,1,0)==Error::unsupported);state.reset();
    assert(state.brightness(0,64,0)==Error::unsupported);state.reset();
    assert(state.brightness(0,63,31)==Error::none&&state.snapshot().effect[0].amount==16);
    assert(state.strength(0,-31)==Error::none&&state.snapshot().effect[0].amount==-16);
    assert(state.brightness(0,63,-32)==Error::unsupported);state.reset();
    assert(state.disable(0)==Error::none&&state.disable(4)==Error::unsupported);state.reset();
    assert(state.strength(0,0)==Error::unsupported);state.reset();
    assert(!write_menu_display_capture(nullptr,121));FILE* f=std::tmpfile();assert(f&&write_menu_display_capture(f,121)&&std::fclose(f)==0);
}
