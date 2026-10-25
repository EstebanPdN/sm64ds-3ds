#pragma once
#include <cstdint>

// ABI of the recovered ARM SDK entry points. All pointers must refer to real
// accessible memory; conversion of DS VRAM/register addresses belongs to the
// renderer/platform integration, not these CPU routines.
extern "C" {
// Copies exactly size bytes. Like the original halfword/word implementation,
// unaligned edge reads require the containing halfwords to be accessible.
// Source and destination must not overlap, unless they are identical.
void CpuCopy8(const void* source, void* destination, std::uint32_t size);

// Original LZ10 halfword-store entry point: source starts at its four-byte
// compressed header and destination is halfword aligned. Source/destination
// capacities and validity must be checked by the resource loader first.
// Preserves original pending-byte and token semantics, including not flushing
// an unmatched final byte. This is not a bounded parser for untrusted input.
void DecompressLZ16(const void* source, void* destination);

// Original 4x4 identity constructor (16 word-aligned fx32 values): entries
// 0, 5, 10 and 15 are 4096, all others zero. Writes exactly 64 bytes.
void func_02052ec8(std::int32_t* matrix);

// Nine fx32 values in the original packed matrix order, 4096 representing one.
void Matrix3x3_SetRotationZ(std::int32_t* matrix, std::int32_t sine, std::int32_t cosine);
// Original packed 3x3 X/Y constructors, including negation modulo 2^32.
void func_02052550(std::int32_t* matrix, std::int32_t sine, std::int32_t cosine);
void func_0205256c(std::int32_t* matrix, std::int32_t sine, std::int32_t cosine);
// Original packed 4x3 scale constructor; translation is zero. Writes 48 bytes.
void func_020527e8(std::int32_t* matrix, std::int32_t x, std::int32_t y, std::int32_t z);
// Original packed 4x3 X/Y/Z rotation constructors (translation is cleared).
void func_02052800(std::int32_t* matrix, std::int32_t sine, std::int32_t cosine);
void func_02052820(std::int32_t* matrix, std::int32_t sine, std::int32_t cosine);
void func_0205283c(std::int32_t* matrix, std::int32_t sine, std::int32_t cosine);
}
