#pragma once

#include <cstddef>

class Heap;

namespace sm64ds {

// The original expanding allocator owns this arena for the process lifetime.
// Call before the original game's static initializers or resource loaders.
// Existing heap globals must still be null. The buffer must be 32-byte aligned
// and remain valid; the in-place form also permits deterministic CPU testing.
bool initialize_game_root_heap_in_place(void* arena, std::size_t bytes);
bool initialize_game_root_heap(std::size_t bytes);
Heap* native_game_root_heap();
// Startup verification for the development CIA: checks an allocation round trip.
bool verify_game_root_heap();

} // namespace sm64ds
