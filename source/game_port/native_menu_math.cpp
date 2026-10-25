// Native arithmetic leaves from pinned OV007. Original menu geometry and
// particle behavior are retained; DS divider/sqrt/IME accesses are replaced
// with integer calculations whose widths and rounding match the ARM9 leaves.
#include "sm64ds/fixed_math.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

extern "C" {
[[noreturn]] void sm64ds_native_missing(const char* operation);
void func_02053320(std::int32_t scale, const std::int32_t* vector,
                   const std::int32_t* addend, std::int32_t* output);
std::int32_t DotVec3(const std::int32_t* first, const std::int32_t* second);
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
std::int16_t signed16(std::uint16_t bits) {
    std::int16_t value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
std::int32_t subtract(std::int32_t a, std::int32_t b) {
    return signed32(std::uint32_t(a) - std::uint32_t(b));
}
std::int32_t negate(std::int32_t value) {
    return signed32(0u - std::uint32_t(value));
}
std::uint64_t square(std::int32_t value) {
    return std::uint64_t(std::int64_t(value) * value);
}
std::uint64_t distance_squared(const std::int32_t* first,
                               const std::int32_t* second) {
    std::uint64_t result = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        result += square(subtract(first[axis], second[axis]));
    return result;
}
std::int32_t product_q12(std::int32_t first, std::int32_t second,
                         std::uint64_t rounding = 0) {
    const auto bits = std::uint64_t(std::int64_t(first) * second) + rounding;
    // Only bits 12..43 survive the original ASR/ORR and 32-bit return.
    return signed32(std::uint32_t(bits >> 12));
}
}  // namespace

extern "C" int func_ov007_020c30ac(const std::int32_t* points, int count,
                                   const std::int32_t* target, int max_radius,
                                   std::int32_t* out_distance) {
    // The ROM leaves bestIdx uninitialized for an empty search and returns
    // the caller's incoming r6. That path has no portable result contract.
    if (count <= 0)
        sm64ds_native_missing("OV007 nearest-3D search requires at least one point");
    std::int64_t best_squared = std::numeric_limits<std::int64_t>::max();
    int best = -1;
    for (int i = 0; i < count; ++i, points += 3) {
        const auto squared = signed64(distance_squared(points, target));
        if (squared < best_squared) { best = i; best_squared = squared; }
    }
    if (max_radius >= 0 && std::int64_t(max_radius) * max_radius < best_squared)
        return -1;
    if (out_distance)
        *out_distance = sm64ds::math::game_sqrt(std::uint64_t(best_squared));
    return best;
}

extern "C" int func_ov007_020c31e0(const std::int32_t* x, const std::int32_t* y,
                                   int count, int center_x, int center_y,
                                   int max_radius, std::int32_t* out_distance) {
    std::int64_t best_squared = std::numeric_limits<std::int64_t>::max();
    int best = -1;
    for (int i = 0; i < count; ++i) {
        const auto dx = subtract(center_x, x[i]);
        const auto dy = subtract(center_y, y[i]);
        const auto squared = signed64(square(dx) + square(dy));
        if (squared < best_squared) { best = i; best_squared = squared; }
    }
    if (max_radius >= 0 && std::int64_t(max_radius) * max_radius < best_squared)
        return -1;
    if (out_distance)
        *out_distance = sm64ds::math::game_sqrt(std::uint64_t(best_squared));
    return best;
}

extern "C" void func_ov007_020c368c(std::int16_t* vectors, int count, int clamp) {
    // As in the ROM, count <= 1 still processes the first vector. The original
    // pipeline overlaps consecutive independent computations; this context
    // has no interrupt-side arithmetic unit to preserve between those steps.
    const int vector_count = count > 1 ? count : 1;
    for (int i = 0; i < vector_count; ++i, vectors += 3) {
        const std::int32_t input[3] = {vectors[0], vectors[1], vectors[2]};
        // ARM9 MUL/MLA first accumulate at 32 bits, then ASR sign-extends the
        // wrapped sum to the 64-bit denominator/radicand. NormalizeVec3's
        // 64-bit square sum is consequently different for extreme s16 data.
        std::uint32_t square_bits = 0;
        for (auto value : input) square_bits += std::uint32_t(square(value));
        const auto squared = std::int64_t(signed32(square_bits));
        const auto reciprocal = squared ? (std::int64_t(1) << 56) / squared : -1;
        const auto root = signed32(sm64ds::math::integer_sqrt(std::uint64_t(squared) << 2));
        const auto scale = std::uint64_t(reciprocal) * std::uint64_t(std::int64_t(root));
        for (unsigned axis = 0; axis < 3; ++axis) {
            const auto bits = scale * std::uint64_t(std::int64_t(input[axis])) +
                              (std::uint64_t(1) << 44);
            auto output = signed16(std::uint16_t(bits >> 45));
            // Positive saturation happens AFTER narrowing to the s16 stored
            // component. The original imposes no negative saturation.
            if (clamp && output > 0xfff) output = 0xfff;
            vectors[axis] = output;
        }
    }
}

namespace {
// Original ARM32 layout: position pointer at 0, velocity at 4, accumulated
// force at 16. Both particle pointers and rest length are stored in the link.
struct MenuParticle {
    const std::int32_t* position;
    std::int32_t velocity[3];
    std::int32_t force[3];
};
struct MenuSpring {
    MenuParticle* first;
    MenuParticle* second;
    std::int32_t rest_length;
};
#if defined(__arm__)
static_assert(sizeof(MenuParticle) == 28 && sizeof(MenuSpring) == 12,
              "OV007 spring ABI requires 32-bit native pointers");
#endif
}  // namespace

extern "C" void func_ov007_020c7d60(void* object, int stiffness, int friction) {
    auto& spring = *static_cast<MenuSpring*>(object);
    std::int32_t direction[3];
    for (unsigned axis = 0; axis < 3; ++axis)
        direction[axis] = subtract(spring.first->position[axis], spring.second->position[axis]);
    const auto squared = distance_squared(spring.first->position, spring.second->position);
    const auto root = sm64ds::math::integer_sqrt(squared << 2);
    // Unlike the nearest-point leaves, the ROM implements signed /2 here:
    // negative roots are truncated toward zero rather than ASR's floor.
    const auto length = signed32(root + 1u) / 2;
    const auto displacement = subtract(length, spring.rest_length);
    const auto restoring = negate(product_q12(displacement, stiffness, 0x800));
    sm64ds::math::normalize3(direction, direction);

    // These pure-memory leaves remain the adopted game implementations.
    func_02053320(restoring, direction, spring.first->force, spring.first->force);
    func_02053320(negate(restoring), direction, spring.second->force, spring.second->force);
    std::int32_t first_velocity[3], second_velocity[3];
    const auto first_dot = DotVec3(direction, spring.first->velocity);
    const auto second_dot = DotVec3(direction, spring.second->velocity);
    for (unsigned axis = 0; axis < 3; ++axis) {
        first_velocity[axis] = product_q12(first_dot, direction[axis]);
        second_velocity[axis] = product_q12(second_dot, direction[axis]);
    }
    const auto drag = negate(friction);
    func_02053320(drag, first_velocity, spring.first->force, spring.first->force);
    func_02053320(drag, second_velocity, spring.second->force, spring.second->force);
}
