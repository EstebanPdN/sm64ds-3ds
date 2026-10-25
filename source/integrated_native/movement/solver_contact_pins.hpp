#pragma once
#include "solver_contacts.h"
#include "libsm64.h"
#include <array>
#include <cstddef>
namespace movement_geometry {
template<std::size_t Capacity> class SolverContactPins {
    std::array<const SM64SurfaceCollisionData*,Capacity> surfaces{};
    std::array<const SM64SurfaceObjectTransform*,Capacity> platforms{};
    std::size_t surface_count=0,platform_count=0;
    bool pin_platform(const SM64SurfaceObjectTransform* platform) {
        if(!platform)return true;
        for(std::size_t i=0;i<platform_count;++i)if(platforms[i]==platform)return true;
        if(platform_count==Capacity)return false;
        platforms[platform_count++]=platform;return true;
    }
    static int visit(const Motion64Contacts* contacts,void* context) {
        auto& self=*static_cast<SolverContactPins*>(context);
        for (const auto* s:{contacts->floor,contacts->ceil,contacts->wall,contacts->object_floor}) {
            if (!s || motion64_permanent_surface(s)) continue;
            // A retained floor/ceiling/wall can outlive marioObj->platform
            // (for example while jumping). Its nested transform remains live.
            if(!self.pin_platform(s->transform))return 0;
            bool duplicate=false;
            for (std::size_t i=0;i<self.surface_count;++i) duplicate|=self.surfaces[i]==s;
            if (duplicate) continue;
            if (self.surface_count==Capacity) return 0;
            self.surfaces[self.surface_count++]=s;
        }
        if(!self.pin_platform(contacts->platform))return 0;
        return 1;
    }
public:
    bool collect() {
        surface_count=platform_count=0;
        if (motion64_visit_contacts(visit,this)<0) {surface_count=platform_count=0;return false;}
        return true;
    }
    const SM64SurfaceCollisionData* const* surface_data() const {return surfaces.data();}
    std::size_t surface_size() const {return surface_count;}
    const SM64SurfaceObjectTransform* const* platform_data() const {return platforms.data();}
    std::size_t platform_size() const {return platform_count;}
};
}
