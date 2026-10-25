#pragma once

#include <cstddef>
#include <cstdint>

namespace sm64ds {

constexpr std::size_t kNativePersonalBytes = 0x50;

// These diagnostics deliberately contain no nickname, birthday or comment.
struct NativePersonalStats {
    std::uint32_t copies{}, failures{};
    std::int32_t service_result{};
    const char* last_error{};
    bool initialization_attempted{}, ready{}, english_fallback{};
};

// Call after native_platform_init and before the original boot. Read CFGU once
// and release this provider's service reference before returning. Repeated calls
// reuse the first outcome, including a failure; no invented DS firmware image.
// The supplied EU ROM has EN/FR/DE/IT/ES resources. Other valid CTR languages use
// English explicitly. Invalid CFGU data/service errors fail initialization.
bool native_personal_initialize();
NativePersonalStats native_personal_stats();

// Copies the exact SDK export layout: language, colour, birthday, UTF-16 name
// (10 units), name length, UTF-16 comment (26 units), comment length: 80 bytes.
// Name/birthday/language derive from CFGU. DS-only colour=0 (grey) and comment
// empty are explicit compatibility defaults, not a claimed DS profile read.
// Destination must be halfword aligned and hold >=80 bytes. Failure performs
// no destination stores and marks the native platform failed.
bool native_personal_copy(void* destination, std::size_t capacity);

} // namespace sm64ds

// Original ARM ABI; the caller supplies one halfword-aligned >=80-byte output.
// Invalid use terminates through the existing native missing-service boundary.
extern "C" void func_02059eb0(unsigned char* destination);
