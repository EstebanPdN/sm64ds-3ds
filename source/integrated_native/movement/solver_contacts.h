#ifndef SM64DS_MOTION_SOLVER_CONTACTS_H
#define SM64DS_MOTION_SOLVER_CONTACTS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct SM64SurfaceCollisionData;
struct SM64SurfaceObjectTransform;
typedef struct Motion64Contacts {
    int32_t mario_id;
    const struct SM64SurfaceCollisionData *floor,*ceil,*wall,*object_floor;
    const struct SM64SurfaceObjectTransform* platform;
} Motion64Contacts;
typedef int (*Motion64ContactsVisitor)(const Motion64Contacts*,void*);
// Simulation-thread only. Neither API changes the bound global solver state.
// Invalid IDs leave output unchanged. Visit returns-1 on invalid visitor/abort.
int motion64_get_contacts(int32_t mario_id,Motion64Contacts* out);
int motion64_visit_contacts(Motion64ContactsVisitor visitor,void* context);
// The engine's permanent water pseudo-floor must not be mistaken for a foreign
// cache allocation. Other external pointers still require explicit ownership.
int motion64_permanent_surface(const struct SM64SurfaceCollisionData* surface);
// Synchronize pre-step held-button history after native/DS ownership transfer.
// No simulation/action change or global binding change. Invalid IDs return0.
// A genuinely new press must not be included in this previous-held sample.
int motion64_seed_buttons(int32_t mario_id,int jump,int attack,int crouch);
#ifdef __cplusplus
}
#endif
#endif
