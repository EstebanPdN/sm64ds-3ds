#include "sm64ds/game_primitives.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

int main() {
    // Fx12 cardinal rotations plus arbitrary and wraparound ARM register values.
    const std::array<std::int32_t, 7> inputs{{0, 4096, -4096, 2896, -1337,
        std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()}};
    for (auto sine : inputs) for (auto cosine : inputs) {
        std::array<std::int32_t, 11> guarded{};
        guarded.front() = guarded.back() = 0x13572468;
        Matrix3x3_SetRotationZ(guarded.data() + 1, sine, cosine);
        assert(guarded.front() == 0x13572468 && guarded.back() == 0x13572468);
        assert(guarded[1] == cosine && guarded[2] == sine && guarded[5] == cosine);
        std::uint32_t negative;
        std::memcpy(&negative, &guarded[4], sizeof(negative));
        assert(negative + static_cast<std::uint32_t>(sine) == 0u);
        for (int position : {3, 6, 7, 8}) assert(guarded[position] == 0);
        assert(guarded[9] == 4096);
    }
}
