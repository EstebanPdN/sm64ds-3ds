#pragma once
#include <cstdint>

namespace sm64ds {
// Sources are verified, bounded ROM data slices, never executable code. Native
// destinations belong to the linker. The caller resolves every replacement to
// a supported native function/data address before submitting this transaction.
// This primitive does not resolve symbols or invent providers for missing ones.
struct NativeDataSection {
    uint8_t* destination;
    const uint8_t* source;
    uint32_t size;
    uint32_t zero_fill; // exactly 0 (copy) or 1 (original BSS)
};
struct NativeDataFixup {
    uint32_t section;
    uint32_t offset;
    uint32_t expected;
    uint32_t replacement;
};
enum class NativeDataResult : uint32_t {
    success, invalid_table, invalid_section, overlapping_storage,
    invalid_fixup, unexpected_source_word,
};
// Fixups must be sorted by (section, offset), with no overlapping words.
// All checks precede writes: any rejected transaction leaves every destination
// unchanged. No allocation, DS service, C++ exception, or static initializer.
extern "C" NativeDataResult sm64ds_restore_native_data(
    const NativeDataSection* sections, uint32_t section_count,
    const NativeDataFixup* fixups, uint32_t fixup_count);
}
