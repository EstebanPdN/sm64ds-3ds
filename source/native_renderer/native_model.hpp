#pragma once
#include "citro3d_backend.hpp"
#include "model_bank.hpp"
#include "model_instance.hpp"

namespace sm64ds::render {
// Shared, resident model asset. Instances supply pose/camera and optional
// material values; they do not duplicate geometry or decode assets per draw.
// The backend object must outlive this asset. All calls, including destruction,
// run on the owning render thread. GPU storage retires through its next fence.
class NativeModel {
public:
    NativeModel() = default;
    ~NativeModel() { reset(); }
    NativeModel(const NativeModel&) = delete;
    NativeModel& operator=(const NativeModel&) = delete;
    NativeModel(NativeModel&& other) noexcept;
    NativeModel& operator=(NativeModel&& other) noexcept;
    // Requires an empty model and the backend upload phase. Failure restores
    // GPU bytes/slots immediately; only a complete model becomes visible.
    Error upload(Citro3DBackend&, const ModelBank&,std::size_t batch_budget=0,bool trace=false) noexcept;
    // Sprite-only residency: upload the explicitly requested images, with no
    // model geometry/material allocation. Extending a matching resident bank
    // stages only missing textures; failure leaves its live handles untouched.
    Error upload_images(Citro3DBackend&,const ModelBank&,
                        const std::array<bool,ModelBank::max_images>& required) noexcept;
    Error draw(const Matrix* object_to_clip,std::size_t matrix_count,
               const Material* overrides=nullptr,std::size_t override_count=0) const noexcept;
    Error draw_prepared(const PreparedPart*,std::size_t count,LightingPaletteView lighting={}) const noexcept;
    void reset() noexcept;
    unsigned parts() const noexcept { return parts_; }
    unsigned matrices() const noexcept { return matrices_; }
    unsigned materials() const noexcept { return materials_; }
    unsigned images() const noexcept { return images_; }
    bool geometry_ready() const noexcept {return geometry_ready_;}
    std::size_t metadata_bytes() const noexcept { return bytes_+batch_bytes_; }
    // Borrowed values: the asset retains ownership of these texture handles.
    Material material(unsigned index) const noexcept;
    Texture image(unsigned index) const noexcept;
    // Borrowed handle/range by value, valid until this asset is retired.
    BatchRange resident_range(unsigned part) const noexcept {return batch_range_&&part<parts_?batch_range_[part]:BatchRange{};}
    struct DrawSpan {Mesh mesh{};Material material{};std::size_t first=0,count=0;};
    // Borrowed until this asset is retired. Native2D composition resolves this
    // only after pinning its bank; these handles never enter captured frames.
    Error span(unsigned part,DrawSpan&) const noexcept;
private:
    friend class SceneRenderer;
    // palette_resolved: the caller resolved every state of this palette
    // (resolve_lighting for each index below state_count) without error.
    Error validate_prepared(const PreparedPart*,std::size_t,LightingPaletteView,bool palette_resolved=false) const noexcept;
    Error draw_prepared_part(unsigned,const PreparedPart&,LightingPaletteView) const noexcept;
    bool empty_part(unsigned index) const noexcept {return empty_parts_[index/32]&(1u<<(index%32));}
    void* batch_storage_=nullptr;
    Mesh* batch_mesh_=nullptr;
    BatchRange* batch_range_=nullptr;
    unsigned batch_meshes_=0;
    std::size_t batch_bytes_=0;
    Citro3DBackend* owner_=nullptr;
    void* storage_=nullptr;
    Mesh* mesh_=nullptr;
    Texture* image_=nullptr;
    Material* material_=nullptr;
    ModelBank::Part* part_=nullptr;
    unsigned meshes_=0,images_=0,materials_=0,parts_=0,matrices_=0;
    std::size_t bytes_=0;
    bool material_bindings_=false;
    bool geometry_ready_=false;
    std::array<std::uint32_t,ModelBank::max_parts/32> empty_parts_{};
    std::array<std::uint8_t,32> source_sha256_{};
    void take(NativeModel&) noexcept;
};
}
