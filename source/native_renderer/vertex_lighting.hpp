#pragma once
#include "lighting_uniforms.hpp"
namespace sm64ds::render {
// Semantic inputs have already passed validate_lighting; normals come from a
// validated native normal mesh. Exact RGB5 lighting, expanded to RGBA8 for the
// same 1/255 normalization as the existing PICA kernel. No position transform.
std::array<std::uint8_t,4> vertex_lighting(const LightingInput&,
    const std::array<std::int16_t,3>& normal) noexcept;
// Invariant material/light products and owned tables for one captured state.
class PreparedVertexLighting {
public:
    PreparedVertexLighting()=default;
    explicit PreparedVertexLighting(const LightingInput&) noexcept;
    std::array<std::uint8_t,4> colour(const std::array<std::int16_t,3>&) const noexcept;
private:
    LightingState::Basis normal_{};
    struct Light {std::array<std::int16_t,3> direction{};
        std::array<std::uint32_t,3> diffuse{},specular{};std::int32_t reciprocal=0;};
    std::array<Light,4> light_{};
    std::array<std::uint32_t,3> base_{};
    std::array<std::uint8_t,128> shininess_{};
    std::array<std::uint16_t,32> toon_{};
    unsigned count_=0;
    bool specular_=false,table_=false,toon_mode_=false,grey_=false;
};
}
