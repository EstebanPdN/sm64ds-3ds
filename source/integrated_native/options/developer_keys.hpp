#pragma once
#include <cstdint>
// Developer chords: L+R+A full dump, L+R+B partial dump (no RAM), L+R+X
// delete dumps. L has no game function; while it is held, the chord keys
// never reach the game either. Bits match libctru's KEY_* values.
namespace developer_keys {
constexpr uint32_t A = 1u << 0, B = 1u << 1, R = 1u << 8, L = 1u << 9, X = 1u << 10;
constexpr uint32_t game_keys(uint32_t keys, uint32_t held) {
    if (held & L) keys &= ~(R | A | B | X);
    return keys & ~L;
}
static_assert(game_keys(L | R | A, L | R | A) == 0, "chord never reaches the game");
static_assert(game_keys(R | A, R | A) == (R | A), "R and A keep their game functions");
}
