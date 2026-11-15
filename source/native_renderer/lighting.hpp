#pragma once
#include "contracts.hpp"
namespace sm64ds::render {
struct NativeLight {
    std::array<std::int16_t,3> direction{}; // signed11 eye-space diffuse direction
    std::array<std::uint8_t,3> colour{};   // original five-bit channels
    std::int32_t specular_reciprocal=0;
    bool operator==(const NativeLight& b) const noexcept {
        return direction==b.direction&&colour==b.colour&&specular_reciprocal==b.specular_reciprocal;
    }
};
struct LightSet {
    std::array<NativeLight,4> light{};
    std::array<std::uint8_t,128> shininess{};
    std::array<std::uint16_t,32> toon{};
    std::uint64_t generation=0;
    std::uint8_t known=0;
    bool table_known=false;
    bool toon_known=false;
};
// Game-thread semantic source. A direction is transformed once at its original
// light update, not when a later mesh is drawn. Snapshots own every value.
// Neither this API nor its storage contains graphics commands/register state.
class LightingState {
public:
    // PLAYABLE MODE (temporary): unknown lights/table get a fixed white
    // view-space default instead of refusing the lit model.
    static void set_lenient(bool) noexcept;
    using Basis=std::array<std::int32_t,9>;
    void reset() noexcept;
    void view(const Basis&) noexcept;
    Error light(unsigned index,const std::array<std::int16_t,3>& signed10,std::uint16_t colour) noexcept;
    Error table(const std::uint8_t* values,std::size_t count) noexcept;
    Error snapshot(unsigned enabled_mask,bool needs_table,LightSet& output) const noexcept;
    bool has_view() const noexcept {return view_valid_;}
    std::uint8_t known() const noexcept {return state_.known;}
    std::uint64_t generation() const noexcept {return state_.generation;}
private:
    Basis view_{};
    LightSet state_{};
    bool view_valid_=false;
    bool advance() noexcept;
};
}
