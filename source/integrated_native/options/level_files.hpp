#pragma once
// Files each level reads while it loads (FAT file ids), for the star select
// preload in hal/fs.cpp. The list of a level is the union of its loads, most
// recent load first, kept in sdmc:/3ds/sm64ds/level-files.bin; levels never
// loaded on this console start from the built-in lists (level_files_seed.hpp).
// Portable: host tests use it.
#include <cstdint>
#include <cstring>

namespace level_files {
constexpr unsigned levels = 64, per_level = 160;

struct Seed { uint8_t level; uint8_t count; const uint16_t* ids; };

struct Store {
    char magic[8];
    uint32_t catalog;  // FAT file count: lists of another ROM layout are dropped
    uint16_t count[levels];
    uint16_t ids[levels][per_level];

    void reset(uint32_t catalog_files) {
        std::memcpy(magic, "SMLVLF01", 8);
        catalog = catalog_files;
        std::memset(count, 0, sizeof count);
        std::memset(ids, 0, sizeof ids);
    }
    bool valid(uint32_t catalog_files) const {
        if (std::memcmp(magic, "SMLVLF01", 8) || catalog != catalog_files) return false;
        for (unsigned level = 0; level < levels; ++level) {
            if (count[level] > per_level) return false;
            for (unsigned i = 0; i < count[level]; ++i)
                if (ids[level][i] >= catalog_files) return false;
        }
        return true;
    }
    const uint16_t* list(unsigned level, unsigned& n) const {
        n = level < levels ? count[level] : 0;
        return n ? ids[level] : nullptr;
    }
    // Recorded files first (load order), then the older ones that still fit.
    // Returns true when the stored list changed.
    bool merge(unsigned level, const uint16_t* recorded, unsigned n) {
        if (level >= levels) return false;
        uint16_t merged[per_level];
        unsigned m = 0;
        auto add = [&](uint16_t id) {
            if (id >= catalog || m == per_level) return;
            for (unsigned i = 0; i < m; ++i) if (merged[i] == id) return;
            merged[m++] = id;
        };
        for (unsigned i = 0; i < n; ++i) add(recorded[i]);
        for (unsigned i = 0; i < count[level]; ++i) add(ids[level][i]);
        if (m == count[level] && !std::memcmp(merged, ids[level], m * sizeof *merged)) return false;
        std::memcpy(ids[level], merged, m * sizeof *merged);
        count[level] = uint16_t(m);
        return true;
    }
    // Built-in lists for levels without a recorded one.
    void seed(const Seed* seeds, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            if (seeds[i].level < levels && !count[seeds[i].level]) merge(seeds[i].level, seeds[i].ids, seeds[i].count);
    }
};
}
