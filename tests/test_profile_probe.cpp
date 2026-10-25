#include "profile_probe.hpp"
#include "hitch_capture.hpp"
#include <cassert>
#include <stdexcept>
extern "C" { volatile uint64_t sm64ds_frame_prof[48]={}; }
static uint64_t tick=0;
static unsigned reads=0;
uint64_t frame_profile::ticks(){++reads;return tick;}
int main(){
    {profile_probe::Scope disabled(37,false);tick=100;}
    assert(!reads&&!sm64ds_frame_prof[37]);
    {profile_probe::Scope active(37,true);tick=150;}
    assert(reads==2&&sm64ds_frame_prof[37]==50);
    try {profile_probe::Scope active(38,true);tick=175;throw std::runtime_error("unwind");}
    catch(const std::runtime_error&){}
    assert(sm64ds_frame_prof[38]==25);
    assert(profile_probe::context(7,false,0,0)==0x534d0007);
    assert(profile_probe::context(0,true,0x8000,1)==0x534d8c00);
    const uint32_t before=0xfffffff0;
    profile_probe::audio_ticks.store(before);profile_probe::audio_ticks.fetch_add(48);
    assert(uint32_t(profile_probe::audio_ticks.load()-before)==48);
    profile_probe::HitchCapture h;
    uint64_t now=1;
    const auto step=[&](unsigned ms,bool control=true){now+=profile_probe::HitchCapture::hz*ms/1000;return h.observe(now,control);};
    assert(!step(1));for(unsigned i=0;i<120;++i)assert(!step(17));
    assert(!step(30));assert(!step(30));assert(step(30));assert(h.captures()==1);
    for(unsigned i=0;i<10;++i)assert(!step(30));
    h.reset_interval();assert(!step(1000));for(unsigned i=0;i<120;++i)assert(!step(17));
    assert(!step(51)); // ten-second cooldown remains across capture I/O
    for(unsigned i=0;i<500;++i)assert(!step(17));
    assert(!step(30));assert(!step(30));assert(step(30));assert(h.captures()==2);
    for(unsigned i=0;i<1000;++i)assert(!step(60));
    profile_probe::HitchCapture modal;
    for(unsigned i=0;i<500;++i)assert(!modal.observe(i*10000000ull,false));
    assert(!modal.captures());
    // A normal locked30 cadence must not consume the diagnostic budget.
    profile_probe::HitchCapture locked30;
    uint64_t at30=1;
    const auto step30=[&](unsigned ms,unsigned fps=30){at30+=locked30.hz*ms/1000;return locked30.observe(at30,true,fps);};
    assert(!step30(33));for(unsigned i=0;i<500;++i)assert(!step30(33));
    assert(!locked30.captures());
    assert(!step30(51));assert(!step30(51));assert(step30(51));
    assert(locked30.captures()==1);
    for(unsigned i=0;i<400;++i)assert(!step30(33));
    // Changing the lock resets settling, but preserves cooldown and budget.
    assert(!step30(1000,60));for(unsigned i=0;i<120;++i)assert(!step30(17,60));
    assert(!step30(30,60));assert(!step30(30,60));assert(step30(30,60));
    assert(locked30.captures()==2);
    profile_probe::HitchCapture severe30;
    uint64_t severe=1;assert(!severe30.observe(severe,true,30));
    for(unsigned i=0;i<120;++i){severe+=severe30.hz*33/1000;assert(!severe30.observe(severe,true,30));}
    severe+=severe30.hz*101/1000;assert(severe30.observe(severe,true,30));
}
