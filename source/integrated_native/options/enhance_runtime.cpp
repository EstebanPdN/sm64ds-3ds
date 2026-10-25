// visual-enhancements phase 2: see enhance_runtime.h.
#include "enhance_runtime.h"
#include "enhance_overlay.h"
#include "enhance_settings.h"
#include "diagnostic_panel.hpp"
#include "native_diagnostics.hpp"
#include "native_lower_state.hpp"
#include "native_lower_painter.hpp"
#include "overlay_publication.hpp"
#include "developer_keys.hpp"
extern "C" bool sm64ds_intro_skip_input(bool,bool);
extern "C" bool sm64ds_intro_skip_hint();
#include "cstick_camera.h"
#include "native_gameplay_camera.hpp"
extern "C" unsigned sm64ds_render60_scene() __attribute__((weak));
extern "C" int sm64ds_front_options_active();
extern "C" void sm64ds_front_options_update(uint32_t,bool,bool,int,int);
extern "C" uint64_t sm64ds_native_pause_image_key();
extern "C" void sm64ds_native_pause_image(uint32_t*);
#include <3ds.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <malloc.h>


namespace enhance_runtime {
namespace {
struct Channel : OverlayPublication {
    int width = 0, height = 0;
    uint32_t* buffer[2] = {nullptr, nullptr};
    bool allocate(int w, int h) {
        if (buffer[0]) return true;
        width = w; height = h;
        buffer[0] = new (std::nothrow) uint32_t[size_t(w) * h];
        buffer[1] = new (std::nothrow) uint32_t[size_t(w) * h];
        if (!buffer[0] || !buffer[1]) { delete[] buffer[0]; delete[] buffer[1]; buffer[0] = buffer[1] = nullptr; return false; }
        return true;
    }
    // Game thread: may write a new image only once the worker consumed the
    // published one (it never reads the other slot).
    uint32_t* writable() {
        if (!can_write()) return nullptr;
        return buffer[1 - slot.load(std::memory_order_relaxed)];
    }
};
Channel g_channels[2];
enhance::DiagnosticPanel g_panel;
bool& g_diagnostics_open=g_panel.visible;
enhance::Diagnostics g_diagnostics;
uint32_t g_hide_until_release = 0;
bool g_settings_announced = false;
// FPS strip statistics (worker writes, game thread reads)
std::atomic<uint32_t> g_total_presents{0};
std::atomic<uint32_t> g_frames{0};
uint64_t g_pause_image_key=0;
uint64_t g_diagnostics_tick=0;
uint32_t g_diagnostics_frames=0;
std::atomic<uint32_t> g_frame_us_total{0};
std::atomic<uint32_t> g_gpu_us_total{0};
uint64_t g_fps_tick = 0;
uint32_t g_fps_updates = 0;

void update_fps_strip() {
    const auto& live = enhance::live();
    Channel& c = g_channels[1];
    ++g_fps_updates;
    if (!live.show_fps.load(std::memory_order_relaxed)) { c.hide();g_fps_tick=0;g_fps_updates=0;g_frames.exchange(0);g_frame_us_total.exchange(0);g_gpu_us_total.exchange(0); return; }
    const uint64_t now = svcGetSystemTick();
    if (!g_fps_tick) { g_fps_tick = now; g_fps_updates = 0; return; }
    const double seconds = double(now - g_fps_tick) / double(SYSCLOCK_ARM11);
    if (seconds < .5 && c.visible.load(std::memory_order_relaxed)) return;
    if (!c.allocate(256, 16)) return;
    uint32_t* img = c.writable();
    if (!img) return;
    const uint32_t frames = g_frames.exchange(0), frame_us = g_frame_us_total.exchange(0), gpu_us = g_gpu_us_total.exchange(0);
    char text[40];
    const double ups = seconds > 0 ? g_fps_updates / seconds : 0;
    if (live.gpu_timing.load(std::memory_order_relaxed) && frames)
        std::snprintf(text, sizeof text, "%4.1f upd/s  GPU %4.1f ms", ups, gpu_us / 1000.0 / frames);
    else
        std::snprintf(text, sizeof text, "%.1f FPS", frames / (seconds > 0 ? seconds : 1));
    (void)frame_us;
    for (int i = 0; i < 256 * 16; ++i) img[i] = 0;
    enhance::draw_game_text(img,256,16,0,0,text,true);
    // Keep the actual text against the right margin of its transparent strip.
    int right=0;for(int x=0;x<256;++x)for(int y=0;y<16;++y)if(img[y*256+x]>>24)right=x+1;
    if(right<256)for(int y=0;y<16;++y){std::memmove(img+y*256+256-right,img+y*256,right*sizeof(uint32_t));std::memset(img+y*256,0,(256-right)*sizeof(uint32_t));}
    c.publish();
    g_fps_tick = now; g_fps_updates = 0;
}
}  // namespace

uint32_t filter_input(uint32_t held, uint32_t down, bool& touch_allowed) {
    const bool intro_input=sm64ds_intro_skip_input((held&KEY_START)!=0,(down&KEY_START)!=0);
    const bool was_front=sm64ds_front_options_active()!=0;
    if(was_front){touchPosition pen{};hidTouchRead(&pen);sm64ds_front_options_update(down,(held&KEY_TOUCH)!=0,(down&KEY_TOUCH)!=0,pen.px,pen.py);}
    const bool front_input=was_front||sm64ds_front_options_active();
    if (!g_settings_announced) {
        // boot_apply() (game_entry.cpp) normally published the settings.
        g_settings_announced = true;
        if (enhance::live().generation.load(std::memory_order_acquire) == 0) enhance::publish(enhance::Settings::safe());
    }
    const bool was_open=g_diagnostics_open;
    const bool enabled=enhance::live().developer_overlay.load(std::memory_order_relaxed);
    g_panel.update(enabled,front_input?0:down);
    // This is a live telemetry panel, never the independent options menu.
    // In particular it does not inhibit Circle Pad or C-Stick input.
    enhance::live().overlay_open.store(false,std::memory_order_release);
    Channel& c=g_channels[0];
    const bool gameplay=sm64ds_render60_scene&&sm64ds_render60_scene()==3&&!enhance::live().pause_scene.load(std::memory_order_acquire)&&!g_diagnostics_open&&!front_input;
    const bool camera_hud=cstick_camera::gameplay_camera_available(gameplay);
    const uint64_t pause_image_key=sm64ds_native_pause_image_key();
    if(g_diagnostics_open){
        const uint64_t now=svcGetSystemTick();
        const double seconds=double(now-g_diagnostics_tick)/double(SYSCLOCK_ARM11);
        if(!g_diagnostics_tick || seconds>=.5 || !was_open){
            const uint32_t frames=g_total_presents.load(std::memory_order_relaxed);
            g_diagnostics.fps=g_diagnostics_tick?double(frames-g_diagnostics_frames)/seconds:0.;
            g_diagnostics.frame_ms=g_diagnostics.fps>0?1000./g_diagnostics.fps:0.;
            g_diagnostics.linear_free=unsigned(linearSpaceFree());
            g_diagnostics.heap_free=unsigned(mallinfo().fordblks);
            APT_CheckNew3DS(&g_diagnostics.new_model);
            g_diagnostics.render_width=enhance::live().actual_render_width.load(std::memory_order_relaxed);
            g_diagnostics.render_height=enhance::live().actual_render_height.load(std::memory_order_relaxed);
            g_diagnostics.dumps_enabled=enhance::live().developer_dumps.load(std::memory_order_relaxed);
            g_diagnostics.fallback=enhance::live().native_fallback.load(std::memory_order_relaxed);
            if(c.allocate(320,240))if(auto* img=c.writable()){
                enhance::render_diagnostics(img,g_diagnostics);c.publish();
            }
            g_diagnostics_tick=now;g_diagnostics_frames=frames;
        }
    }else {
        if(was_open){c.hide();g_pause_image_key=0;}
        const uint64_t key=pause_image_key;
        if(key){
            if(key!=g_pause_image_key)c.hide();
            if((key!=g_pause_image_key||was_open||!c.visible.load(std::memory_order_relaxed))&&c.allocate(320,240)){
                if(auto* img=c.writable()){sm64ds_native_pause_image(img);c.publish();g_pause_image_key=key;}
            }
        }else if(sm64ds_render60_scene&&sm64ds_render60_scene()==1){
            constexpr uint64_t title_version_key=0x4000000000000000ull;
            if(g_pause_image_key!=title_version_key)c.hide();
            if((g_pause_image_key!=title_version_key||was_open||!c.visible.load(std::memory_order_relaxed))&&c.allocate(320,240))if(auto* img=c.writable()){
                std::memset(img,0,320*240*sizeof(uint32_t));
                struct VersionCanvas {uint32_t* pixels;void pixel(int x,int y,uint32_t color){pixels[y*320+x]=color;}} canvas{img};
                native_lower::version(canvas);c.publish();g_pause_image_key=title_version_key;
            }
        }else if(sm64ds_intro_skip_hint()){
            if(g_pause_image_key!=~uint64_t(0))c.hide();
            if(g_pause_image_key!=~uint64_t(0)&&c.allocate(320,240))if(auto* img=c.writable()){
                std::memset(img,0,320*240*sizeof(uint32_t));
                struct HintCanvas { uint32_t* pixels; void pixel(int x,int y,uint32_t color){pixels[y*320+x]=color;} } canvas{img};
                native_lower::frame(canvas,90,205,140,28);
                native_lower::centered(canvas,211,"Start to omit",0xff8d4300);
                c.publish();g_pause_image_key=~uint64_t(0);
            }
        }else if(camera_hud){
            if(g_pause_image_key!=native_gameplay_camera::image_key)c.hide();
            if((g_pause_image_key!=native_gameplay_camera::image_key||was_open||!c.visible.load(std::memory_order_relaxed))&&c.allocate(320,240)){
                if(auto* img=c.writable()){
                    native_gameplay_camera::render(img);c.publish();g_pause_image_key=native_gameplay_camera::image_key;
                }
            }
        }else{c.hide();g_pause_image_key=0;}
    }
    native_lower::wanted.store(!g_diagnostics_open&&pause_image_key&&pause_image_key==g_pause_image_key&&c.visible.load(std::memory_order_acquire),std::memory_order_release);
    if(was_open && !g_diagnostics_open)g_hide_until_release=held&KEY_SELECT;
    g_hide_until_release&=held;
    update_fps_strip();
    if(front_input)g_hide_until_release|=held;
    // A newly enabled button must already have a real uploaded image. Keep
    // publication/acknowledgement ownership in OverlayPublication unchanged.
    const bool arrows_visible=camera_hud&&g_pause_image_key==native_gameplay_camera::image_key&&
        c.visible.load(std::memory_order_acquire)&&native_lower::gpu_ready.load(std::memory_order_acquire)&&
        c.acked.load(std::memory_order_acquire)==c.published.load(std::memory_order_acquire);
    const bool stylus_camera=cstick_camera::capture_stylus(gameplay&&!(g_hide_until_release&KEY_TOUCH),arrows_visible);
    touch_allowed = !front_input&&!(g_hide_until_release&KEY_TOUCH)&&!g_diagnostics_open&&!stylus_camera;
    if(intro_input)g_hide_until_release|=held&KEY_START;
    uint32_t keys = held & ~g_hide_until_release;
    keys=enhance::DiagnosticPanel::game_keys(enabled,keys);
    keys = developer_keys::game_keys(keys, held);
    if (keys & KEY_SELECT) keys &= ~uint32_t(KEY_START);   // the chord never reaches the game
    return front_input?0:keys;
}

const uint32_t* overlay_image(unsigned kind, uint32_t& generation, int& width, int& height) {
    if (kind > 1) return nullptr;
    Channel& c = g_channels[kind];
    if (!c.acquire(generation)) return nullptr;
    width = c.width; height = c.height;
    return c.buffer[c.slot.load(std::memory_order_relaxed)];
}

void acknowledge(unsigned kind, uint32_t generation) {
    if (kind <= 1) g_channels[kind].acknowledge(generation);
}

void note_present(double frame_ms, double gpu_ms) {
    g_total_presents.fetch_add(1,std::memory_order_relaxed);
    if (!enhance::live().show_fps.load(std::memory_order_relaxed)) return;
    g_frames.fetch_add(1, std::memory_order_relaxed);
    g_frame_us_total.fetch_add(uint32_t(frame_ms * 1000.0), std::memory_order_relaxed);
    g_gpu_us_total.fetch_add(uint32_t(gpu_ms * 1000.0), std::memory_order_relaxed);
}

void shutdown() {
    native_lower::wanted.store(false);native_lower::gpu_ready.store(false);
    // Caller has joined the presentation worker and ended the GPU frame.
    for (auto& c:g_channels) {
        c.hide(); delete[] c.buffer[0]; delete[] c.buffer[1];
        c.buffer[0]=c.buffer[1]=nullptr;
        c.reset();c.width=c.height=0;
    }
}
}  // namespace enhance_runtime

// Both the normal input transfer and the retail menu's SDK sampler must
// respect modal ownership, including the pen release that closes the panel.
extern "C" int sm64ds_front_options_blocked(){
 return sm64ds_front_options_active() || (enhance_runtime::g_hide_until_release&KEY_TOUCH);
}

