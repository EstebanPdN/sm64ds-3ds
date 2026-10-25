#pragma once

#include <cstddef>
#include <cstdint>

namespace sm64ds {

enum class NativeCaptureSource { composited, three_d_only, source_b, blend };

// Call at the engine-A line-0 boundary. The same request is completed once at
// its line-192 boundary, after a real image from this rendering frame exists.
// A disabled or non-LCDC destination never needs a fabricated input image.
struct NativeCaptureRequest {
    std::uint32_t control{};
    std::uint64_t frame_serial{};
    std::uint8_t destination_control{};
    bool destination_lcdc{};

    bool enabled() const { return (control & 0x80000000u) != 0; }
    unsigned bank() const { return (control >> 16) & 3; }
    unsigned write_offset() const { return ((control >> 18) & 3) * 0x8000u; }
    unsigned width() const { return ((control >> 20) & 3) ? 256 : 128; }
    unsigned height() const {
        constexpr unsigned heights[] = {128,64,128,192};
        return heights[(control >> 20) & 3];
    }
    NativeCaptureSource source() const {
        const auto mode = (control >> 29) & 3;
        return mode == 0 ? ((control & 0x01000000u) ? NativeCaptureSource::three_d_only
                                                                : NativeCaptureSource::composited)
                         : mode == 1 ? NativeCaptureSource::source_b : NativeCaptureSource::blend;
    }
    bool needs_composited() const {
        return enabled() && destination_lcdc && source() == NativeCaptureSource::composited;
    }
    bool needs_3d_only() const {
        return enabled() && destination_lcdc && source() == NativeCaptureSource::three_d_only;
    }
};

// Bytes are r,g,b,a in original 256x192 DS coordinates, top row first. These
// are completed engine-A images before master brightness/display-mode select,
// never a 400x240 presentation or an underlay pretending to contain 3D.
// A zero serial is unavailable, even if an allocated zero-filled buffer exists.
// The source buffers must not overlap the destination's physical VRAM bank.
struct NativeCaptureImage {
    const std::uint8_t* rgba8{};
    std::size_t bytes{};
    std::size_t row_stride_bytes = 256 * 4;
    std::uint64_t rendered_frame{};
};
struct NativeCaptureSources {
    NativeCaptureImage composited;
    NativeCaptureImage three_d_only;
};

enum class NativeCaptureStatus { idle, captured, no_lcdc_destination, failed };
enum class NativeCaptureError {
    none, unsupported_source, unavailable_image, invalid_image,
    frame_mismatch, configuration_changed, unavailable_video
};
struct NativeCaptureResult {
    NativeCaptureStatus status = NativeCaptureStatus::idle;
    NativeCaptureError error = NativeCaptureError::none;
    NativeCaptureSource source = NativeCaptureSource::composited;
    std::uint64_t rendered_frame{};
    std::uint32_t bytes_written{};
    unsigned bank{}, write_offset{}, width{}, height{};
    bool okay() const { return status != NativeCaptureStatus::failed; }
};

NativeCaptureRequest native_capture_begin_frame(std::uint64_t frame_serial);
NativeCaptureResult native_capture_complete_frame(const NativeCaptureRequest& request,
                                                  const NativeCaptureSources& sources);

}  // namespace sm64ds
