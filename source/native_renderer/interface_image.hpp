#pragma once
#include "contracts.hpp"

namespace sm64ds::render {
// Borrowed only during a synchronous upload. Straight-alpha AARRGGBB words,
// top row first; neither a source-engine surface nor a GPU handle.
struct InterfaceImage {
    const std::uint32_t* pixels=nullptr;
    std::size_t count=0;
    unsigned width=0,height=0,stride=0;
};
struct InterfaceCrop {
    unsigned x=0,y=0,width=0,height=0,texture_width=0,texture_height=0;
    bool empty() const noexcept {return width==0&&height==0;}
};
Error validate(const InterfaceImage&) noexcept;
// Scan once per publication, never once per display frame. Empty alpha needs
// no texture. All arithmetic is bounded by the native LCD size.
Error interface_crop(const InterfaceImage&,InterfaceCrop&) noexcept;
Error tile_interface(const InterfaceImage&,const InterfaceCrop&,void*,std::size_t) noexcept;
// Owned frame upload: exactly 256x192 ARGB words into an existing 512x512
// RGBA8 Morton texture. Writes the 512x384 doubled image; retains padding.
void tile_frame_image_2x(const std::uint32_t* image,std::uint32_t* texture) noexcept;
// Native map foreground: 256x192 in 256x256, no CPU pixel doubling.
void tile_frame_image_1x(const std::uint32_t* image,std::uint32_t* texture) noexcept;
// Source rows [first,end) only. An 8-row band k occupies one contiguous
// block: 1x words [(31-k)*2048, +2048), 2x words [(62-2k)*4096, +8192).
void tile_frame_rows_2x(const std::uint32_t* image,std::uint32_t* texture,unsigned first,unsigned end) noexcept;
void tile_frame_rows_1x(const std::uint32_t* image,std::uint32_t* texture,unsigned first,unsigned end) noexcept;
constexpr unsigned frame_bands=24,frame_band_rows=8;
constexpr std::size_t frame_band_word(bool sharp,unsigned band) noexcept {return sharp?std::size_t(62-2*band)*4096:std::size_t(31-band)*2048;}
constexpr std::size_t frame_band_words(bool sharp) noexcept {return sharp?8192:2048;}
}
