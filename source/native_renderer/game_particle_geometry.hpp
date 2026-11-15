#pragma once
#include "game_model_pose.hpp"
#include "native_quads.hpp"

namespace sm64ds::render::game {
// Semantic billboard input. The original simulation supplies the centroid
// already in view space; the GPU applies this local pose and native projection.
// No CPU per-vertex transform, source texture fetch or command buffer is used.
struct ParticleBillboard {
    std::array<std::int32_t,3> position{};
    std::int32_t scale=4096;
    std::int16_t multiplier=4096,aspect=4096,sine=0,cosine=4096;
    std::uint16_t colour=32767;
    std::uint8_t alpha=31,repeat_s=0,repeat_t=0;
    unsigned image_width=8,image_height=8;
    std::uint16_t sample_width=8,sample_height=8;
};
struct ParticleBillboardGeometry {
    Affine object=unit_affine();
    std::array<Vertex,4> vertex{};
};
// Transactional output. Source fixed-point matrix rounding and signed16 UV
// endpoint wrapping happen once per billboard, never once per vertex/texel.
Error particle_billboard(const ParticleBillboard&,ParticleBillboardGeometry&) noexcept;
}
