#pragma once
#include <cstdint>

namespace sm64ds {
struct NativeSaveStats {
    std::uint32_t reads{}, writes{}, verifies{}, failures{};
    bool created{};
    const char* last_error{};
};
NativeSaveStats native_save_stats();
const char* native_save_path();
} // namespace sm64ds
