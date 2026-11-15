#pragma once
#include "lighting.hpp"
namespace sm64ds::render::game_hooks {
struct LightingStatistics {std::uint64_t views=0,source_calls=0,updates=0,failures=0;};
void lighting_reset() noexcept;
void lighting_view(const std::int32_t* affine12) noexcept;
Error capture_lighting(unsigned mask,bool needs_table,LightSet&) noexcept;
const LightingStatistics& lighting_statistics() noexcept;
std::uint8_t lighting_known() noexcept;
}
