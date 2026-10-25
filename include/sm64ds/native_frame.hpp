#pragma once
#include "sm64ds/game_draw.hpp"
#include "sm64ds/game_input.hpp"
#include "sm64ds/game_renderer.hpp"
#include "sm64ds/game_textures.hpp"
#include "sm64ds/native_2d_cache.hpp"
#include <memory>
#include <vector>

namespace sm64ds {
// Glue for the original begin/step loop. This class does not run game logic,
// choose a scene, supply a camera, or replace a missed simulation tick.
//
// Startup: gfxInitDefault(), init(). Each display VBlank: sample_input().
// Each ORIGINAL loop iteration: begin_tick(), original touch/pad samplers,
// input_ready(), original scene update/render, end_tick(). Each presentation:
// present(). The original runtime owns its VBlank divider and game cadence.
// Contains three 256x256 2D images and their exact input baseline: allocate statically or on the host heap,
// never on the application's small default stack.
class NativeFrame {
public:
    NativeFrame():textures_(game_geometry()) {}
    bool init(std::string& error);
    void sample_input();
    bool begin_tick(std::string& error);
    void input_ready();
    bool end_tick(std::string& error);
    bool present(std::string& error);
    // Quiesced diagnostic checkpoint of real native storage. The writer logs
    // its outcome separately; diagnostic I/O never changes game success.
    bool snapshot_memory(const char* prefix,const char* phase);
    void exit();
    // Development evidence from the first completed frame after the original
    // scene-1 request, plus checkpoints on original tick 180 and the first
    // safely observed ready title. These flags never select a scene or change
    // game input.
    void request_scene_debug_snapshot() {
        scene_snapshot_pending_=scene_snapshot_requested_=true;
        scene_visible_snapshot_enabled_=true;
    }
    // Observe the first ordinary scene-3 request, without asserting that its
    // Stage has initialized. Preserve the next completed stream/presentation.
    bool request_stage_debug_snapshot() {
        if(!stage_snapshot_requested_) {
            stage_snapshot_requested_=stage_snapshot_pending_=true;
            return true;
        }
        return false;
    }
    const GameInputSample& input() const {return tick_input_;}
    const GameRenderer::Stats& render_stats() const {return renderer_.stats;}
    const Native2DScanoutCache::Stats& scanout_cache_stats() const {return scanout_cache_.stats();}
    std::uint64_t screen_revision() const {return scanout_cache_.revision();}
    struct Stats {
        uint64_t input_samples=0,game_ticks=0,presentations=0,captures=0;
        uint64_t decode_ticks=0,scanout_ticks=0,submit_ticks=0;
        uint32_t words=0;
    };
    Stats stats;
    GameProjection projection;
    // Optional seat for the ORIGINAL camera input bridge. The New3DS C-stick
    // is sampled separately from the DS directional pad. Without this callback
    // it is available through input(), but camera support must not be claimed.
    using CameraInputHook=void (*)(void* context,const GameInputSample& input);
    void set_camera_input_hook(CameraInputHook hook,void* context) {
        camera_hook_=hook;camera_context_=context;
    }
    static void publish_input_hook(void* context) {
        static_cast<NativeFrame*>(context)->input_ready();
    }
private:
    GameRenderer renderer_;
    GameTextures textures_;
    GameInputLatch latch_;
    GameInputSample tick_input_{};
    GameInputSample input_previous_{};
    Native2DFrame screens_;
    // Allocate original-resolution capture storage only when the original
    // scene arms a valid composited Source A capture into an LCDC bank.
    std::unique_ptr<Native2DFrame> capture_layers_;
    std::vector<std::uint8_t> capture_rgba_;
    std::vector<ntr::GxTriangle> triangles_;
    CameraInputHook camera_hook_=nullptr;
    void* camera_context_=nullptr;
    bool initialized_=false,tick_open_=false,input_published_=false;
    bool stage_snapshot_requested_=false,stage_snapshot_pending_=false;
    bool scene_snapshot_pending_=false,scene_snapshot_requested_=false;
    bool scene_visible_snapshot_enabled_=false,scene_visible_snapshot_pending_=false;
    bool scene_ready_snapshot_taken_=false,scene_ready_snapshot_pending_=false;
    bool requested_memory_snapshot_taken_=false;
    std::uintptr_t diagnostic_heap_start_=0;
    std::size_t diagnostic_heap_bytes_=0;
    struct TitleDiagnostic {
        std::uintptr_t context=0;
        std::uint32_t valid=0;
        std::int16_t state=-1,substate=-1,pending=-1,subpending=-1;
        std::int32_t counter=0,subcounter=0,opacity=-1,input_lock=0,touch_release=0;
        std::uint16_t keys_held=0,keys_prior=0,touch_x=0,touch_y=0,touch_now=0,touch_prior=0;
        bool ready=false;
    };
    TitleDiagnostic title_previous_{};
    bool title_logged_=false;
    bool record_title_diagnostic();
    void write_snapshot_stream(const char* prefix);
    void write_snapshot_scanout(const char* prefix);
    void write_snapshot_metadata(const char* prefix);
    Native2DScanoutCache scanout_cache_;
};
}
