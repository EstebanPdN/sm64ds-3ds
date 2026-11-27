#pragma once
#include "contracts.hpp"

namespace sm64ds::render {
// Borrowed immutable whole-map pixels and an owned foreground publication.
// Coordinates are signed 8-fraction texel positions at integer output pixels.
// Pixel identity + generation includes dimensions; change generation whenever
// that raster's bytes or layout change. Foreground belongs to this frame.
struct AffineImage {
    const std::uint32_t* pixels=nullptr;
    const std::uint32_t* foreground=nullptr;
    std::uint64_t generation=0;
    unsigned width=0,height=0;
    std::int32_t x=0,y=0;
    std::int16_t xx=256,xy=0,yx=0,yy=256;
    std::uint32_t backdrop=0xff000000u;
    bool wrap=false;
    // BUILD129 upload hint: the foreground equals the base identified by
    // (foreground_base, foreground_base_generation) outside the 8-row bands
    // in foreground_bands (bit n = rows 8n..8n+7). ~0u: no hint.
    const void* foreground_base=nullptr;
    std::uint64_t foreground_base_generation=0;
    std::uint32_t foreground_bands=~0u;
    // BUILD141: text BG layers drawn over the map, back to front, each
    // sampled at (x+hofs, y+vofs) with wrap; key changes when its pixels do.
    // With layers, behind holds the OBJs between the map and the layers and
    // foreground only the OBJs in front of them.
    struct Layer {const std::uint32_t* pixels=nullptr;std::uint64_t key=0;unsigned width=0,height=0;std::int32_t hofs=0,vofs=0;};
    Layer layer[3]{};unsigned layers=0;
    const std::uint32_t* behind=nullptr;
    // BUILD141: one per preparation of the foreground, behind plane and
    // layers; a held image (the previous capture's map copied) keeps it, so
    // its uploads are skipped. 0: unknown, always compared.
    std::uint64_t serial=0;
};
inline bool valid(const AffineImage& image) noexcept {
    return image.pixels&&image.foreground&&image.generation&&
        texture_dimensions(image.width,image.height)&&image.width<=512&&image.height<=512;
}
inline std::array<float,6> affine_image_uv(const AffineImage& image) noexcept {
    // Pixel centres evaluate the original integer output coordinates. Bias
    // exact texel boundaries inward by half the smallest source fraction.
    return {float(image.xx)/image.width,float(image.xy)*192/(256*image.width),
        float((double(image.x)-.5*(image.xx+image.xy))/256+1.0/512)/image.width,
        float(image.yx)/image.height,float(image.yy)*192/(256*image.height),
        float((double(image.y)-.5*(image.yx+image.yy))/256+1.0/512)/image.height};
}
}
