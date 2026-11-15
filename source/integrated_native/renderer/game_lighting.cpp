#include "game_lighting.hpp"
#include "model_components_hooks.hpp"
#include <cstring>
namespace sm64ds::render::game_hooks {
namespace { LightingState state;LightingStatistics counters;std::array<std::uint16_t,32> toon{};bool toon_known=false; }
void lighting_reset() noexcept {state.reset();counters={};}
void lighting_view(const std::int32_t* matrix) noexcept {
    if(!matrix){state.reset();++counters.failures;report_image_failure();return;}
    LightingState::Basis view;std::memcpy(view.data(),matrix,sizeof(view));state.view(view);++counters.views;
}
Error capture_lighting(unsigned mask,bool table,LightSet& output) noexcept {
    const auto result=state.snapshot(mask,table,output);
    if(result==Error::none){output.toon=toon;output.toon_known=toon_known;}return result;
}
const LightingStatistics& lighting_statistics() noexcept {return counters;}
std::uint8_t lighting_known() noexcept {return state.known();}
void source_light(const void* original) noexcept {
    ++counters.source_calls;
    if(!original){state.reset();++counters.failures;report_image_failure();return;}
    const auto* bytes=static_cast<const char*>(original);
    std::int32_t enabled,id;std::memcpy(&enabled,bytes+36,4);if(!enabled)return;
    std::memcpy(&id,bytes,4);
    std::array<std::int16_t,3> direction;
    for(unsigned i=0;i<3;++i){std::int16_t value;std::memcpy(&value,bytes+28+i*2,2);
        const int shifted=value>=0?value/8:-((-int(value)+7)/8);
        const unsigned bits=unsigned(shifted)&1023u;direction[i]=std::int16_t(bits>=512?int(bits)-1024:int(bits));}
    std::uint16_t colour;std::memcpy(&colour,bytes+34,2);
    if(state.light(unsigned(id),direction,colour)!=Error::none){state.reset();++counters.failures;report_image_failure();return;}
    ++counters.updates;
}
#ifdef __3DS__
extern "C" void __real_func_020555a4(void*);
extern "C" void __wrap_func_020555a4(void* source){
    using namespace sm64ds::render::game_hooks;
    if(!native_active()){__real_func_020555a4(source);return;}
    if(source){std::memcpy(toon.data(),source,64);toon_known=true;}
    else {toon_known=false;report_image_failure();}
}
#endif
}
extern "C" void __real_func_ov007_020c232c(void*);
extern "C" void __wrap_func_ov007_020c232c(void* source){
    if(!sm64ds::render::game_hooks::native_active()){__real_func_ov007_020c232c(source);return;}
    sm64ds::render::game_hooks::source_light(source);
}
