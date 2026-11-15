#pragma once
#include "model_bank.hpp"
#include "lighting_palette.hpp"

namespace sm64ds::render {
// Offline-authored associations to original MODEL state, never a graphics
// instruction program. Definitions have stable lifetime in the native catalog.
enum class ColourSource : std::uint8_t { literal, diffuse, emission, lighting };
enum class ImageUse : std::uint8_t {model,sprite};
struct ImageBinding {
    std::uint32_t texture, palette, image;
    std::uint16_t width,height; // compact immutable metadata; CPU bank may retire
    ImageUse use=ImageUse::model;
};
static_assert(sizeof(ImageBinding)==20);
struct MaterialBinding {
    std::uint16_t source_material, width, height;
    ColourSource colour;
    bool uv_transform;
    bool normal_uv=false;
    bool diffuse_override=false;
    std::uint32_t diffuse_ambient=0;
};
struct PartBinding {
    std::uint16_t bone, material;
    bool billboard;
    std::uint16_t normal_bone=0xffffu;
    std::uint16_t lighting_bone=0xffffu;
};
// PLAYABLE MODE: triangles spanning bones keep each vertex in its own bone
// space. Each frame the producer re-expresses them in their part's bone space
// (live bone transforms) and the renderer draws them with that part's exact
// material, lighting and UV state from a dynamic normal mesh.
struct SkinVertex {float x,y,z,u,v;std::uint16_t bone;std::uint8_t r,g,b;std::int16_t nx,ny,nz,uv_source;};
struct SkinGroup {std::uint16_t part;std::uint32_t first,count;}; // count: vertices, multiple of 3
struct ModelBindings {
    std::uint32_t file_id;
    std::array<std::uint8_t,32> source_sha256;
    unsigned bones, source_materials;
    const ImageBinding* image;
    unsigned images;
    const MaterialBinding* material;
    unsigned materials;
    const PartBinding* part;
    unsigned parts;
    const SkinGroup* skin=nullptr;
    unsigned skins=0;
    const SkinVertex* skin_vertex=nullptr;
};
Error validate(const ModelBank&,const ModelBindings&) noexcept;

// Captured semantic game state. The game adapter resolves its original math
// and material storage BEFORE this API; no DS command/register types cross it.
struct LiveMaterial {
    Material state; // native depth/sampler/UV/alpha; texture resolved by identity
    std::uint32_t texture=ModelBank::no_texture, palette=ModelBank::no_texture;
    std::uint32_t diffuse=0xffffffffu, emission=0xffffffffu; // RGB from live game
    bool visible=true;
    bool diffuse_available=true,emission_available=true;
    std::uint16_t normal_half_width=0,normal_half_height=0;
    LightingMaterial lighting{};
    // BUILD134: where the material stage found this texture/palette, so the
    // part stage does not search again: the index, in the ModelBindings::image
    // the material was captured against, of the first model entry with this
    // identity. no_image = not known (the part stage searches as before).
    static constexpr std::uint16_t no_image=0xffffu;
    std::uint16_t image=no_image;
};
struct NormalBasis { std::int32_t value[9]{}; };
struct PartInstance {
    Matrix object_to_clip{};
    Material material{};
    bool visible=false;
    std::uint16_t lighting=no_lighting;
};
// Game-thread snapshot before GPU resources need to be resident. Image is a
// prepared catalog index; material.texture is always empty. No game pointers
// or renderer resource handles are retained by this value.
struct PreparedPart {
    Matrix object_to_clip{};
    Material material{};
    std::uint32_t image=ModelBank::no_texture;
    bool visible=false;
    std::uint16_t lighting=no_lighting;
    bool highlight=false;
};
struct InstanceInputs {
    const Matrix* ordinary=nullptr;
    const Matrix* billboard=nullptr;
    std::size_t poses=0;
    const LiveMaterial* material=nullptr;
    std::size_t materials=0;
    const Texture* images=nullptr;
    std::size_t image_count=0;
    const NormalBasis* normal=nullptr; // indexed by original bone, independent of position pose
    const NormalBasis* light_ordinary=nullptr;
    const NormalBasis* light_billboard=nullptr;
    const LightSet* lights=nullptr;
};
// Zero allocation. Validate the entire visible instance before touching output.
// All-hidden instances need no pose/image inputs. Output belongs to the frame;
// later animation/camera writes cannot mutate the captured draw parameters.
Error capture_instance(const ModelBindings&,const InstanceInputs&,
                       PartInstance* output,std::size_t capacity,LightingPalette* lighting=nullptr) noexcept;
// A SceneFrame appends into an unpublished tail. On failure its owner discards
// that tail and publishes neither its parts nor its occurrence. Other callers
// retain the transactional output contract by default.
enum class CaptureOutput { retained, unpublished };
Error capture_prepared(const ModelBindings&,const InstanceInputs&,
                       PreparedPart* output,std::size_t capacity,LightingPalette* lighting=nullptr,
                       CaptureOutput=CaptureOutput::retained) noexcept;
}
