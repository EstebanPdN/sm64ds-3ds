#pragma once
#include "contracts.hpp"
#include <cstdio>
namespace sm64ds::render::game_hooks {
// Original producer semantics only. These masks name source layers; no source
// register word or graphics packet is accepted by the native GPU backend.
enum class DisplayEffect : std::uint8_t {none,alpha,brightness};
struct SourceEffect {
    DisplayEffect mode=DisplayEffect::none;
    std::uint8_t first=0,second=0,alpha_a=0,alpha_b=0;
    std::int8_t amount=0;
    bool known=false;
};
struct SourceBackground {
    std::uint16_t resource=0xffffu,width=0,height=0;
    std::uint8_t layer=0,priority=0;
    bool seen=false,has_content=false,dirty=false;
    std::int32_t x=0,y=0; // original integer pixel scroll, before hardware wrap
};
struct SourceDisplaySnapshot {
    std::array<SourceEffect,2> effect{};
    std::array<SourceBackground,8> background{};
    std::array<std::uint64_t,2> background_epochs{};
    std::uint64_t generation=0;
    bool failure=false;
};
class MenuDisplayState {
public:
    void reset() noexcept {state_={};}
    void invalidate() noexcept {state_.failure=true;}
    Error backgrounds_begin(unsigned engines) noexcept;
    Error brightness(unsigned engine,unsigned targets,int amount) noexcept;
    Error strength(unsigned engine,int amount) noexcept;
    Error alpha(unsigned engine,unsigned first,unsigned second,unsigned a,unsigned b) noexcept;
    Error disable(unsigned engines) noexcept;
    Error background(unsigned engines,const SourceBackground&) noexcept;
    const SourceDisplaySnapshot& snapshot() const noexcept {return state_;}
    // Whole-output lowering is allowed only when every potentially visible
    // layer and the backdrop are covered. A partial mask must stay explicit.
    Error whole_tone(unsigned engine,unsigned visible,int& output) const noexcept;
    // Until the native display consumer accounts for these effects/layers,
    // only an explicitly known, unaffected presentation may pass its gate.
    Error unaffected() const noexcept;
private:
    SourceDisplaySnapshot state_{};
    Error fail() noexcept {state_.failure=true;return Error::unsupported;}
};
struct MenuDisplayStatistics {
    std::uint64_t brightness=0,strength=0,alpha=0,disable=0,background=0;
    std::uint64_t dirty_backgrounds=0,skipped_bytes=0,failures=0;
    std::uint64_t background_group_ticks=0;
};
void menu_display_scene_changed() noexcept;
const SourceDisplaySnapshot& menu_display_snapshot() noexcept;
const MenuDisplayStatistics& menu_display_statistics() noexcept;
Error menu_display_unaffected() noexcept;
void menu_display_clock(std::uint64_t (*)()) noexcept;
// Private diagnostic semantics, never full scene/physical/RAM acceptance.
bool write_menu_display_capture(FILE*,std::uint64_t frame) noexcept;
}
