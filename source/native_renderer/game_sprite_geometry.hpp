#pragma once
#include "native_quads.hpp"
namespace sm64ds::render::game {
// Semantic inputs of the original menu sprite producer (c4684/c49bc).
// Pixel extents are chosen by that producer, not decoded from GPU registers.
struct SpriteGeometry {
    unsigned image_width=8,image_height=8,sample_width=8,sample_height=8;
    unsigned repeat_s=1,repeat_t=1;
    std::int16_t offset_s=0,offset_t=0;
    std::array<std::uint16_t,4> colour{{32767,32767,32767,32767}};
    std::uint8_t alpha=31;
    bool direct_quad=false,corner_colours=false;
    const std::uint16_t* tiles=nullptr;
    std::size_t tile_count=0;
};
struct SpriteGeometryResult {
    std::size_t vertices=0,source_quads=0;
    bool collapsed=false;
};
// Local-space vertices only; PICA applies the object/camera matrix. A uniform
// untiled grid collapses to one quad when its signed UV range is continuous.
// Wrapped UV seams, tile order/holes and source fixed-point endpoint rounding
// are preserved. Failure changes neither output nor result.
Error sprite_geometry(const SpriteGeometry&,Vertex*,std::size_t capacity,SpriteGeometryResult&) noexcept;
}
