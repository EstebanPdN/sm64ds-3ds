#pragma once

// Renderer-owned data. No retail, ntr, FIFO, VRAM-bank or display-list types.
#include <cstddef>
#include <cstdint>
#include <array>
#include <cstring>

namespace sm64ds::render {

enum class Error {
    none, invalid_data, invalid_phase, stale_handle, capacity, budget,
    out_of_memory, gpu_failure, unsupported
};
const char* describe(Error error) noexcept;

struct Vertex {
    float x, y, z;
    float u, v;
    std::uint8_t r, g, b, a;
};
static_assert(sizeof(Vertex) == 24, "GPU vertex ABI");
static_assert(offsetof(Vertex,x) == 0 && offsetof(Vertex,u) == 12 && offsetof(Vertex,r) == 20,
              "PICA loaders must consume the entire declared stride");
// Optional native payload. Signed raw normals retain their ten-bit lattice;
// the last lane selects NORMAL + explicit offset(0), NORMAL + live offset(1),
// or a direct TEXCOORD override(2).
struct NormalVertex { Vertex base;std::int16_t x,y,z,uv_source; };
static_assert(sizeof(NormalVertex)==32 && offsetof(NormalVertex,x)==24);

// Row-major, column-vector transform. Already includes the target orientation
// and PICA clip convention. Its producer must not bake camera transforms into
// static vertex resources. Stereo changes this matrix, not the mesh.
struct Matrix { float row[4][4]; };
Matrix identity() noexcept;
bool finite(const Matrix& matrix) noexcept;

struct MeshView {
    const Vertex* vertices = nullptr;
    std::size_t vertex_count = 0;
    const std::uint16_t* indices = nullptr;
    std::size_t index_count = 0;
};
// Prepared GPU payload: little-endian float32 Vertex records and uint16
// indices. Byte spans can be unaligned, e.g. members of a prepared asset bank.
// The loader need not create a second decoded CPU mesh to upload these bytes.
struct MeshBytes {
    const std::uint8_t* vertices = nullptr;
    std::size_t vertex_bytes = 0;
    const std::uint8_t* indices = nullptr;
    std::size_t index_bytes = 0;
    bool normals = false;
};
struct UvDomain {
    float low[2]{},high[2]{};
    std::uint8_t fractional_bits[2]{};
    void include(float u,float v) noexcept {
        const float coordinates[2]={u,v};
        for(unsigned i=0;i<2;++i){
            const auto value=coordinates[i];if(value<low[i])low[i]=value;if(value>high[i])high[i]=value;
            std::uint32_t bits;std::memcpy(&bits,&value,4);
            const unsigned exponent=(bits>>23)&255;
            const std::uint32_t significand=(bits&0x7fffff)|(exponent?0x800000:0);
            if(!significand)continue;
            const int count=(exponent?150-int(exponent):149)-__builtin_ctz(significand);
            if(count>fractional_bits[i])fractional_bits[i]=std::uint8_t(count);
        }
    }
    bool accepts(unsigned source_width,unsigned source_height) const noexcept;
};
Error validate(const MeshView& mesh,UvDomain* uv_domain=nullptr) noexcept;
Error validate(const MeshBytes& mesh,UvDomain* uv_domain=nullptr) noexcept;
// Current typed triangle-stream vertices. Its owner creates immutable identity
// indices once; this boundary validates only the values that can change.
Error validate(const NormalVertex*,std::size_t count,UvDomain* uv_domain=nullptr) noexcept;
std::size_t mesh_bytes(const MeshView& mesh) noexcept;
bool texture_dimensions(unsigned width, unsigned height) noexcept;
enum class TextureFormat : std::uint32_t { rgba8,alpha4 };
std::size_t texture_bytes(unsigned width,unsigned height,TextureFormat) noexcept;

// Import/load-time conversion only: tightly packed RGBA bytes, top row first,
// to PICA RGBA8 (ABGR bytes, Morton tiles, bottom source row first).
// Native UV (0,0) denotes source top-left. Reorient offline, so nearest sampling
// retains floor(S),floor(T) at exact integer boundaries. Buffers cannot overlap.
Error cook_rgba8(const std::uint8_t* source, std::size_t source_bytes,
                 unsigned width, unsigned height, std::uint8_t* destination,
                 std::size_t destination_bytes) noexcept;

struct MeshTag {};
struct TextureTag {};
template<class Tag> struct Handle {
    std::uint64_t generation = 0;
    std::uint16_t slot = 0;
    const void* domain = nullptr;
    explicit operator bool() const noexcept { return generation != 0; }
    bool operator==(Handle b) const noexcept {
        return generation == b.generation && slot == b.slot && domain == b.domain;
    }
    bool operator!=(Handle b) const noexcept { return !(*this == b); }
};
using Mesh = Handle<MeshTag>;
using Texture = Handle<TextureTag>;

// Premultiplied sources are native render intermediates. Their RGB already
// includes alpha; final composition must not multiply it a second time.
enum class Blend : std::uint8_t { opaque, alpha, premultiplied, highlight };
enum class Cull : std::uint8_t { none, back, front };
// Value3 stays reserved by the on-disk two-bit sampler contract. Border is
// an explicit runtime-only mode for immutable native map textures.
enum class Wrap : std::uint8_t { clamp, repeat, mirror, transparent_border=4 };
enum class UvMode : std::uint8_t { affine, fixed, normal };
struct FixedUv {
    // Two native affine rows, signed 20.12 coefficients. The vertex shader
    // floors each sum and wraps to signed16 BEFORE texture interpolation.
    std::array<std::int32_t,6> coefficient{{4096,0,0,0,4096,0}};
    std::uint16_t width=8,height=8; // normalized source-vertex UV dimensions
    std::array<std::int16_t,2> offset{}; // normal-source offset; unused by affine-source fixed math
    bool operator==(const FixedUv& b) const noexcept {
        return coefficient==b.coefficient && width==b.width && height==b.height && offset==b.offset;
    }
};
// Same bounded coefficient storage, interpreted by UvMode. Normal mode uses
// two three-component rows, shift21 and the separate NORMAL-latched offset.
using NormalUv=FixedUv;
struct Material {
    Texture texture{}; // zero means vertex colour only
    Blend blend = Blend::opaque;
    Cull cull = Cull::back;
    Wrap wrap_s = Wrap::clamp, wrap_t = Wrap::clamp;
    bool depth_test = true;
    bool depth_write = true;
    // Indexed sprite banks retain palette-zero RGB with an alpha mask. PICA's
    // alpha combiner can ignore that mask for opaque-zero draws, so one image
    // serves both states without duplicate textures or per-frame conversion.
    bool sample_alpha = true;
    std::uint8_t alpha_cutoff = 0; // discard alpha <= cutoff
    // Affine normalized-UV transform and a native RGBA multiplier. Animated
    // materials update these uniforms while immutable vertex buffers stay put.
    std::array<float,6> uv{{1,0,0,0,1,0}};
    UvMode uv_mode=UvMode::affine;
    FixedUv fixed_uv{};
    NormalUv& normal_uv() noexcept {return fixed_uv;}
    const NormalUv& normal_uv() const noexcept {return fixed_uv;}
    std::uint32_t tint = 0xffffffffu; // 0xRRGGBBAA
    std::int8_t brightness = 0; // RGB tone after sampling; alpha is unchanged
    // DS POLYGON_ATTR bit 15: the image's fog applies to this polygon. Only
    // set while the fog experiment is on (see ds_fog.hpp).
    bool fog = false;
    // BUILD140: DS polygon ID 1. Every SM64DS shadow is drawn with ID 1
    // (ShadowModel::RenderAll) and the DS draws no shadow over a pixel of its
    // own ID: characters, trees and most actors carry ID 1, so their own
    // shadow volume never darkens them; the stage (ID 0) still receives it.
    bool shadow_id = false;
    bool operator==(const Material& b) const noexcept;
};
bool valid(const Material& material) noexcept;

// A frame has an upload phase followed by any number of ordered passes.
// Mutation is impossible after the first pass: queued GPU work owns resources
// until the next successful FrameBegin, not merely until FrameEnd returns.
class FrameState {
public:
    enum class Phase { idle, upload, drawing };
    bool begin() noexcept;
    bool pass() noexcept;
    bool end() noexcept;
    bool can_mutate() const noexcept { return phase_ == Phase::upload; }
    bool can_draw() const noexcept { return phase_ == Phase::drawing; }
    Phase phase() const noexcept { return phase_; }
private:
    Phase phase_ = Phase::idle;
};

// Fixed metadata storage; slots are invalidated when released. Exhausted
// generation counters retire instead of resurrecting an ancient handle.
// This pool owns metadata only; the GPU backend owns and releases each value.
template<class T, class Tag, std::size_t N> class Slots {
    static_assert(N > 0 && N <= 65536);
    struct Entry { T value{}; std::uint64_t generation = 1; bool live = false; };
    Entry entries_[N]{};
public:
    Slots() = default;
    Slots(const Slots&) = delete;
    Slots& operator=(const Slots&) = delete;
    Handle<Tag> available() const noexcept {
        for (std::size_t i = 0; i < N; ++i)
            if (!entries_[i].live && entries_[i].generation)
                return {entries_[i].generation, std::uint16_t(i), this};
        return {};
    }
    T* commit(Handle<Tag> h, const T& value) noexcept {
        if (!h || h.slot >= N || h.domain != this) return nullptr;
        auto& e = entries_[h.slot];
        if (e.live || e.generation != h.generation) return nullptr;
        e.value = value; e.live = true; return &e.value;
    }
    T* get(Handle<Tag> h) noexcept {
        if (!h || h.slot >= N || h.domain != this) return nullptr;
        auto& e = entries_[h.slot];
        return e.live && e.generation == h.generation ? &e.value : nullptr;
    }
    const T* get(Handle<Tag> h) const noexcept {
        if (!h || h.slot >= N || h.domain != this) return nullptr;
        const auto& e = entries_[h.slot];
        return e.live && e.generation == h.generation ? &e.value : nullptr;
    }
    bool release(Handle<Tag> h) noexcept {
        if (!get(h)) return false;
        auto& e = entries_[h.slot];
        e.value = {}; e.live = false; ++e.generation; return true;
    }
    template<class F> void each(F&& f) {
        for (std::size_t i = 0; i < N; ++i)
            if (entries_[i].live) f(Handle<Tag>{entries_[i].generation, std::uint16_t(i), this}, entries_[i].value);
    }
};

struct Statistics {
    std::uint64_t draws = 0, triangles = 0, mesh_binds = 0;
    std::uint64_t material_binds = 0, matrix_uploads = 0;
    std::uint64_t uv_uploads=0,tint_uploads=0,sampler_binds=0,pipeline_changes=0;
    std::uint64_t lighting_preparations=0,lighting_uniform_rows=0,lighting_state_changes=0;
    std::uint64_t lit_draws=0;
    std::uint64_t affine_draws=0,shader_binds=0;
    std::uint64_t mesh_upload_bytes = 0, texture_upload_bytes = 0;
    std::uint64_t resource_creates = 0, resource_destroys = 0;
    std::uint64_t rejected = 0;
    std::uint64_t wait_ticks = 0, submit_ticks = 0, upload_ticks = 0;
    std::uint64_t sdk_draw_ticks=0,sdk_draw_samples=0;
    // BUILD140, measured with timings off too: this image's GPU/LCD waits, and
    // the overlapped second image's submit_overlapped entry/exit ticks.
    std::uint64_t blocked_ticks=0,tail_begin_tick=0,tail_end_tick=0;
    std::size_t live_linear_bytes = 0, peak_linear_bytes = 0;
    // Renderer-owned render textures and their depth attachments. External
    // LCD targets/SDK metadata are not included in these resource counters.
    std::size_t live_vram_bytes=0,peak_vram_bytes=0;
    std::size_t owned_gpu_bytes() const noexcept {return live_linear_bytes+live_vram_bytes;}
    // SDK's previous completed queue time, not isolated raster time. In an
    // emulator these are guest timings, never physical console measurements.
    float previous_gpu_queue_ms = 0;
};
}
