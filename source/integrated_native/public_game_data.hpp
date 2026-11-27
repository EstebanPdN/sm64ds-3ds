#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <dsp_bootstrap.hpp>

namespace public_game_data {
struct Span {
    uint32_t offset;
    uint32_t size;
};

enum class Status { ready, missing, layout, invalid, memory };

inline Status load(const char* path, uint8_t* destination, std::size_t capacity,
                   const Span* spans, std::size_t count, std::size_t size,
                   const std::array<uint8_t, 32>& expected) {
    if (!count || !size || size > capacity) return Status::layout;
    std::size_t previous_end = 0;
    std::size_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto [offset, length] = spans[i];
        if (!length || offset < previous_end || offset > capacity ||
            length > capacity - offset || length > size - total) return Status::layout;
        previous_end = std::size_t(offset) + length;
        total += length;
    }
    if (total != size) return Status::layout;

    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    if (!file) return Status::missing;
    // The retail operator new is unavailable until smoke_main starts.
    std::unique_ptr<uint8_t, decltype(&std::free)> payload(
        static_cast<uint8_t*>(std::malloc(size)), std::free);
    if (!payload) return Status::memory;
    if (std::fread(payload.get(), 1, size, file.get()) != size ||
        std::fgetc(file.get()) != EOF || std::ferror(file.get()) ||
        dsp_bootstrap::sha256(payload.get(), size) != expected) return Status::invalid;

    std::size_t cursor = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto [offset, length] = spans[i];
        std::memcpy(destination + offset, payload.get() + cursor, length);
        cursor += length;
    }
    return Status::ready;
}

#ifdef __3DS__
inline constexpr std::size_t MaxSpans = 8192;
struct Layout {
    uint32_t size;
    uint32_t count;
    std::array<uint8_t, 32> sha256;
    Span spans[MaxSpans];
};
static_assert(sizeof(Layout) == 40 + 8 * MaxSpans);

extern "C" {
extern uint8_t sm64ds_external_begin[], sm64ds_external_end[];
extern const Layout sm64ds_external_layout;
}

inline bool prepare() {
    constexpr const char* path = "sdmc:/3ds/sm64ds/build142-data.bin";
    const auto& layout = sm64ds_external_layout;
    const auto capacity = reinterpret_cast<uintptr_t>(sm64ds_external_end) -
                          reinterpret_cast<uintptr_t>(sm64ds_external_begin);
    const auto status = layout.count <= MaxSpans
        ? load(path, sm64ds_external_begin, capacity, layout.spans, layout.count,
               layout.size, layout.sha256)
        : Status::layout;
    if (status == Status::ready) return true;
    const char* detail = "Invalid BUILD 142 data layout.";
    if (status == Status::missing) detail = "The BUILD 142 data file is missing.";
    if (status == Status::invalid) detail = "The BUILD 142 data file is damaged or incompatible.";
    if (status == Status::memory) detail = "Insufficient memory to verify BUILD 142 data.";
    std::fprintf(stderr, "%s\n", detail);
    consoleInit(GFX_BOTTOM, nullptr);
    std::printf("%s\n\nPrepare the SD files on your computer.\n\n"
                "/3ds/sm64ds/build142-data.bin\n\nPress A to exit.\n", detail);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_A) break;
        gspWaitForVBlank();
    }
    return false;
}
#endif
}
