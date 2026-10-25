#pragma once
// Old / New 3DS profiles: the one place that turns the console model and the
// memory the system granted into resource budgets.
//   Old 3DS: 80 MB application memory (exheader SystemMode), 268 MHz, two
//            cores; the presentation worker shares core 1 with the system.
//   New 3DS: 124 MB (SystemModeExt), 804 MHz with L2 cache, the worker on
//            core 2 (CanAccessCore2), larger caches.
// A New 3DS that runs in a smaller mode (the 3DSX through the Homebrew
// Launcher) keeps the Old memory budgets, so neither profile can ask for
// memory its mode does not have. Portable: host tests use it.
#include <cstddef>
#include <cstdint>

namespace console_profile {
struct Profile {
    bool new_model;
    bool large_memory;          // application region of the 124 MB mode
    uint32_t app_memory;        // bytes the system granted the application
    std::size_t gpu_textures;   // GPU texture cache (linear heap)
    std::size_t decoded_textures; // decoded DS textures (heap)
    std::size_t level_preload;  // star select preload (heap)
    int worker_core;            // presentation worker core (fallback: core 1)
};

constexpr uint32_t large_memory_threshold = 100u << 20;

constexpr Profile make(bool new_model, uint32_t app_memory) {
    const bool large = new_model && app_memory >= large_memory_threshold;
    return {new_model, large, app_memory,
            large ? std::size_t(12) << 20 : std::size_t(8) << 20,
            large ? std::size_t(16) << 20 : std::size_t(8) << 20,
            large ? std::size_t(8) << 20 : std::size_t(3) << 20,
            new_model ? 2 : 1};
}

static_assert(make(false, 80u << 20).gpu_textures == 8u << 20, "Old keeps the tested budgets");
static_assert(make(false, 124u << 20).decoded_textures == 8u << 20, "only a New 3DS uses the large budgets");
static_assert(make(true, 80u << 20).level_preload == 3u << 20 && make(true, 80u << 20).worker_core == 2,
              "New 3DS in a small mode: Old memory budgets, its own cores");
static_assert(make(true, 124u << 20).large_memory && make(true, 124u << 20).decoded_textures == 16u << 20,
              "New 3DS 124 MB mode");

// Detected once on the console (console_profile.cpp).
const Profile& current();
}
