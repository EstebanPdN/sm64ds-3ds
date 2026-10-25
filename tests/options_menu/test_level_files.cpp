// Star select preload: per-level file lists (merge order, cap, validation).
#include "level_files.hpp"
#include <cassert>
#include <cstdio>
using namespace level_files;

int main() {
    static Store store;
    store.reset(2000);
    assert(store.valid(2000) && !store.valid(1999));
    // First load of a level stores its files in load order.
    const uint16_t first[] = {10, 11, 12, 10};
    assert(store.merge(6, first, 4));
    unsigned n = 0;
    const uint16_t* ids = store.list(6, n);
    assert(n == 3 && ids[0] == 10 && ids[1] == 11 && ids[2] == 12);
    // Same files again: nothing to save.
    assert(!store.merge(6, first, 3));
    // Another star loads other objects: newest first, older ones kept.
    const uint16_t second[] = {20, 11};
    assert(store.merge(6, second, 2));
    ids = store.list(6, n);
    assert(n == 4 && ids[0] == 20 && ids[1] == 11 && ids[2] == 10 && ids[3] == 12);
    // Ids outside this ROM's catalog are never stored.
    const uint16_t bad[] = {1999, 2000, 40000};
    store.merge(7, bad, 3);
    ids = store.list(7, n);
    assert(n == 1 && ids[0] == 1999);
    // The cap keeps the most recent load whole.
    static uint16_t many[per_level + 40];
    for (unsigned i = 0; i < per_level + 40; ++i) many[i] = uint16_t(100 + i);
    assert(store.merge(8, many, per_level + 40));
    ids = store.list(8, n);
    assert(n == per_level && ids[0] == 100 && ids[per_level - 1] == 100 + per_level - 1);
    // Seeds only fill levels without a recorded list.
    const uint16_t seed6[] = {99}, seed9[] = {30, 31};
    const Seed seeds[] = {{6, 1, seed6}, {9, 2, seed9}};
    store.seed(seeds, 2);
    store.list(6, n);
    assert(n == 4);
    ids = store.list(9, n);
    assert(n == 2 && ids[0] == 30);
    // Out-of-range levels are ignored; corrupted counts are rejected.
    assert(!store.merge(levels, first, 1) && !store.list(levels, n) && n == 0);
    store.count[3] = per_level + 1;
    assert(!store.valid(2000));
    std::puts("PASS level files: load-order union, catalog bounds, cap, seeds, validation");
}
