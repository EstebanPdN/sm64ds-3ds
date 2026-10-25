// GBATEK DISPCAPCNT and VRAM allocation contract. This backend receives real
// original-resolution engine-A pixels; it does not render or invent a frame.
#include "sm64ds/native_capture.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/native_video.hpp"

#include <limits>

namespace sm64ds {
namespace {
constexpr std::uint32_t kCaptureControl = 0x04000064;
constexpr std::uint32_t kVramControl = 0x04000240;
constexpr std::uint32_t kPhysicalVram = 0x06800000;
constexpr std::uint32_t kBankBytes = 0x20000;
constexpr std::uint32_t kEnable = 0x80000000u;
constexpr std::size_t kSourceRowBytes = 256 * 4;

unsigned allocation_mask(unsigned bank) { return bank < 2 ? 0x83 : 0x87; }
bool is_lcdc(unsigned bank, std::uint8_t control) {
    // GBATEK: MST bit2 is unused for A/B, while C/D use all three bits.
    return (control & allocation_mask(bank)) == 0x80;
}
NativeCaptureResult fail(NativeCaptureResult result, NativeCaptureError error,
                         const char* diagnostic) {
    result.status = NativeCaptureStatus::failed;
    result.error = error;
    native_platform_fail(diagnostic);
    return result;
}
bool overlap(const void* first, std::size_t first_bytes,
             const void* second, std::size_t second_bytes) {
    const auto a = reinterpret_cast<std::uintptr_t>(first);
    const auto b = reinterpret_cast<std::uintptr_t>(second);
    return a <= b ? b - a < first_bytes : a - b < second_bytes;
}
}  // namespace

NativeCaptureRequest native_capture_begin_frame(std::uint64_t frame_serial) {
    NativeCaptureRequest result;
    result.control = static_cast<std::uint32_t>(native_video_read(kCaptureControl,4));
    result.frame_serial = frame_serial;
    if (result.enabled()) {
        result.destination_control = static_cast<std::uint8_t>(
            native_video_read(kVramControl + result.bank(),1));
        result.destination_lcdc = is_lcdc(result.bank(),result.destination_control);
    }
    return result;
}

NativeCaptureResult native_capture_complete_frame(const NativeCaptureRequest& request,
                                                  const NativeCaptureSources& sources) {
    NativeCaptureResult result;
    result.source = request.source();
    result.bank = request.bank();
    result.write_offset = request.write_offset();
    result.width = request.width();
    result.height = request.height();
    if (!request.enabled()) return result;

    const auto current = static_cast<std::uint32_t>(native_video_read(kCaptureControl,4));
    // Completion is the line-192 boundary, even for a small capture or a bank
    // unavailable to the unit. No supported/unsupported-source decision is
    // permitted to leave enable stuck high and turn the caller into a poll loop.
    if (!native_video_write(kCaptureControl,current & ~kEnable,4) || !native_video_ok())
        return fail(result,NativeCaptureError::unavailable_video,
                    "Native display capture has unavailable video state");
    if (current != request.control)
        return fail(result,NativeCaptureError::configuration_changed,
                    "Native display capture control changed during the captured frame");
    const auto bank_control = static_cast<std::uint8_t>(
        native_video_read(kVramControl + request.bank(),1));
    const auto mask = allocation_mask(request.bank());
    if ((bank_control & mask) != (request.destination_control & mask))
        return fail(result,NativeCaptureError::configuration_changed,
                    "Native display capture destination allocation changed during the frame");
    if (result.source == NativeCaptureSource::source_b || result.source == NativeCaptureSource::blend)
        return fail(result,NativeCaptureError::unsupported_source,
                    "Native display capture needs an unimplemented Source B or A+B blend backend");

    // GBATEK explicitly allocates the capture destination to LCDC. Its VRAM
    // notes give the capture unit access only to Plain-CPU allocations. The
    // original scene reset arms 0x80000000 after disabling banks; completing
    // that request clears enable without overwriting texture/BG/OBJ memory.
    if (!is_lcdc(request.bank(),bank_control)) {
        result.status = NativeCaptureStatus::no_lcdc_destination;
        return result;
    }
    const auto& image = result.source == NativeCaptureSource::three_d_only
                      ? sources.three_d_only : sources.composited;
    if (!request.frame_serial || !image.rendered_frame || !image.rgba8)
        return fail(result,NativeCaptureError::unavailable_image,
                    "Native display capture has no completed image for its selected Source A");
    if (image.rendered_frame != request.frame_serial)
        return fail(result,NativeCaptureError::frame_mismatch,
                    "Native display capture image belongs to a different rendering frame");
    if (image.row_stride_bytes < kSourceRowBytes ||
        image.row_stride_bytes > (std::numeric_limits<std::size_t>::max() - kSourceRowBytes) / 191)
        return fail(result,NativeCaptureError::invalid_image,
                    "Native display capture image has an invalid RGBA8 row stride");
    const auto required_bytes = image.row_stride_bytes * 191 + kSourceRowBytes;
    if (image.bytes < required_bytes)
        return fail(result,NativeCaptureError::invalid_image,
                    "Native display capture image does not contain all 256 by 192 source pixels");
    auto* destination = static_cast<std::uint8_t*>(native_video_pointer(
        kPhysicalVram + request.bank() * kBankBytes,kBankBytes));
    if (!destination)
        return fail(result,NativeCaptureError::unavailable_video,
                    "Native display capture cannot resolve its owned physical VRAM bank");
    if (overlap(image.rgba8,required_bytes,destination,kBankBytes))
        return fail(result,NativeCaptureError::invalid_image,
                    "Native display capture RGBA8 input overlaps its destination VRAM bank");

    for (unsigned y = 0; y < result.height; ++y) {
        const auto* row = image.rgba8 + std::size_t(y) * image.row_stride_bytes;
        for (unsigned x = 0; x < result.width; ++x) {
            const auto* pixel = row + x * 4;
            const auto packed = std::uint16_t((pixel[0] >> 3) | ((pixel[1] >> 3) << 5) |
                ((pixel[2] >> 3) << 10) | (pixel[3] ? 0x8000 : 0));
            // Crop the upper-left source without resizing. The destination is
            // a tightly packed capture-size bitmap, wrapping within THIS bank.
            const auto offset = (result.write_offset + (y * result.width + x) * 2) & (kBankBytes - 1);
            destination[offset] = static_cast<std::uint8_t>(packed);
            destination[offset + 1] = static_cast<std::uint8_t>(packed >> 8);
        }
    }
    result.status = NativeCaptureStatus::captured;
    result.rendered_frame = image.rendered_frame;
    result.bytes_written = result.width * result.height * 2;
    return result;
}
}  // namespace sm64ds
