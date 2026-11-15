#pragma once
#include "contracts.hpp"
#include "mesh_groups.hpp"
#include "lighting_uniforms.hpp"
#include "interface_image.hpp"
#include "indexed_draw_packet.hpp"
#include "native_batch.hpp"
#include "pica_state.hpp"
#include <3ds.h>
#include <citro3d.h>
#include "citro3d_program_cache.hpp"

namespace sm64ds::render {
// One render-thread owner. Caller owns C3D_Init/Fini and external targets; this
// backend owns its render-to-texture surfaces. No
// other renderer may submit or change C3D state between begin() and end().
// Call shutdown() before C3D_Fini, and do not retain handles across object
// lifetimes. init() does not allocate from the retail game's C++ heap.
class Citro3DBackend {
public:
    Citro3DBackend() = default;
    Citro3DBackend(const Citro3DBackend&) = delete;
    Citro3DBackend& operator=(const Citro3DBackend&) = delete;
    // Compiled shader bytes are trusted build input and must remain alive.
    Error init(const void* shader, std::size_t bytes, std::size_t resource_budget,
               bool measure_timings = false, bool prefer_affine = true) noexcept;
    Error shutdown() noexcept;
    Error begin(bool pace = true) noexcept;
    // BUILD134 (render worker on the game's core, Lock FPS 60/Unlocked): the
    // second image of an update is built in its own command buffer while the
    // GPU still draws the first, instead of waiting for it in FrameBegin.
    // Its GPU commands are recorded and queued by submit_overlapped().
    bool overlap_available() noexcept;
    bool overlapped() const noexcept {return overlap_;}
    Error begin_overlapped() noexcept;
    Error end_overlapped() noexcept;
    // Waits (blocked, at raised_priority when >=0) for the first image to
    // finish, queues the recorded commands, and presents `target` on its
    // screen one LCD refresh after the first image (fence) or at once.
    Error submit_overlapped(C3D_RenderTarget* target,bool fence,int raised_priority,int normal_priority) noexcept;
    void discard_overlapped() noexcept;
    Error create_mesh(const MeshView&, Mesh& output) noexcept;
    Error create_mesh_bytes(const MeshBytes&, Mesh& output) noexcept;
    Error create_mesh_group(const MeshGroups&,unsigned group,Mesh& output) noexcept;
    // Persistent dynamic storage. The index pattern is allocated/uploaded once;
    // subsequent frames flush only their active vertex prefix after the fence.
    Error create_quad_stream(std::size_t capacity,Mesh& output,bool palette=false) noexcept;
    // validated: the domain validate_quads() returned for exactly these
    // vertices; the stream then does not validate them a second time.
    Error update_quad_stream(Mesh,const Vertex*,std::size_t vertices,const std::uint8_t* selectors=nullptr,
                             const UvDomain* validated=nullptr) noexcept;
    // PLAYABLE MODE: persistent triangle-list normal mesh rewritten per frame.
    Error create_skin_stream(std::size_t vertices,Mesh& output,bool palette=false) noexcept;
    Error update_skin_stream(Mesh,const NormalVertex*,std::size_t vertices,const std::uint8_t* selectors=nullptr) noexcept;
    bool batches_ready() const noexcept {return batch_ready_;}
    // Load-time geometry gets fixed slot selectors. Ranges contain no borrowed
    // CPU pointer; all instance uniforms are copied at draw submission.
    Error create_resident_batch(const BatchGeometry&,unsigned first,unsigned parts,Mesh&,BatchRange*) noexcept;
    Error draw_batch(const NativeBatchPlan&,unsigned span) noexcept;
    Error create_texture(unsigned width, unsigned height, const void* tiled_abgr,
                         std::size_t bytes, Texture& output,TextureFormat format=TextureFormat::rgba8) noexcept;
    // Fence-protected UI upload directly into owned texture storage. Reuses
    // equal-sized allocations, crops transparent margins, clears padding.
    // On a resize the old allocation is released before reserving its successor.
    Error upload_interface(const InterfaceImage&,Texture&,InterfaceCrop&) noexcept;
    // PLAYABLE MODE temporary 2D bridge: one complete 256x192 AARRGGBB frame
    // into an owned 256x256 RGBA8 texture (allocated once, rows 192+ stay 0).
    // Same fence rule as upload_interface: only between begin() and the pass.
    // shadow: optional caller copy of the pixels last published to this
    // texture (256x192 words); with it only changed 8-row bands are re-tiled.
    // changed (optional): set when any texel was rewritten and published.
    // candidates: bands (bit n = rows 8n..8n+7) that may differ from shadow;
    // the caller guarantees every other band equals it (BUILD129 map hint).
    Error upload_frame_image(const std::uint32_t* argb,Texture&,bool sharp=true,std::uint32_t* shadow=nullptr,bool* changed=nullptr,
                             std::uint32_t candidates=~0u) noexcept;
    // Changed immutable map only: power-of-two ARGB raster, nearest sampled.
    Error upload_map_image(const std::uint32_t*,unsigned width,unsigned height,Texture&) noexcept;
    // Render-to-texture allocations/explicit destruction occur between frames.
    // SDK1.7.1 forbids RenderTargetDelete while a frame is open. retire() is
    // allowed while queued; the next begin() deletes these outside that frame.
    // Texture+optional depth attachment share the ordinary resource budget.
    // Texture coordinates follow the rendered framebuffer axes. Composition
    // supplies its explicit UV orientation (e.g. V flip for a Y-up view).
    // vram=false: a render target in the linear heap (FCRAM), for large
    // surfaces the 6 MiB VRAM cannot hold alongside the framebuffers.
    Error create_surface(unsigned width,unsigned height,bool depth,Texture& output,bool linear=false,bool vram=true) noexcept;
    C3D_RenderTarget* surface(Texture) const noexcept; // borrowed until retirement/destruction
    Error destroy(Mesh) noexcept;
    Error destroy(Texture) noexcept;
    // Logical release may occur after submission; storage remains alive until
    // the next successful begin() drains the GPU. Same render-thread owner.
    Error retire(Mesh) noexcept;
    Error retire(Texture) noexcept;
    bool can_upload() const noexcept { return ready_ && state_.can_mutate(); }
    bool can_draw() const noexcept { return ready_ && state_.can_draw(); }
    bool between_frames() const noexcept {return ready_&&state_.phase()==FrameState::Phase::idle;}
    bool texture_alive(Texture t) const noexcept {
        if(!t)return ready_;
        const auto* resource=textures_.get(t);
        return ready_ && resource && !resource->retired;
    }
    bool texture_ready(Texture t) const noexcept {
        if(!t)return ready_;
        const auto* r=textures_.get(t);
        return ready_&&r&&!r->retired&&(!r->target||r->initialized);
    }
    bool fixed_uv_compatible(Mesh mesh,const FixedUv& uv) const noexcept {
        const auto* resource=meshes_.get(mesh);
        return resource&&!resource->retired&&resource->uv.accepts(uv.width,uv.height);
    }
    bool normal_uv_compatible(Mesh mesh,const NormalUv& uv) const noexcept {
        const auto* resource=meshes_.get(mesh);
        return resource&&!resource->retired&&resource->normals&&resource->uv.accepts(uv.width,uv.height);
    }
    bool lighting_compatible(Mesh mesh) const noexcept {
        const auto* resource=meshes_.get(mesh);
        return resource&&!resource->retired&&resource->normals;
    }
    struct Viewport {unsigned x,y,width,height;};
    Error pass(C3D_RenderTarget*, bool clear, std::uint32_t rgba,const Viewport* area=nullptr) noexcept;
    Error draw(Mesh, const Material&, const Matrix& mvp,
               std::size_t first_index = 0, std::size_t index_count = 0,
               const LightingInput* lighting = nullptr) noexcept;
    // One stage of the DS shadow volumes over closed meshes whose faces wind
    // outwards (black vertices, their alpha is the darkening). mask: where a
    // far (back) face fails the depth test, the stencil takes `reference`;
    // nothing else is written. Otherwise: near (front) faces that pass the
    // depth test darken the pixels holding `reference` and release them, so
    // overlapping volumes darken a pixel once. Needs a stencil target.
    Error draw_shadow(Mesh,const Matrix& mvp,bool mask,std::uint8_t reference) noexcept;
    // Fog experiment, for the pass in progress: a PICA fog table (128 values
    // and 128 changes, see ds_fog.hpp) and its colour (red in the low byte)
    // for the materials that carry the fog bit; nullptr draws without fog.
    void fog(const float* table,std::uint32_t colour) noexcept;
    Error end() noexcept;
    // BUILD134 (Old 3DS, 60 FPS pairs): hold this image's LCD copy until the
    // previous image's swap reached the screen, checked at the end of the
    // CPU work instead of at begin() (where the wait would idle the core).
    void set_late_fence(bool value) noexcept {late_fence_=value;}
    const Statistics& stats() const noexcept { return stats_; }
    // BUILD131 test-mode dump. The next frame that ends copies its PICA
    // command words and one record per draw into caller storage. Arm and read
    // only while the render worker is drained; an unarmed frame pays one
    // predictable branch per draw.
    struct DrawRecord {
        std::uint32_t mesh,texture,first,count,command_word;
        std::uint16_t width,height;
        std::uint8_t format,flags,blend,cull,depth,pass; // flags: 1 lit, 2 batch, 4 affine, 8 sampled alpha
        std::uint16_t alpha_cutoff;
    };
    struct FrameCapture {
        std::uint32_t* words=nullptr;std::size_t word_capacity=0,word_count=0,word_total=0;
        DrawRecord* draws=nullptr;std::size_t draw_capacity=0,draw_count=0,draw_total=0;
        unsigned passes=0;bool armed=false,complete=false;
        const std::uint32_t* base=nullptr; // libctru's buffer base at begin()
    };
    void arm_frame_capture(std::uint32_t* words,std::size_t word_capacity,DrawRecord* draws,std::size_t draw_capacity) noexcept {
        capture_={};capture_.words=words;capture_.word_capacity=word_capacity;capture_.draws=draws;capture_.draw_capacity=draw_capacity;
        capture_.armed=words!=nullptr;
    }
    void disarm_frame_capture() noexcept {capture_={};}
    const FrameCapture& frame_capture() const noexcept {return capture_;}
    struct TextureInfo {std::uint32_t slot,address,bytes;std::uint16_t width,height;std::uint8_t format,vram,target,initialized;};
    std::size_t describe_textures(TextureInfo* out,std::size_t capacity) const noexcept;
    struct MeshTotals {unsigned meshes=0,batches=0;std::size_t bytes=0,indices=0;};
    MeshTotals describe_meshes() const noexcept;
private:
    FrameCapture capture_{};
    struct MeshResource {
        void* storage = nullptr;
        const std::uint16_t* indices = nullptr;
        std::size_t count = 0, bytes = 0,vertex_bytes=0;
        std::uint32_t index_configuration=0;
        std::size_t quad_capacity=0;
        C3D_BufInfo binding{};
        UvDomain uv{};
        bool retired = false,normals=false,batch=false,batch_source_valid=true;
    };
    struct TextureResource {
        C3D_Tex gpu{};
        std::size_t bytes=0;
        C3D_RenderTarget* target=nullptr;
        bool retired=false,initialized=false,vram=false;
    };
    Slots<MeshResource, MeshTag, 256> meshes_;
    // Menu layouts retain separate animated-cell banks. These CPU handles
    // share the same byte budget; 128 slots forced otherwise needless reloads.
    Slots<TextureResource, TextureTag, 256> textures_;
    // BUILD129: pass() finds its target's surface (or that it has none)
    // here instead of scanning all 256 texture slots for each of ~5 passes
    // per image. create_surface and destroy, the only places that add or
    // remove render targets, clear it.
    struct TargetSurface {C3D_RenderTarget* target=nullptr;TextureResource* resource=nullptr;};
    TargetSurface target_surfaces_[8]{};unsigned target_surface_count_=0;
    FrameState state_;
    DVLB_s* shader_ = nullptr;
    shaderProgram_s program_{};
    // Optional second DVLE shares code storage/uniform locations with the
    // general kernel. Ordinary unlit draws enter its short, branch-free path.
    shaderProgram_s affine_program_{};
    bool affine_ready_=false,affine_bound_=false;
    shaderProgram_s batch_program_{};
    bool batch_ready_=false,batch_bound_=false;
    int batch_uv_uniform_=-1,batch_tint_uniform_=-1;
    C3D_Tex unused_sampler_{};
    int matrix_uniform_ = -1;
    int uv_uniform_ = -1, tint_uniform_ = -1;
    int fixed_uv_uniform_ = -1, fixed_uv_switch_ = -1;
    int normal_uv_uniform_ = -1, normal_uv_switch_ = -1;
    int light_normal_uniform_=-1,light_direction_uniform_=-1,light_colour_uniform_=-1;
    int light_material_uniform_=-1,shininess_uniform_=-1,light_loop_uniform_=-1;
    int lighting_switch_=-1,lights_switch_=-1,specular_switch_=-1,shininess_switch_=-1;
    int toon_switch_=-1;
    int grey_switch_=-1;
    LightingCache lighting_cache_{};
    bool lighting_valid_=false,lighting_enabled_=false;
    bool ready_ = false, timings_ = false, present_fence_ = true, late_fence_ = false;
    bool program_cache_valid_=true;
    bool retirements_ = false;
    std::size_t budget_ = 0, live_ = 0, peak_ = 0;
    std::size_t vram_=0,peak_vram_=0;
    const void* active_colour_=nullptr;
    bool active_depth_=false;
    Mesh bound_mesh_{};
    Material bound_material_{};
    Matrix bound_matrix_{};
    bool material_valid_ = false, matrix_valid_ = false,vertex_material_valid_=false;
    std::array<Matrix,8> palette_matrix_{};
    std::array<BatchBasis,8> palette_basis_{};
    std::uint8_t palette_matrix_valid_=0,palette_basis_valid_=0;
    UvMode general_uv_mode_=UvMode::affine;
    bool general_uv_valid_=false;
    unsigned bound_sample_width_=0,bound_sample_height_=0;
    bool bound_alpha_mask_=false;
    C3D_FogLut fog_table_{};bool fog_ready_=false;
    // 0 ordinary draw, 1 shadow mask, 2 shadow render; the reference of 1/2.
    std::uint8_t shadow_stage_=0,shadow_reference_=0,bound_shadow_stage_=0,bound_shadow_reference_=0;
    unsigned sdk_sample_phase_=0;
    bool packed_draws_=true;
    IndexedDrawPacket draw_packet_{};
    // BUILD134 exact cost cuts (perf_switch.hpp; sin-recortes.txt = old paths).
    // cuts_: sampled once at init. raw_state_: cuts_, packed draws and an SDK
    // whose shadow layout was confirmed; then a pass still states its whole
    // attribute/buffer/effect blocks through the SDK and the draws inside it
    // write only the registers that change (pica_state.hpp).
    bool cuts_=false,raw_state_=false;
    bool overlap_=false,overlap_checked_=false,overlap_ok_=false;
    std::uint32_t* overlap_buffer_=nullptr;
    gxCmdQueue_s overlap_queue_{};
    gxCmdEntry_s overlap_entries_[32]{};
    Citro3DShadow shadow_{};
    pica::Deltas deltas_{};          // this draw's single-register writes
    std::uint8_t bound_layout_=0;    // attribute loaders in the SDK shadow: 3, 4 or 5; 0 unknown
    bool buffers_resident_=false;    // this pass sent the complete buffer table
    // Units 1/2 (never sampled): the old path re-sends them with the first
    // draw of every pass. Later passes of an image skip that when the units
    // still hold the neutral texture with the parameters it has at that draw.
    bool aux_units_pending_=false,aux_units_valid_=false;
    std::uint32_t aux_units_param_=0;
    C3D_TexEnv neutral_stage_{};
    Statistics stats_{};
    Error reject(Error error) noexcept { ++stats_.rejected; return error; }
    void account(std::size_t bytes,bool vram=false) noexcept;
    Error draw_impl(Mesh,const Material&,const Matrix&,std::size_t,std::size_t,const LightingInput*,const BatchSpan*) noexcept;
    Error upload_mesh(const MeshBytes&,const UvDomain&,Mesh& output,const MeshGroups* groups=nullptr,unsigned group=0) noexcept;
};
}
