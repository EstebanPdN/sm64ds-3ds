#include "sm64ds/native_frame.hpp"
#include "sm64ds/native_capture.hpp"
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_snapshot.hpp"
#include "sm64ds/native_camera.hpp"
#include "sm64ds/game_heap.hpp"
#include <3ds.h>
#include <cstdio>
#include <utility>
#include <cstring>

extern "C" {
// Original communication records used by func_0203dabc/func_0203dae4.
// Touch at +4 is three u16 values; each player's record has stride 0x24.
extern unsigned char data_020a1154[];
extern unsigned char data_020a1162[];
// OV007 owns a four-byte context pointer slot and a 32-bit input-lock table.
extern std::uint8_t* data_ov007_0210342c;
extern std::int32_t data_ov007_02104bec[];
}

namespace sm64ds {
namespace {
FILE* open_snapshot(const char* prefix,const char* suffix,const char* mode) {
    char path[128];
    std::snprintf(path,sizeof path,"sdmc:/3ds/sm64ds/native/%s%s",prefix,suffix);
    return std::fopen(path,mode);
}
bool diagnostic_span(std::uintptr_t start,std::size_t bytes,
                     std::uintptr_t address,std::size_t length) {
    // The process-lifetime original root arena is cached only after its real
    // constructor. Subtraction avoids wrapping address+length on bad pointers.
    if(!start || address<start)return false;
    const auto offset=address-start;
    return offset<=bytes && length<=bytes-offset;
}
template<class T> T diagnostic_value(std::uintptr_t address,std::size_t offset) {
    // The root header is owned by our native heap constructor; other callers
    // first validate the complete recovered record's extent in that arena.
    // memcpy also avoids manufacturing a C++ object/layout over the ROM data.
    T value{};
    std::memcpy(&value,reinterpret_cast<const void*>(address+offset),sizeof value);
    return value;
}
}

bool NativeFrame::init(std::string& error) {
    if(initialized_) {error="El frame nativo ya esta inicializado";return false;}
    scanout_cache_.reset_stats();scanout_cache_.invalidate();
    if(!renderer_.init(error))return false;
    input_init();osSetSpeedupEnable(false);
    initialized_=true;tick_open_=input_published_=scene_snapshot_pending_=false;stats={};latch_.reset();
    scene_visible_snapshot_enabled_=scene_visible_snapshot_pending_=false;
    scene_snapshot_requested_=scene_ready_snapshot_taken_=scene_ready_snapshot_pending_=false;
    requested_memory_snapshot_taken_=false;
    stage_snapshot_requested_=stage_snapshot_pending_=false;
    title_previous_={};title_logged_=false;
    diagnostic_heap_start_=diagnostic_heap_bytes_=0;
    // Heap's constructor owns start/size at +4/+8; native_main creates this
    // real root before NativeFrame::init, and its arena lives until exit.
    static_assert(sizeof(std::uintptr_t)==4,"Original heap and OV007 pointers are ARM32");
    if(const auto* root=native_game_root_heap()) {
        const auto address=reinterpret_cast<std::uintptr_t>(root);
        diagnostic_heap_start_=diagnostic_value<std::uintptr_t>(address,4);
        diagnostic_heap_bytes_=diagnostic_value<std::uint32_t>(address,8);
        if(diagnostic_heap_start_>UINTPTR_MAX-diagnostic_heap_bytes_)
            diagnostic_heap_start_=diagnostic_heap_bytes_=0;
    }
    input_previous_={};
    if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/input.log","w"))std::fclose(file);
    if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/title-state.log","w"))std::fclose(file);
    ntr::gx_reset();ntr::gx_set_texture_resolver(GameTextures::resolver,&textures_);
    sm64ds_draw_reset();
    return true;
}

void NativeFrame::sample_input() {
    if(!initialized_)return;
    // DS directional buttons retain their original game meaning. Only the
    // actual New3DS C-stick reaches the camera field, unlike the asset viewer.
    latch_.sample(input_poll(false));++stats.input_samples;
}

bool NativeFrame::begin_tick(std::string& error) {
    if(!initialized_ || tick_open_) {error="Orden de inicio de frame no valido";return false;}
    tick_input_=latch_.consume();
    // Match the original sampler's opposing-direction normalization before
    // publishing the latched record after it. Brief edges still survive.
    uint16_t blocked=0;
    if((tick_input_.pad.held&0x30)==0x30)blocked|=0x30;
    if((tick_input_.pad.held&0xc0)==0xc0)blocked|=0xc0;
    tick_input_.pad.held&=uint16_t(~blocked);
    tick_input_.pad.pressed&=uint16_t(~blocked);
    const uint16_t touch[3]={tick_input_.touch.x,tick_input_.touch.y,tick_input_.touch.touched};
    std::memcpy(data_020a1154+4,touch,sizeof touch);
    std::memcpy(data_020a1162,&tick_input_.pad.held,sizeof tick_input_.pad.held);
    // Retain commands submitted during boot or between simulation ticks.
    // Completed submissions are retired by end_tick, after actual decoding.
    tick_open_=true;input_published_=false;
    return true;
}

void NativeFrame::input_ready() {
    if(!tick_open_ || input_published_)return;
    // Called AFTER the unchanged original communication samplers. It preserves
    // a press/release that occurred between two original simulation ticks.
    publish_game_input(tick_input_);
    // Evidence of the record consumed by the unchanged original samplers and
    // published to the game, rather than the display-frame HID samples.
    if(stats.game_ticks==0 || tick_input_.pad.held!=input_previous_.pad.held ||
       tick_input_.pad.pressed!=input_previous_.pad.pressed ||
       tick_input_.touch.x!=input_previous_.touch.x ||
       tick_input_.touch.y!=input_previous_.touch.y ||
       tick_input_.touch.touched!=input_previous_.touch.touched) {
        if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/input.log","a")) {
            std::fprintf(file,"input tick=%llu held=%u pressed=%u touch_x=%u touch_y=%u touched=%u\n",
                static_cast<unsigned long long>(stats.game_ticks+1),
                unsigned(tick_input_.pad.held),unsigned(tick_input_.pad.pressed),
                unsigned(tick_input_.touch.x),unsigned(tick_input_.touch.y),
                unsigned(tick_input_.touch.touched));
            std::fclose(file);
        }
    }
    input_previous_=tick_input_;
    if(camera_hook_)camera_hook_(camera_context_,tick_input_);
    input_published_=true;
}

