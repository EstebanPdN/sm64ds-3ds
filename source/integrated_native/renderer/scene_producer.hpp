#pragma once
#include "scene_frame.hpp"
#include "game_model_pose.hpp"
#include "model_components_hooks.hpp"

namespace sm64ds::render::game_hooks {
struct SkinMatrix {float m[12];};
struct ProducerWorkspace {
    std::array<LiveMaterial,ModelBank::max_materials> material{};
    std::array<Matrix,ModelBank::max_matrices> ordinary{},billboard{};
    std::array<NormalBasis,ModelBank::max_matrices> normal{};
    std::array<NormalBasis,ModelBank::max_matrices> light_ordinary{},light_billboard{};
    LightSet lights{};
    // Game-thread scratch for one immutable capture. Flags are cleared before
    // skinning each model; another actor or a later pose cannot reuse its data.
    std::array<SkinMatrix,ModelBank::max_matrices> skin_pose{},skin_inverse{};
    std::array<std::uint8_t,ModelBank::max_matrices> skin_pose_state{};
};
// Concrete original ModelComponents -> owned native scene snapshot submitter.
// Keep workspace on its game-thread owner, not the callback stack. The renderer
// resolves catalog image IDs after publication; no GPU/SD operations occur here.
// PLAYABLE MODE diagnostics: capture failures by stage (materials, projection,
// poses, normals, lighting bases, lights, append, binding).
struct CaptureFailures {std::uint64_t stage[8]{};Error last[8]{};};
const CaptureFailures& capture_failures() noexcept;
extern std::uint64_t skin_ticks; // PLAYABLE profiling
// BUILD131: where a model capture spends its time, for the test reports
// (materials, poses, normal bases, lighting bases and lights, parts, skin).
// Off by default: six clock reads per model.
struct StageProfile {std::uint64_t ticks[6]{},models=0;};
extern StageProfile stage_profile;
void set_stage_profile(bool enabled) noexcept;
// BUILD134: the producer keeps, per skinned actor, the bone transforms of its
// last capture and (once they repeat) the vertices they produced, in memory
// owned by the game thread. A capture with byte-identical bones copies them.
// Definitions are immutable; a test that rebuilds one in place forgets first.
void forget_skin_reuse() noexcept;
class SceneProducer {
public:
    SceneProducer(SceneFrame& frame,ProducerWorkspace& workspace,
                  const std::int16_t* sine_cosine,std::size_t pairs) noexcept;
    void begin_frame() noexcept; // projection validity never leaks across frames
    // Double-buffered capture: the render worker may still read the other frame.
    void set_frame(SceneFrame& frame) noexcept {frame_=&frame;}
    void owner(std::uint64_t identity) noexcept {owner_=identity;}
    Error projection(const Matrix& native_view_to_clip) noexcept;
    static bool submit(const ModelBindings&,const ::ModelComponents&,
                       const ::Matrix4x3*,const ::Vector3*,void*) noexcept;
    // The game's built-in shadow volumes (shape 0 cylinder, 1 cuboid).
    static bool submit_shadow(const ::ModelComponents&,const ::Matrix4x3*,unsigned shape,void*) noexcept;
private:
    std::uint64_t owner_=0;
    SceneFrame* frame_;
    ProducerWorkspace& workspace_;
    const std::int16_t* table_;
    std::size_t pairs_;
    Matrix projection_{};
    bool projection_valid_=false;
    Error capture(const ModelBindings&,const ::ModelComponents&,const ::Matrix4x3*,const ::Vector3*) noexcept;
    Error capture_shadow(const ::ModelComponents&,const ::Matrix4x3*,unsigned shape) noexcept;
    void append_skin(const ModelBindings&,const ::ModelComponents&,InstanceIdentity) noexcept;
};
}
