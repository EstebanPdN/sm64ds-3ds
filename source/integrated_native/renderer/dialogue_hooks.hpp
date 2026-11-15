#pragma once
#include "dialogue_text.hpp"
namespace sm64ds::render::game_hooks {
void configure_dialogue(bool font_available) noexcept;
Error capture_dialogue(SceneFrame&,Screen main) noexcept;
struct DialogueWindow {ScreenClip clip{};std::int8_t brightness=0;bool owned=false;};
DialogueWindow dialogue_window() noexcept;
struct DialogueStatistics {
    std::uint64_t starts=0,characters=0,lines=0,scroll_steps=0,frames=0,failures=0;
};
const DialogueStatistics& dialogue_statistics() noexcept;
}
