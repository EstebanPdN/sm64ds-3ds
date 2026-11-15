#pragma once
#include "model_instance.hpp"

namespace sm64ds::render::game {
// Original material ABI, copied from live game state. Native image identity
// comes separately from animation state, never from a virtual VRAM address.
struct RuntimeMaterial {
    std::uint32_t texture,palette;
    std::int32_t scale_x,scale_y;
    std::uint16_t angle,padding;
    std::int32_t translate_x,translate_y;
    std::uint32_t texture_flags,palette_address,polygon,diffuse_ambient,specular_emission;
};
static_assert(sizeof(RuntimeMaterial)==48);
static_assert(offsetof(RuntimeMaterial,polygon)==36 && offsetof(RuntimeMaterial,translate_x)==20);
struct Extent { unsigned width,height; };
struct SelectedImage {
    std::uint32_t texture=ModelBank::no_texture,palette=ModelBank::no_texture;
    Extent sampled{8,8};
    bool binary_alpha=true;
};
// uv_texture is the extent used by the original runtime textureId for its
// matrix; selected may be another animated image. vertex_basis belongs to
// cooked UVs. Sine/cosine are the game's table values, not new approximations.
// Failed admission preserves output. Hidden/no-face materials need no image.
// PLAYABLE MODE (temporary): approximate unsupported polygon/texture policies
// (fog ignored, decal/toon as normal, shadow/wireframe hidden, mixed alpha
// blended, UV mode 3 as mode 0) instead of refusing the material.
void set_lenient_materials(bool) noexcept;
// Fog experiment: carry POLYGON_ATTR's fog bit into the material (lenient mode).
void set_material_fog(bool) noexcept;
bool lenient_materials() noexcept;
Error capture_material(const RuntimeMaterial&,const SelectedImage&,Extent vertex_basis,Extent uv_texture,
                       std::int16_t sine,std::int16_t cosine,LiveMaterial& output,
                       std::uint8_t alpha_cutoff=0) noexcept;
// BUILD134: the same material built directly in frame scratch instead of in a
// temporary that is then copied. Success writes exactly capture_material's
// value; a refusal leaves `output` partly written (scratch is discarded then).
Error capture_material_scratch(const RuntimeMaterial&,const SelectedImage&,Extent vertex_basis,Extent uv_texture,
                               std::int16_t sine,std::int16_t cosine,LiveMaterial& output,
                               std::uint8_t alpha_cutoff=0) noexcept;
}