bool NativeFrame::end_tick(std::string& error) {
    const uint64_t started=svcGetSystemTick();
    if(!tick_open_ || !input_published_) {error="Falta la entrada del frame original";return false;}
    tick_open_=false;
    if(!sm64ds_draw_ok()) {error="El stream GX original desbordo su buffer";return false;}
    stats.words=sm64ds_draw_count();
    const bool title_ready=record_title_diagnostic();
    // Preserve the first original menu stream before decoding. A rejected
    // packet must remain available for an isolated reproducer as well.
    if(scene_snapshot_pending_)write_snapshot_stream("scene-1-first");
    if(stage_snapshot_pending_)write_snapshot_stream("scene-3-requested");
    // Preserve the later visible-title checkpoint on original tick 180 and
    // its next presentation. Counting game ticks keeps this tied to one
    // consumed GX stream even when the original VBlank cadence differs.
    if(scene_visible_snapshot_enabled_ && stats.game_ticks+1==180) {
        write_snapshot_stream("scene-1-visible");
        scene_visible_snapshot_enabled_=false;scene_visible_snapshot_pending_=true;
    }
    if(scene_snapshot_requested_ && title_ready && !scene_ready_snapshot_taken_) {
        write_snapshot_stream("scene-1-ready");
        scene_ready_snapshot_taken_=scene_ready_snapshot_pending_=true;
    }
    // A scene may spend an iteration waiting without submitting new geometry.
    // Retain its last completed image instead of creating an extra game pose.
    if(stats.words) {
        triangles_.clear();
        if(!ntr::gx_begin_frame()) {error="Stream GX anterior incompleto";return false;}
        textures_.begin_frame();
        const auto* words=sm64ds_draw_entries();
        for(uint32_t i=0;i<stats.words;++i) {
            if(words[i].port)ntr::gx_write_port(0x04000400+words[i].port*4,words[i].value);
            else ntr::gx_write_fifo(words[i].value);
        }
        if(!ntr::gx_asset_stream_valid()) {
            const auto diagnostic=ntr::gx_get_stream_diagnostics();
            char message[320];
            std::snprintf(message,sizeof message,
                "GX rejected: reason=%u command=%02x texture=%08lx palette=%08lx "
                "fifo=%02x params=%u/%u queued=%u port=%02x params=%u/%u",
                unsigned(diagnostic.failure),unsigned(diagnostic.failing_command),
                static_cast<unsigned long>(diagnostic.texture_image),
                static_cast<unsigned long>(diagnostic.texture_palette),
                unsigned(diagnostic.fifo_command),unsigned(diagnostic.fifo_parameters),
                unsigned(diagnostic.fifo_needed),unsigned(diagnostic.fifo_commands_remaining),
                unsigned(diagnostic.port_command),unsigned(diagnostic.port_parameters),
                unsigned(diagnostic.port_needed));
            error=message;return false;
        }
        size_t count=0;const auto* triangles=ntr::gx_polygons(count);
        if(count)triangles_.assign(triangles,triangles+count);
        ntr::gx_get_projection(projection.matrix.data());projection.native_matrix=true;
    }
    sm64ds_draw_reset();
    stats.decode_ticks=svcGetSystemTick()-started;
    ++stats.game_ticks;return true;
}

