#include "sm64ds/native_boot.hpp"
#include "sm64ds/native_frame.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/native_runtime.hpp"
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_save.hpp"
#include "sm64ds/native_alarm.hpp"
#include "sm64ds/native_personal.hpp"
#include "sm64ds/native_camera.hpp"
#include "sm64ds/game_files.hpp"
#include "sm64ds/game_heap.hpp"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

extern "C" { unsigned int __stacksize__=512*1024; }
namespace {
constexpr const char* directory="sdmc:/3ds/sm64ds/native";
constexpr const char* logfile="sdmc:/3ds/sm64ds/native/runtime.log";
sm64ds::NativeBoot* active_boot=nullptr;
sm64ds::NativeFrame* active_frame=nullptr;
void log(const char* message) {
    // Both panels belong to Citro3D. A libctru text console retains a CPU
    // framebuffer pointer and would overwrite the lower display during logs.
    if(FILE* file=std::fopen(logfile,"a")) {
        std::fprintf(file,"%s\n",message);std::fclose(file);
    }
}
struct Context {sm64ds::NativeFrame frame;std::string error;};
bool begin_tick(void* value) {auto& c=*static_cast<Context*>(value);return c.frame.begin_tick(c.error);}
bool input_ready(void* value) {static_cast<Context*>(value)->frame.input_ready();return true;}
void camera_input(void* value,const sm64ds::GameInputSample& input) {
    auto& frame=*static_cast<sm64ds::NativeFrame*>(value);
    sm64ds::native_camera_publish(input.frame.camera,frame.stats.game_ticks+1,
                                  sm64ds::native_platform_stats().vblanks);
}
bool frame_ready(void* value) {auto& c=*static_cast<Context*>(value);return c.frame.end_tick(c.error);}
bool power_tick(void*) {return aptMainLoop();}
void report(void* value,const char* message) {
    log(message);
    if(std::string(message)=="original scene request id=1")
        static_cast<Context*>(value)->frame.request_scene_debug_snapshot();
    if(std::string(message)=="original scene request id=3") {
        auto& frame=static_cast<Context*>(value)->frame;
        if(frame.request_stage_debug_snapshot())
            frame.snapshot_memory("ram-stage-requested",
                "original scene 3 request before SpawnIfNecessary; current tick open; Stage initialization unverified");
    }
}
bool runtime_ok(void*) {return sm64ds::native_platform_ok()&&sm64ds::native_video_ok();}
}

extern "C" [[noreturn]] void sm64ds_native_missing(const char* name) {
    // Publish STOP only after the evidence writer finishes. The bounded host
    // supervisor stops immediately when it observes that terminal record.
    if(active_frame)active_frame->snapshot_memory("ram-stop",name);
    log("STOP: original game reached an unavailable native service");
    log(name);
    if(active_boot && active_boot->operation())log(active_boot->operation());
    if(auto* files=sm64ds::bound_native_game_files(); files && !files->last_error().empty())
        log(files->last_error().c_str());
    const auto save=sm64ds::native_save_stats();
    if(save.last_error)log(save.last_error);
    log("playability=unverified physical_hardware=unverified");
    std::exit(2);
}
extern "C" [[noreturn]] void Crash() {sm64ds_native_missing("Original Crash()");}

