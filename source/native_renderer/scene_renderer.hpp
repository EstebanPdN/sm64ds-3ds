#pragma once
#include "native_model.hpp"
#include "scene_frame.hpp"
#include "model_catalog.hpp"
#include "image_catalog.hpp"
#include "native_images.hpp"
#include "screen_compositor.hpp"
#include "canvas_catalog.hpp"
#include "affine_image.hpp"

namespace sm64ds::render {
// Render-thread resource/target owner. Its input is an immutable native scene;
// there are no original game types, addresses or graphics instruction buffers.
class SceneRenderer {
public:
    struct Metrics {std::uint64_t frames=0,refused=0,loads=0,evictions=0,load_ticks=0;
        std::uint64_t batch_frames=0,batch_fallbacks=0,batch_prepare_ticks=0;
        std::uint64_t load_failures=0;
        std::uint32_t failed_bank=0;
        Error load_error=Error::none;
        std::uint64_t image_source_reads=0,image_source_hits=0;
        std::size_t image_source_bytes=0,source_peak_bytes=0,composition_bytes=0,composition_peak_bytes=0;};
    Error init(ModelCatalog,const char* bank_directory,const void* shader,std::size_t shader_bytes,
               std::size_t budget,bool timings=false,bool prefer_affine=true,ImageCatalog images={},CanvasCatalog canvases={},bool antialias=false) noexcept;
    // Startup look-ahead: queue catalog identities, then load under the next
    // render-owner fence after pinning the visible scene. Ordinary LRU/budget
    // rules still apply; a speculative failure never discards the live frame.
    Error preload_model(std::uint32_t file_id) noexcept;
    Error preload_model_images(std::uint32_t file_id) noexcept;
    Error preload_image_bank(std::uint32_t bank_id) noexcept;
    Error preload_canvas(std::uint32_t bank_id) noexcept;
    // Matrices already include the selected LCD's aspect/orientation. The
    // renderer never rescales/transforms static vertices on the CPU.
    Error draw(const SceneView& scene) noexcept {return draw(scene,scene.scene_screen);}
    Error draw(const SceneView&,Screen output) noexcept;
    // BUILD134: the exact second image of an interpolated pair, built while
    // the GPU draws the first one (Citro3DBackend::begin_overlapped) and
    // shown on the following LCD refresh (fence) or as soon as it is drawn.
    bool second_available() noexcept {return backend_.overlap_available();}
    Error draw_second(const SceneView&,Screen output,bool fence,int raised_priority,int normal_priority) noexcept;
    struct InterfacePublication {InterfaceImage image{};std::uint32_t revision=0;};
    // Two borrowed publications, uploaded inside the scene's single GPU frame.
    // A null image hides that channel. Caller retains both leases until return.
    Error draw(const SceneView&,Screen,const std::array<InterfacePublication,2>&) noexcept;
    // Synchronous presentation-side UI leases. Caller acknowledges borrowed
    // pixels only after success; no borrowed pointer survives this function.
    // Hidden channels need no GPU work; unchanged revisions need no scan/fence.
    Error interface_image(unsigned channel,std::uint32_t revision,const InterfaceImage&) noexcept;
    void hide_interface(unsigned channel) noexcept;
    // PLAYABLE MODE (temporary; NOT original-scene acceptance). Lenient draws
    // skip unsupported pixel policies and occurrences whose resources fail.
    void set_lenient(bool value) noexcept {lenient_=value;}
    void set_pace(bool value) noexcept {pace_=value;}
    void set_late_pace(bool value) noexcept {backend_.set_late_fence(value);}
    void screen_tone(int top,int bottom) noexcept {screen_tone_={top,bottom};}
    void geometry_tone(int amount) noexcept {geometry_tone_=amount;}
    // Wait until all queued GPU work has completed (diagnostic captures).
    Error finish() noexcept {if(!ready_)return Error::invalid_phase;auto e=backend_.begin(false);if(e!=Error::none)return e;return backend_.end();}
    std::uint64_t skipped_occurrences() const noexcept {return skipped_total_;}
    // Temporary 2D bridge for engines without native 2D producers. Pointers
    // are 256x192 AARRGGBB frames borrowed until the next draw() returns.
    // back: under 3D, front: over 3D (both on the 3D screen); other: the
    // non-3D screen. Null hides a plane. geometry=false hides native 3D.
    struct Bridge2D {const std::uint32_t* back=nullptr;const std::uint32_t* front=nullptr;const std::uint32_t* other=nullptr;
        const AffineImage* other_affine=nullptr;
        bool geometry=true;int brightness_three=0,brightness_other=0; // -16 black..16 white
        bool back_dirty=true,front_dirty=true,other_dirty=true; // false: reuse uploaded texture
        const std::uint32_t* sky=nullptr;bool sky_dirty=true;
        bool extend_back=false,extend_other=false,extend_front=false; // edge columns uniform per row: fill 400-wide bars
        ScreenClip geometry_tone_clip{};
        std::int8_t geometry_tone=0;
    };
    void bridge_2d(const Bridge2D& value) noexcept {bridge_=value;bridge_active_=true;}
    void clear_bridge_2d() noexcept {bridge_={};bridge_active_=false;}
    struct InterfaceMetrics {std::uint64_t publications=0,uploads=0,reuses=0,scan_pixels=0,upload_bytes=0,upload_ticks=0;};
    const InterfaceMetrics& interface_metrics() const noexcept {return interface_metrics_;}
    Error shutdown() noexcept;
    // Diagnostic completed-target readback only, outside normal frame work.
    // Destination is caller-owned linear memory, exactly400*240*3 RGB8 bytes
    // in the portrait BGR layout used by the display transfer.
    // Completed geometry only, resampled to the original 256x192 canvas.
    Error capture_geometry(Screen,std::uint32_t* argb,std::size_t pixels) noexcept;
    Error capture_rgb8(void* destination,std::size_t bytes) noexcept;
    Error capture_rgb8(Screen,void* destination,std::size_t bytes) noexcept;
    const Metrics& metrics() const noexcept {return metrics_;}
    const Statistics& backend_metrics() const noexcept {return backend_.stats();}
    // BUILD131 test-mode dump: see Citro3DBackend::arm_frame_capture.
    void arm_frame_capture(std::uint32_t* words,std::size_t word_capacity,Citro3DBackend::DrawRecord* draws,std::size_t draw_capacity) noexcept {
        backend_.arm_frame_capture(words,word_capacity,draws,draw_capacity);}
    void disarm_frame_capture() noexcept {backend_.disarm_frame_capture();}
    const Citro3DBackend::FrameCapture& frame_capture() const noexcept {return backend_.frame_capture();}
    std::size_t describe_textures(Citro3DBackend::TextureInfo* out,std::size_t capacity) const noexcept {return backend_.describe_textures(out,capacity);}
    Citro3DBackend::MeshTotals describe_meshes() const noexcept {return backend_.describe_meshes();}
    std::size_t output_vram_bytes() const noexcept {return compositor_.metrics().output_vram_bytes;}
    // Last draw's wall ticks per stage: validate, wait, resources, dynamic,
    // preflight, plan, geometry, compose, finish (zero after an early refusal).
    const std::array<std::uint32_t,9>& stage_ticks() const noexcept {return stage_ticks_;}
private:
    std::array<std::uint32_t,9> stage_ticks_{};
    void record_load_failure(std::uint32_t,Error) noexcept;
    struct Resident {
        NativeModel model;const ModelBindings* binding=nullptr;std::uint64_t used=0;
        std::array<std::uint32_t,4> required_images{};
        bool pinned=false,required_geometry=false;
    };
    std::array<Resident,64> resident_{};
    struct ImageResident {
        NativeImages images;
        const ImageBankBinding* binding=nullptr;
        std::uint64_t used=0;
        std::array<bool,ImageBank::max_images> required{};
        bool pinned=false;
    };
    std::array<ImageResident,8> image_resident_{};
    // One immutable, validated source bank; bounded independently of GPU LRU.
    // Extending an animated/menu bank must not reread its whole file per image.
    void* image_source_=nullptr;
    const ImageBankBinding* image_source_binding_=nullptr;
    ImageBank image_source_bank_{};
    bool load_trace_=false;
    unsigned trace_frames_=0;
    struct CanvasResident {
        NativeModel model;const CanvasBinding* binding=nullptr;std::uint64_t used=0;bool pinned=false;
    };
    std::array<CanvasResident,8> canvas_resident_{};
    Citro3DBackend backend_;
    ModelCatalog catalog_;
    ImageCatalog images_;
    CanvasCatalog canvases_;
    C3D_RenderTarget* target_=nullptr;
    Texture scene_surface_{};
    unsigned scene_scale_=1; // horizontal 2x supersampling; interface stays at LCD resolution
    ScreenCompositor compositor_;
    char directory_[192]{};
    std::uint64_t scene_=0,serial_=0;
    bool ready_=false,timings_=false,have_scene_=false,c3d_owned_=false;
    bool completed_target_=false;
    Metrics metrics_{};
    enum class PreloadKind {model,model_images,image_bank,canvas};
    struct Preload {std::uint32_t id;PreloadKind kind;};
    std::array<Preload,8> preload_{};
    unsigned preload_count_=0;
    Error queue_preload(std::uint32_t,PreloadKind) noexcept;
    struct InterfaceResident {Texture texture{};InterfaceCrop crop{};std::uint32_t revision=0;bool visible=false;};
    std::array<InterfaceResident,2> interface_{};
    InterfaceMetrics interface_metrics_{};
    bool lenient_=false,bridge_active_=false,pace_=true;
    Bridge2D bridge_{};
    std::array<int,2> screen_tone_{};
    int geometry_tone_=0;
    // 2D bridge planes, sampled linearly at the LCD's 1.25x from a nearest 2x
    // copy (bridge_texture_, 512x512). The top screen's engine-A planes (0, 1)
    // get it from the GPU, as the bottom map does: the CPU tiles the plane
    // once (bridge_base_, 256x256) and a VRAM render target is expanded. The
    // CPU writing the 2x copy itself cost four times the bytes to tile, flush
    // and fetch from FCRAM. Render targets must live in VRAM (6 MiB, shared
    // with the scene and the map), so a plane keeps the CPU 2x path when no
    // room is left after the map's reserve.
    enum class BridgePath:std::uint8_t{undecided,gpu,cpu};
    std::array<Texture,4> bridge_texture_{},bridge_base_{};
    std::array<BridgePath,4> bridge_path_{};
    std::array<bool,4> bridge_expand_{},bridge_surface_ready_{};
    // Pixels last published to bridge_base_[0..3] and map_foreground_
    // (256x192 words each, lazily allocated): band-delta uploads.
    std::uint32_t* frame_shadow_[5]{};
    // BUILD134: a shadow holds published pixels only after a complete upload
    // wrote it. One allocated later than its texture (an earlier malloc
    // failed) was compared as it came from malloc: a band equal to that
    // garbage was never uploaded.
    bool frame_shadow_valid_[5]{};
    Error upload_plane(unsigned index,const std::uint32_t* argb,Texture&,bool sharp,bool* changed=nullptr,std::uint32_t candidates=~0u) noexcept;
    Texture map_raster_{},map_surface_{},map_sharp_{},map_foreground_{};
    Mesh map_quad_{};
    // Fog experiment: the PICA table of the last fog seen, rebuilt on change.
    DsFog fog_source_{};FogDepth fog_depth_{};std::array<float,256> fog_table_{};bool fog_table_valid_=false;
    Mesh shadow_stream_{};          // every shadow volume of the image, as quads
    Vertex* shadow_vertices_=nullptr; // their view-space vertices (lazily allocated)
    std::size_t shadow_vertex_count_=0;
    const void* map_identity_=nullptr;
    std::uint64_t map_generation_=0;
    // BUILD129: base and OBJ bands of the foreground last published to
    // map_foreground_ (its shadow). Valid only after a successful upload.
    bool map_hint_valid_=false;
    const void* map_hint_base_=nullptr;
    std::uint64_t map_hint_generation_=0;
    std::uint32_t map_hint_bands_=~0u;
    // BUILD129: map_sharp_ still shows the last drawn map when nothing it
    // samples changed since (raster, foreground, affine, backdrop, wrap):
    // its two passes are skipped. map_dirty_ is set by any changing upload.
    bool map_drawn_=false,map_dirty_=true;
    AffineImage map_drawn_image_{};
    Error initialize_map_surfaces() noexcept;
    Error initialize_bridge_surfaces() noexcept;
    Error ensure_screen_quad() noexcept;
    Error upload_shadows(const SceneView&) noexcept;
    Error draw_shadows(const SceneView&) noexcept;
    Error expand_bridge_planes() noexcept;
    Error upload_map(const AffineImage&) noexcept;
    Error draw_map(const AffineImage&) noexcept;
    Error draw_map_passes(const AffineImage&) noexcept;
    std::array<bool,1024> skip_{};
    std::uint64_t skipped_total_=0;
    Error compose_bridge(const SceneView&,Screen,const OrderedLayers&) noexcept;
    Error screen_layer(const ImageLayer&,ScreenLayer&) noexcept;
    Mesh quad_stream_{},skin_stream_{};
    // The GPU may still read the first image's skin vertices: the second
    // image writes its own stream.
    Mesh skin_stream_second_{};
    bool second_=false,second_fence_=false;int second_raised_=-1,second_normal_=-1;
    NativeBatchPlan batch_plan_;
    DynamicBatchRanges dynamic_ranges_;
    struct BatchCommand {std::uint32_t occurrence=0;std::int32_t part=-1;unsigned span=0;bool batch=false;};
    std::array<BatchCommand,4096> batch_command_{};
    std::size_t batch_commands_=0;
    bool batches_=false,batch_cache_=true;
    std::size_t batch_cache_budget() const noexcept;
    std::size_t quad_capacity_=0;
    ScreenLayer* layers_=nullptr;
    std::size_t layer_capacity_=0;
    Resident* find(std::uint32_t file_id) noexcept;
    // BUILD134: find() runs four or five times per occurrence, each a walk
    // over 64 residents and their bindings. Last slot found per identity; a
    // hit is checked against the slot and a miss walks as before.
    std::array<std::uint8_t,64> find_hint_{};
    bool cuts_=false; // perf_switch.hpp, sampled at init
    UvDomain quad_domain_{}; // of this image's quads, from the frame boundary
    ImageResident* find_images(std::uint32_t bank_id) noexcept;
    CanvasResident* find_canvas(std::uint32_t bank_id) noexcept;
    bool evict() noexcept;
    Error prepare(const SceneView&) noexcept;
    Error require_image(std::uint32_t bank,std::uint16_t image) noexcept;
    Error layer_storage(std::size_t) noexcept;
    Error compose(const SceneView&,Screen,const OrderedLayers&) noexcept;
    Error load(Resident&,const ModelBindings&) noexcept;
    Error load(ImageResident&,const ImageBankBinding&) noexcept;
    Error load(CanvasResident&,const CanvasBinding&) noexcept;
    Error upload_interface(unsigned,std::uint32_t,const InterfaceImage&) noexcept;
    Error draw_frame(const SceneView&,Screen,const std::array<InterfacePublication,2>*) noexcept;
    bool prepare_batches(const SceneView&) noexcept;
    Error draw_occurrence(const SceneView&,std::size_t,int part=-1) noexcept;
};
}