bool NativeFrame::record_title_diagnostic() {
    TitleDiagnostic current;
    current.context=reinterpret_cast<std::uintptr_t>(data_ov007_0210342c);
    current.input_lock=data_ov007_02104bec[0];
    const auto valid_span=[this](std::uintptr_t address,std::size_t bytes) {
        return diagnostic_span(diagnostic_heap_start_,diagnostic_heap_bytes_,address,bytes);
    };
    // b68e8 allocates 0x188 bytes. Before that constructor the slot is null;
    // no pointed-to field is read until its complete record fits the arena.
    if(valid_span(current.context,0x188)) {
        current.valid=1;
        const auto state=diagnostic_value<std::uintptr_t>(current.context,4);
        const auto substate=diagnostic_value<std::uintptr_t>(current.context,8);
        // c9388/c934c own 0x10-byte state records: current s16 at +0,
        // pending s16 at +2, submode s32 at +4, counter s32 at +0xc.
        if(valid_span(state,0x10)) {
            current.valid|=2;
            current.state=diagnostic_value<std::int16_t>(state,0);
            current.pending=diagnostic_value<std::int16_t>(state,2);
            current.counter=diagnostic_value<std::int32_t>(state,0xc);
        }
        if(valid_span(substate,0x10)) {
            current.valid|=4;
            current.substate=diagnostic_value<std::int16_t>(substate,0);
            current.subpending=diagnostic_value<std::int16_t>(substate,2);
            current.subcounter=diagnostic_value<std::int32_t>(substate,0xc);
        }
        // b693c fills 24 element pointers. b1cf0 reads opacity through
        // ctx+0x130 -> element[0] -> object+0x10, with TWO pointer loads.
        const auto element=diagnostic_value<std::uintptr_t>(current.context,0x130);
        // b4c04 allocates each element's complete 0x1c-byte record.
        if(valid_span(element,0x1c)) {
            const auto object=diagnostic_value<std::uintptr_t>(element,0);
            if(valid_span(object,0x14)) {
                current.valid|=8;
                current.opacity=diagnostic_value<std::int32_t>(object,0x10);
            }
        }
        const auto touch=diagnostic_value<std::uintptr_t>(current.context,0x50);
        const auto keys=diagnostic_value<std::uintptr_t>(current.context,0x54);
        if(valid_span(touch,0x44)) {
            current.valid|=16;
            current.touch_x=diagnostic_value<std::uint16_t>(touch,8);
            current.touch_y=diagnostic_value<std::uint16_t>(touch,0xa);
            current.touch_now=diagnostic_value<std::uint16_t>(touch,0xc);
            current.touch_prior=diagnostic_value<std::uint16_t>(touch,0x14);
            current.touch_release=diagnostic_value<std::int32_t>(touch,0x24);
        }
        if(valid_span(keys,0x70)) {
            current.valid|=32;
            current.keys_held=diagnostic_value<std::uint16_t>(keys,0);
            current.keys_prior=diagnostic_value<std::uint16_t>(keys,2);
        }
    }
    current.ready=current.valid==0x3f && current.state==0 && current.substate==0 &&
                  current.opacity==0x1000 && current.input_lock==0;
    const auto& previous=title_previous_;
    // Include counters in transitions and each 30-tick sample (including tick
    // 180), without a record on every unchanged frame.
    if(!title_logged_ || (stats.game_ticks+1)%30==0 ||
       current.context!=previous.context || current.valid!=previous.valid ||
       current.state!=previous.state || current.substate!=previous.substate ||
       current.pending!=previous.pending || current.subpending!=previous.subpending ||
       current.input_lock!=previous.input_lock || current.ready!=previous.ready ||
       current.keys_held!=previous.keys_held || current.keys_prior!=previous.keys_prior ||
       current.touch_x!=previous.touch_x || current.touch_y!=previous.touch_y ||
       current.touch_now!=previous.touch_now || current.touch_prior!=previous.touch_prior) {
        if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/title-state.log","a")) {
            std::fprintf(file,"title-state tick=%llu ctx=%08lx valid=%02x state=%d substate=%d "
                "pending=%d subpending=%d counter=%ld subcounter=%ld opacity=%ld lock=%ld ready=%u "
                "keys_held=%u keys_prior=%u touch_x=%u touch_y=%u touch_now=%u touch_prior=%u touch_release=%ld\n",
                static_cast<unsigned long long>(stats.game_ticks+1),static_cast<unsigned long>(current.context),
                unsigned(current.valid),int(current.state),int(current.substate),
                int(current.pending),int(current.subpending),static_cast<long>(current.counter),
                static_cast<long>(current.subcounter),static_cast<long>(current.opacity),
                static_cast<long>(current.input_lock),unsigned(current.ready),
                unsigned(current.keys_held),unsigned(current.keys_prior),unsigned(current.touch_x),
                unsigned(current.touch_y),unsigned(current.touch_now),unsigned(current.touch_prior),
                static_cast<long>(current.touch_release));
            std::fclose(file);
        }
    }
    title_previous_=current;title_logged_=true;
    return current.ready;
}

