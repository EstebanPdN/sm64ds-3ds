#pragma once
#include "sm64ds/input.hpp"
#include <cstdint>

namespace sm64ds {
// Copy once after the original samplers on EVERY original input-ready tick.
// Scheduler time is logical VBlank at exactly 60/1; this never samples wall time.
void native_camera_publish(Stick camera,std::uint64_t game_tick,
                           std::uint64_t logical_vblank);
// Startup only. Lifecycle invalidation uses separate internal paths which
// preserve the publication/consumption timestamp and microsecond residue.
void native_camera_reset();
struct NativeCameraStats {
    std::uint64_t publications=0,consumed_ticks=0,manual_updates=0,duplicate_updates=0;
    std::uint64_t reseeds=0,blocked=0,constrained=0,inverted_intervals=0;
    std::uint64_t last_tick=0,last_vblank=0;
    std::uint32_t generation=0,ready=0,armed=0,status=0;
    std::int32_t desired_radius=0,solved_radius=0;
    std::int16_t yaw=0,pitch=0;
    std::uint32_t elapsed_us=0;
};
const NativeCameraStats& native_camera_stats();
}

// Internal body adapter and GP fixture entry points. No pointer supplied here
// is walked before the live original dispatch/lifecycle identity is admitted.
extern "C" {
void sm64ds_native_camera_publish_q15(std::int32_t x,std::int32_t y,
    std::uint64_t game_tick,std::uint64_t logical_vblank);
void sm64ds_native_camera_reset();
const sm64ds::NativeCameraStats* sm64ds_native_camera_stats();
int sm64ds_native_camera_prepare(char* self,const void* resolved_mode,
    const void* resolved_view,int secondary,std::int32_t minimum_radius,
    std::int32_t maximum_radius);
int sm64ds_native_camera_height_range(char* self,std::int32_t floor,
                                      std::int32_t ceiling);
void sm64ds_native_camera_finish(char* self,int manual);
}
