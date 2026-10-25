#pragma once
#include <cstddef>
#include <cstdint>

namespace sm64ds {
// Native storage for the original display engine state. No DS address is
// dereferenced. VRAM aliases resolve to the same physical bank bytes, so a
// bank reassignment does not copy or lose its contents.
void native_video_reset();
void* native_video_pointer(std::uint32_t address, std::size_t bytes);
std::uint64_t native_video_read(std::uint32_t address, unsigned bytes);
bool native_video_write(std::uint32_t address, std::uint64_t value, unsigned bytes);
bool native_video_write_callback(void*, std::uint32_t address,
                                 std::uint32_t value, unsigned bytes);
bool native_video_copy(std::uint32_t destination, const void* source, std::size_t bytes);
bool native_video_ok();
std::uint32_t native_video_bad_address();
// Derived from the consumed GX decoder plus the complete pending submission;
// returns false and marks a platform failure when the stream cannot be queried.
bool native_video_gx_status(std::uint32_t& status);
} // namespace sm64ds
