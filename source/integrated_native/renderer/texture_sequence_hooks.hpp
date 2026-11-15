#pragma once
#include "game_material.hpp"
struct ModelComponents;
struct BMD_File;

namespace sm64ds::render::game_hooks {
struct ImageIdentity { std::uint32_t texture=ModelBank::no_texture,palette=ModelBank::no_texture; };
// Game-thread state. Initial identities are read only until the game's BTP
// updater supplies semantic choices. No virtual texture/palette address lookup.
bool selected_image(const ::ModelComponents&,unsigned material,ImageIdentity&) noexcept;
bool image_state_valid(const ::ModelComponents&) noexcept;
// BUILD134: selected_image for a caller that has just seen image_state_valid
// succeed for this model, with no game update in between (one validity
// lookup per model instead of one per material).
bool selected_image_checked(const ::ModelComponents&,unsigned material,ImageIdentity&) noexcept;
void initialize_image_tracking() noexcept; // install/startup only
void reset_image_tracking(const ::ModelComponents&) noexcept; // actual model init
void retire_image_tracking(const ::BMD_File*) noexcept; // before last file release
}
