#include "sm64ds/game_draw.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>

// Adapted from the pinned decomp's G3i::PerspectiveW_ and
// cstd::reciprocal_async. These are the same fixed-point operations and matrix
// coefficients; only the DS divider/FIFO service boundaries change. The
// existing native game_math object owns the ONE pending divider result, shared
// by fdiv_async, reciprocal_async, fdiv_result and ldiv_result.
extern "C" {
void _ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE(int numerator, int denominator);
int _ZN4cstd4fdivEii(int numerator, int denominator);
int _ZN4cstd11fdiv_resultEv();
std::int64_t _ZN4cstd11ldiv_resultEv();
}

namespace {
std::int32_t signed32(std::uint32_t bits) {
    std::int32_t value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
std::int64_t signed64(std::uint64_t bits) {
    std::int64_t value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
std::int32_t rounded_product_high(std::int32_t value, std::int64_t scale) {
    const auto product = std::uint64_t(std::int64_t(value)) * std::uint64_t(scale);
    return signed32(std::uint32_t((product + 0x80000000ULL) >> 32));
}
}

extern "C" void _ZN4cstd16reciprocal_asyncE5Fix12IiE(int value) {
    _ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE(0x1000, value);
}

extern "C" void _ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(
    int sin_fov, int cos_fov, int aspect, int near_plane, int far_plane,
    int scale_w, int emit, int* matrix) {
    auto vertical = _ZN4cstd4fdivEii(cos_fov, sin_fov);
    if (scale_w != 0x1000) {
        // This product is a 32-bit intermediate in the recovered source.
        // Preserve ARM register wrap before signed division truncates to zero.
        vertical = signed32(std::uint32_t(vertical) * std::uint32_t(scale_w)) / 0x1000;
    }
    _ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE(vertical, aspect);
    if (emit) sm64ds_draw_port(0x10, 0); // MTX_MODE: projection

    if (matrix) {
        for (const unsigned slot : {1u,2u,3u,4u,6u,7u,8u,9u,12u,13u,15u})
            matrix[slot] = 0;
        matrix[11] = signed32(0u - std::uint32_t(scale_w));
    }
    const auto horizontal = _ZN4cstd11fdiv_resultEv();
    const auto depth_span = signed32(std::uint32_t(near_plane) - std::uint32_t(far_plane));
    _ZN4cstd16reciprocal_asyncE5Fix12IiE(depth_span);
    if (emit) {
        const std::int32_t prefix[10] = {horizontal,0,0,0,0,vertical,0,0,0,0};
        for (const auto value : prefix) sm64ds_draw_port(0x16, std::uint32_t(value));
    }
    if (matrix) { matrix[0] = horizontal; matrix[5] = vertical; }

    auto inverse_depth = _ZN4cstd11ldiv_resultEv();
    if (scale_w != 0x1000) {
        const auto bits = std::uint64_t(inverse_depth) * std::uint64_t(std::int64_t(scale_w));
        inverse_depth = signed64(bits) / 0x1000;
    }
    const auto depth_sum = signed32(std::uint32_t(far_plane) + std::uint32_t(near_plane));
    const auto depth_z = rounded_product_high(depth_sum, inverse_depth);
    const auto twice_near = signed32(std::uint32_t(near_plane) << 1);
    const auto depth_product = std::uint64_t(std::int64_t(twice_near)) *
                               std::uint64_t(std::int64_t(far_plane)) + 0x800;
    const auto depth_scaled = signed32(std::uint32_t(depth_product >> 12));
    const auto depth_w = rounded_product_high(depth_scaled, inverse_depth);
    if (emit) {
        const std::int32_t suffix[6] = {
            depth_z,signed32(0u-std::uint32_t(scale_w)),0,0,depth_w,0};
        for (const auto value : suffix) sm64ds_draw_port(0x16, std::uint32_t(value));
    }
    if (matrix) { matrix[10] = depth_z; matrix[14] = depth_w; }
}
