#pragma once
#include "contracts.hpp"

namespace sm64ds::render {
struct FixedUvUniforms { std::array<std::array<float,4>,7> row{}; };
// Load/bind-time decomposition. Intermediate shader integers fit PICA's
// significand; large original coefficients never travel as imprecise floats.
Error pack_fixed_uv(const FixedUv&,unsigned sampled_width,unsigned sampled_height,FixedUvUniforms&) noexcept;
}
