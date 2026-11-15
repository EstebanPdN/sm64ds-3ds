#pragma once
#include "lighting.hpp"
namespace sm64ds::render {
struct LightingMaterial {
    std::uint16_t diffuse=0,ambient=0,specular=0,emission=0;
    std::uint8_t mask=0;
    bool use_shininess=false;
    bool toon=false;
    bool grey=false;
    bool operator==(const LightingMaterial& b) const noexcept {
        return diffuse==b.diffuse&&ambient==b.ambient&&specular==b.specular&&
            emission==b.emission&&mask==b.mask&&use_shininess==b.use_shininess&&toon==b.toon&&grey==b.grey;
    }
};
// Borrowed for one synchronous draw only. Queued GPU work receives copied
// uniform values; it never references this source or a game's live matrices.
struct LightingInput {
    const LightSet* lights=nullptr;
    LightingState::Basis normal{};
    LightingMaterial material{};
};
struct LightingUniforms {
    std::array<std::array<float,4>,12> normal{};
    std::array<std::array<float,4>,4> direction{},colour{},material{};
    std::array<std::array<float,4>,32> shininess{};
    unsigned count=0;
    bool specular=false,table=false,toon=false,grey=false;
};
Error validate_lighting(const LightingInput&) noexcept;
Error pack_lighting(const LightingInput&,LightingUniforms&) noexcept;
struct LightingChanges {
    bool normal=false,direction=false,colour=false,material=false,shininess=false;
    bool switches=false,count=false,packed=false;
};
// Owns the source values, not their addresses. Mutations of a borrowed LightSet
// are detected even if its generation is unchanged or its address is reused.
// invalidate() loses GPU residency while retaining the prepared CPU values.
// BUILD134: prepare() packs in place. It writes each row over the value the
// GPU holds while noting whether it differs, instead of zeroing a 904-byte
// LightingUniforms, packing it, comparing it bank by bank and copying it over
// the cache together with the whole 272-byte LightSet. A bank the material
// does not use is not packed: its rows are never uploaded nor compared until
// a material that uses it packs them (the changes below say so). palette: a
// batch draw, whose shader takes the normal basis from its matrix palette;
// the 12 normal rows are left for the next ordinary draw to pack and
// changes.normal is unspecified (always false).
// set_reference(true) selects the BUILD 133 implementation: same changes and
// same uploaded rows (tests/native_renderer/render_cuts.cpp).
class LightingCache {
public:
    Error prepare(const LightingInput&,LightingChanges&,bool palette=false) noexcept;
    const LightingUniforms& uniforms() const noexcept {return uniforms_;}
    void invalidate() noexcept {resident_=false;}
    void set_reference(bool value) noexcept {reference_=value;}
private:
    bool matches(const LightingInput&) const noexcept;
    bool same_source(const LightingInput&) const noexcept;
    Error prepare_reference(const LightingInput&,LightingChanges&) noexcept;
    LightSet lights_{};
    LightingState::Basis normal_{};
    LightingMaterial material_{};
    LightingUniforms uniforms_{};
    bool prepared_=false,resident_=false;
    bool reference_=false;
    // uniforms_.normal is not the packing of normal_ (a palette draw changed it).
    bool normal_rows_stale_=false;
};
}
