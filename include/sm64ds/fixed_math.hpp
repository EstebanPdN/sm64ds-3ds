#pragma once
#include <cstdint>

namespace sm64ds::math {
// Results of the game's arithmetic primitives, without DS register accesses.
int32_t divide(int32_t numerator, int32_t denominator);
int32_t remainder(int32_t numerator, int32_t denominator);
int64_t divide_q32(int32_t numerator, int32_t denominator);
int32_t round_q32_to_q12(int64_t quotient);
int32_t divide_q12(int32_t numerator, int32_t denominator);
uint32_t integer_sqrt(uint64_t value);
int32_t game_sqrt(uint64_t value);
int32_t distance3(const int32_t* a, const int32_t* b);
// Reproduces the original divide/sqrt unit normalization, including rounding,
// wrapping intermediate registers and in-place operation.
void normalize3(const int32_t* source, int32_t* destination);
int32_t audio_distance3(const int32_t* position);
}
