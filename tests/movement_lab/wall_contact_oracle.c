// Test-only access to the actual imported engine's mesh path.
#include "libsm64.h"
#include "load_surfaces.h"
#include "decomp/engine/surface_collision.h"
void fixture_wall_mesh(const struct SM64Surface* surfaces,unsigned count) {
    sm64_static_surfaces_load(surfaces,count);
}
const struct SM64SurfaceCollisionData* fixture_wall_surface(unsigned index) {
    return loaded_surface_iter_get_at_index(0,index);
}
int fixture_original_walls(struct SM64WallCollisionData* query) {
    return find_wall_collisions(query);
}
