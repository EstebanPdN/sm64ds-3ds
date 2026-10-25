#pragma once
#include <cstddef>
#include <cstdint>

namespace sm64ds {
// Logical texture/palette addresses are offsets in these owned buffers, never
// DS bus addresses. The original Model allocator still chooses the offsets.
// Rendering consumes the uploaded bytes separately; upload success is not a
// claim that an original game frame has been rendered.
struct GameGeometry {
    static constexpr uint32_t max_texture_bytes=512*1024;
    static constexpr uint32_t max_palette_bytes=96*1024;
    alignas(32) uint8_t textures[max_texture_bytes]{};
    alignas(32) uint8_t palettes[max_palette_bytes]{};
    uint32_t texture_capacity=0, palette_capacity=0;
    uint32_t texture_generation=0, palette_generation=0;
    uint32_t texture_uploads=0, palette_uploads=0;
    uint32_t texture_bytes=0, palette_bytes=0;
    bool texture_transfer=false, palette_transfer=false;
    bool highlight_shading=false;

    bool set_texture_banks(uint16_t banks);
    bool set_palette_banks(uint16_t banks);
    bool begin(bool palette);
    bool upload(bool palette,const void* source,uint32_t offset,uint32_t size);
    bool end(bool palette);
};
GameGeometry& game_geometry();
}

extern "C" void sm64ds_geometry_set_highlight(unsigned enabled);
