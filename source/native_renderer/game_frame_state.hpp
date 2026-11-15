#pragma once
#include "contracts.hpp"
namespace sm64ds::render {
struct SceneClear {
    std::uint32_t rgba=0x182028ffu;
    std::uint8_t polygon_id=63;
    bool fog_mark=false;
};
namespace game {
// Original typed clear-colour arguments -> owned native clear state. Only the
// established far-depth contract is admitted; other depths need a raster test.
Error capture_clear(std::uint16_t rgb,int alpha,int depth,int polygon_id,bool fog,SceneClear&) noexcept;
}
}
