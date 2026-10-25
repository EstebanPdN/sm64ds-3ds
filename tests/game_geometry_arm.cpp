#include "sm64ds/game_geometry.hpp"
extern "C" uint8_t* native_geometry_storage(unsigned palette) {
    auto& g=sm64ds::game_geometry();
    return palette ? g.palettes : g.textures;
}
extern "C" void native_geometry_stats(uint32_t* out) {
    const auto& g=sm64ds::game_geometry();
    const uint32_t values[]={g.texture_capacity,g.palette_capacity,g.texture_uploads,
        g.palette_uploads,g.texture_bytes,g.palette_bytes,g.texture_generation,
        g.palette_generation,g.highlight_shading,g.texture_transfer,g.palette_transfer};
    for(unsigned i=0;i<11;++i) out[i]=values[i];
}
