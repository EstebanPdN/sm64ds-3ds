#pragma once
#include "contracts.hpp"

namespace sm64ds::render {
struct ModelBindings;
struct ImageBinding;
const ImageBinding* sprite_image(const ModelBindings&,std::uint32_t texture,std::uint32_t palette) noexcept;
// Four local-space vertices per quad, in authored perimeter order. Matrices
// remain GPU uniforms. Source producer callbacks copy only changing positions,
// UVs and colours; they never transform vertices or interpret command streams.
struct QuadState {
    enum class Bank : std::uint8_t { model,images };
    static constexpr std::uint16_t no_image=0xffffu;
    Material material{}; // texture handle must be empty until resource resolve
    Matrix object_to_clip{};
    std::uint16_t image=no_image;
    // Optional semantic pixel policy.255 denotes ordinary native geometry.
    // Original DS polygon-ID/fog composition is retained until its native
    // stencil/fog policy is implemented; the scene consumer refuses it.
    std::uint8_t polygon_id=255;
    bool fog=false;
    Bank bank=Bank::model;
};
constexpr std::uint32_t untextured_quads=0xffffffffu;
constexpr std::size_t max_frame_quads=4096;
bool valid(const QuadState&) noexcept;
bool same(const QuadState&,const QuadState&) noexcept;
// The UV domain is computed only for a caller that takes it.
Error validate_quads(const Vertex*,std::size_t vertices,UvDomain* domain=nullptr) noexcept;
}
