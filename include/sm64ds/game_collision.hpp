#pragma once
#include <cstdint>

namespace sm64ds {
// ARM11 bridge to the original game's static KCL collider and ground query.
// Storage is caller-owned and must be aligned to at least four bytes.
constexpr unsigned collision_storage_size = 0x50;
struct GroundHit { int32_t y=0, normal[3]{}; uint16_t triangle=0; };
struct SphereHit { int32_t displacement[3]{}; uint32_t flags=0; };
bool collision_initialize(void* storage, void* validated_kcl, void* validated_clps);
bool collision_ground(void* storage, const int32_t position[3], int32_t lower_bound,
                      GroundHit& hit);
bool collision_sphere(void* storage,const int32_t center[3],int32_t radius,SphereHit& hit);
bool collision_line(void* storage,const int32_t start[3],const int32_t end[3],int32_t hit[3]);
void collision_destroy(void* storage);
}
