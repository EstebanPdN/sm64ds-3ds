/* Exact DS divider and square-root results without slow integer loops.
 *
 * ARMv6K has no integer divide instruction: libgcc's 64-bit divide is a
 * shift-subtract loop of hundreds of cycles, and the DS square-root unit was
 * emulated by a 32-step restoring loop in 64-bit arithmetic. The ARM11 VFP
 * divides and takes square roots of doubles in about 30 cycles. These helpers
 * return exactly what C's truncating division and floor(sqrt()) return:
 *  - a double holds every integer below 2^53, and the correctly rounded
 *    quotient of two such integers truncates to the exact quotient;
 *  - larger operands (below 2^61) get an estimate that the exact 64-bit
 *    remainder corrects, as NormalizeVec3's 2^56 / |v|^2 needs.
 * Technique after sm64ds-enhanced (MIT) patches 0018, 0037 and 0052.
 * C99 and C++: port/ntr/io.cpp, port/hal/cstd_div.c and the host test use it.
 */
#ifndef SM64DS_DS_MATH_UNIT_H
#define SM64DS_DS_MATH_UNIT_H
#include <stdint.h>
#include <math.h>

/* VFPv2 converts only 32-bit integers: build the double from both halves.
 * Exact below 2^53; above, the sum rounds (callers correct the result). */
static inline double sm64ds_ds_math_double(int64_t v)
{
    return (double)(int32_t)(v >> 32) * 4294967296.0 + (double)(uint32_t)v;
}

/* C's n / d and n % d. Returns 0 (outputs untouched) when the operands are
 * outside the exact range: d == 0, |n| or |d| >= 2^61, or a quotient whose
 * double reaches 2^53 (callers then use the library divide). */
static inline int sm64ds_ds_math_divide(int64_t n, int64_t d, int64_t *quot, int64_t *rem)
{
    const int64_t limit = (int64_t)1 << 61;
    if (d == 0 || n <= -limit || n >= limit || d <= -limit || d >= limit)
        return 0;
    const double x = sm64ds_ds_math_double(n) / sm64ds_ds_math_double(d);
    if (!(x > -9007199254740992.0 && x < 9007199254740992.0))
        return 0;
    int64_t q = (x > -2147483648.0 && x < 2147483648.0) ? (int64_t)(int32_t)x : (int64_t)x;
    /* |q * d| <= |n| + 2|d| < 2^63 for these bounds. */
    int64_t r = n - q * d;
    const int64_t ad = d < 0 ? -d : d, s = d < 0 ? -1 : 1;
    if (n >= 0) {
        while (r < 0) { q -= s; r += ad; }
        while (r >= ad) { q += s; r -= ad; }
    } else {
        while (r > 0) { q += s; r -= ad; }
        while (r <= -ad) { q -= s; r += ad; }
    }
    *quot = q;
    *rem = r;
    return 1;
}

/* floor(sqrt(v)) for every 64-bit v: the DS square-root unit's result. */
static inline uint32_t sm64ds_ds_math_isqrt64(uint64_t v)
{
    if (!v)
        return 0;
    const double f = (double)(uint32_t)(v >> 32) * 4294967296.0 + (double)(uint32_t)v;
    const double e = sqrt(f);
    uint32_t r = e >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)e;
    while ((uint64_t)r * r > v)
        --r;
    while (r < 0xFFFFFFFFu && ((uint64_t)r + 1) * ((uint64_t)r + 1) <= v)
        ++r;
    return r;
}

/* cstd::fdiv: (int)(((long long)a << 12) / b), b != 0. */
static inline int32_t sm64ds_ds_math_fdiv(int32_t a, int32_t b)
{
    const int64_t n = (int64_t)a * 4096;
    if (n == (int32_t)n && !(n == INT32_MIN && b == -1))
        return (int32_t)n / b;
    int64_t q, r;
    if (sm64ds_ds_math_divide(n, b, &q, &r))
        return (int32_t)q;
    return (int32_t)(n / b);
}
#endif
