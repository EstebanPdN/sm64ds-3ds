#pragma once
#include "frame_profile.h"
#include <atomic>
#include <cstdint>

namespace profile_probe {
// Each ordinary slot has one writer. Audio uses its own lock-free counter:
// the audio thread stays active while the presentation worker is drained.
inline std::atomic<uint32_t> audio_ticks{0};
static_assert(std::atomic<uint32_t>::is_always_lock_free,"Audio diagnostic counter");
struct Scope {
    unsigned slot;
    bool active;
    uint64_t start;
    Scope(unsigned s,bool enabled):slot(s),active(enabled),start(enabled?frame_profile::ticks():0){}
    ~Scope(){if(active)sm64ds_frame_prof[slot]+=frame_profile::ticks()-start;}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
inline uint32_t context(unsigned flags,bool camera,uint16_t yaw,int height_relation){
    return 0x534d0000u|(flags&7u)|(camera?(0x8000u|((unsigned(yaw)>>8)<<3)|
        ((height_relation>0?1u:height_relation<0?2u:0u)<<11)):0u);
}
}
