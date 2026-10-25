#pragma once
#include "ds_world_query.h"
#include "libsm64.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace movement_geometry {
// Handles provide stable identity storage to the N64 core; DS's original
// TransformPos provides transport. Never use these zeroed Euler fields as a
// substitute for the registered platform provider. Pin nested surface handles
// as well as marioObj->platform before each simulation tick.
template<std::size_t Capacity=32> class StablePlatforms {
    struct Entry {
        SM64SurfaceObjectTransform handle{};
        DsWorldPlatformRef source{};
        uint64_t used=0;
        bool occupied=false,retained=false,issued=false;
    };
    std::array<Entry,Capacity> entries{};
    uint64_t clock=0;
    bool opened=false;
    static bool same(const DsWorldPlatformRef& a,const DsWorldPlatformRef& b) {
        return a.collider==b.collider && a.actor==b.actor && a.kcl==b.kcl &&
               a.owner_id==b.owner_id && a.slot==b.slot && a.kind==b.kind;
    }
public:
    StablePlatforms()=default;
    StablePlatforms(const StablePlatforms&)=delete;
    StablePlatforms& operator=(const StablePlatforms&)=delete;
    bool begin_tick(const SM64SurfaceObjectTransform* const* pins,std::size_t count) {
        if(count>Capacity || (count&&!pins))return false;
        for(std::size_t i=0;i<count;++i) {
            if(!pins[i])continue;
            bool owned=false;for(const auto& entry:entries)owned|=entry.occupied && &entry.handle==pins[i];
            if(!owned)return false;
        }
        for(auto& entry:entries){entry.retained=false;entry.issued=false;}
        for(std::size_t i=0;i<count;++i)for(auto& entry:entries)
            if(pins[i] && &entry.handle==pins[i])entry.retained=true;
        opened=true;return true;
    }
    SM64SurfaceObjectTransform* acquire(const DsWorldPlatformRef& source) {
        if(!opened || !source.collider || !source.kcl || source.slot>=24 || source.kind<1 || source.kind>2)return nullptr;
        if(clock==std::numeric_limits<uint64_t>::max()) {
            for(auto& entry:entries)entry.used=0;clock=0;
        }
        ++clock;
        for(auto& entry:entries)if(entry.occupied&&same(entry.source,source)) {
            entry.used=clock;entry.issued=true;return &entry.handle;
        }
        Entry* candidate=nullptr;
        for(auto& entry:entries) {
            if(!entry.occupied){candidate=&entry;break;}
            if(!entry.retained&&!entry.issued&&(!candidate||entry.used<candidate->used))candidate=&entry;
        }
        if(!candidate)return nullptr;
        *candidate=Entry{};candidate->source=source;candidate->used=clock;
        candidate->occupied=candidate->issued=true;return &candidate->handle;
    }
    int displace(const SM64SurfaceObjectTransform* handle,float position[3],int16_t* yaw) const {
        if(!handle || !position || !yaw)return -1;
        const Entry* owner=nullptr;
        for(const auto& entry:entries)if(entry.occupied && &entry.handle==handle){owner=&entry;break;}
        if(!owner)return -1;
        int32_t input[3],result[4];
        for(unsigned i=0;i<3;++i) {
            const double raw=std::round(double(position[i])*4096.);
            if(!std::isfinite(raw)||raw<INT32_MIN||raw>INT32_MAX)return -1;
            input[i]=int32_t(raw);
        }
        if(sm64ds_platform_displace(&owner->source,input,result)!=1 || result[3]<INT16_MIN || result[3]>INT16_MAX)return -1;
        for(unsigned i=0;i<3;++i)position[i]=result[i]/4096.f;
        *yaw=int16_t(result[3]);return 1;
    }
    void reset_after_solver_deleted() {
        for(auto& entry:entries)entry=Entry{};
        clock=0;opened=false;
    }
};
}