void NativeFrame::write_snapshot_stream(const char* prefix) {
    if(FILE* file=open_snapshot(prefix,"-gx.bin","wb")) {
        std::fwrite(sm64ds_draw_entries(),sizeof(sm64ds_draw_entries()[0]),stats.words,file);
        std::fclose(file);
    }
    const auto& geometry=game_geometry();
    if(FILE* file=open_snapshot(prefix,"-textures.bin","wb")) {
        std::fwrite(geometry.textures,1,sizeof geometry.textures,file);std::fclose(file);
    }
    if(FILE* file=open_snapshot(prefix,"-palettes.bin","wb")) {
        std::fwrite(geometry.palettes,1,sizeof geometry.palettes,file);std::fclose(file);
    }
    if(FILE* file=open_snapshot(prefix,"-geometry.json","w")) {
        std::fprintf(file,"{\"texture_capacity\":%u,\"palette_capacity\":%u,"
            "\"texture_generation\":%u,\"palette_generation\":%u,"
            "\"texture_uploads\":%u,\"palette_uploads\":%u,"
            "\"texture_bytes\":%u,\"palette_bytes\":%u,"
            "\"texture_transfer\":%s,\"palette_transfer\":%s,\"highlight_shading\":%s}\n",
            unsigned(geometry.texture_capacity),unsigned(geometry.palette_capacity),
            unsigned(geometry.texture_generation),unsigned(geometry.palette_generation),
            unsigned(geometry.texture_uploads),unsigned(geometry.palette_uploads),
            unsigned(geometry.texture_bytes),unsigned(geometry.palette_bytes),
            geometry.texture_transfer?"true":"false",geometry.palette_transfer?"true":"false",
            geometry.highlight_shading?"true":"false");
        std::fclose(file);
    }
}

