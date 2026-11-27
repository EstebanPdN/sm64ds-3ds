#include "menu_display_state.hpp"
#include "menu_backgrounds.hpp"
#include <limits>
namespace sm64ds::render::game_hooks {
namespace {
bool same(const SourceEffect& a,const SourceEffect& b) noexcept {
    return a.mode==b.mode&&a.first==b.first&&a.second==b.second&&a.alpha_a==b.alpha_a&&a.alpha_b==b.alpha_b&&a.amount==b.amount&&a.known==b.known;
}
MenuDisplayState display;MenuDisplayStatistics stats;
std::uint64_t (*clock_source)()=nullptr;
}
Error MenuDisplayState::backgrounds_begin(unsigned engines) noexcept {
    if(!engines||(engines&~3u))return fail();
    // Match the original background publication boundary, which may run
    // between game updates. Resetting at game begin would erase latched state.
    for(unsigned e=0;e<2;++e)if(engines&(1u<<e)){
        for(unsigned i=0;i<4;++i)state_.background[e*4+i]={};
        ++state_.background_epochs[e];
    }
    return Error::none;
}
Error MenuDisplayState::brightness(unsigned engine,unsigned targets,int amount) noexcept {
    if(engine>=2||targets>63||amount<-31||amount>31)return fail();
    const auto normalized=amount<-16?-16:amount>16?16:amount;
    SourceEffect next;next.mode=DisplayEffect::brightness;next.first=std::uint8_t(targets);next.amount=std::int8_t(normalized);next.known=true;
    if(!same(next,state_.effect[engine])){state_.effect[engine]=next;++state_.generation;}
    return Error::none;
}
Error MenuDisplayState::strength(unsigned engine,int amount) noexcept {
    if(engine>=2||amount<-31||amount>31||!state_.effect[engine].known)return fail();
    auto next=state_.effect[engine];
    // The original helper changes sign only for an already-enabled brightness
    // effect. It must not turn a disabled or alpha effect into brightness.
    if(next.mode==DisplayEffect::brightness)next.amount=std::int8_t(amount<-16?-16:amount>16?16:amount);
    if(!same(next,state_.effect[engine])){state_.effect[engine]=next;++state_.generation;}
    return Error::none;
}
Error MenuDisplayState::alpha(unsigned engine,unsigned first,unsigned second,unsigned a,unsigned b) noexcept {
    if(engine>=2||first>63||second>63||a>31||b>31)return fail();
    SourceEffect next;next.mode=DisplayEffect::alpha;next.first=std::uint8_t(first);next.second=std::uint8_t(second);next.alpha_a=std::uint8_t(a>16?16:a);next.alpha_b=std::uint8_t(b>16?16:b);next.known=true;
    if(!same(next,state_.effect[engine])){state_.effect[engine]=next;++state_.generation;}
    return Error::none;
}
Error MenuDisplayState::disable(unsigned engines) noexcept {
    if(engines&~3u)return fail();
    for(unsigned e=0;e<2;++e)if(engines&(1u<<e)){SourceEffect next;next.known=true;
        if(!same(next,state_.effect[e])){state_.effect[e]=next;++state_.generation;}
    }
    return Error::none;
}
Error MenuDisplayState::background(unsigned engines,const SourceBackground& bg) noexcept {
    if(!engines||(engines&~3u)||bg.layer>3||bg.priority>3||bg.resource>=8||!bg.width||!bg.height||bg.width>128||bg.height>128)return fail();
    for(unsigned e=0;e<2;++e)if((engines&(1u<<e))&&state_.background[e*4+bg.layer].seen)return fail();
    for(unsigned e=0;e<2;++e)if(engines&(1u<<e)){auto& out=state_.background[e*4+bg.layer];out=bg;out.seen=true;}
    return Error::none;
}
Error MenuDisplayState::whole_tone(unsigned engine,unsigned visible,int& output) const noexcept {
    if(engine>=2||visible>63||!(visible&32u))return Error::invalid_data;
    if(state_.failure||!state_.effect[engine].known)return Error::unsupported;
    const auto& e=state_.effect[engine];int result=0;
    if(e.mode==DisplayEffect::brightness&&e.amount){
        const auto selected=visible&e.first;
        if(selected&&selected!=visible)return Error::unsupported;
        if(selected)result=e.amount;
    }else if(e.mode==DisplayEffect::alpha&&(visible&e.first)&&(visible&e.second))return Error::unsupported;
    output=result;return Error::none;
}
Error MenuDisplayState::unaffected() const noexcept {
    if(state_.failure)return Error::unsupported;
    for(const auto& e:state_.effect)if(!e.known||e.mode==DisplayEffect::alpha||
        (e.mode==DisplayEffect::brightness&&e.amount))return Error::unsupported;
    for(const auto& bg:state_.background)if(bg.seen)return Error::unsupported;
    return Error::none;
}
void menu_display_scene_changed() noexcept {display.reset();}
const SourceDisplaySnapshot& menu_display_snapshot() noexcept {return display.snapshot();}
const MenuDisplayStatistics& menu_display_statistics() noexcept {return stats;}
Error menu_display_unaffected() noexcept {return display.unaffected();}
void menu_display_clock(std::uint64_t (*clock)()) noexcept {clock_source=clock;}
bool write_menu_display_capture(FILE* file,std::uint64_t frame) noexcept {
    if(!file)return false;
    const auto& s=display.snapshot();
    if(std::fprintf(file,"{\"format\":\"SM64_NATIVE_SOURCE_DISPLAY_1\",\"scope\":\"typed-source-display-state\",\"original_scene_accepted\":false,\"physical_capture\":false,\"frame\":%llu,\"generation\":%llu,\"failure\":%s,\"effects\":[",
        static_cast<unsigned long long>(frame),static_cast<unsigned long long>(s.generation),s.failure?"true":"false")<0)return false;
    for(unsigned i=0;i<2;++i){const auto& e=s.effect[i];
        if(std::fprintf(file,"%s{\"known\":%s,\"mode\":%u,\"first\":%u,\"second\":%u,\"alpha\":[%u,%u],\"amount\":%d}",i?",":"",e.known?"true":"false",unsigned(e.mode),unsigned(e.first),unsigned(e.second),unsigned(e.alpha_a),unsigned(e.alpha_b),int(e.amount))<0)return false;
    }
    if(std::fprintf(file,"],\"background_epochs\":[%llu,%llu],\"backgrounds\":[",
        static_cast<unsigned long long>(s.background_epochs[0]),static_cast<unsigned long long>(s.background_epochs[1]))<0)return false;
    for(unsigned i=0;i<8;++i){const auto& bg=s.background[i];
        if(std::fprintf(file,"%s{\"seen\":%s,\"resource\":%u,\"layer\":%u,\"priority\":%u,\"size\":[%u,%u],\"has_content\":%s,\"dirty\":%s,\"scroll\":[%ld,%ld]}",
            i?",":"",bg.seen?"true":"false",unsigned(bg.resource),unsigned(bg.layer),unsigned(bg.priority),unsigned(bg.width),unsigned(bg.height),bg.has_content?"true":"false",bg.dirty?"true":"false",static_cast<long>(bg.x),static_cast<long>(bg.y))<0)return false;
    }
    return std::fputs("]}\n",file)!=EOF&&!std::ferror(file);
}
}
#ifdef SM64DS_FRESH_NATIVE_GAME
#include "model_components_hooks.hpp"
#include <cstring>
extern "C" {
void __real__ZN3G2x18SetBlendBrightnessEPVtts(void*,std::uint16_t,std::int16_t);
void __real__ZN3G2x13SetBlendAlphaEPVttttj(void*,std::uint16_t,std::uint16_t,std::uint16_t,std::uint32_t);
void __real_func_020551f0(void*,int);
void __real_func_ov007_020c897c(void*);
void __real_func_ov007_020c076c(void*,int);
void __real_func_ov007_020c03b0(void*);
extern void* data_ov007_0210342c;
}
namespace sm64ds::render::game_hooks {
namespace {
template<class T>T field(const void* p,unsigned offset) noexcept {T v;std::memcpy(&v,static_cast<const char*>(p)+offset,sizeof(v));return v;}
unsigned engine(const void* port) noexcept {
    return std::uintptr_t(port)==0x04000050?0:std::uintptr_t(port)==0x04001050?1:2;
}
void result(Error error) noexcept {if(error!=Error::none)++stats.failures;}
void capture_background(void* p,unsigned flags) noexcept {
    ++stats.background;SourceBackground bg;
    const auto type=field<int>(p,0),priority=field<int>(p,4);
    if(type<0||type>3||priority<0||priority>3){result(display.background(0,bg));return;}
    bg.layer=std::uint8_t(type);bg.priority=std::uint8_t(priority);
    bg.width=field<std::uint16_t>(p,0x1c);bg.height=field<std::uint16_t>(p,0x1e);
    bg.has_content=field<std::uintptr_t>(p,0x10)!=0;bg.dirty=bg.has_content&&field<int>(p,0xc)!=0;
    // Source spans are identified through the live owner table. No source
    // address, mutable tilemap pointer or original graphics packet is retained.
    if(data_ov007_0210342c){const auto* owner=field<const void*>(data_ov007_0210342c,0x74);
        if(owner){const auto* table=field<const void* const*>(owner,0x34);
            if(table)for(unsigned i=0;i<8;++i)if(table[i]==p){bg.resource=std::uint16_t(i);break;}
        }
    }
    const auto scroll=[](std::int32_t a,std::int32_t b,std::int32_t& output){const auto v=std::int64_t(a)+b;
        if(v<std::numeric_limits<std::int32_t>::min()||v>std::numeric_limits<std::int32_t>::max())return false;
        output=std::int32_t(v>=0?v/4096:-((-v+4095)/4096));return true;};
    if(!scroll(field<std::int32_t>(p,0x20),field<std::int32_t>(p,0x28),bg.x)||
       !scroll(field<std::int32_t>(p,0x24),field<std::int32_t>(p,0x2c),bg.y)){result(display.background(0,bg));return;}
    result(display.background(flags,bg));
    menu_background_identity(p,field<const void*>(p,bg.resource==1?0x14:0x10),bg.width,bg.height);
    menu_background_publish(bg.resource,p,flags,bg.layer,bg.priority,bg.x,bg.y);
    // Preserve the original producer's sole CPU-side mutation while avoiding
    // its cache flush, tilemap transfer and emulated scroll-register writes.
    if(bg.dirty){const int clean=0;std::memcpy(static_cast<char*>(p)+0xc,&clean,4);
        ++stats.dirty_backgrounds;stats.skipped_bytes+=std::uint64_t(bg.width)*bg.height*2*
            (unsigned(bool(flags&1))+unsigned(bool(flags&2)));
    }
}
}
}
extern "C" void __wrap__ZN3G2x18SetBlendBrightnessEPVtts(void* port,std::uint16_t targets,std::int16_t amount){
    using namespace sm64ds::render::game_hooks;
    if(!native_2d_active()){__real__ZN3G2x18SetBlendBrightnessEPVtts(port,targets,amount);return;}
    ++stats.brightness;result(display.brightness(engine(port),targets,amount));
}
extern "C" void __wrap__ZN3G2x13SetBlendAlphaEPVttttj(void* port,std::uint16_t first,std::uint16_t second,std::uint16_t a,std::uint32_t b){
    using namespace sm64ds::render::game_hooks;
    if(!native_2d_active()){__real__ZN3G2x13SetBlendAlphaEPVttttj(port,first,second,a,b);return;}
    ++stats.alpha;result(display.alpha(engine(port),first,second,a,b));
}
extern "C" void __wrap_func_020551f0(void* port,int amount){
    using namespace sm64ds::render::game_hooks;
    if(!native_2d_active()){__real_func_020551f0(port,amount);return;}
    ++stats.strength;result(display.strength(engine(port),amount));
}
extern "C" void __wrap_func_ov007_020c897c(void* p){
    using namespace sm64ds::render::game_hooks;
    if(!native_2d_active()){__real_func_ov007_020c897c(p);return;}
    const auto mode=field<int>(p,0);
    const auto flags=field<unsigned>(p,4);
    // Preserve all three source animation-state writes, without its MMIO.
    const std::int32_t zero=0,complete=4096;
    std::memcpy(static_cast<char*>(p)+8,&zero,4);
    std::memcpy(static_cast<char*>(p)+20,&complete,4);
    std::memcpy(static_cast<char*>(p)+32,&zero,4);
    if(mode>=2&&mode<=4){++stats.disable;result(display.disable(flags));}
    else if(mode==1){display.invalidate();result(sm64ds::render::Error::unsupported);} // mosaic needs a native producer
}
extern "C" void __wrap_func_ov007_020c076c(void* p,int flags){
    using namespace sm64ds::render::game_hooks;
    if(!native_2d_active()){__real_func_ov007_020c076c(p,flags);return;}
    capture_background(p,unsigned(flags));
}
extern "C" void __wrap_func_ov007_020c03b0(void* p){
    using namespace sm64ds::render::game_hooks;
    const bool active=native_2d_active();const auto start=active&&clock_source?clock_source():0;
    if(active){const auto flags=field<unsigned>(p,0);result(display.backgrounds_begin(flags));menu_background_group_begin(flags);}
    __real_func_ov007_020c03b0(p);
    if(active&&clock_source)stats.background_group_ticks+=clock_source()-start;
}
namespace sm64ds::render::game_hooks {
void menu_display_commit_registers() noexcept {
    const auto& s=display.snapshot();
    for(unsigned e=0;e<2;++e){const auto& effect=s.effect[e];if(!effect.known)continue;
        void* port=reinterpret_cast<void*>(std::uintptr_t(e?0x04001050u:0x04000050u));
        if(effect.mode==DisplayEffect::brightness)__real__ZN3G2x18SetBlendBrightnessEPVtts(port,effect.first,effect.amount);
        else if(effect.mode==DisplayEffect::alpha)__real__ZN3G2x13SetBlendAlphaEPVttttj(port,effect.first,effect.second,effect.alpha_a,effect.alpha_b);
        else *static_cast<volatile std::uint16_t*>(port)=0; // disabled effect (func_ov007_020c897c modes 2-4)
    }
}
}
#endif
