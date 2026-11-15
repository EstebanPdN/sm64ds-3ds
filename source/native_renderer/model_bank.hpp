#pragma once
#include "contracts.hpp"

namespace sm64ds::render {
// SM64NMD2/version2 or SM64NMD3/version3 (optional normal32 meshes).
// Both use top-left UV(0,0), bottom-row-first tiled images.
// All integers and float32 payloads are little-endian. Version1 is refused.
// No native pointer, display list or GPU compatibility register is serialized.
// open() validates a borrowed bank without allocating. The bytes must remain
// immutable/alive until its consumer finishes uploading/copying the metadata.
class ModelBank {
public:
    struct Image { const std::uint8_t* data; std::size_t bytes; unsigned width, height; };
    struct Surface { std::uint32_t texture; Material material; };
    struct Part { std::uint32_t mesh, material, matrix, first, count; };
    static constexpr std::uint32_t no_texture = 0xffffffffu;
    static constexpr std::size_t header_bytes = 128, record_bytes = 32;
    static constexpr std::size_t material_record_bytes = 64;
    static constexpr unsigned max_meshes=256, max_images=128, max_materials=512, max_parts=1024, max_matrices=256;
    static Error open(const void* data, std::size_t bytes, ModelBank& output) noexcept;
    unsigned meshes() const noexcept { return meshes_; }
    unsigned images() const noexcept { return images_; }
    unsigned materials() const noexcept { return materials_; }
    unsigned parts() const noexcept { return parts_; }
    unsigned matrices() const noexcept { return matrices_; }
    bool requires_material_bindings() const noexcept { return material_bindings_; }
    MeshBytes mesh(unsigned index) const noexcept;
    Image image(unsigned index) const noexcept;
    Surface surface(unsigned index) const noexcept;
    Part part(unsigned index) const noexcept;
    // False only when every triangle repeats an index (provably no pixels).
    // Placeholder parts still retain their original state for animated skin.
    bool has_distinct_index_triangle(unsigned part) const noexcept;
    const std::uint8_t* source_sha256() const noexcept { return data_ ? data_+56 : nullptr; }
    explicit operator bool() const noexcept { return data_ != nullptr; }
    // CRC covers the whole file with header bytes 52..55 treated as zero.
    static std::uint32_t checksum(const std::uint8_t*, std::size_t) noexcept;
private:
    const std::uint8_t* data_=nullptr;
    std::size_t bytes_=0;
    unsigned meshes_=0,images_=0,materials_=0,parts_=0,matrices_=0;
    std::size_t mesh_table_=0,image_table_=0,material_table_=0,part_table_=0;
    bool material_bindings_=false;
};
}
