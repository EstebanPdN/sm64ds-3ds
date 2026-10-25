#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace mesh_uniform {
// Only valid inside one unified draw scope. The palette is immutable there;
// CPU clip draws and eye transforms do not write object[] or texdims.
// Compare source values, never frame pointers or recycled matrix indices.
class Cache {
    std::array<std::array<std::int32_t, 16>, 16> matrices_{};
    std::uint16_t valid_ = 0;
    int width_ = 0, height_ = 0;
    bool dimensions_valid_ = false;
public:
    std::uint64_t matrix_requests = 0, matrix_hits = 0;
    std::uint64_t dimension_requests = 0, dimension_hits = 0;
    void invalidate() { valid_ = 0; dimensions_valid_ = false; }
    bool matrix(unsigned slot, const std::int32_t* fixed) {
        ++matrix_requests;
        if (slot >= matrices_.size()) return true;
        const auto bit = std::uint16_t(1u << slot);
        auto& saved = matrices_[slot];
        if ((valid_ & bit) && std::memcmp(saved.data(), fixed, sizeof(saved)) == 0) {
            ++matrix_hits;
            return false;
        }
        std::memcpy(saved.data(), fixed, sizeof(saved));
        valid_ |= bit;
        return true;
    }
    bool dimensions(int width, int height) {
        ++dimension_requests;
        if (dimensions_valid_ && width_ == width && height_ == height) {
            ++dimension_hits;
            return false;
        }
        width_ = width; height_ = height; dimensions_valid_ = true;
        return true;
    }
};
}
