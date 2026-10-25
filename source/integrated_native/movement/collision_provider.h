#ifndef SM64DS_MOTION_COLLISION_PROVIDER_H
#define SM64DS_MOTION_COLLISION_PROVIDER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct SM64SurfaceCollisionData;
struct SM64WallCollisionData;
struct SM64SurfaceObjectTransform;
enum Motion64FloorResult { MOTION64_FLOOR_ERROR=-1, MOTION64_FLOOR_MISSING=0, MOTION64_FLOOR_HIT=1 };
typedef int (*Motion64FloorProvider)(float x,float y,float z,float* height,
    struct SM64SurfaceCollisionData** surface,void* context);
// Simulation-thread only. A provider owns stable returned surface storage.
// Delete/reset the Mario instance before releasing that storage on scene exit.
// NULL keeps the unmodified N64 mesh path (the existing isolated laboratory).
void motion64_set_floor_provider(Motion64FloorProvider provider,void* context);
uint32_t motion64_floor_provider_faults(void);
// Returns0 only when no provider is registered. A registered provider's miss
// or error never falls through to an approximate/inconsistent secondary mesh.
int motion64_query_floor(float x,float y,float z,float* height,
    struct SM64SurfaceCollisionData** surface);
// Same hit/missing/error result contract for ceilings. Callback owns N64 input
// coordinate/admission semantics; registered misses never use the old mesh.
void motion64_set_ceiling_provider(Motion64FloorProvider provider,void* context);
uint32_t motion64_ceiling_provider_faults(void);
int motion64_query_ceiling(float x,float y,float z,float* height,
    struct SM64SurfaceCollisionData** surface);
// Nonnegative collision count, negative error. Updates are staged so invalid
// results cannot corrupt the caller's position or leak mixed mesh contacts.
typedef int (*Motion64WallProvider)(struct SM64WallCollisionData*,void*);
void motion64_set_wall_provider(Motion64WallProvider provider,void* context);
uint32_t motion64_wall_provider_faults(void);
int motion64_query_walls(struct SM64WallCollisionData* data,int* count);
// Registered callback transports the position through the original DS moving
// collider and returns1; anything else is an error, never Euler fallback.
typedef int (*Motion64PlatformProvider)(const struct SM64SurfaceObjectTransform*,
    float position[3],int16_t* yaw_delta,void* context);
void motion64_set_platform_provider(Motion64PlatformProvider provider,void* context);
uint32_t motion64_platform_provider_faults(void);
// 0 unregistered,1 valid staged transport,-1 fault. Failed calls preserve
// position/yaw. The stable transform pointer is an identity handle for DS.
int motion64_query_platform(const struct SM64SurfaceObjectTransform* platform,
    float position[3],int16_t* yaw_delta);
#ifdef __cplusplus
}
#endif
#endif
