#pragma once
#include "game_model_pose.hpp"
#include "game_sprite_geometry.hpp"
#include "game_wave_grid.hpp"
#include "game_sketch_geometry.hpp"
namespace sm64ds::render::game_hooks {
struct MenuSpriteDraw {
    game::SpriteGeometry geometry;
    game::Affine object;
    Material material;
    std::uint32_t file_id=untextured_quads;
    std::uint16_t image=QuadState::no_image;
    std::uint8_t polygon_id=63;
    bool fog=false;
};
using SubmitMenuSprite=bool (*)(const MenuSpriteDraw&,void*);
bool install_menu_sprites(SubmitMenuSprite,void*) noexcept;
struct MenuGridDraw {
    game::WaveGrid geometry;
    game::Affine object;
    Material material;
    LightingMaterial lighting;
    LightSet lights;
    std::uint32_t file_id=untextured_quads;
    std::uint16_t image=QuadState::no_image;
};
using SubmitMenuGrid=bool (*)(const MenuGridDraw&,void*);
bool install_menu_grid(SubmitMenuGrid,void*) noexcept;
using SubmitMenuStroke=bool (*)(const game::SketchStroke&,void*);
bool install_menu_strokes(SubmitMenuStroke,void*) noexcept;
void menu_sprite_clock(std::uint64_t (*)()) noexcept; // nullptr removes all clock reads
void menu_descriptor_retiring(const void*) noexcept;
struct MenuSpriteStatistics {
    std::uint64_t descriptors=0,calls=0,draws=0,failures=0,source_quads=0,native_quads=0;
    std::uint64_t capture_ticks=0;
    const char* last_issue="ok";
    std::uint32_t last_detail=0;
};
const MenuSpriteStatistics& menu_sprite_statistics() noexcept;
void menu_sprite_geometry_count(std::size_t source,std::size_t native) noexcept;
struct MenuImageDiagnostic {std::uintptr_t descriptor=0;std::uint32_t file=0xffffffffu,image=0xffffffffu;};
bool menu_image_diagnostic(unsigned slot,MenuImageDiagnostic&) noexcept;
}
