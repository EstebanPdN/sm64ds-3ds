#pragma once
#include "model_instance.hpp"

namespace sm64ds::render::game {
// Original game fixed-point affine math ABI: four column vectors. These are
// live game poses, never a graphics matrix stack or an instruction program.
struct Affine { std::int32_t value[12]; };
static_assert(sizeof(Affine)==48);
Affine unit_affine() noexcept;
// BUILD131: vector normalisations run so far (three per normalised basis,
// one per billboard axis pair); the test reports show them per update.
std::uint64_t normalizations() noexcept;
// Configure once at startup; comparison mode retains the exact C owner.
void use_affine_assembly(bool) noexcept;
Affine compose(const Affine& local,const Affine& parent) noexcept;
Error project_pose(const Affine&,const Matrix& view_to_clip,Matrix&) noexcept;
// The texture-normal basis follows the original last palette bone and excludes
// position scale/model scale. Only referenced bases are read/computed.
Error build_normal_bases(const ModelBindings&,const void* transforms,const void* runtime_bones,
                         std::size_t bone_count,const Affine* view,NormalBasis* output,std::size_t capacity,
                         const LiveMaterial* materials=nullptr) noexcept;
// A lit normal keeps the palette bone active at its own source event. Ordinary
// lighting excludes position/model scale; billboard normals use the normalized
// final facing basis, already in view space. Hidden/unlit parts read no bones.
Error build_lighting_bases(const ModelBindings&,const void* transforms,const void* runtime_bones,
                          std::size_t bone_count,const Affine* view,const std::int32_t* scale,
                          const LiveMaterial*,NormalBasis* ordinary,NormalBasis* billboard,std::size_t capacity,
                          const NormalBasis* prepared_texture=nullptr) noexcept;
// Compute each referenced ordinary/billboard bone once. Cooked vertices already
// include source model scale. Projection is native view state. Output is caller
// scratch, discarded on error; it must not alias the input storage.
Error build_poses(const ModelBindings&,const Affine* bones,std::size_t bone_count,
                  const Affine* object_to_view,const std::int32_t* scale_xyz,
                  const Matrix& view_to_clip,Matrix* ordinary,Matrix* billboard,
                  std::size_t capacity) noexcept;
// BUILD134: build_poses keeps, per definition, which bones its skin vertices
// name. Definitions are immutable; a tool or test that rebuilds one in place
// (same address, other vertices) forgets the kept marks first.
void forget_skin_bones() noexcept;
// Original Matrix4x3 storage has the same 48-byte ABI but is a different C++
// type. Read only referenced bones via memcpy; never alias through Affine*.
Error build_poses_bytes(const ModelBindings&,const void* bones,std::size_t bone_count,
                  const Affine* object_to_view,const std::int32_t* scale_xyz,
                  const Matrix& view_to_clip,Matrix* ordinary,Matrix* billboard,
                  std::size_t capacity) noexcept;
}
