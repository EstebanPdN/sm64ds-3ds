#pragma once
#include "model_instance.hpp"
#include "game_frame_state.hpp"
#include "native_quads.hpp"
#include "image_layer.hpp"
#include "ds_fog.hpp"
#include <array>

namespace sm64ds::render {
// Opaque simulation identities: owner includes its allocation generation;
// component distinguishes multiple models on one actor. Never dereferenced.
struct InstanceIdentity {
    std::uint64_t owner=0,component=0;
    std::uint32_t piece=0;
    bool tracked() const noexcept {return owner&&component;}
};
struct Occurrence {
    enum class Kind : std::uint8_t {model,quads,skin,mesh}; // skin/mesh: state = global part index
    std::uint32_t file_id,first,count;
    Kind kind=Kind::model;
    std::uint16_t state=0;
    InstanceIdentity identity{};
};
// DS shadow volume (polygon mode 3): the closed prism the game places under
// an actor. The renderer marks where its far faces lie behind the scene and
// darkens where its near faces lie in front of it (depth-fail stencil).
// APPROXIMATION: the DS also keeps a volume off polygons that share its
// polygon ID (the actor itself); polygon IDs are not carried here.
// The pose is the volume's own space to view space (three columns and the
// translation, in the units of its vertices): every volume of an image shares
// the projection, so the renderer transforms their few vertices itself and
// draws all of them with two draws instead of two per volume.
struct ShadowVolume {
    enum class Shape : std::uint8_t {cylinder,cuboid};
    std::array<float,12> pose{};
    std::uint8_t opacity=0; // DS alpha, 1..31
    Shape shape=Shape::cylinder;
};
constexpr std::size_t max_frame_shadows=64;
struct SceneView {
    const Occurrence* occurrence=nullptr;
    const PreparedPart* part=nullptr;
    std::size_t occurrences=0,parts=0;
    std::uint64_t scene=0,serial=0;
    LightingPaletteView lighting{};
    SceneClear clear{};
    const Vertex* quad_vertex=nullptr;
    const QuadState* quad_state=nullptr;
    std::size_t quad_vertices=0,quad_states=0;
    const ImageLayer* image_layer=nullptr;
    std::size_t image_layers=0;
    std::uint16_t scene_order=0;
    Screen scene_screen=Screen::top;
    const NormalVertex* skin_vertex=nullptr;
    std::size_t skin_vertices=0;
    const ShadowVolume* shadow=nullptr;
    std::size_t shadows=0;
    Matrix shadow_projection{}; // view to clip, valid when shadows != 0
    DsFog fog{};                // fog experiment: registers and the image's view to clip
    Matrix fog_projection{};
};
struct QuadStorage {
    Vertex* vertex=nullptr;std::size_t vertices=0;
    QuadState* state=nullptr;std::size_t states=0;
};
// One game-thread capture/consumer ownership cycle. Synchronization between
// threads belongs to the publisher; a sealed frame is immutable until release.
// Storage is supplied at startup, never allocated in model callbacks.
class SceneFrame {
public:
    enum class Phase {idle,capturing,sealed,consuming};
    SceneFrame(Occurrence* occurrences,std::size_t occurrence_capacity,
               PreparedPart* parts,std::size_t part_capacity,
               LightSet* lights=nullptr,std::size_t light_capacity=0,
               PreparedLighting* lighting=nullptr,std::size_t lighting_capacity=0,
               QuadStorage quads={},ImageLayer* images=nullptr,std::size_t image_capacity=0) noexcept;
    Error begin(std::uint64_t scene,std::uint64_t serial,SceneClear clear={}) noexcept;
    Error clear(SceneClear) noexcept;
    Error append(const ModelBindings&,const InstanceInputs&,InstanceIdentity={}) noexcept;
    // Only adjacent, identical draw states coalesce. A model or any state
    // change is an ordering barrier, including for translucent quads.
    Error append_quads(std::uint32_t file_id,const QuadState&,const Vertex*,std::size_t vertices) noexcept;
    Error append_image(const ImageLayer&) noexcept;
    // Copy an atlas-backed screen batch into the same owned vertex arena as
    // native geometry. No original VRAM/OAM, GPU handles or borrowed arrays.
    Error append_image_quads(const ImageLayer&,const Vertex*,std::size_t vertices) noexcept;
    // PLAYABLE MODE: vertices already in the space of global part `part`.
    void set_skin_storage(NormalVertex* storage,std::size_t capacity) noexcept {skin_=storage;skin_capacity_=capacity;}
    Error append_skin(std::uint32_t file_id,std::uint32_t part,const NormalVertex*,std::size_t count,InstanceIdentity={}) noexcept;
    // Procedural triangle mesh with its own material/lighting. Texture identity
    // comes from a prepared model bank; no static model geometry is required.
    Error append_mesh(std::uint32_t file_id,const PreparedPart&,const LightingInput*,const NormalVertex*,std::size_t count) noexcept;
    // Drawn after every occurrence, in capture order. A full table drops the
    // volume (one actor without a shadow), never the frame.
    Error append_shadow(const ShadowVolume&,const Matrix& view_to_clip) noexcept;
    // Fog experiment: the DS fog registers of this image and its projection.
    Error fog(const DsFog&,const Matrix& view_to_clip) noexcept;
    std::size_t captured_shadows() const noexcept {return used_shadows_;}
    NormalVertex* skin_workspace(std::size_t& capacity) noexcept {capacity=skin_capacity_-used_skin_;return skin_?skin_+used_skin_:nullptr;}
    std::size_t captured_skin_vertices() const noexcept {return used_skin_;}
    Error presentation(Screen,std::uint16_t order) noexcept;
    // Borrowed unused tail, valid only during capture. Producers may generate
    // directly into it and append that same span without a staging allocation
    // or copy. It is never part of a published frame until append succeeds.
    struct QuadWrite {Vertex* data=nullptr;std::size_t capacity=0;};
    QuadWrite quad_workspace() noexcept;
    // Parts of the most recently appended model occurrence (capture phase only).
    const PreparedPart* last_model_parts() const noexcept {
        return used_occurrences_&&occurrences_[used_occurrences_-1].kind==Occurrence::Kind::model?parts_+occurrences_[used_occurrences_-1].first:nullptr;}
    std::uint32_t last_model_first_part() const noexcept {return used_occurrences_?occurrences_[used_occurrences_-1].first:0;}
    void invalidate(Error) noexcept;
    // PLAYABLE MODE (temporary, incomplete coverage): a failed item is dropped
    // and counted instead of refusing the whole frame. Strict mode is default.
    void set_lenient(bool value) noexcept {lenient_=value;}
    bool lenient() const noexcept {return lenient_;}
    std::uint64_t dropped() const noexcept {return dropped_;}
    Error seal(bool producers_ok,std::uint64_t legacy_writers) noexcept;
    Error acquire(SceneView&) noexcept;
    Error release() noexcept;
    Phase phase() const noexcept {return phase_;}
    Error failure() const noexcept {return failure_;}
    std::size_t captured_parts() const noexcept {return used_parts_;}
    std::size_t captured_occurrences() const noexcept {return used_occurrences_;}
    std::size_t captured_quads() const noexcept {return used_quad_vertices_/4;}
    std::size_t captured_quad_states() const noexcept {return used_quad_states_;}
    std::size_t captured_light_sets() const noexcept {return lighting_.view().light_count;}
    std::size_t captured_light_states() const noexcept {return lighting_.view().state_count;}
    std::size_t captured_images() const noexcept {return used_images_;}
    std::uint16_t captured_scene_order() const noexcept {return scene_order_;}
    Screen captured_scene_screen() const noexcept {return scene_screen_;}
private:
    Occurrence* occurrences_;
    PreparedPart* parts_;
    std::size_t occurrence_capacity_,part_capacity_,used_occurrences_=0,used_parts_=0;
    std::uint64_t scene_=0,serial_=0;
    Phase phase_=Phase::idle;
    Error failure_=Error::none;
    bool lenient_=false;
    NormalVertex* skin_=nullptr;
    std::size_t skin_capacity_=0,used_skin_=0;
    std::uint64_t dropped_=0;
    LightingPalette lighting_;
    SceneClear clear_{};
    QuadStorage quads_{};
    std::size_t used_quad_vertices_=0,used_quad_states_=0;
    ImageLayer* images_=nullptr;
    std::size_t image_capacity_=0,used_images_=0;
    std::uint16_t scene_order_=0;
    Screen scene_screen_=Screen::top;
    std::array<ShadowVolume,max_frame_shadows> shadows_{};
    Matrix shadow_projection_{};
    DsFog fog_{};
    Matrix fog_projection_{};
    std::size_t used_shadows_=0;
};
}
