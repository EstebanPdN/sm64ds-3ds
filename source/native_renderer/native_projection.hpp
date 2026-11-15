#pragma once
#include "contracts.hpp"
namespace sm64ds::render {
// Original CPU projection helper result, column-major signed20.12. This is a
// semantic camera matrix supplied by its producer, never a GX stack readback.
// Adapt clip [-w,+w] -> PICA[-w,0], rotate to the portrait target and preserve
// default4:3 content with horizontal_scale=.8 on the400x240 screen.
Error native_projection(const std::int32_t* original16,float horizontal_scale,Matrix&) noexcept;
}