bool NativeFrame::present(std::string& error) {
    if(!initialized_ || tick_open_) {error="Presentacion fuera de la frontera original";return false;}
    const auto capture=native_capture_begin_frame(stats.presentations+1);
    if(capture.enabled() && capture.destination_lcdc &&
       capture.source()!=NativeCaptureSource::composited) {
        error="El frame original solicita una fuente de captura GPU aun no integrada";return false;
    }
    const uint64_t scanout_started=svcGetSystemTick();
    if(!native_2d_scanout_cached(screens_,scanout_cache_)) {
        error="La pantalla original usa memoria de video aun no adaptada";return false;
    }
    if(capture.needs_composited()) {
        if(!capture_layers_)capture_layers_=std::make_unique<Native2DFrame>();
        if(capture_rgba_.empty())capture_rgba_.resize(256*192*4);
        if(!native_2d_capture_layers(*capture_layers_,error)) {scanout_cache_.invalidate();return false;}
    }
    stats.scanout_ticks=svcGetSystemTick()-scanout_started;
    if(!native_video_ok()) {
        scanout_cache_.invalidate();error="La pantalla original usa memoria de video aun no adaptada";return false;
    }
    // One actual CPU scanout from this development run separates compositor
    // and VRAM corruption from GPU texture presentation without another run.
    if(stats.presentations==2 || scene_snapshot_pending_) {
        write_snapshot_scanout(scene_snapshot_pending_?"scene-1-first":"boot");
    }
    // Current milestone is the ordinary single-view game. Stereo is explicitly
    // deferred; the depth slider must not double submissions during bring-up.
    const uint64_t submit_started=svcGetSystemTick();
    if(!renderer_.draw(triangles_.data(),triangles_.size(),projection,0.0f,error,&screens_,
        capture.needs_composited()?capture_layers_.get():nullptr,
        capture.needs_composited()?capture_rgba_.data():nullptr,screen_revision(),&textures_)) {scanout_cache_.invalidate();return false;}
    stats.submit_ticks=svcGetSystemTick()-submit_started;
    NativeCaptureSources sources;
    if(capture.needs_composited())sources.composited={capture_rgba_.data(),capture_rgba_.size(),
                                                    256*4,capture.frame_serial};
    const auto captured=native_capture_complete_frame(capture,sources);
    if(!captured.okay()) {
        scanout_cache_.invalidate();error="No se pudo completar la captura de pantalla original";return false;
    }
    if(captured.status==NativeCaptureStatus::captured) {
        ++stats.captures;
        // Preserve the first real image and written VRAM from this run for
        // inspection. Synthetic GPU fixture output has a separate directory.
        if(stats.captures==1) {
            if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/capture-first.rgba","wb")) {
                std::fwrite(capture_rgba_.data(),1,capture_rgba_.size(),file);std::fclose(file);
            }
            if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/capture-first-vram.bin","wb")) {
                if(const auto* pixels=native_video_pointer(0x06800000+captured.bank*0x20000,0x20000))
                    std::fwrite(pixels,1,0x20000,file);
                std::fclose(file);
            }
            if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/capture-first.json","w")) {
                std::fprintf(file,"{\"frame\":%llu,\"control\":%u,\"bank\":%u,\"offset\":%u,"
                    "\"width\":%u,\"height\":%u,\"bytes_written\":%u}\n",
                    static_cast<unsigned long long>(captured.rendered_frame),unsigned(capture.control),captured.bank,
                    captured.write_offset,captured.width,captured.height,unsigned(captured.bytes_written));
                std::fclose(file);
            }
        }
    }
    const bool visible_ram=scene_visible_snapshot_pending_;
    const bool ready_ram=scene_ready_snapshot_pending_;
    const bool stage_ram=stage_snapshot_pending_;
    if(scene_snapshot_pending_) {
        write_snapshot_metadata("scene-1-first");
        scene_snapshot_pending_=false;
    }
    if(scene_visible_snapshot_pending_) {
        write_snapshot_scanout("scene-1-visible");
        write_snapshot_metadata("scene-1-visible");
        scene_visible_snapshot_pending_=false;
    }
    if(scene_ready_snapshot_pending_) {
        write_snapshot_scanout("scene-1-ready");
        write_snapshot_metadata("scene-1-ready");
        scene_ready_snapshot_pending_=false;
    }
    if(stage_snapshot_pending_) {
        write_snapshot_scanout("scene-3-requested");
        write_snapshot_metadata("scene-3-requested");
        stage_snapshot_pending_=false;
    }
    ++stats.presentations;
    if(visible_ram)snapshot_memory("ram-title-180","original title introduction at tick 180");
    if(ready_ram)snapshot_memory("ram-title-ready","original title input gate first ready");
    if(stage_ram)snapshot_memory("ram-stage-presented",
        "original scene 3 request after completed presentation; Stage initialization unverified");
    // A host request asks for evidence only. It never selects a scene, writes
    // an original variable or supplies an input event. Poll only at a completed
    // presentation, with the original simulation paused during streaming.
    if(!requested_memory_snapshot_taken_ && stats.presentations%30==0) {
        constexpr const char* request="sdmc:/3ds/sm64ds/native/ram-snapshot-request";
        if(FILE* file=std::fopen(request,"r")) {
            std::fclose(file);std::remove(request);
            requested_memory_snapshot_taken_=true;
            snapshot_memory("ram-final","bounded acquisition final requested checkpoint");
        }
    }
    return true;
}

