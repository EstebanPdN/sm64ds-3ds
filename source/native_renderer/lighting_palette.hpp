#pragma once
#include "lighting_uniforms.hpp"
namespace sm64ds::render {
inline constexpr std::uint16_t no_lighting=0xffffu;
struct PreparedLighting {
    LightingState::Basis normal{};
    LightingMaterial material{};
    std::uint16_t lights=no_lighting;
};
struct LightingPaletteView {
    const LightSet* lights=nullptr;
    const PreparedLighting* state=nullptr;
    std::size_t light_count=0,state_count=0;
};
Error resolve_lighting(const LightingPaletteView&,std::uint16_t,LightingInput&) noexcept;
// Bounded frame-owned values. Hash tables avoid quadratic searches as actors
// add independently animated bases. Only a single copy of each used light/table
// set is retained; individual parts store a two-byte state index.
class LightingPalette {
public:
    static constexpr unsigned max_lights=128,max_states=2048;
    struct Mark {std::size_t lights,states;};
    LightingPalette(LightSet* lights=nullptr,std::size_t light_capacity=0,
                    PreparedLighting* states=nullptr,std::size_t state_capacity=0) noexcept;
    void reset() noexcept;
    Error intern(const LightingInput&,std::uint16_t&) noexcept;
    Mark mark() const noexcept {return {light_count_,state_count_};}
    void rollback(Mark) noexcept;
    LightingPaletteView view() const noexcept {return {lights_,states_,light_count_,state_count_};}
private:
    LightSet* lights_;
    PreparedLighting* states_;
    std::size_t light_capacity_,state_capacity_,light_count_=0,state_count_=0;
    std::array<std::uint16_t,256> light_index_{};
    std::array<std::uint16_t,4096> state_index_{};
    // Last owned value for each mask/table/toon combination. This is only a
    // lookup hint: compare source contents, never trust its pointer/generation.
    std::array<std::uint16_t,64> recent_lights_{};
};
}
