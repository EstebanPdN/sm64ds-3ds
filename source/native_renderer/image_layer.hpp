#pragma once
#include "screen_scene.hpp"

namespace sm64ds::render {
enum class LayerSource : std::uint8_t {images,canvas,solid};
// Frame-owned native placement, independent of original sprites/registers.
// The renderer resolves the logical image through the immutable image catalog.
// Increasing order draws later; equal image orders preserve submission order.
struct ImageLayer {
    std::uint32_t bank_id=0;
    std::uint16_t image=0,order=0;
    Screen screen=Screen::top;
    LayerSource source=LayerSource::images;
    std::uint16_t part=0; // prepared native canvas part; zero for image-only layers
    ScreenRect rectangle{};
    ScreenClip clip{};
    Material material=screen_material(); // no GPU handle until render-time resolve
    std::uint8_t tone_target=0; // optional semantic BG0..3/OBJ/backdrop selection
    // Optional frame-owned quad batch, in this layer's local pixel space.
    // Atlas UVs/vertex colours allow an entire text line or sprite run to
    // share one texture, clip and draw. Zero uses the immutable image/canvas.
    std::uint32_t first_vertex=0,vertices=0;
    static constexpr std::size_t max_layers=ScreenCanvas::max_layers*2-1;
};
bool valid(const ImageLayer&) noexcept;
struct OrderedLayers {
    static constexpr std::uint16_t scene=0xffffu;
    std::array<std::uint16_t,ScreenCanvas::max_layers*2> index{};
    std::size_t top=0,bottom=0;
};
// Complete validation before mutation. The selected 3D scene occupies one
// layer; on equal order it precedes images. Both physical LCDs are bounded.
Error order_layers(const ImageLayer*,std::size_t,Screen scene_screen,
                   std::uint16_t scene_order,OrderedLayers&) noexcept;
}