bool NativeFrame::snapshot_memory(const char* prefix,const char* phase) {
    if(!initialized_)return false;
    const uint64_t started=svcGetSystemTick();
    if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/runtime.log","a")) {
        std::fprintf(file,"ram snapshot begin prefix=%s tick=%llu presentation=%llu\n",prefix,
            static_cast<unsigned long long>(stats.game_ticks),
            static_cast<unsigned long long>(stats.presentations));
        std::fclose(file);
    }
    C3D_FrameSync();
    std::string diagnostic_error;
    const bool okay=native_snapshot_write(prefix,stats.game_ticks,stats.presentations,phase,diagnostic_error);
    if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native/runtime.log","a")) {
        std::fprintf(file,"ram snapshot end prefix=%s status=%s elapsed_us=%llu detail=%s\n",
            prefix,okay?"complete":"failed",
            static_cast<unsigned long long>((svcGetSystemTick()-started)*1000000/SYSCLOCK_ARM11),
            diagnostic_error.c_str());
        std::fclose(file);
    }
    return okay;
}

void NativeFrame::write_snapshot_scanout(const char* prefix) {
    if(FILE* file=open_snapshot(prefix,"-scanout.bin","wb")) {
        std::fwrite(screens_.underlay.pixels,4,256*256,file);
        std::fwrite(screens_.overlay.pixels,4,256*256,file);
        std::fwrite(screens_.bottom.pixels,4,256*256,file);
        std::fclose(file);
    }
    if(FILE* file=open_snapshot(prefix,"-video.bin","wb")) {
        for(const auto span: {std::pair<uint32_t,uint32_t>{0x04000000,0x2000},
                             {0x05000000,0x800},{0x06800000,0xa4000}}) {
            if(const void* data=native_video_pointer(span.first,span.second))
                std::fwrite(data,1,span.second,file);
        }
        std::fclose(file);
    }
}

void NativeFrame::write_snapshot_metadata(const char* prefix) {
    if(FILE* file=open_snapshot(prefix,".json","w")) {
        const auto& camera=native_camera_stats();
        std::fprintf(file,"{\"presentation\":%llu,\"ticks\":%llu,\"gx_words\":%u,"
            "\"triangles\":%zu,\"main_on_top\":%s,\"has_3d\":%s,\"gpu_submission_success\":true,"
            "\"camera\":{\"ready\":%u,\"armed\":%u,\"generation\":%u,\"manual_updates\":%llu,"
            "\"blocked\":%llu,\"constrained\":%llu,\"inverted_intervals\":%llu,\"yaw\":%d,\"pitch\":%d,"
            "\"desired_radius\":%ld,\"solved_radius\":%ld,\"elapsed_us\":%u,"
            "\"tick\":%llu,\"vblank\":%llu}}\n",
            static_cast<unsigned long long>(stats.presentations+1),
            static_cast<unsigned long long>(stats.game_ticks),unsigned(stats.words),triangles_.size(),
            screens_.main_on_top?"true":"false",screens_.has_3d?"true":"false",
            unsigned(camera.ready),unsigned(camera.armed),unsigned(camera.generation),
            (unsigned long long)camera.manual_updates,(unsigned long long)camera.blocked,
            (unsigned long long)camera.constrained,(unsigned long long)camera.inverted_intervals,
            int(camera.yaw),int(camera.pitch),(long)camera.desired_radius,(long)camera.solved_radius,
            unsigned(camera.elapsed_us),(unsigned long long)camera.last_tick,
            (unsigned long long)camera.last_vblank);
        std::fclose(file);
    }
}

void NativeFrame::exit() {
    scanout_cache_.invalidate();
    if(!initialized_)return;
    renderer_.exit();triangles_.clear();ntr::gx_set_texture_resolver(nullptr,nullptr);
    capture_layers_.reset();capture_rgba_.clear();
    input_exit();initialized_=tick_open_=input_published_=false;
}
}
