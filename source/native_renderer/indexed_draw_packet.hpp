#pragma once
#include <array>
#include <cstdint>
namespace sm64ds::render {
// PICA register IDs are checked against the installed SDK at its ARM seam.
// This is precisely Citro3D1.7.1's GPU_TRIANGLES / unsigned16 sequence:
// restart, range, geometry-stage enable, draw, disable and vertex-cache clear.
struct IndexedDrawPacket {
    enum Register:std::uint32_t {
        index=0x227,vertices=0x228,geometry=0x229,offset=0x22a,
        draw=0x22f,vertex_function=0x231,start=0x245,geometry2=0x253,
        primitive=0x25e,restart=0x25f
    };
    static constexpr std::uint32_t header(Register reg,unsigned mask=15) noexcept {
        return (mask<<16)|reg;
    }
    std::array<std::uint32_t,30> words={
        0x300,header(primitive,2),1,header(restart),
        0,header(index),0,header(vertices),0,header(offset),
        0x100,header(geometry,2),0x100,header(geometry2,2),
        0,header(start,1),1,header(draw),1,header(start,1),
        0,header(geometry,2),0,header(geometry2,2),
        1,header(vertex_function),0,header(primitive,8),0,header(primitive,8)
    };
    // The owning backend has validated the index range and command capacity.
    void range(std::uint32_t index_configuration,std::uint32_t count) noexcept {
        words[4]=index_configuration;words[6]=count;
    }
};
}
