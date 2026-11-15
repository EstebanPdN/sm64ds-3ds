#pragma once
#include "model_catalog.hpp"
struct ModelComponents;
struct Matrix4x3;
struct Vector3;

namespace sm64ds::render::game_hooks {
// Game-thread seam. A producer consumes these borrowed live values during the
// call and publishes owned native snapshots. It must not retain game pointers.
using Submit=bool (*)(const ModelBindings&,const ::ModelComponents&,
                     const ::Matrix4x3*,const ::Vector3*,void* user);
using Clock=std::uint64_t (*)();
// BUILD131: the game's two built-in shadow volumes (ShadowModel's cylinder
// and cuboid) are not ROM files and have no native bank. Their draws go to
// this producer instead; shape 0 is the cylinder, 1 the cuboid.
using SubmitShadow=bool (*)(const ::ModelComponents&,const ::Matrix4x3*,unsigned shape,void* user);
struct Statistics {
    std::uint64_t observed_loads=0,observed_releases=0,model_calls=0,submitted=0;
    std::uint64_t missing_models=0,producer_failures=0,lifetime_failures=0;
    std::uint64_t producer_ticks=0; // callback only; clock is disabled by default
    std::uint32_t last_missing_file=0xffffffffu;
    std::uintptr_t last_missing_address=0;
    std::uint64_t load_notifications=0,typed_model_loads=0,direct_model_loads=0;
    std::uint64_t dry_calls=0; // model draws of updates whose image is not shown
};
// BUILD131: which models the native route could not draw. One entry per live
// model file; "reason" says why (1 the load was never observed, so the file
// has no identity; 2 identified but without a native bank; 3 two identities).
// Counts and names let tools/test_mode_report.py match the ROM file offline.
struct MissingModel {
    const void* file=nullptr;
    std::uint32_t file_id=0xffffffffu,calls=0;
    std::uint16_t bones=0,materials=0,textures=0,palettes=0,groups=0;
    std::uint8_t reason=0;
    char material[20]{},texture[20]{};
};
constexpr std::size_t max_missing_models=64;
// Game thread. Entries stay until clear_missing_census(); overflow counts
// calls of models that did not fit.
std::size_t missing_census(const MissingModel** table,std::uint32_t* overflow=nullptr) noexcept;
void clear_missing_census() noexcept;
// Install before file loading. There is no active native route until explicitly
// enabled by the native frame owner. Clear/detach only after its frames retire.
bool install(ModelCatalog,Submit,void* user) noexcept;
void install_shadows(SubmitShadow) noexcept; // same user as install()
bool activate(bool) noexcept;
bool set_clock(Clock) noexcept; // idle frame owner; nullptr removes all clock reads
bool tracking_enabled() noexcept;
bool native_active() noexcept;
// PLAYABLE MODE: original 2D engine writers run (temporary PPU bridge), so
// the native 2D replacement hooks forward unchanged while 3D stays native.
bool native_2d_active() noexcept;
void set_bridge_2d(bool) noexcept;
// Dry update (decided before the game draws): its image will not be shown,
// so model and particle producers capture nothing. Game logic is unaffected:
// native mode never runs the original draw submission anyway.
void set_dry(bool) noexcept;
bool dry() noexcept;
void raw_file_loaded(std::uint16_t fat_id,const void* file) noexcept;
bool tracks_model(const ::ModelComponents&) noexcept;
const ModelBindings* binding_for_file(const void* file) noexcept;
void report_image_failure() noexcept;
void model_loaded(const void* original_handle,const void* model_file) noexcept;
// Typed original loaders without SharedFilePtr must supply a resolved FAT ID
// and retire the identity before freeing/replacing its allocation.
void direct_model_loaded(std::uint16_t file_id,const void* model_file) noexcept;
void model_retiring(const void* model_file) noexcept;
bool begin_frame() noexcept;
bool end_frame() noexcept;
const Statistics& statistics() noexcept;
}
