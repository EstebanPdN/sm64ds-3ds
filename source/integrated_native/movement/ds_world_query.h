#ifndef SM64DS_MOVEMENT_DS_WORLD_QUERY_H
#define SM64DS_MOVEMENT_DS_WORLD_QUERY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Fixed-width ARM process addresses identify contacts; they are not host pointers.
// Keep original CLPS words until an explicit DS-to-N64 property map is ready.
typedef struct DsWorldGroundHit {
    int32_t height, normal[3];
    uint32_t clps[2], collider, actor;
    int32_t owner_id;
    uint16_t triangle, slot;
} DsWorldGroundHit;
typedef struct DsWorldLineHit {
    int32_t position[3], normal[3];
    uint32_t clps[2], collider, actor;
    int32_t owner_id;
    uint16_t triangle, slot;
} DsWorldLineHit;
// Copy of one live KCL prism and its current transform. Positions remain in
// KCL 1/64 world units, normals in 1/1024, extent in 1/65536; transform and
// world_anchor use Fix12. kind: 0 static, 1 moving, 2 moving with extra Y scale.
typedef struct DsWorldPrism {
    int32_t extent, anchor[3];
    int16_t normal[4][3];
    uint16_t attribute, kind;
    uint32_t kcl;
    int32_t transform[12], scale_y, world_anchor[3];
} DsWorldPrism;
// Revalidates registry identity, array indices and current surface properties.
// Unsupported collider classes and stale contacts fail without changing output.
// Simulation-thread only; no allocation, file access or retained raw pointers.
int sm64ds_contact_prism(const DsWorldGroundHit* contact, DsWorldPrism* prism);
// Scene/preload enumeration in registry/triangle order, excluding biased
// triangle zero. count: -1 invalid/unsupported, 0 empty, positive count.
// at: 1 copied, 0 invalid; both outputs unchanged on failure. Height is zero
// because enumeration is geometry, not a ground-height query.
int sm64ds_world_prism_count(unsigned slot);
int sm64ds_world_prism_at(unsigned slot,unsigned triangle,
                          DsWorldGroundHit* hit,DsWorldPrism* prism);
// Original DS actor exclusion plus CLPS pass-through rules. detect_flags is
// the caller's original dBgCh detect mask (1 is ordinary/default). Returns
// wall must be 1 for a wall query: DS effect0x14 also tests detect bit0x40
// only for walls. 1 pass through, 0 collide, -1 stale/invalid. Ability-specific flags remain
// the gameplay owner's responsibility. Does not run the DS sphere solver.
int sm64ds_contact_pass_through(const DsWorldGroundHit* contact,void* actor,
                                uint8_t detect_flags,uint8_t wall);
typedef struct DsWorldWallInput {float x,y,z,offset_y,radius;} DsWorldWallInput;
typedef struct DsWorldWallResult {
    float x,z;
    int32_t count,stored;
    DsWorldGroundHit surfaces[4];
} DsWorldWallResult;
// Uncached reference path for validating/preparing the production wall source.
// Original registry order and filters, N64 cylinder admission/correction, no
// allocation/I/O. 1 hit, 0 missing, -1 invalid/incomplete geometry; output is
// unchanged unless a complete hit result is available. Do not run this full
// geometry reconstruction each gameplay frame: use it to verify the cache.
int sm64ds_world_walls_reference(const DsWorldWallInput* input,void* actor,
                                 uint8_t detect_flags,DsWorldWallResult* result);
typedef struct DsWorldPlatformRef {
    uint32_t collider,actor,kcl;
    int32_t owner_id;
    uint16_t slot,kind;
} DsWorldPlatformRef;
// Original virtual TransformPos/GetAngularVelY transport, with current
// registry/owner/KCL/type validation. Positions use Fix12; result is XYZ plus
// signed yaw delta in element3. 1 transported, 0 invalid/unsupported. Output
// unchanged on failure. Gameplay must call it once per intended carry step.
int sm64ds_platform_displace(const DsWorldPlatformRef* platform,
                             const int32_t position[3],int32_t result[4]);
// Simulation-thread only, no allocation/I/O. Uses the game's live 24-collider
// registry, actor exclusion and original virtual queries (including movers).
// Coordinates and probe height use DS Fix12 units. Output is unchanged on miss
// or invalid arguments. The returned identity is not a stable N64 surface:
// scene/owner lifetime management must precede production solver registration.
int sm64ds_world_ground(const int32_t position[3], void* actor,
                        int32_t probe_height, DsWorldGroundHit* hit);
// Same registry/identity contract, detecting water while excluding ordinary
// solid surfaces. Water height must never enter the solver's solid floor path.
int sm64ds_world_water_ground(const int32_t position[3], void* actor,
                              int32_t probe_height, DsWorldGroundHit* hit);
// N64 floor admission: caller supplies the integer world coordinates used by
// its floor API. Admit floors through Y+78, with the original -11000 lower
// limit. Still returns a DS contact, not a cached SM64SurfaceCollisionData.
int sm64ds_motion_ground(const int32_t world_position[3], void* actor,
                         DsWorldGroundHit* hit);
// Ordinary-surface segment query in DS Fix12 units. Uses original registry
// ordering and world transforms; useful for the ceiling adapter. A ray query
// alone is not the N64 cylinder/wall collision solver.
int sm64ds_world_line(const int32_t start[3], const int32_t end[3],
                      void* actor, DsWorldLineHit* hit);
// Plane height in Fix12 units, retaining fractional precision in double.
// Validates that the contact still belongs to the registered collider/owner.
int sm64ds_contact_plane_height(const DsWorldLineHit* contact,
                                const int32_t position[3],double* height);
// Integer N64-world ceiling query: admit planes throughY-78, with the library's
//100000upper sentinel. Returns1hit,0missing,-1invalid/stale query. Height uses
// DsWorldGroundHit's Fix12 representation; failed calls leave output unchanged.
int sm64ds_motion_ceiling(const int32_t world_position[3],void* actor,
                          DsWorldGroundHit* hit);
#ifdef __cplusplus
}
#endif
#endif
