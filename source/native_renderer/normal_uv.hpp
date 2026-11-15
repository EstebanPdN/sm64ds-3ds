#pragma once
#include "contracts.hpp"
namespace sm64ds::render {
// Base32 convolution keeps every intermediate exactly representable in f24.
// Each of seven coefficient digits occupies two rows (S and T).
struct NormalUvUniforms { std::array<std::array<float,4>,16> row{}; };
Error pack_normal_uv(const NormalUv&,unsigned sampled_width,unsigned sampled_height,NormalUvUniforms&) noexcept;
}
