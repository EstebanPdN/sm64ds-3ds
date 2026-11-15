#pragma once
#include "contracts.hpp"

namespace sm64ds::render {
// SM64NIM1/version1 RGBA8, or SM64NIM2/version2 native RGBA8/A4 resources.
// No dummy model, source
// texture descriptors, pointers or graphics commands. Like ModelBank, images
// use bottom-row-first tiled ABGR bytes and native top-left UV coordinates.
// open() borrows immutable bytes; upload copies pixels before that borrow ends.
class ImageBank {
public:
    enum class Alpha : std::uint32_t { independent, palette_zero_mask };
    struct Image {
        const std::uint8_t* data=nullptr;
        std::size_t bytes=0;
        unsigned width=0,height=0;
        Alpha alpha=Alpha::independent;
        TextureFormat format=TextureFormat::rgba8;
    };
    static constexpr unsigned max_images=128;
    static constexpr std::size_t header_bytes=128,record_bytes=32,max_bytes=64*1024*1024;
    static Error open(const void*,std::size_t,ImageBank&) noexcept;
    // Whole-file CRC32; header bytes28..31 are zero during calculation.
    static std::uint32_t checksum(const std::uint8_t*,std::size_t) noexcept;
    unsigned images() const noexcept {return images_;}
    std::size_t gpu_bytes() const noexcept {return gpu_bytes_;}
    const std::uint8_t* source_sha256() const noexcept {return data_?data_+32:nullptr;}
    Image image(unsigned) const noexcept;
    explicit operator bool() const noexcept {return data_!=nullptr;}
private:
    const std::uint8_t* data_=nullptr;
    unsigned images_=0;
    std::size_t gpu_bytes_=0;
};
}
