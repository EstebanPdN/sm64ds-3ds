#pragma once
#include "contracts.hpp"

namespace sm64ds::render {
enum class Screen : std::uint8_t {top,bottom};
unsigned screen_width(Screen) noexcept;
struct ScreenRect {float x=0,y=0,width=0,height=0;};
struct ScreenClip {
    std::uint16_t left=0,top=0,right=0,bottom=0; // exclusive physical LCD pixels
    bool enabled() const noexcept {return left||top||right||bottom;}
    bool operator==(const ScreenClip& b) const noexcept {return left==b.left&&top==b.top&&right==b.right&&bottom==b.bottom;}
    bool operator!=(const ScreenClip& b) const noexcept {return !(*this==b);}
};
bool valid(const ScreenClip&,unsigned width=400) noexcept;
Material screen_material() noexcept;
struct ScreenLayer {
    Material material=screen_material();
    ScreenRect rectangle;
    // Optional prepared native geometry, e.g. a batched2D sprite/tile stream.
    // Empty selects the compositor's immutable unit quad. Rectangle supplies
    // the mesh's local-to-LCD scale/origin; no per-vertex CPU transform is used.
    Mesh mesh{};
    std::size_t first_index=0,index_count=0;
    ScreenClip clip{};
};
struct ScreenCanvas {
    static constexpr std::size_t max_layers=64;
    std::uint32_t clear=0x000000ffu;
    const ScreenLayer* layer=nullptr;
    std::size_t layers=0;
    // Native whole-output tone, -16 black through0 unchanged to+16 white.
    // Source per-layer/window/target-mask effects need their own preparation;
    // they cannot be silently reinterpreted as this whole-screen operation.
    int brightness=0;
};
Error validate(const ScreenCanvas&) noexcept;
// Native portrait target orientation. Coordinates are physical LCD pixels,
// independent of source image/engine layout. A quad's local span is0..1.
Error screen_pose(Screen,const ScreenRect&,Matrix&) noexcept;
}