int main() {
    gfxInitDefault();
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/sm64ds",0777);mkdir(directory,0777);
    if(FILE* file=std::fopen(logfile,"w"))std::fclose(file);
    log("SM64DS general native integration; original boot/scene/actor loop");
    log("Old3DS baseline; stereo=disabled audio=disabled wireless=disabled physical_hardware=unverified");
    sm64ds::native_platform_init(log);sm64ds::native_video_reset();
    sm64ds::AssetPack pack;std::string error;
    if(!pack.open(std::string(directory)+"/assets.pak",error)) {log(error.c_str());gfxExit();return 1;}
    sm64ds::GameFiles files(pack);
    if(!files.initialize_common_archive(error)) {log(error.c_str());gfxExit();return 1;}
    sm64ds::bind_native_game_files(&files,sm64ds::FileIdDomain::CurrentRomFat);
    if(!sm64ds::native_runtime_load(pack,error)) {log(error.c_str());log("exit=integration_failure");gfxExit();return 1;}
    if(!sm64ds::native_runtime_bind_archives(files,error)) {log(error.c_str());log("exit=integration_failure");gfxExit();return 1;}
    log("native ROM data and pointer tables restored");
    if(!sm64ds::native_personal_initialize()) {log("exit=integration_failure");gfxExit();return 1;}
    log(sm64ds::native_personal_stats().english_fallback?
        "native profile configured; English fallback for EU resources":"native profile configured from CFGU");
    if(!sm64ds::initialize_game_root_heap(8*1024*1024)) {log("original heap init failed");gfxExit();return 1;}
    static Context context;
    if(!context.frame.init(error)) {log(error.c_str());gfxExit();return 1;}
    sm64ds::native_camera_reset();
    context.frame.set_camera_input_hook(camera_input,&context.frame);
    active_frame=&context.frame;
    if(!sm64ds::native_runtime_initialize_main())sm64ds_native_missing("main static initializers");
    log("main original static initializers completed");
    sm64ds::NativeBoot boot;active_boot=&boot;
    sm64ds::NativeBootCallbacks callbacks{&context,sm64ds::native_video_write_callback,
        begin_tick,input_ready,frame_ready,power_tick,report,runtime_ok};
    bool okay=boot.initialize(callbacks);
    uint64_t frames=0;
    while(okay && aptMainLoop()) {
        context.frame.sample_input();
        sm64ds::native_alarm_poll();
        if((hidKeysHeld()&(KEY_START|KEY_SELECT))==(KEY_START|KEY_SELECT))break;
        if(boot.state()==sm64ds::NativeBootState::ready)okay=boot.begin_tick();
        if(!okay)break;
        if(!context.frame.present(context.error)) {okay=false;break;}
        sm64ds::native_platform_advance_vblank();
        if(boot.state()==sm64ds::NativeBootState::waiting_vblank)okay=boot.after_vblank();
        if(++frames<=3 || frames%30==0) {
            const auto& scanout=context.frame.scanout_cache_stats();
            const auto& renderer=context.frame.render_stats();
            const auto& camera=sm64ds::native_camera_stats();
            char message[960];
            std::snprintf(message,sizeof(message),"frame=%llu ticks=%llu gx_words=%lu triangles=%lu unresolved_overlay_data=%lu captures=%llu decode_us=%llu scanout_us=%llu submit_us=%llu audio_deferred=%llu scanout_hits=%llu scanout_recompositions=%llu screen_texture_hits=%lu texture_crc_bytes=%llu decoded_texture_hits=%lu camera_ready=%lu camera_armed=%lu camera_generation=%lu camera_manual=%llu camera_blocked=%llu camera_constrained=%llu camera_yaw=%d camera_pitch=%d camera_desired_radius=%ld camera_solved_radius=%ld camera_elapsed_us=%lu",
                (unsigned long long)frames,(unsigned long long)boot.completed_ticks(),
                (unsigned long)context.frame.stats.words,(unsigned long)context.frame.render_stats().triangles,
                (unsigned long)sm64ds::native_runtime_unresolved_data(),
                (unsigned long long)context.frame.stats.captures,
                (unsigned long long)(context.frame.stats.decode_ticks*1000000/SYSCLOCK_ARM11),
                (unsigned long long)(context.frame.stats.scanout_ticks*1000000/SYSCLOCK_ARM11),
                (unsigned long long)(context.frame.stats.submit_ticks*1000000/SYSCLOCK_ARM11),
                (unsigned long long)sm64ds::native_platform_stats().deferred_audio_calls,
                (unsigned long long)scanout.hits,(unsigned long long)scanout.recompositions,
                (unsigned long)renderer.screen_cache_hits,(unsigned long long)renderer.texture_hash_bytes,
                (unsigned long)renderer.decoded_texture_cache_hits,
                (unsigned long)camera.ready,(unsigned long)camera.armed,(unsigned long)camera.generation,
                (unsigned long long)camera.manual_updates,(unsigned long long)camera.blocked,
                (unsigned long long)camera.constrained,int(camera.yaw),int(camera.pitch),
                (long)camera.desired_radius,(long)camera.solved_radius,(unsigned long)camera.elapsed_us);
            log(message);
        }
    }
    if(!okay) {
        if(boot.failure())log(boot.failure());
        if(boot.operation())log(boot.operation());
        if(!context.error.empty())log(context.error.c_str());
        if(!sm64ds::native_video_ok()) {
            char message[80];std::snprintf(message,sizeof(message),"unhandled video address=%08lx",(unsigned long)sm64ds::native_video_bad_address());log(message);
        }
    }
    context.frame.snapshot_memory("ram-exit",okay?"application clean exit":"integration failure before teardown");
    log(okay?"exit=clean":"exit=integration_failure");
    active_frame=nullptr;sm64ds::native_camera_reset();context.frame.exit();gfxExit();return okay?0:1;
}
