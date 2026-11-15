#pragma once
#include "lighting_uniforms.hpp"
namespace sm64ds::render {
class ModelBank;
struct SceneView;
struct BatchVertex {NormalVertex source;std::array<std::uint8_t,4> rows;};
static_assert(sizeof(BatchVertex)==36&&offsetof(BatchVertex,rows)==32);
// Four consecutive MVP rows for a validated palette slot (0..7). The
// little-endian GPU payload can store this word without a byte loop.
constexpr std::uint32_t packed_palette_rows(std::uint8_t slot) noexcept {
    return 0x03020100u+std::uint32_t(slot)*0x04040404u;
}
using BatchBasis=std::array<std::array<std::int16_t,4>,3>;
// Load-time compact geometry; the owning BatchGeometry retains these bytes.
class BatchSource {
public:
    explicit operator bool() const noexcept {return vertices_!=nullptr;}
    const BatchVertex* vertices() const noexcept {return vertices_;}
    const std::uint16_t* indices() const noexcept {return indices_;}
    std::size_t vertex_count() const noexcept {return vertex_count_;}
    std::size_t index_count() const noexcept {return index_count_;}
    bool normals() const noexcept {return normals_;}
private:
    const BatchVertex* vertices_=nullptr;
    const std::uint16_t* indices_=nullptr;
    std::uint32_t vertex_count_=0,index_count_=0;
    bool normals_=false;
    friend class BatchGeometry;
};
class BatchGeometry {
public:
    BatchGeometry()=default;
    ~BatchGeometry(){reset();}
    BatchGeometry(const BatchGeometry&)=delete;
    BatchGeometry& operator=(const BatchGeometry&)=delete;
    BatchGeometry(BatchGeometry&&) noexcept;
    BatchGeometry& operator=(BatchGeometry&&) noexcept;
    Error prepare(const ModelBank&,std::size_t budget) noexcept;
    BatchSource source(unsigned part) const noexcept;
    unsigned parts() const noexcept {return parts_;}
    std::size_t bytes() const noexcept {return bytes_;}
    void reset() noexcept;
private:
    struct Part {std::size_t vertices=0,indices=0;std::uint32_t vertex_count=0,index_count=0;bool normals=false;};
    void* storage_=nullptr;
    std::size_t bytes_=0;
    unsigned parts_=0;
};
struct BatchRange {
    Mesh mesh{};
    std::uint32_t first=0,count=0;
    std::uint8_t slot=0;
    explicit operator bool() const noexcept {return bool(mesh)&&count;}
};
// BUILD134: a span borrows its matrices and its LightSet from the frame the
// plan was built from, which the consumer leases until the plan's last draw.
// Copying them cost 64 bytes per palette slot and 272 per span, written while
// planning and read back while drawing (about 25 KB per image). The material
// (it carries the resolved texture) and the 16-bit normal bases stay owned.
struct BatchSpan {
    Mesh mesh{};
    Material material{};
    std::array<const Matrix*,8> palette{}; // used slots only
    std::array<BatchBasis,8> basis{};
    const LightSet* lights=nullptr;        // when lighting.mask
    LightingMaterial lighting{};
    std::uint32_t first=0,count=0;
    std::uint8_t used=0;
    bool lit=false;
};
// Only immutable draw ranges and per-instance uniforms. No vertex or index
// storage is built, copied or uploaded during presentation planning.
class NativeBatchPlan {
public:
    static constexpr std::size_t max_vertices=65535,max_spans=256;
    void reset() noexcept;
    void barrier() noexcept {open_=false;}
    // Capture already owns validated matrices/materials/lighting. Unsupported
    // shader features leave the existing plan intact for the ordinary draw.
    // The matrix and lighting->lights are borrowed, unchanged, until the next
    // reset() or the last draw of this plan, whichever comes first.
    Error append(BatchRange,const Material&,const Matrix*,const LightingInput*,unsigned& span) noexcept;
    Error seal() noexcept;
    bool ready() const noexcept {return sealed_;}
    std::size_t spans() const noexcept {return sealed_?spans_:0;}
    const BatchSpan* span(unsigned i) const noexcept {return sealed_&&i<spans_?&span_[i]:nullptr;}
    static bool supports(const LightingInput*) noexcept;
private:
    std::array<BatchSpan,max_spans> span_{};
    std::size_t spans_=0;
    bool open_=false,sealed_=false;
    Error error_=Error::invalid_phase;
};
// Selectors for the single dynamic upload. Ranges sharing mutable vertices
// are excluded from batching; their ordinary draws still use current prefixes.
class DynamicBatchRanges {
public:
    static constexpr unsigned max_occurrences=1024,max_skin_vertices=4095*3,max_quad_vertices=4096*4;
    // Called after the owning scene boundary validated all occurrence ranges.
    bool prepare(const SceneView&) noexcept;
    const std::uint8_t* skin() const noexcept {return ready_?skin_.data():nullptr;}
    const std::uint8_t* quads() const noexcept {return ready_?quad_.data():nullptr;}
    bool eligible(unsigned occurrence) const noexcept {return ready_&&eligible_[occurrence];}
    unsigned slot(unsigned occurrence) const noexcept {return slot_[occurrence];}
private:
    std::array<std::uint8_t,max_skin_vertices> skin_{};
    std::array<std::uint8_t,max_quad_vertices> quad_{};
    std::array<std::uint16_t,max_quad_vertices> owner_{};
    std::array<bool,max_occurrences> eligible_{};
    std::array<std::uint8_t,max_occurrences> slot_{};
    bool ready_=false;
};
// BUILD134: one pass over the image's skin vertices. The same checks and UV
// domain as validate(const NormalVertex*,count,&domain), made while each
// vertex is written to its GPU record (it was read once to validate and
// again to copy). False: some vertex is invalid and the domain is
// unspecified; every record is still written, as the lenient upload did.
// selectors (palette slots below 8) may be null; count may be zero.
bool pack_skin_vertices(const NormalVertex* source,std::size_t count,const std::uint8_t* selectors,
                        BatchVertex* output,UvDomain& domain) noexcept;
bool copy_skin_vertices(const NormalVertex* source,std::size_t count,NormalVertex* output,UvDomain& domain) noexcept;
}
