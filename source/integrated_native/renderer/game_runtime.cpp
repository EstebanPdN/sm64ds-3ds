// Only the generated experimental game defines this integration. The ordinary
// maintained game and portable host library do not initialize another renderer.
#ifdef SM64DS_FRESH_NATIVE_GAME
#include "game_runtime.h"
#include "scene_producer.hpp"
#include "game_lighting.hpp"
#include "scene_renderer.hpp"
#include "game_material.hpp"
#include "lighting.hpp"
#include "scene_capture.hpp"
#include "scene_interpolation.hpp"
#include "perf_switch.hpp"
#include "ntr/rt.h"
#include "native_projection.hpp"
#include "writer_provenance.hpp"
#include "menu_sprite_hooks.hpp"
#include "particle_hooks.hpp"
#include "menu_oam_hooks.hpp"
#include "menu_display_state.hpp"
#include "menu_backgrounds.hpp"
#include "menu_backdrops.hpp"
#include "dialogue_hooks.hpp"
#include "frame_timeline.hpp"
#include "../native_diagnostics.hpp"
#include "../audio_diagnostics.hpp"
#include "../audio_mixer_kernels.hpp"
#include "build_version.hpp"
#include "console_profile.hpp"
#include "sm64ds_fresh_native_shader_shbin.h"
#include "ntr/mmio.h"
#include "ntr/ppu.h"
#include "bridge_composition.hpp"
#include "test_mode.h"
#include "observed_cadence.h"
#if __has_include("../enhance_runtime.h")
#include "../enhance_runtime.h"
#include "../native_lower_state.hpp"
#define SM64DS_NATIVE_INTERFACE_RUNTIME 1
#endif
#include <algorithm>
#include <optional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <atomic>
#include <sys/stat.h>

using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
extern "C" std::int16_t data_02082214[];
extern "C" void sm64ds_native_cadence_pause_ended(std::uint64_t);
// BUILD115 exit safety (defined at the end of this file).
extern "C" void sm64ds_fatal_handlers_install(void);
extern "C" void sm64ds_fresh_native_mark_clean_exit(void);
extern "C" void sm64ds_heap_stats(unsigned* used,unsigned* free_chunks,unsigned* unclaimed);
// Set when a fatal error stopped a thread other than the game thread.
volatile int sm64ds_fatal_thread_pending=0;
namespace {
// One complete capture. Two of them alternate: the game captures one while
// the render worker draws the other (sealed frames are immutable).
struct Store {
    ntr::Framebuffer sky{};
    ntr::Framebuffer map_foreground{};
    AffineImage map{};
    std::array<Occurrence,768> occurrences{};
    std::array<PreparedPart,4096> parts{};
    std::array<Vertex,4096*4> quad_vertices{};
    std::array<QuadState,256> quad_states{};
    std::array<LightSet,LightingPalette::max_lights> lights{};
    std::array<PreparedLighting,LightingPalette::max_states> lighting{};
    std::array<ImageLayer,64> images{};
    std::array<NormalVertex,4095*3> skin{};
    SceneFrame frame{occurrences.data(),occurrences.size(),parts.data(),parts.size(),
        lights.data(),lights.size(),lighting.data(),lighting.size(),
        {quad_vertices.data(),quad_vertices.size(),quad_states.data(),quad_states.size()},images.data(),images.size()};
};
struct Runtime {
    Store store[2];
    unsigned cap=0;            // store being captured by the game thread
    SceneFrame* fr=&store[0].frame;
    hooks::ProducerWorkspace workspace;
    hooks::SceneProducer producer{store[0].frame,workspace,data_02082214,4096};
    SceneRenderer renderer;
    WriterProvenance* provenance=nullptr;
    Matrix camera{};
    game::Affine view=game::unit_affine();
    bool view_valid=false;
    SceneClear clear{};
    std::uint64_t matrix_resets=0;
    bool clear_known=false;
    std::uint64_t scene=1,serial=0,writers=0,frames=0,refused=0;
    std::uint64_t recovery_skips=0;
    std::uint64_t diagnostic_pause_tick=0;
    aptHookCookie lifecycle_cookie{};
    std::uint64_t lifecycle_pause_tick=0;
    unsigned lifecycle_pauses=0;
    std::uint64_t presented_event=0;
    bool presented_once=false;
    bool recovery_cut=false;
    // Dry updates (after sm64ds-enhanced's dry ticks): an update whose image
    // will not be shown is known before the game draws, so the model, skin
    // and particle producers do no work. behind: late on the fixed timeline;
    // cap: Old's one image per two display events in 60 Hz scenes; busy: the
    // render worker shares the game's core and is still drawing (BUILD121).
    enum class Dry:std::uint8_t{none,behind,cap,busy};
    Dry dry=Dry::none;
    unsigned unpresented=0;   // consecutive updates whose image was not shown
    std::uint64_t dry_behind=0,dry_cap=0,late_skips=0,busy_drops=0,dry_busy=0;
    // BUILD121: total ticks of finished updates (DS logic, capture, 2D and
    // presentation), written by the game side, read by the render worker.
    std::uint64_t game_active_ticks=0;
    unsigned selector_language=6;
    std::uint64_t component_capture_frame=0;
    bool open=false,graphics=false,camera_valid=false,timings=false;
    bool diagnostic_continue=false;
    std::atomic<bool> unsubmitted_refusal{false};
    // PLAYABLE MODE (default; SM64DS_FRESH_NATIVE_STRICT=1 restores refusal).
    // Temporary: lenient capture + original 2D engines through the PPU bridge.
    bool playable=false,pace=false,native_menu=false;
    struct Playable {std::uint64_t frames=0,last_tick=0,frame_ticks=0,intervals=0,present_ticks=0,bridge_ticks=0,max_frame_ticks=0;
        std::uint64_t begin_tick=0,update_ticks=0,draw_ticks=0,wait_ticks=0,upload_ticks=0,submit_ticks=0,model_mark=0,skin_mark=0;
        std::uint64_t draws=0,matrices=0,light_rows=0,materials=0,shader_binds=0,pipeline=0,mesh_binds=0;
        std::uint64_t sdk_draw_ticks=0,sdk_draw_samples=0;
        std::uint64_t vblanks[4]{};} stats; // frame length histogram: 1,2,3,>=4 VBlanks
};
struct BridgeBuffers {ntr::Framebuffer zero,back,front,other;bool front_any;};
BridgeBuffers* bridge=nullptr;
Runtime* runtime=nullptr;
void lifecycle_hook(APT_HookType,void*);
bool requested=false;
Screen main_lcd(){return (ntr::io_read(0x04000304,2)&0x8000)?Screen::top:Screen::bottom;}
// The title toy captures engine A once, then remaps bank D as its BG3 canvas.
// Ownership changes only across wait_worker(); no live GPU/game memory is shared.
struct HeadCanvas {
    ntr::Framebuffer geometry{},composite{};
    std::array<std::uint16_t,256*192> pixels{};
    bool requested=false,ready=false;
};
HeadCanvas* head_canvas=nullptr;
thread_local bool native_worker_thread=false;
volatile int dev_worker_fault=0; // SM64DS_DEV_FAULT=worker:N (development)
Error consume_interface(){
#ifdef SM64DS_NATIVE_INTERFACE_RUNTIME
    for(unsigned channel=0;channel<2;++channel){
        std::uint32_t revision=0;int width=0,height=0;
        const auto* pixels=enhance_runtime::overlay_image(channel,revision,width,height);
        if(!pixels){runtime->renderer.hide_interface(channel);continue;}
        if(width<=0||height<=0||width>400||height>240)return Error::invalid_data;
        const InterfaceImage image{pixels,std::size_t(width)*height,unsigned(width),unsigned(height),unsigned(width)};
        const auto error=runtime->renderer.interface_image(channel,revision,image);
        if(error!=Error::none)return error;
        enhance_runtime::acknowledge(channel,revision);
    }
#endif
    return Error::none;
}
void capture_semantics(const char* label,bool (*write)(FILE*,std::uint64_t),std::uint64_t tick,std::uint64_t frame){
    char path[160],temporary[168];
    const int n=std::snprintf(path,sizeof(path),"sdmc:/3ds/sm64ds/native-%s-%llu-%llu.json",label,
        static_cast<unsigned long long>(tick),static_cast<unsigned long long>(frame));
    if(n<0||std::size_t(n)>=sizeof(path))return;
    std::snprintf(temporary,sizeof(temporary),"%s.tmp",path);FILE* file=std::fopen(temporary,"wb");bool okay=false;
    if(file){okay=write(file,frame);if(std::fflush(file)!=0)okay=false;if(std::fclose(file)!=0)okay=false;}
    if(okay)okay=std::rename(temporary,path)==0;
    if(!okay)std::remove(temporary);
    std::printf("Fresh native %s capture: status=%s frame=%llu path=%s diagnosticOnly=1 originalSceneAccepted=0\n",label,
        okay?"PASS":"FAIL",static_cast<unsigned long long>(frame),path);
}
void capture_interface(std::uint64_t tick,std::uint64_t frame){
#ifdef SM64DS_NATIVE_INTERFACE_RUNTIME
    // Explicit diagnostic boundary on the same game/presentation thread. The
    // regular consumer already copied this publication; no source pointer is
    // persisted, and the producer cannot run while this synchronous export runs.
    for(unsigned channel=0;channel<2;++channel){
        std::uint32_t revision=0;int width=0,height=0;
        const auto* pixels=enhance_runtime::overlay_image(channel,revision,width,height);
        if(!pixels||width<=0||height<=0||width>400||height>240)continue;
        const auto bytes=std::size_t(width)*height*4;std::uint32_t sum=2166136261u;
        for(std::size_t i=0;i<bytes;++i)sum=(sum^reinterpret_cast<const std::uint8_t*>(pixels)[i])*16777619u;
        char path[160],temporary[168];
        std::snprintf(path,sizeof path,"sdmc:/3ds/sm64ds/native-interface-%llu-%llu-%u.argb",
            static_cast<unsigned long long>(tick),static_cast<unsigned long long>(frame),channel);
        std::snprintf(temporary,sizeof temporary,"%s.tmp",path);
        FILE* file=std::fopen(temporary,"wb");bool okay=false;
        if(file){okay=std::fwrite(pixels,1,bytes,file)==bytes;if(std::fflush(file)!=0)okay=false;if(std::fclose(file)!=0)okay=false;}
        if(okay)okay=std::rename(temporary,path)==0;if(!okay)std::remove(temporary);
        std::printf("Fresh native interface capture: status=%s frame=%llu channel=%u revision=%lu width=%d height=%d bytes=%u fnv1a=%08lx path=%s sourcePixelsOnly=1 originalSceneAccepted=0\n",
            okay?"PASS":"FAIL",static_cast<unsigned long long>(frame),channel,static_cast<unsigned long>(revision),width,height,unsigned(bytes),static_cast<unsigned long>(sum),path);
    }
#else
    (void)tick;(void)frame;
#endif
}
void capture_component(bool producers_ok,int unmigrated_2d){
    auto& r=*runtime;
    if(!r.component_capture_frame||r.frames!=r.component_capture_frame)return;
    // Borrow only the owned native arrays before seal clears a refused frame.
    // This does not acquire/publish a SceneFrame or relax its complete-scene gate.
    r.component_capture_frame=0;
    const auto source_tick=svcGetSystemTick();
    // Source semantic snapshots remain useful when an unrelated model/cell
    // refuses its own capture. They do not publish that invalid component.
    capture_semantics("display",hooks::write_menu_display_capture,source_tick,r.frames);
    capture_semantics("background",hooks::write_menu_background_capture,source_tick,r.frames);
    capture_interface(source_tick,r.frames);
    if(!producers_ok||r.fr->failure()!=Error::none){
        std::printf("Fresh native component capture: refused invalid producer snapshot frame=%llu\n",static_cast<unsigned long long>(r.frames));return;
    }
    const auto start=svcGetSystemTick();char path[160],temporary[168];
    const int n=std::snprintf(path,sizeof(path),"sdmc:/3ds/sm64ds/native-component-%llu-%llu.json",
        static_cast<unsigned long long>(start),static_cast<unsigned long long>(r.frames));
    if(n<0||std::size_t(n)>=sizeof(path))return;
    std::snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    const SceneView view{r.store[r.cap].occurrences.data(),r.store[r.cap].parts.data(),r.fr->captured_occurrences(),r.fr->captured_parts(),
        r.scene,r.serial,{r.store[r.cap].lights.data(),r.store[r.cap].lighting.data(),r.fr->captured_light_sets(),r.fr->captured_light_states()},
        r.clear,r.store[r.cap].quad_vertices.data(),r.store[r.cap].quad_states.data(),r.fr->captured_quads()*4,r.fr->captured_quad_states(),
        r.store[r.cap].images.data(),r.fr->captured_images(),r.fr->captured_scene_order(),r.fr->captured_scene_screen(),
        r.store[r.cap].skin.data(),r.fr->captured_skin_vertices()};
    FILE* file=std::fopen(temporary,"wb");bool okay=false;
    if(file){okay=write_component_capture(file,view,{r.frames,r.writers,r.clear_known,unmigrated_2d!=0});
        if(std::fflush(file)!=0)okay=false;
        if(std::fclose(file)!=0)okay=false;
    }
    if(okay)okay=std::rename(temporary,path)==0;
    if(!okay)std::remove(temporary);
    std::printf("Fresh native component capture: status=%s frame=%llu path=%s ticks=%llu diagnosticOnly=1 originalSceneAccepted=0\n",
        okay?"PASS":"FAIL",static_cast<unsigned long long>(r.frames),path,
        static_cast<unsigned long long>(svcGetSystemTick()-start));

}
bool submit_menu_cell(const ImageLayer& image,void* user){
    auto& owner=*static_cast<Runtime*>(user);
    return owner.open&&owner.fr->append_image(image)==Error::none;
}
bool submit_particle(const hooks::ParticleDraw& draw,void* user){
    auto& owner=*static_cast<Runtime*>(user);
    if(!owner.open||!owner.camera_valid)return false;
    QuadState state;state.bank=QuadState::Bank::images;state.material=draw.material;
    state.image=draw.image;state.polygon_id=draw.polygon_id;state.fog=draw.fog;
    // Original particle centroid/basis is already in view space. Applying
    // owner.view again would move/rotate every billboard twice.
    auto error=game::project_pose(draw.geometry.object,owner.camera,state.object_to_clip);
    if(error==Error::none)error=owner.fr->append_quads(draw.bank_id,state,draw.geometry.vertex.data(),4);
    if(error!=Error::none)owner.fr->invalidate(error);
    return error==Error::none;
}
bool submit_menu_sprite(const hooks::MenuSpriteDraw& draw,void* user){
    auto& owner=*static_cast<Runtime*>(user);
    if(!owner.open||!owner.camera_valid||!owner.view_valid)return false;
    QuadState state;state.material=draw.material;state.image=draw.image;state.polygon_id=draw.polygon_id;state.fog=draw.fog;
    auto error=game::project_pose(game::compose(draw.object,owner.view),owner.camera,state.object_to_clip);
    if(error!=Error::none){owner.fr->invalidate(error);return false;}
    const auto work=owner.fr->quad_workspace();game::SpriteGeometryResult result;
    error=game::sprite_geometry(draw.geometry,work.data,work.capacity,result);
    if(error!=Error::none){owner.fr->invalidate(error);return false;}
    if(result.vertices)error=owner.fr->append_quads(draw.file_id,state,work.data,result.vertices);
    if(error!=Error::none){owner.fr->invalidate(error);return false;}
    hooks::menu_sprite_geometry_count(result.source_quads,result.vertices/4);return true;
}
bool submit_menu_stroke(const game::SketchStroke& stroke,void* user){
    auto& owner=*static_cast<Runtime*>(user);
    if(!owner.open||!owner.camera_valid||!owner.view_valid)return false;
    QuadState state;state.material.cull=Cull::none;state.material.blend=stroke.alpha<31?Blend::alpha:Blend::opaque;
    state.material.depth_write=stroke.alpha==31;state.polygon_id=6;
    auto error=game::project_pose(owner.view,owner.camera,state.object_to_clip);
    const auto work=owner.fr->quad_workspace();std::size_t count=0;
    if(error==Error::none)error=game::sketch_geometry(stroke,work.data,work.capacity,count);
    if(error==Error::none&&count)error=owner.fr->append_quads(untextured_quads,state,work.data,count);
    if(error!=Error::none)owner.fr->invalidate(error);
    return error==Error::none;
}
bool submit_menu_grid(const hooks::MenuGridDraw& draw,void* user){
    auto& owner=*static_cast<Runtime*>(user);
    if(!owner.open||!owner.camera_valid||!owner.view_valid)return false;
    PreparedPart part;part.material=draw.material;part.image=draw.image;part.visible=true;part.highlight=draw.lighting.grey;
    // ca5f0 pushes the position stack (0x04000444 is MTX_PUSH, not
    // MTX_MODE), then translates/scales locally under the current camera.
    // Scaling the camera translation clips the entire grid out of view.
    auto error=game::project_pose(game::compose(draw.object,owner.view),owner.camera,part.object_to_clip);
    std::size_t capacity=0,count=0;auto* vertices=owner.fr->skin_workspace(capacity);
    if(error==Error::none)error=game::wave_grid(draw.geometry,vertices,capacity,count);
    if(owner.timings&&error==Error::none){
        static unsigned snapshots=0;
        if(snapshots++<2){
            float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};unsigned inside=0;
            for(std::size_t i=0;i<count;++i){const auto& v=vertices[i].base;float clip[4];
                for(unsigned r=0;r<4;++r){const auto& m=part.object_to_clip.row[r];clip[r]=m[0]*v.x+m[1]*v.y+m[2]*v.z+m[3];}
                if(clip[3]<=0)continue;
                for(unsigned k=0;k<3;++k){const auto q=clip[k]/clip[3];lo[k]=std::min(lo[k],q);hi[k]=std::max(hi[k],q);}
                inside+=std::abs(clip[0])<=clip[3]&&std::abs(clip[1])<=clip[3]&&clip[2]>=-clip[3]&&clip[2]<=0;
            }
            std::printf("Native grid: file=%lu image=%u vertices=%u insideClip=%u bounds=[%.3f,%.3f,%.3f]-[%.3f,%.3f,%.3f] highlight=%u\n",
                (unsigned long)draw.file_id,unsigned(draw.image),unsigned(count),inside,lo[0],lo[1],lo[2],hi[0],hi[1],hi[2],unsigned(part.highlight));
        }
    }
    LightingInput light;light.lights=&draw.lights;light.material=draw.lighting;
    std::copy_n(owner.view.value,9,light.normal.begin());
    if(error==Error::none)error=owner.fr->append_mesh(draw.file_id,part,&light,vertices,count);
    if(error!=Error::none)owner.fr->invalidate(error);
    return error==Error::none;
}
void verdict(Error error){
    if(error!=Error::none)++runtime->refused;
    if(runtime->frames%120==1||(error!=Error::none&&runtime->refused==1)){
        if(runtime->provenance){
            std::printf("Fresh native writer summary: frame=%llu origins=%u dropped=%llu diagnosticOnly=1\n",
                static_cast<unsigned long long>(runtime->frames),unsigned(runtime->provenance->size()),
                static_cast<unsigned long long>(runtime->provenance->dropped()));
            for(unsigned i=0;i<runtime->provenance->size();++i){const auto& origin=runtime->provenance->entries()[i];
                std::printf("Fresh native writer: frame=%llu kind=%lu caller=%08lx target=%08lx width=%lu calls=%llu units=%llu\n",
                    static_cast<unsigned long long>(runtime->frames),static_cast<unsigned long>(origin.kind),
                    static_cast<unsigned long>(origin.caller),static_cast<unsigned long>(origin.target),static_cast<unsigned long>(origin.width),
                    static_cast<unsigned long long>(origin.calls),static_cast<unsigned long long>(origin.units));
            }
        }
        const auto& hooks_stats=hooks::statistics();const auto& resources=runtime->renderer.metrics();
        const auto& sprites=hooks::menu_sprite_statistics();
        const auto& particles=hooks::particle_statistics();
        const auto& oam=hooks::menu_oam_statistics();
        const auto& display=hooks::menu_display_statistics();
        const auto& backgrounds=hooks::menu_background_statistics();
        const auto& ui=runtime->renderer.interface_metrics();
        std::printf("Fresh native interface: publications=%llu uploads=%llu reuses=%llu scannedPixels=%llu uploadBytes=%llu uploadTicks=%llu wholeSceneAccepted=0\n",
            static_cast<unsigned long long>(ui.publications),static_cast<unsigned long long>(ui.uploads),static_cast<unsigned long long>(ui.reuses),
            static_cast<unsigned long long>(ui.scan_pixels),static_cast<unsigned long long>(ui.upload_bytes),static_cast<unsigned long long>(ui.upload_ticks));
        std::printf("Fresh native background: registered=%llu retired=%llu crops=%llu replaced=%llu avoidedFillBytes=%llu avoidedCopyBytes=%llu paletteWrites=%llu paletteBatches=%llu unknown=%llu emitted=%llu hiddenWindows=%llu\n",
            static_cast<unsigned long long>(backgrounds.registered),static_cast<unsigned long long>(backgrounds.retired),
            static_cast<unsigned long long>(backgrounds.crops),static_cast<unsigned long long>(backgrounds.replaced),
            static_cast<unsigned long long>(backgrounds.filled_bytes),static_cast<unsigned long long>(backgrounds.copied_bytes),
            static_cast<unsigned long long>(backgrounds.palette_writes),static_cast<unsigned long long>(backgrounds.palette_batches),
            static_cast<unsigned long long>(backgrounds.unknown),static_cast<unsigned long long>(backgrounds.emitted),static_cast<unsigned long long>(backgrounds.hidden_windows));
        std::printf("Fresh native display: brightness=%llu strength=%llu alpha=%llu disabled=%llu backgrounds=%llu dirty=%llu avoidedTransferBytes=%llu failures=%llu backgroundGroupTicks=%llu coverageComplete=0\n",
            static_cast<unsigned long long>(display.brightness),static_cast<unsigned long long>(display.strength),
            static_cast<unsigned long long>(display.alpha),static_cast<unsigned long long>(display.disable),static_cast<unsigned long long>(display.background),
            static_cast<unsigned long long>(display.dirty_backgrounds),static_cast<unsigned long long>(display.skipped_bytes),static_cast<unsigned long long>(display.failures),static_cast<unsigned long long>(display.background_group_ticks));
        std::printf("Fresh native menu2D: resources=%llu liveCells=%u calls=%llu associated=%llu unassociated=%llu captured=%llu failures=%llu cacheHits=%llu captureTicks=%llu wholeSceneAccepted=0\n",
            static_cast<unsigned long long>(oam.resources),oam.live_cells,static_cast<unsigned long long>(oam.calls),
            static_cast<unsigned long long>(oam.associated),static_cast<unsigned long long>(oam.unassociated),
            static_cast<unsigned long long>(oam.captured),static_cast<unsigned long long>(oam.failures),
            static_cast<unsigned long long>(oam.cache_hits),static_cast<unsigned long long>(oam.capture_ticks));
        std::printf("Fresh native particles: resources=%llu managers=%llu binds=%llu calls=%llu capturedDraws=%llu failures=%llu flushes=%llu issue=%s detail=%08lx\n",
            static_cast<unsigned long long>(particles.resource_loads),static_cast<unsigned long long>(particles.manager_loads),
            static_cast<unsigned long long>(particles.bindings),static_cast<unsigned long long>(particles.calls),
            static_cast<unsigned long long>(particles.draws),static_cast<unsigned long long>(particles.failures),
            static_cast<unsigned long long>(particles.flushes),particles.last_issue,static_cast<unsigned long>(particles.last_detail));
        std::printf("Fresh native sprites: descriptorUpdates=%llu calls=%llu capturedDraws=%llu failures=%llu sourceQuads=%llu nativeQuads=%llu issue=%s detail=%08lx\n",
            static_cast<unsigned long long>(sprites.descriptors),static_cast<unsigned long long>(sprites.calls),
            static_cast<unsigned long long>(sprites.draws),static_cast<unsigned long long>(sprites.failures),
            static_cast<unsigned long long>(sprites.source_quads),static_cast<unsigned long long>(sprites.native_quads),sprites.last_issue,static_cast<unsigned long>(sprites.last_detail));
        if(runtime->timings&&sprites.failures)for(unsigned i=0;i<64;++i){hooks::MenuImageDiagnostic image;
            if(hooks::menu_image_diagnostic(i,image))std::printf("Fresh native sprite identity: descriptor=%08lx file=%lu image=%lu diagnosticOnly=1\n",
                static_cast<unsigned long>(image.descriptor),static_cast<unsigned long>(image.file),static_cast<unsigned long>(image.image));
        }
        std::printf("Fresh native frame=%llu scene=%llu status=%s models=%llu missing=%llu missingFile=%lu refused=%llu legacyWords=%llu loads=%llu gpuLinearBytes=%u surfaceVRAM=%u outputVRAM=%u\n",
            static_cast<unsigned long long>(runtime->frames),static_cast<unsigned long long>(runtime->scene),describe(error),
            static_cast<unsigned long long>(hooks_stats.model_calls),static_cast<unsigned long long>(hooks_stats.missing_models),
            static_cast<unsigned long>(hooks_stats.last_missing_file),
            static_cast<unsigned long long>(runtime->refused),static_cast<unsigned long long>(runtime->writers),
            static_cast<unsigned long long>(resources.loads),unsigned(runtime->renderer.backend_metrics().live_linear_bytes),
            unsigned(runtime->renderer.backend_metrics().live_vram_bytes),unsigned(runtime->renderer.output_vram_bytes()));
        std::printf("Fresh native source: fsNotifications=%llu modelLoads=%llu directModels=%llu registeredLoads=%llu missingAddress=%08lx\n",
            static_cast<unsigned long long>(hooks_stats.load_notifications),static_cast<unsigned long long>(hooks_stats.typed_model_loads),
            static_cast<unsigned long long>(hooks_stats.direct_model_loads),
            static_cast<unsigned long long>(hooks_stats.observed_loads),static_cast<unsigned long>(hooks_stats.last_missing_address));
        const auto& lighting=hooks::lighting_statistics();
        const auto& light_backend=runtime->renderer.backend_metrics();
        std::printf("Fresh native lighting: views=%llu sourceCalls=%llu updates=%llu failures=%llu knownMask=%u gpuLitDraws=%llu uniformRows=%llu\n",
            static_cast<unsigned long long>(lighting.views),static_cast<unsigned long long>(lighting.source_calls),
            static_cast<unsigned long long>(lighting.updates),static_cast<unsigned long long>(lighting.failures),unsigned(hooks::lighting_known()),
            static_cast<unsigned long long>(light_backend.lit_draws),static_cast<unsigned long long>(light_backend.lighting_uniform_rows));
        if(runtime->timings){
            const auto& backend=runtime->renderer.backend_metrics();
            std::printf("Fresh native timing ticks: modelCaptureTotal=%llu spriteCaptureTotal=%llu particleGroupTotal=%llu bankLoadTotal=%llu lastQueueWait=%llu lastUpload=%llu lastSubmit=%llu previousQueueMs=%.3f acceptedFrames=%llu (guest in emulator; bank load includes upload)\n",
                static_cast<unsigned long long>(hooks_stats.producer_ticks),static_cast<unsigned long long>(sprites.capture_ticks),
                static_cast<unsigned long long>(particles.capture_ticks),static_cast<unsigned long long>(resources.load_ticks),
                static_cast<unsigned long long>(backend.wait_ticks),static_cast<unsigned long long>(backend.upload_ticks),
                static_cast<unsigned long long>(backend.submit_ticks),double(backend.previous_gpu_queue_ms),
                static_cast<unsigned long long>(resources.frames));
        }
    }
}
}
// BUILD124 Old 3DS core layout. PM caps the application's core-1 time at
// (exheader MaxCpu & 0x7F) percent: 0x9E allowed 30 and APT refused 80 with
// d8e05bf4 (BUILD88 hardware log). With MaxCpu 0xD0 player_present asks for
// 80; when APT grants it, the render worker owns core 1's single application
// thread and audio pumps on core 0 above the game thread, so a game update
// and an image no longer share one core (BUILD123 tour: Jolly Roger Bay and
// the Bowser fights at 15-20 FPS with update + render above 33 ms).
namespace {
std::atomic<int> render_core1_granted{0},audio_core_used{-1};
}
// BUILD129: opt-in only. The BUILD128 hardware tour (render core 1 at 80%,
// audio core 0) lost on 45 of 49 levels (27.5 -> 23.9 FPS): GSP and FS run
// on core 1 and share its time slices, so the measured GPU time doubled
// (~9 -> ~21 ms), submit/finish and SD loads slowed, and the game thread
// grew 40-50% with the mixer on core 0. sdmc:/3ds/sm64ds/nucleo-render-1.txt
// still selects that layout for experiments. Development builds:
// SM64DS_FRESH_NATIVE_WORKER_CORE (env.txt).
// BUILD131 experiment, opt-in: DS fog through the PICA fog table (ds_fog.hpp;
// an approximation over window depth). sdmc:/3ds/sm64ds/niebla.txt, or
// SM64DS_NATIVE_FOG=1 in a development build's env.txt.
static bool fog_wanted(){
    if(const char* c=std::getenv("SM64DS_NATIVE_FOG"))return std::atoi(c)!=0;
    struct stat st;return stat("sdmc:/3ds/sm64ds/niebla.txt",&st)==0;
}
static bool fog_experiment=false;
static DsFog read_fog(){
    DsFog fog;const auto* io=static_cast<const std::uint8_t*>(ntr::host_ptr(0x04000000));
    if(!io)return fog;
    std::uint16_t control,offset;std::uint32_t colour;
    std::memcpy(&control,io+0x60,2);std::memcpy(&colour,io+0x358,4);std::memcpy(&offset,io+0x35c,2);
    fog.enabled=bool(control&0x80u);fog.alpha_only=bool(control&0x40u);fog.shift=std::uint8_t((control>>8)&15u);
    fog.offset=std::uint16_t(offset&0x7fffu);fog.colour=colour;std::memcpy(fog.table.data(),io+0x360,32);
    return fog;
}
extern "C" int sm64ds_fresh_native_render_core1_wanted(void){
    if(const char* c=std::getenv("SM64DS_FRESH_NATIVE_WORKER_CORE"))return std::atoi(c)==1;
    struct stat st;return stat("sdmc:/3ds/sm64ds/nucleo-render-1.txt",&st)==0;
}
// player_present, after APT answered and the mixer thread exists.
extern "C" void sm64ds_fresh_native_core_layout(int render_core1,int audio_core){
    render_core1_granted.store(render_core1?1:0,std::memory_order_relaxed);
    audio_core_used.store(audio_core,std::memory_order_relaxed);
}
// BUILD121: the hardware tour stopped after 25 relaunches. Boot 25 could not
// create the render worker and boot 26 not even the game thread, with the
// heap intact; every boot had also failed to create the fs prefetch thread
// on core 1. The APPLICATION resource limit outlives a process, so these
// counts show a leak from one boot to the next.
namespace {
struct ResourceUse {s64 current[10]{},limit[10]{};bool ok=false;};
ResourceUse resource_use(){
    ResourceUse use;::Handle handle=0;
    if(R_FAILED(svcGetResourceLimit(&handle,CUR_PROCESS_HANDLE)))return use;
    ResourceLimitType names[10]={RESLIMIT_PRIORITY,RESLIMIT_COMMIT,RESLIMIT_THREAD,RESLIMIT_EVENT,RESLIMIT_MUTEX,
        RESLIMIT_SEMAPHORE,RESLIMIT_TIMER,RESLIMIT_SHAREDMEMORY,RESLIMIT_ADDRESSARBITER,RESLIMIT_CPUTIME};
    use.ok=R_SUCCEEDED(svcGetResourceLimitCurrentValues(use.current,handle,names,10))&&
           R_SUCCEEDED(svcGetResourceLimitLimitValues(use.limit,handle,names,10));
    svcCloseHandle(handle);return use;
}
void log_resource_use(const char* when){
    const auto u=resource_use();
    if(!u.ok){std::printf("Resource limits %s: unavailable\n",when);return;}
    std::printf("Resource limits %s: threads=%lld/%lld events=%lld/%lld mutexes=%lld/%lld semaphores=%lld/%lld timers=%lld/%lld "
        "sharedMemory=%lld/%lld arbiters=%lld/%lld cpuTime=%lld/%lld commitKiB=%lld/%lld\n",when,
        u.current[2],u.limit[2],u.current[3],u.limit[3],u.current[4],u.limit[4],u.current[5],u.limit[5],u.current[6],u.limit[6],
        u.current[7],u.limit[7],u.current[8],u.limit[8],u.current[9],u.limit[9],u.current[1]/1024,u.limit[1]/1024);
}
}
extern "C" int sm64ds_fresh_native_startup(){
    const char* option=std::getenv("SM64DS_FRESH_NATIVE");requested=option&&std::strcmp(option,"1")==0;
    if(!requested)return 1;
    if(runtime)return 0;
    sm64ds_fatal_handlers_install();
    log_resource_use("boot");
    // BUILD121: no fs prefetch thread. It asks for core 1, where libctru
    // allows one application thread, owned here by audio; on hardware every
    // boot failed to create it (Azahar does not enforce the rule). Its level
    // cache never existed on the console, so memory matches the hardware.
    // env.txt (development builds) can still set SM64DS_NO_FS_PREFETCH=0.
    setenv("SM64DS_NO_FS_PREFETCH","1",0);
    sm64ds_test_mode_boot(); // before the profile switch below is read
    sm64ds_fresh_native_math_configure();
    void* storage=std::malloc(sizeof(Runtime));if(!storage)return 0;
    runtime=new(storage)Runtime{};const auto catalog=prepared_model_catalog();const auto images=prepared_image_catalog();
    const auto menu=hooks::prepared_menu_cells();const auto backgrounds=hooks::prepared_menu_backgrounds();
    const auto objects=hooks::prepared_menu_objects();
    const auto cells_error=hooks::validate(menu,images),objects_error=hooks::validate(objects,images);
    const auto backgrounds_error=hooks::validate(backgrounds,prepared_canvas_catalog());
    if(!catalog.size()||!images.size()||cells_error!=Error::none||objects_error!=Error::none||backgrounds_error!=Error::none){
        std::printf("Fresh native startup: catalog mismatch models=%u images=%u cells=%s objects=%s backgrounds=%s\n",
            unsigned(catalog.size()),unsigned(images.size()),describe(cells_error),describe(objects_error),describe(backgrounds_error));
        runtime->~Runtime();std::free(runtime);runtime=nullptr;return 0;
    }
    const char* writers=std::getenv("SM64DS_FRESH_NATIVE_WRITERS");
    if(writers&&std::strcmp(writers,"1")==0){void* trace=std::malloc(sizeof(WriterProvenance));
        if(!trace){runtime->~Runtime();std::free(runtime);runtime=nullptr;return 0;}
        runtime->provenance=new(trace)WriterProvenance{};
    }
    {   // BUILD131: drop shadows are on. sdmc:/3ds/sm64ds/sin-sombras.txt (or
        // SM64DS_NATIVE_SHADOWS=0 in env.txt) leaves them out, to compare
        // their cost on a console or if a GPU shows them wrong.
        const char* e=std::getenv("SM64DS_NATIVE_SHADOWS");struct stat st;
        const bool off=e?std::atoi(e)==0:stat("sdmc:/3ds/sm64ds/sin-sombras.txt",&st)==0;
        if(!off)hooks::install_shadows(hooks::SceneProducer::submit_shadow);
        std::printf("Native shadow volumes: %s\n",off?"off (sin-sombras.txt)":"on");
    }
    if(!hooks::install(catalog,hooks::SceneProducer::submit,&runtime->producer)||!hooks::install_menu_sprites(submit_menu_sprite,runtime)||!hooks::install_menu_grid(submit_menu_grid,runtime)||!hooks::install_menu_strokes(submit_menu_stroke,runtime)||
       !hooks::install_particles(images,submit_particle,runtime)||
       !hooks::install_menu_cells(menu,submit_menu_cell,runtime,objects,main_lcd)||!hooks::install_menu_backgrounds(backgrounds,prepared_canvas_catalog())||
       !hooks::install_menu_backdrop_views(hooks::prepared_menu_backdrops(),prepared_canvas_catalog())||!hooks::activate(true)){
        if(runtime->provenance){runtime->provenance->~WriterProvenance();std::free(runtime->provenance);}
        runtime->~Runtime();std::free(runtime);runtime=nullptr;return 0;
    }
    hooks::lighting_reset();
    const char* capture=std::getenv("SM64DS_FRESH_NATIVE_COMPONENT_CAPTURE");
    if(capture&&*capture){
        unsigned value=0;bool okay=true;
        for(const char* c=capture;*c;++c){if(*c<'0'||*c>'9'||value>100000){okay=false;break;}value=value*10+unsigned(*c-'0');}
        if(okay&&value&&value<=1000000)runtime->component_capture_frame=value;
        else std::printf("Fresh native component capture: invalid frame option; disabled\n");
    }
    const char* strict=std::getenv("SM64DS_FRESH_NATIVE_STRICT");
    runtime->playable=!(strict&&std::strcmp(strict,"1")==0);
    if(runtime->playable){
        void* buffers=std::malloc(sizeof(BridgeBuffers));
        if(!buffers){runtime->playable=false;std::puts("Fresh native playable: bridge allocation failed; strict mode");}
        else{bridge=static_cast<BridgeBuffers*>(buffers);std::memset(bridge,0,sizeof(BridgeBuffers));
            for(auto& s:runtime->store){s.frame.set_lenient(true);s.frame.set_skin_storage(s.skin.data(),s.skin.size());}hooks::set_bridge_2d(true);game::set_lenient_materials(true);LightingState::set_lenient(true);
            std::puts("Fresh native PLAYABLE mode: lenient capture, temporary PPU 2D bridge, incomplete effects accepted (not original-scene acceptance)");}
        if(runtime->playable&&fog_wanted()){fog_experiment=true;game::set_material_fog(true);
            std::puts("Native fog experiment: on (DS fog table through the PICA fog table; approximation over window depth)");}
    }
    hooks::configure_dialogue(runtime->playable&&images.find(6000));
    hooks::menu_oam_lenient(runtime->playable);
    const char* profile=std::getenv("SM64DS_FRESH_NATIVE_PROFILE");runtime->timings=profile?std::strcmp(profile,"1")==0:native_diagnostics::enabled();
    hooks::set_stage_profile(runtime->timings);
    const char* continuation=std::getenv("SM64DS_FRESH_NATIVE_DIAGNOSTIC_CONTINUE");runtime->diagnostic_continue=continuation&&std::strcmp(continuation,"1")==0;
    if(runtime->diagnostic_continue)std::puts("Fresh native diagnostics: continueUnsubmittedFrames=1 originalSceneAccepted=0 gpuFailuresNotIgnored=1");
    if(runtime->timings){hooks::set_clock(svcGetSystemTick);hooks::menu_sprite_clock(svcGetSystemTick);hooks::particle_clock(svcGetSystemTick);hooks::menu_oam_clock(svcGetSystemTick);hooks::menu_display_clock(svcGetSystemTick);}
    std::printf("Fresh native enabled: captureBytes=%u catalog=%u imageCatalog=%u legacyGxWrites=guarded wholeGameCoverage=0 canvasCatalog=%u\n",
        unsigned(sizeof(Runtime)),unsigned(catalog.size()),unsigned(images.size()),unsigned(prepared_canvas_catalog().size()));return 1;
}
extern "C" int sm64ds_fresh_native_enabled(){return requested&&runtime;}
extern "C" void sm64ds_fresh_native_actor(std::uint64_t identity){if(runtime)runtime->producer.owner(identity);}
extern "C" int sm64ds_fresh_native_on_worker(){return native_worker_thread;}
extern "C" void sm64ds_fresh_native_menu_preload_selector(unsigned language){
    if(runtime&&language<6)runtime->selector_language=language;
}
extern "C" void sm64ds_fresh_native_menu_2d_start(){
    if(!runtime||!runtime->playable)return;
    const auto* option=std::getenv("SM64DS_NATIVE_MENU_2D");
    if(!option||*option!='0'){
        runtime->native_menu=true;hooks::set_bridge_2d(false);
        std::puts("Native menu2D test: enabled; PPU preparation/composition bypassed; remaining effects are diagnostic gaps");
    }
}
extern "C" bool sm64ds_render60_need_exact();
namespace {
bool render_worker_blocks_update(); // render worker section below
void note_game_active();            // presentation section below
// At least one update in three is shown, however late the game runs.
constexpr unsigned kMaxUnpresented=2;
// BUILD134: Display > Lock FPS reaches the native renderer (until now only
// the console model decided, so an Old 3DS could not choose 60).
//   30       one exact image per update (two display events).
//   60       interpolated image plus the exact one, each on its own LCD refresh.
//   Unlocked the same two images without waiting for the LCD.
// Auto resolves to 30 on Old and 60 on New (enhance_settings.h). The game's
// 30 Hz update clock never changes. SM64DS_FPS_LOCK=0/1/2 forces Unlocked/30/60
// in development builds.
struct PresentMode{unsigned divisor;bool interpolate,sync;unsigned lock;};
PresentMode present_mode(){
    static const int forced=[](){const char* e=std::getenv("SM64DS_FPS_LOCK");return e&&*e>='0'&&*e<='2'&&!e[1]?*e-'0':-1;}();
    // BUILD135 development check of a change from the pause menu while
    // playing: SM64DS_FPS_LOCK_CYCLE=N steps 30 -> 60 -> Unlocked every N
    // game updates (env.txt, .3dsx only; the CIA never reads it).
    static const unsigned cycle=[](){const char* e=std::getenv("SM64DS_FPS_LOCK_CYCLE");return e?unsigned(std::strtoul(e,nullptr,10)):0u;}();
    unsigned lock=forced>=0?unsigned(forced):unsigned(enhance::live().fps_lock.load(std::memory_order_relaxed));
    if(cycle&&runtime){constexpr unsigned steps[]={enhance::kFpsLock30,enhance::kFpsLock60,enhance::kFpsLockOff};lock=steps[unsigned(runtime->frames/cycle)%3];}
    if(lock==enhance::kFpsLock30)return {2,false,false,lock};
    return {1,true,lock==enhance::kFpsLock60,lock};
}
// Old 3DS (render worker on the game's core): both images of an update must
// fit, with the update itself, in one 33.4 ms period. Game thread only.
struct InterpolationBudget{
    std::uint64_t image_ticks=0,second_ticks=0; // smoothed worker cost: first image, exact second image
    unsigned hold=0,backoff=30,streak=0;        // updates left at one image; next hold; clean interpolated updates
    std::uint64_t allowed=0,over=0,misses=0;    // decisions and missed second images
} interpolation_budget;
bool interpolation_fits(std::uint64_t update_ticks){
    auto& b=interpolation_budget;
    if(b.hold){--b.hold;return false;}
    if(!b.image_ticks)return false; // no measurement yet
    const auto second=b.second_ticks?b.second_ticks:b.image_ticks;
    // 92% of the period: the rest covers audio maintenance and wake-up jitter.
    const bool fits=update_ticks+b.image_ticks+second<=std::uint64_t(SYSCLOCK_ARM11)*560190ull*2ull/33513982ull*92ull/100ull;
    if(fits)++b.allowed;else ++b.over;
    return fits;
}
void interpolation_missed(){
    auto& b=interpolation_budget;++b.misses;b.hold=b.backoff;b.backoff=std::min(b.backoff*2u,480u);b.streak=0;
}
bool skips_enabled(){static const bool enabled=[](){const char* e=std::getenv("SM64DS_FRESH_NATIVE_SKIP");return !e||*e!='0';}();return enabled;}
// No skip while an exact image is owed: the title canvas, a diagnostic or
// component capture, or a DS display capture the game asked for.
bool exact_image_owed(const Runtime& r){
    static const bool capture_frame=std::getenv("SM64DS_GAME_CAPTURE_FRAME")!=nullptr;
    return capture_frame||(head_canvas&&head_canvas->requested)||r.component_capture_frame==r.frames+1||
        native_diagnostics::requested||native_diagnostics::partial_requested||sm64ds_render60_need_exact()||
        (ntr::io_read(0x04000064,4)&0x80000000u)||ntr::io_display_capture();
}
// Decided before the game draws (sm64ds_fresh_native_begin).
Runtime::Dry dry_decision(const Runtime& r){
    static const bool enabled=[](){const char* e=std::getenv("SM64DS_FRESH_NATIVE_DRY");return !e||*e!='0';}();
    if(!enabled||!skips_enabled()||!r.playable||exact_image_owed(r))return Runtime::Dry::none;
    // rt_run counts the display event before the hook that presents this
    // update, so its image would carry event rt_frame()+1.
    const auto divisor=present_mode().divisor;
    if(r.presented_once&&ntr::rt_frame()+1-r.presented_event<divisor)return Runtime::Dry::cap;
    // BUILD121: the BUILD 120 hardware tour discarded 37% of all updates after
    // a full capture because the worker was still drawing. A worker on the
    // game's core at a lower priority cannot advance while the game thread
    // runs, so such a capture can never be submitted: skip its producers and
    // give that time to the render. Not limited by kMaxUnpresented (nothing
    // could be shown); the first update after the job ends captures again.
    static const bool busy_dry=[](){const char* e=std::getenv("SM64DS_FRESH_NATIVE_BUSY_DRY");return !e||*e!='0';}();
    if(busy_dry&&render_worker_blocks_update())return Runtime::Dry::busy;
    if(r.unpresented<kMaxUnpresented&&observed_cadence::behind())return Runtime::Dry::behind;
    return Runtime::Dry::none;
}
}
extern "C" int sm64ds_fresh_native_allow_skip(){
    if(!requested||!runtime)return 1;
    return skips_enabled()&&runtime->playable&&runtime->open&&!exact_image_owed(*runtime);
}
// The original-frame hook asks this instead of its own lateness test. Only
// an open capture can be skipped: the intermediate VBlank of a 30 Hz update
// always presents (it finishes New 3DS interpolation). A dry capture has
// nothing to draw, so it is always skipped; a cap-dry one goes to the
// presentation path, whose display cap drops it.
extern "C" int sm64ds_fresh_native_skip_wanted(int capturing){
    if(!runtime||!runtime->playable||!runtime->open)return 0;
    if(runtime->dry==Runtime::Dry::behind||runtime->dry==Runtime::Dry::busy)return 1;
    if(runtime->dry==Runtime::Dry::cap)return 0;
    return !capturing&&sm64ds_fresh_native_allow_skip()&&runtime->unpresented<kMaxUnpresented&&observed_cadence::behind();
}
extern "C" void sm64ds_fresh_native_skip(){
    if(!runtime||!runtime->playable||!runtime->open)return;
    // Retire only the unpublished capture. The render worker may still own
    // the other store; no GPU resource, retained pose or submitted lease moves.
    hooks::menu_oam_end();hooks::end_frame();runtime->open=false;
    if(runtime->fr->seal(true,0)==Error::none)runtime->fr->release();
    if(runtime->dry==Runtime::Dry::none)++runtime->late_skips;
    ++runtime->frames;++runtime->recovery_skips;runtime->recovery_cut=true;++runtime->unpresented;
    note_game_active();
}
extern "C" int sm64ds_fresh_native_graphics(){
    if(!runtime)return 0;
    const char* root=std::getenv("SM64DS_FRESH_NATIVE_ASSETS");if(!root)root="sdmc:/3ds/sm64ds/native";
    const auto error=runtime->renderer.init(prepared_model_catalog(),root,sm64ds_fresh_native_shader_shbin,
        sm64ds_fresh_native_shader_shbin_size,12*1024*1024,runtime->timings,true,prepared_image_catalog(),prepared_canvas_catalog(),console_profile::current().antialias);
    std::printf("Native presentation profile: targetFPS=%u antialias=%ux interface=unchanged logicClock=fixed\n",
        present_mode().divisor==2?30u:60u,console_profile::current().antialias?2u:1u);
    runtime->graphics=error==Error::none;
    // Registered after C3D_Init: drain before Citro3D disables VBlank on suspend.
    if(runtime->graphics)aptHook(&runtime->lifecycle_cookie,lifecycle_hook,nullptr);
    runtime->renderer.set_lenient(runtime->playable);
    // The game owns its 30 Hz cadence; this worker retains the GPU fence
    // without adding another VBlank wait to every update.
    {const char* v=std::getenv("SM64DS_FRESH_NATIVE_PACE");runtime->pace=v&&*v=='1';runtime->renderer.set_pace(runtime->pace);}
    if(runtime->graphics&&runtime->playable){
        // Known menu resource identity: data/DSMT/face_demo_mario.bmd.
        // Upload while the first scene is still hidden, instead of at head reveal.
        const auto* preload=std::getenv("SM64DS_NATIVE_PRELOAD_HEAD");
        if(!preload||*preload!='0'){
            const auto warm=runtime->renderer.preload_model(202);
            std::printf("Native head preload queued: %s\n",describe(warm));
            // The head reveal also introduces shared menu buttons/portraits.
            // Warm those banks while hidden, through the same bounded owner.
            const auto* menu=std::getenv("SM64DS_NATIVE_PRELOAD_MENU");
            if(!menu||*menu!='0'){
                const auto buttons=runtime->renderer.preload_model_images(222);
                const auto portraits=runtime->renderer.preload_image_bank(4101);
                std::printf("Native menu preload queued: buttons=%s portraits=%s\n",describe(buttons),describe(portraits));
            }
        }
    }
    if(!runtime->graphics)std::printf("Fresh native initialization: %s\n",describe(error));
    return runtime->graphics;
}
extern "C" void sm64ds_fresh_native_scene_changed(){
    if(!runtime)return;
    if(runtime->native_menu){runtime->native_menu=false;hooks::set_bridge_2d(runtime->playable);std::puts("Native menu2D test: scene exit; gameplay route restored");}
    ++runtime->scene;runtime->camera_valid=runtime->view_valid=false;hooks::lighting_reset();hooks::menu_oam_scene_changed();hooks::menu_display_scene_changed();hooks::menu_background_scene_changed();
    if(runtime->open)runtime->fr->invalidate(Error::unsupported); // never publish a mixed-scene transition frame
    // BUILD121: buffered log from the first scene change on. game_main runs
    // the whole game with stdout unbuffered, so every printf was a synchronous
    // SD write on the game or render thread, and a render job that printed
    // while the game thread used the SD waited for it (hardware: 1.4-2.5 s
    // stalls). Startup and the title stay unbuffered for boot diagnostics.
    // Flushed here (a scene change already loads files), every 20 s, inside
    // test report pauses, at shutdown and on the fatal paths.
    static bool buffered=false;
    if(!buffered){static char log_buffer[32*1024];buffered=std::setvbuf(stdout,log_buffer,_IOFBF,sizeof log_buffer)==0;
        // BUILD129: BUILD128's post-tour log shows the game thread's
        // once-a-minute fflush froze play for 0.4-0.7 s (21-25 KB per SD
        // write). The lowest-priority thread on the game's core now writes
        // the buffer every 20 s (under 10 KB, so it never fills during
        // play); it only runs while the game and render threads wait.
        if(buffered)threadCreate([](void*){for(;;){svcSleepThread(20000000000LL);std::fflush(stdout);}},nullptr,4096,0x3f,-2,true);}
    else std::fflush(stdout);
}
extern "C" void sm64ds_playable_script_tick();
extern "C" void sm64ds_fresh_native_begin(){
    if(!runtime)return;
    sm64ds_test_mode_update(); // may write a report: excluded from update timing below
    sm64ds_playable_script_tick();
    // Development check of the BUILD115 exit path (env.txt, .3dsx only; the
    // CIA ignores env.txt): SM64DS_DEV_FAULT=terminate:N or abort:N fails on
    // update N exactly like an exhausted heap or an abort site would.
    {static const struct Fault {int kind;unsigned at;} fault=[]{
         const char* v=std::getenv("SM64DS_DEV_FAULT");Fault f{0,0};if(!v)return f;
         if(!std::strncmp(v,"terminate:",10)){f.kind=1;f.at=unsigned(std::atoi(v+10));}
         else if(!std::strncmp(v,"abort:",6)){f.kind=2;f.at=unsigned(std::atoi(v+6));}
         else if(!std::strncmp(v,"worker:",7)){f.kind=3;f.at=unsigned(std::atoi(v+7));}
         return f;}();
     static unsigned updates=0;
     if(fault.kind&&++updates==fault.at){
         std::printf("SM64DS_DEV_FAULT: %s at update %u\n",fault.kind==1?"throw std::bad_alloc":fault.kind==2?"abort()":"render worker throws std::bad_alloc",updates);
         if(fault.kind==1)throw std::bad_alloc();
         if(fault.kind==2)std::abort();
         __atomic_store_n(&dev_worker_fault,1,__ATOMIC_RELEASE);
     }}
    // A fatal error on another thread: exit from here (see fatal_abort_signal).
    if(__atomic_load_n(&sm64ds_fatal_thread_pending,__ATOMIC_ACQUIRE)){
        std::printf("FATAL: another thread failed; the game thread exits\n");std::fflush(stdout);std::abort();
    }
    runtime->stats.begin_tick=svcGetSystemTick();
    if(runtime->open){hooks::end_frame();hooks::menu_oam_end();runtime->fr->seal(false,0);runtime->open=false;}
    runtime->writers=0;
    hooks::menu_oam_trace(runtime->component_capture_frame&&runtime->frames+1==runtime->component_capture_frame);
    if(runtime->provenance)runtime->provenance->reset();
    runtime->dry=dry_decision(*runtime);hooks::set_dry(runtime->dry!=Runtime::Dry::none);
    if(runtime->dry==Runtime::Dry::behind)++runtime->dry_behind;else if(runtime->dry==Runtime::Dry::cap)++runtime->dry_cap;
    else if(runtime->dry==Runtime::Dry::busy)++runtime->dry_busy;
    const auto error=runtime->fr->begin(runtime->scene,++runtime->serial,runtime->clear);
    if(error!=Error::none)return;
    runtime->open=hooks::begin_frame();runtime->producer.begin_frame();
    if(!runtime->open){runtime->fr->seal(false,0);return;}
    if(!hooks::menu_oam_begin()){hooks::end_frame();runtime->open=false;runtime->fr->seal(false,0);return;}
    if(runtime->camera_valid)runtime->producer.projection(runtime->camera);
}
extern "C" void sm64ds_fresh_native_projection(const std::int32_t* original){
    if(!runtime)return;
    Screen screen=Screen::top;std::uint16_t order=0;
    if(runtime->playable)screen=main_lcd();
    else hooks::menu_oam_presentation(screen,order);
    const auto error=native_projection(original,screen==Screen::bottom?1.f:.8f,runtime->camera);runtime->camera_valid=error==Error::none;
    if(runtime->open){if(error==Error::none)runtime->producer.projection(runtime->camera);else runtime->fr->invalidate(error);}
}
extern "C" void sm64ds_fresh_native_view(const std::int32_t* view){if(runtime){
    hooks::lighting_view(view);runtime->view_valid=view!=nullptr;
    if(view)std::memcpy(runtime->view.value,view,sizeof(runtime->view.value));
}}
extern "C" void sm64ds_fresh_native_reset_graphics(){
    if(!runtime)return;
    ++runtime->matrix_resets;runtime->camera_valid=false;
    runtime->view=game::unit_affine();runtime->view_valid=true;
    if(runtime->open)runtime->producer.begin_frame();
    const std::int32_t view[12]={4096,0,0,0,4096,0,0,0,4096,0,0,0};hooks::lighting_view(view);
}
extern "C" void sm64ds_fresh_native_clear_colour(std::uint16_t rgb,int alpha,int depth,int id,int fog){
    if(!runtime)return;
    SceneClear next;auto error=game::capture_clear(rgb,alpha,depth,id,fog!=0,next);
    if(error==Error::unsupported&&runtime->playable)error=game::capture_clear(rgb,alpha,32767,id,fog!=0,next); // depth ignored (temporary)
    if(error!=Error::none){runtime->clear_known=false;if(runtime->open)runtime->fr->invalidate(error);return;}
    runtime->clear=next;runtime->clear_known=true;
    if(runtime->open)runtime->fr->clear(next);
}
extern "C" void sm64ds_fresh_native_legacy_words(std::uint32_t count){if(runtime&&runtime->open)runtime->writers+=count;}
extern "C" void sm64ds_fresh_native_legacy_write(std::uint32_t count,std::uint32_t kind,std::uintptr_t caller,std::uint32_t address){
    sm64ds_fresh_native_legacy_words(count);
    if(runtime&&runtime->open&&runtime->provenance)runtime->provenance->record(caller,kind,address,4,count);
}
extern "C" void sm64ds_fresh_native_writer_probe(std::uint32_t kind,std::uintptr_t caller,std::uint32_t address,std::uint32_t width){
    if(runtime&&runtime->open&&runtime->provenance)runtime->provenance->record(caller,kind,address,width,1);
}
int present_playable();
void stop_render_worker();
void finish_interpolated_frame(bool);
extern "C" void sm64ds_ds_census_report(std::uint64_t);
namespace {
void note_game_active(){
    if(!runtime||!runtime->stats.begin_tick)return;
    const auto total=__atomic_load_n(&runtime->game_active_ticks,__ATOMIC_RELAXED)+(svcGetSystemTick()-runtime->stats.begin_tick);
    __atomic_store_n(&runtime->game_active_ticks,total,__ATOMIC_RELAXED);
}
}
extern "C" int sm64ds_fresh_native_present(int unmigrated_2d){
    if(!runtime||!runtime->graphics)return 0;
    // Drain hidden leases even when an unmigrated source component refuses
    // the base scene. Visible leases retire only after a synchronous upload.
    if(!runtime->playable){const auto interface_error=consume_interface();
        if(interface_error!=Error::none){runtime->unsubmitted_refusal=false;return 0;}}
    if(!runtime->open){if(runtime->playable)finish_interpolated_frame(false);return 1;} // endpoint on the intermediate VBlank
    const bool cells_ok=hooks::menu_oam_end();
    const bool models_ok=hooks::end_frame();
    Screen screen;std::uint16_t order;auto backgrounds_ok=Error::none;
    if(hooks::menu_oam_presentation(screen,order)){
        runtime->fr->presentation(screen,order);backgrounds_ok=hooks::menu_background_emit(screen,submit_menu_cell,runtime,runtime->native_menu);
    }
    if(backgrounds_ok!=Error::none)runtime->fr->invalidate(backgrounds_ok);
    const bool valid=cells_ok&&models_ok&&backgrounds_ok==Error::none;runtime->open=false;++runtime->frames;
    if(runtime->frames%120==1){const auto& stats=hooks::statistics();
        std::printf("Fresh native frame state: resets=%llu clearKnown=%u clearRGBA=%08lx polygon=%u fogMark=%u\n",
            static_cast<unsigned long long>(runtime->matrix_resets),unsigned(runtime->clear_known),static_cast<unsigned long>(runtime->clear.rgba),
            unsigned(runtime->clear.polygon_id),unsigned(runtime->clear.fog_mark));
        std::printf("Fresh native capture: producersOk=%u failure=%s occurrences=%u parts=%u lightSets=%u lightStates=%u quads=%u submittedModels=%llu producerFailures=%llu unmigrated2D=%u\n",
            unsigned(valid),describe(runtime->fr->failure()),unsigned(runtime->fr->captured_occurrences()),unsigned(runtime->fr->captured_parts()),
            unsigned(runtime->fr->captured_light_sets()),unsigned(runtime->fr->captured_light_states()),unsigned(runtime->fr->captured_quads()),
            static_cast<unsigned long long>(stats.submitted),static_cast<unsigned long long>(stats.producer_failures),unsigned(unmigrated_2d!=0));
    }
    capture_component(valid,unmigrated_2d);
    if(runtime->playable&&runtime->provenance&&runtime->frames%120==0){
        for(unsigned i=0;i<runtime->provenance->size();++i){const auto& origin=runtime->provenance->entries()[i];
            std::printf("PLAYABLE writer frame=%llu caller=%08lx kind=%lu target=%08lx calls=%llu units=%llu\n",
                (unsigned long long)runtime->frames,(unsigned long)origin.caller,(unsigned long)origin.kind,
                (unsigned long)origin.target,(unsigned long long)origin.calls,(unsigned long long)origin.units);
        }
    }
    if(runtime->playable){const int presented=present_playable();note_game_active();return presented;}
    // Typed source capture is not yet complete native display coverage. Never
    // silently ignore a known fade, blend or still-unprepared background.
    if(hooks::menu_display_unaffected()!=Error::none)runtime->fr->invalidate(Error::unsupported);
    auto error=runtime->fr->seal(valid&&runtime->clear_known,runtime->writers+unsigned(unmigrated_2d!=0));
    runtime->unsubmitted_refusal=error!=Error::none;
    if(error==Error::none){SceneView view;error=runtime->fr->acquire(view);
        if(error==Error::none){error=runtime->renderer.draw(view);runtime->fr->release();}
        else runtime->unsubmitted_refusal=true;
    }
    verdict(error);return error==Error::none;
}
namespace {
int master_brightness(std::uint32_t address){
    const auto value=unsigned(ntr::io_read(address,2));const int factor=int(std::min(value&31u,16u));
    const auto mode=(value>>14)&3;return mode==1?factor:mode==2?-factor:0;
}
// Widescreen bars may continue a plane only when each row's 8 edge pixels are
// one colour (sky/gradient/flat backdrop). Content at the edges stays 4:3.
bool uniform_edges(const ntr::Framebuffer& f){
    for(unsigned y=0;y<unsigned(ntr::SCREEN_H);++y){const auto l=f.px[y][0],r=f.px[y][ntr::SCREEN_W-1];
        for(unsigned x=1;x<8;++x)if(f.px[y][x]!=l||f.px[y][ntr::SCREEN_W-1-x]!=r)return false;}
    return true;
}

// ---- Render worker -----------------------------------------------------------
// The game thread captures a SceneFrame and prepares the DS 2D candidate planes
// (PPU cache, pinned while the worker reads them), then hands both to this
// worker and continues with the next update. The worker composes the 2D bridge,
// uploads, submits, waits for the GPU/VBlank and presents. All C3D work happens
// here once the worker runs.
struct Job {
    unsigned store=0;Screen three=Screen::top;
    unsigned selector_language=6;
    bool geometry=true,a_has_2d=false,reuse_a=false,reuse_b=true,native_2d=false;
    bool native_map=false;
    std::array<int,2> tone{};
    int geometry_tone=0;
    hooks::DialogueWindow dialogue_window{};
    std::optional<ntr::PpuPreparedCandidates> a,b;
    int brightness_three=0,brightness_other=0;
    std::uint64_t update_ticks=0,source_2d_ticks=0,scene=0,frame=0;
    std::uint32_t dispcnt[2]{};
    bool sky_valid=false,sky_dirty=false,capture_head=false,interpolate=false,endpoint_only=false;
    bool sync=false,keep_history=false; // Lock FPS 60 waits for the LCD; pose history only when interpolating
    hooks::Statistics producer_stats{};
    std::uint64_t skin_ticks=0,writers=0,dropped=0;
    std::uint64_t bridge_ticks=0,draw_ticks=0; // this job's worker scopes
    std::uint64_t game_active_start=0;           // runtime->game_active_ticks when the job began
    unsigned occurrences=0,quads=0;
};
struct Worker {
    FrameTimeline timeline;
    SceneView diagnostic_view{}; // borrowed until the game captures again
    std::uint64_t last_load_ticks=0,last_producer_ticks=0,last_skin_ticks=0;
    Thread thread=nullptr;LightEvent go{},done{};
    bool busy=false;
    bool ppu_pinned=false;
    bool shares_game_core=false; // core 0 below the game thread's priority (Old 3DS)
    int core=0;
    std::atomic<bool> quit{false},pending{false};
    std::atomic<bool> gpu_map_failed{false};
    std::uint64_t gpu_map_retry_at=0,gpu_map_scene=~0ull; unsigned gpu_map_retries=0; // game thread only
    Job job;
    PoseHistory* history=nullptr;
    SceneView endpoint_view{};
    SceneRenderer::Bridge2D endpoint_planes{};
    bool endpoint_pending=false,sync_interpolation=true;
    std::uint64_t midpoint_frames=0,endpoint_frames=0;
    std::uint64_t image_ticks=0,second_ticks=0; // last job's active cost, read by the game thread when idle
    std::uint64_t overlapped_frames=0;
    int raised_priority=-1,normal_priority=-1;  // second image's blocked waits (shared core)
    struct Cost {
        std::uint64_t last=0,work=0,max_work=0,max_interval=0,interval_ticks=0;
        std::uint64_t scene=0,source_2d_ticks=0,max_source_2d_ticks=0;
        Screen screen=Screen::top;
        bool geometry=false;
        std::uint64_t wait=0,active=0,max_active=0;
        double previous_gpu_ms=0,max_previous_gpu_ms=0;
        std::array<std::uint64_t,120> intervals{};
        unsigned count=0,interval_count=0,long_intervals=0,over_budget=0;
        unsigned base_count=0,endpoint_count=0;
    } cost;


    bool a_valid=false,b_valid=false,front_any=false,a_extend=false,b_extend=false,front_extend=false;
    std::optional<ntr::PpuPreparedCandidates> last_a,last_b;
};
Worker* worker=nullptr;

void compose_bridge(Job& job,SceneRenderer::Bridge2D& out){
    auto& w=*worker;
    out.geometry=job.geometry;out.back=out.front=nullptr;out.back_dirty=out.front_dirty=false;out.other_dirty=false;
    out.geometry_tone_clip=job.dialogue_window.clip;out.geometry_tone=job.dialogue_window.brightness;
    if(job.a){w.last_a.reset();w.last_a.emplace(*job.a);}
    if(job.b){w.last_b.reset();w.last_b.emplace(*job.b);}
    if(job.a_has_2d){
        if(job.a){const auto& a=*job.a;
            ntr::ppu_compose_candidates(a,bridge->zero,bridge->back,false);
            w.a_valid=true;w.front_any=false;w.a_extend=uniform_edges(bridge->back);out.back_dirty=out.front_dirty=true;
            if(job.geometry){
                const unsigned gp=a.state.geometry_priority;const auto* layer=a.front_layer();const auto* priority=a.front_priority();bool any=false;
                // Exact per-state decision table (bridge_policy). BUILD124:
                // windows and per-OBJ alpha too; the pause and course-clear
                // menus evaluated the whole policy per pixel (~35 ms per top
                // screen on the Old 3DS).
                static bridge_policy::ForegroundTable table;
                table.configure(gp,a.state.blend,a.state.alpha,a.state.brightness);
                const auto* back_layer=a.back_layer();const auto* back_priority=a.back_priority();
                const auto* front=&a.front.px[0][0];const auto* resolved=&bridge->back.px[0][0];auto* out=&bridge->front.px[0][0];
                const auto* window=a.state.has_windows?a.window_mask():nullptr;
                const auto* obj_alpha=a.state.has_obj_alpha?a.front_obj_alpha():nullptr;
                // One loop per input shape: only the arrays a frame has.
                auto compose=[&](auto window_at,auto alpha_at){
                    for(unsigned i=0;i<unsigned(ntr::SCREEN_W*ntr::SCREEN_H);++i){
                        const auto value=table.pixel(front[i],resolved[i],layer[i],priority[i],back_layer[i],back_priority[i],window_at(i),alpha_at(i));
                        out[i]=value;any|=value!=0;
                    }
                };
                const auto no_window=[](unsigned){return 63u;};const auto no_alpha=[](unsigned){return 0u;};
                const auto window_mask=[window](unsigned i){return unsigned(window[i]);};
                const auto own_alpha=[obj_alpha](unsigned i){return unsigned(obj_alpha[i]);};
                if(window&&obj_alpha)compose(window_mask,own_alpha);
                else if(window)compose(window_mask,no_alpha);
                else if(obj_alpha)compose(no_window,own_alpha);
                else compose(no_window,no_alpha);
                if(const auto* cut=a.state.window_cut)for(unsigned r=0;r<cut->count;++r){const auto& rect=cut->rects[r];any=true;
                    for(unsigned y=rect.y0;y<rect.y1;++y)std::fill_n(&bridge->front.px[y][rect.x0],rect.x1-rect.x0,cut->color|0xff000000u);}
                w.front_any=any;w.front_extend=uniform_edges(bridge->front);
                if(!w.front_extend&&!a.state.window_cut){
                    // Text four pixels from the edge must not disable the
                    // full-width BG0 effect. Extend only edge texels proven
                    // to be geometry's effect, never a foreground object.
                    w.front_extend=true;
                    for(unsigned y=0;y<192&&w.front_extend;++y)for(unsigned x:{0u,255u}){
                        const auto i=y*256+x;
                        if(!bridge_policy::geometry_effect_edge(bridge->front.px[y][x],a.state.has_windows?a.window_mask()[i]:63,
                            a.state.blend,a.state.brightness))w.front_extend=false;
                    }
                }
            }
        }
        if(w.a_valid){out.back=&bridge->back.px[0][0];if(job.geometry&&w.front_any)out.front=&bridge->front.px[0][0];}
    }else w.a_valid=false;
    if(job.native_map){w.b_valid=false;w.last_b.reset();out.other_affine=&runtime->store[job.store].map;}
    else if(job.b){ntr::ppu_compose_candidates(*job.b,bridge->zero,bridge->other,false);w.b_valid=true;out.other_dirty=true;w.b_extend=uniform_edges(bridge->other);}
    out.other=w.b_valid?&bridge->other.px[0][0]:nullptr;
    out.extend_back=w.a_extend;out.extend_other=w.b_extend;
    out.extend_front=w.front_extend;
    out.brightness_three=job.brightness_three;out.brightness_other=job.brightness_other;
    // The title sky is a layer behind the logo/text, not a flat edge column.
    // Only engine B occupies the top panel in these menu scenes.
    if(job.three==Screen::bottom){
        out.sky=job.sky_valid?&runtime->store[job.store].sky.px[0][0]:nullptr;out.sky_dirty=job.sky_dirty;
        out.extend_other=false;
    }
}

void record_presentation(std::uint64_t start,Error error){
    auto& w=*worker;auto& c=w.cost;
    const auto end=svcGetSystemTick();
    const auto& gpu=runtime->renderer.backend_metrics();const auto& resources=runtime->renderer.metrics();
    FrameTiming row{w.job.frame,w.job.scene,start,end,w.job.update_ticks,w.job.source_2d_ticks,
        resources.load_ticks-w.last_load_ticks,gpu.wait_ticks,std::uint32_t(gpu.previous_gpu_queue_ms*1000),
        std::uint32_t(gpu.draws),std::uint32_t(gpu.triangles),w.job.quads,std::uint32_t(resources.loads),
        std::uint32_t(resources.evictions),std::uint32_t(error),w.job.endpoint_only};
    row.bridge=w.job.bridge_ticks;row.draw=w.job.draw_ticks;row.upload=gpu.upload_ticks;row.submit=gpu.submit_ticks;
    // Producer/skin clocks are cumulative game-thread scopes (profile only).
    row.producer=w.job.producer_stats.producer_ticks-std::min(w.job.producer_stats.producer_ticks,w.last_producer_ticks);
    row.skin=w.job.skin_ticks-std::min(w.job.skin_ticks,w.last_skin_ticks);
    if(!w.job.endpoint_only){w.last_producer_ticks=w.job.producer_stats.producer_ticks;w.last_skin_ticks=w.job.skin_ticks;}
    row.matrices=std::uint32_t(gpu.matrix_uploads);row.light_rows=std::uint32_t(gpu.lighting_uniform_rows);
    row.upload_bytes=std::uint32_t(gpu.texture_upload_bytes+gpu.mesh_upload_bytes);row.material_binds=std::uint32_t(gpu.material_binds);
    row.pipeline_changes=std::uint32_t(gpu.pipeline_changes);row.shader_binds=std::uint32_t(gpu.shader_binds);
    {const auto overlap=__atomic_load_n(&runtime->game_active_ticks,__ATOMIC_RELAXED)-w.job.game_active_start;
     row.game_overlap=std::uint32_t(std::min<std::uint64_t>(overlap,0xffffffffu));}
    row.stages=runtime->renderer.stage_ticks();
    row.route_2d=(w.job.native_2d?1u:0u)|(w.job.a_has_2d?2u:0u)|(w.job.a?4u:0u)|(w.job.b?8u:0u)|(w.job.native_map?16u:0u)|(w.job.sky_valid?32u:0u);
    w.timeline.push(row);
    w.last_load_ticks=resources.load_ticks;
    if(error!=Error::none){c={};return;}
    // Menu geometry is routed to the lower LCD. Keep the same interval and
    // spike accounting there, without mixing different scenes/display modes.
    if(c.scene!=w.job.scene||c.screen!=w.job.three||c.geometry!=w.job.geometry){
        c={};c.scene=w.job.scene;c.screen=w.job.three;c.geometry=w.job.geometry;
    }
    const auto now=svcGetSystemTick(),work=now-start;
    if(c.last){const auto interval=now-c.last;c.intervals[c.interval_count++]=interval;
        c.interval_ticks+=interval;
        c.max_interval=std::max(c.max_interval,interval);c.long_intervals+=interval>SYSCLOCK_ARM11/40;}
    c.last=now;c.work+=work;c.max_work=std::max(c.max_work,work);c.over_budget+=work>SYSCLOCK_ARM11/60;
    const auto& metrics=runtime->renderer.backend_metrics();
    const auto active=work>metrics.wait_ticks?work-metrics.wait_ticks:0;
    c.wait+=metrics.wait_ticks;c.active+=active;c.max_active=std::max(c.max_active,active);
    c.previous_gpu_ms+=metrics.previous_gpu_queue_ms;
    c.max_previous_gpu_ms=std::max(c.max_previous_gpu_ms,double(metrics.previous_gpu_queue_ms));
    if(w.job.endpoint_only)++c.endpoint_count;else {++c.base_count;c.source_2d_ticks+=w.job.source_2d_ticks;
        c.max_source_2d_ticks=std::max(c.max_source_2d_ticks,w.job.source_2d_ticks);}
    if(++c.count==120){
        const double ms=1000.0/double(SYSCLOCK_ARM11);
        std::printf("Native presentation cost: images=%u fps=%.2f meanWorkMs=%.3f maxWorkMs=%.3f maxIntervalMs=%.3f workOver16ms=%u intervalOver25ms=%u midpoint=%llu endpoint=%llu screen=%s geometry=%u scene=%llu\n",
            c.count,double(c.interval_count)*double(SYSCLOCK_ARM11)/double(c.interval_ticks),c.work*ms/c.count,c.max_work*ms,c.max_interval*ms,c.over_budget,c.long_intervals,
            (unsigned long long)w.midpoint_frames,(unsigned long long)w.endpoint_frames,c.screen==Screen::top?"top":"bottom",unsigned(c.geometry),(unsigned long long)c.scene);
        std::sort(c.intervals.begin(),c.intervals.begin()+c.interval_count);
        std::printf("Native presentation phases: samples=%u base=%u endpoint=%u activeWallMeanMs=%.3f activeWallMaxMs=%.3f syncWaitMeanMs=%.3f previousGpuMeanMs=%.3f previousGpuMaxMs=%.3f intervalP95Ms=%.3f intervalP99Ms=%.3f timings=%u source2DPrepareMeanMs=%.3f source2DPrepareMaxMs=%.3f\n",
            c.count,c.base_count,c.endpoint_count,c.active*ms/c.count,c.max_active*ms,c.wait*ms/c.count,
            c.previous_gpu_ms/c.count,c.max_previous_gpu_ms,c.intervals[(c.interval_count*95+99)/100-1]*ms,
            c.intervals[(c.interval_count*99+99)/100-1]*ms,unsigned(runtime->timings),
            c.base_count?c.source_2d_ticks*ms/c.base_count:0,c.max_source_2d_ticks*ms);
        // Preserve the boundary interval: clearing last would hide one spike
        // out of every 120 images. Active wall time excludes only FrameBegin's
        // measured wait; GPU queue time belongs to the preceding submission.
        const auto scene=c.scene;const auto screen=c.screen;const auto geometry=c.geometry;
        c={};c.last=now;c.scene=scene;c.screen=screen;c.geometry=geometry;
    }
}
void report_playable(Runtime& r,Error error,Screen three);
void run_job(){
    auto& r=*runtime;auto& job=worker->job;auto& s=r.stats;
    const auto start=svcGetSystemTick();
    job.game_active_start=__atomic_load_n(&r.game_active_ticks,__ATOMIC_RELAXED);
    if(job.endpoint_only){
        r.renderer.set_pace(r.pace||job.sync);r.renderer.set_late_pace(false);
        if(job.native_2d)r.renderer.clear_bridge_2d();else r.renderer.bridge_2d(worker->endpoint_planes);
        r.renderer.screen_tone(job.tone[0],job.tone[1]);
        r.renderer.geometry_tone(job.geometry_tone);
        const auto draw_start=svcGetSystemTick();
        // BUILD134, worker on the game's core: FrameBegin would wait here for
        // the GPU to finish the interpolated image (8.75 ms mean on the Old
        // 3DS, BUILD 133 tour) with the core idle; the pair then cannot fit
        // in a period. Build this image meanwhile instead.
        const bool overlap=worker->shares_game_core&&sm64ds::perf::cuts()&&r.renderer.second_available();
        // The fence between the two images of a pair applies in every mode:
        // without it (Unlocked) the exact image replaces the interpolated one
        // before the LCD ever shows it. It is blocked time, not CPU.
        const auto error=overlap?r.renderer.draw_second(worker->endpoint_view,job.three,true,worker->raised_priority,worker->normal_priority):
            r.renderer.draw(worker->endpoint_view,job.three);
        if(overlap&&error==Error::none)++worker->overlapped_frames;
        job.bridge_ticks=0;job.draw_ticks=svcGetSystemTick()-draw_start;
        if(error==Error::none){worker->history->remember(worker->endpoint_view);++worker->endpoint_frames;}
        else {worker->history->reset();++r.refused;}
#ifdef SM64DS_NATIVE_INTERFACE_RUNTIME
        if(error==Error::none)enhance_runtime::note_present(double(svcGetSystemTick()-start)*1000.0/double(SYSCLOCK_ARM11),
            double(r.renderer.backend_metrics().previous_gpu_queue_ms));
#endif
        record_presentation(start,error);
        {const auto work=svcGetSystemTick()-start,wait=r.renderer.backend_metrics().wait_ticks;worker->second_ticks=work>wait?work-wait:0;}
        r.store[job.store].frame.release();worker->endpoint_pending=false;job.endpoint_only=false;
        return;
    }
    // The menu loader publishes its actual source language. Only the GPU
    // owner queues resources; the game never edits a running renderer's LRU.
    if(job.selector_language<6){
        const auto objects=hooks::prepared_menu_objects();
        for(unsigned engine=0;engine<2;++engine){
            const auto* view=hooks::menu_obj_view(objects,3,engine,job.selector_language);
            if(!view)continue;
            std::uint32_t previous=0xffffffffu;
            for(std::size_t b=0;b<view->cells.banks;++b){const auto bank=view->cells.bank[b].image_bank;
                if(bank==previous)continue;
                previous=bank;const auto result=r.renderer.preload_image_bank(bank);
                std::printf("Native selector preload queued: language=%u images=%u status=%s\n",job.selector_language,bank,describe(result));
            }
        }
        const auto backdrops=hooks::prepared_menu_backdrops();
        for(std::size_t i=0;i<backdrops.views;++i){const auto& view=backdrops.view[i];
            if(view.layout!=3||!(view.languages&(1u<<job.selector_language)))continue;
            const auto result=r.renderer.preload_canvas(view.bank);
            std::printf("Native selector preload queued: canvas=%u status=%s\n",view.bank,describe(result));
        }
        job.selector_language=6;
    }
    std::array<SceneRenderer::InterfacePublication,2> images{};
#ifdef SM64DS_NATIVE_INTERFACE_RUNTIME
    for(unsigned c=0;c<2;++c){auto& p=images[c];int width=0,height=0;
        const auto* pixels=enhance_runtime::overlay_image(c,p.revision,width,height);
        if(pixels)p.image={pixels,std::size_t(width)*height,unsigned(width),unsigned(height),unsigned(width)};
    }
#endif
    // Test mode status strip (channel 1) and final panel (channel 0) replace
    // the ordinary overlays; their high-bit revisions never alias them.
    bool test_override[2]{};
    {const std::uint32_t* pixels=nullptr;
     if(const auto revision=sm64ds_test_mode_strip(&pixels)){images[1].image={pixels,256*16,256,16,256};images[1].revision=0x80000000u|revision;test_override[1]=true;}
     pixels=nullptr;
     if(const auto revision=sm64ds_test_mode_panel(&pixels)){images[0].image={pixels,320*240,320,240,320};images[0].revision=0x80000000u|revision;test_override[0]=true;}}
    SceneRenderer::Bridge2D planes;
    job.bridge_ticks=job.draw_ticks=0;
    if(job.native_2d)r.renderer.clear_bridge_2d();
    else{const auto bridge_start=svcGetSystemTick();compose_bridge(job,planes);job.bridge_ticks=svcGetSystemTick()-bridge_start;s.bridge_ticks+=job.bridge_ticks;r.renderer.bridge_2d(planes);}
    r.renderer.screen_tone(job.tone[0],job.tone[1]);
    r.renderer.geometry_tone(job.geometry_tone);
    auto& frame=r.store[job.store].frame;
    SceneView view;auto error=frame.acquire(view);
    if(error==Error::none){
        worker->diagnostic_view=view;
        job.occurrences=unsigned(view.occurrences);job.quads=unsigned(view.quad_vertices/4);job.dropped=frame.dropped();
                SceneView presentation=view;
        bool middle=false;
        if(job.interpolate&&worker->history->midpoint(view,presentation)==Error::none)
            middle=worker->history->blended_parts()!=0;
        // On the game's core the LCD fence is applied to the second image
        // only (submit_overlapped): waiting here would idle the shared core.
        r.renderer.set_pace(r.pace||(middle&&job.sync&&!worker->shares_game_core));
        r.renderer.set_late_pace(middle&&job.sync&&worker->shares_game_core);
        const auto draw_start=svcGetSystemTick();error=r.renderer.draw(presentation,job.three,images);job.draw_ticks=svcGetSystemTick()-draw_start;s.draw_ticks+=job.draw_ticks;
        if(error!=Error::none&&job.native_map){
            worker->gpu_map_failed.store(true,std::memory_order_release);
            std::printf("Native map publication failed: status=%s; CPU fallback next frame\n",describe(error));
        }
        if(middle&&error==Error::none){
            worker->endpoint_view=view;worker->endpoint_pending=true;++worker->midpoint_frames;
            worker->endpoint_planes=planes;auto& held=worker->endpoint_planes;
            held.back_dirty=held.front_dirty=held.other_dirty=held.sky_dirty=false;
        }
        const auto& b=r.renderer.backend_metrics();s.wait_ticks+=b.wait_ticks;s.upload_ticks+=b.upload_ticks;s.submit_ticks+=b.submit_ticks;s.sdk_draw_ticks+=b.sdk_draw_ticks;s.sdk_draw_samples+=b.sdk_draw_samples;
        s.draws+=b.draws;s.matrices+=b.matrix_uploads;s.light_rows+=b.lighting_uniform_rows;s.materials+=b.material_binds;s.shader_binds+=b.shader_binds;s.pipeline+=b.pipeline_changes;s.mesh_binds+=b.mesh_binds;
        if(error==Error::none&&job.capture_head){
            error=r.renderer.capture_geometry(job.three,&head_canvas->geometry.px[0][0],256*192);
            if(error==Error::none){
                if(worker->last_a)ntr::ppu_compose_candidates(*worker->last_a,head_canvas->geometry,head_canvas->composite,false);
                else head_canvas->composite=head_canvas->geometry;
                for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
                    const auto colour=head_canvas->composite.px[y][x];
                    head_canvas->pixels[y*256+x]=std::uint16_t(0x8000|((colour>>19)&31)|((colour>>6)&992)|((colour<<7)&31744));
                }
                head_canvas->ready=true;
                if(std::getenv("SM64DS_FRESH_NATIVE_PROFILE")){
                    if(FILE* file=std::fopen("sdmc:/3ds/sm64ds/native-head-capture.argb","wb")){
                        std::fwrite(&head_canvas->composite,1,sizeof(ntr::Framebuffer),file);std::fclose(file);
                    }
                }
            }
            std::printf("Native head capture: status=%s screen=%u\n",describe(error),unsigned(job.three));
        }
        if(!worker->endpoint_pending){
            // BUILD134: the pose history is only read by interpolation; at
            // Lock FPS 30 (the Old 3DS default) copying every part and skin
            // vertex of every image was unused work.
            if(error==Error::none&&job.keep_history)worker->history->remember(view);else worker->history->reset();
            frame.release();
        }
        if(job.frame%120==0)std::printf("Native interpolation: midpoint=%llu endpoint=%llu retained=%u blendedParts=%u\n",
            (unsigned long long)worker->midpoint_frames,(unsigned long long)worker->endpoint_frames,
            unsigned(worker->endpoint_pending),unsigned(worker->history->blended_parts()));
    }
#ifdef SM64DS_NATIVE_INTERFACE_RUNTIME
    if(error==Error::none)for(unsigned c=0;c<2;++c)if(images[c].image.pixels&&!test_override[c])enhance_runtime::acknowledge(c,images[c].revision);
    native_lower::gpu_ready.store(error==Error::none&&images[0].image.pixels,std::memory_order_release);
#endif
    record_presentation(start,error);
    {const auto work=svcGetSystemTick()-start,wait=r.renderer.backend_metrics().wait_ticks;worker->image_ticks=work>wait?work-wait:0;}
    s.update_ticks+=job.update_ticks;
    r.unsubmitted_refusal=error!=Error::none;
    const auto end=svcGetSystemTick();++s.frames;s.present_ticks+=end-start;
    if(s.last_tick){const auto length=end-s.last_tick;++s.intervals;s.frame_ticks+=length;if(length>s.max_frame_ticks)s.max_frame_ticks=length;
        const auto vblank=SYSCLOCK_ARM11/60;const auto n=(length+vblank/2)/vblank;++s.vblanks[n<=1?0:n==2?1:n==3?2:3];}
    s.last_tick=end;
    if(error!=Error::none)++r.refused;
#ifdef SM64DS_NATIVE_INTERFACE_RUNTIME
    else enhance_runtime::note_present(double(end-start)*1000.0/double(SYSCLOCK_ARM11),double(r.renderer.backend_metrics().previous_gpu_queue_ms));
#endif
    {   // Diagnostics: SM64DS_PLAYABLE_SHOTS="f1,f2,..." completed-GPU captures of both LCDs.
        static unsigned shots[32];static unsigned shot_count=0;static bool parsed=false;
        if(!parsed){parsed=true;if(const char* v=std::getenv("SM64DS_PLAYABLE_SHOTS"))
            while(*v&&shot_count<32){shots[shot_count++]=unsigned(std::strtoul(v,const_cast<char**>(&v),10));if(*v==',')++v;else break;}}
        for(unsigned i=0;i<shot_count&&error==Error::none;++i)if(shots[i]==s.frames){
            // Exact borrowed candidates of this sealed frame, while pinned.
            // This is CPU layer evidence, kept distinct from completed LCDs.
            auto dump=[&](const char* name,const void* data,std::size_t bytes){
                if(!data)return;
                char path[128];std::snprintf(path,sizeof path,"sdmc:/3ds/sm64ds/shot-%05u-%s.bin",unsigned(s.frames),name);
                if(FILE* f=std::fopen(path,"wb")){std::fwrite(data,1,bytes,f);std::fclose(f);}
            };
            dump("bridge-back",planes.back,sizeof(ntr::Framebuffer));
            dump("bridge-front",planes.front,sizeof(ntr::Framebuffer));
            dump("bridge-other",planes.other,sizeof(ntr::Framebuffer));
            dump("bridge-sky",planes.sky,sizeof(ntr::Framebuffer));
            for(unsigned side=0;side<2;++side){const auto& c=side?worker->last_b:worker->last_a;
                std::printf("PLAYABLE snapshot frame=%llu engine=%u disp=%08lx candidatesChanged=%u geometry=%u master=%d\n",
                    (unsigned long long)s.frames,side,(unsigned long)job.dispcnt[side],unsigned(bool(c)),unsigned(job.geometry),side?job.brightness_other:job.brightness_three);
                if(c){const auto& state=c->state;
                    std::printf("PLAYABLE layers frame=%llu engine=%u blend=%04x alpha=%04x brightness=%u master=%04x windows=%u cut=%u gp=%u resolved=%u\n",
                        (unsigned long long)s.frames,side,state.blend,state.alpha,state.brightness,state.master,unsigned(state.has_windows),state.window_cut?state.window_cut->count:0,unsigned(state.geometry_priority),unsigned(state.resolved));
                    dump(side?"b-colors":"a-colors",&c->front,sizeof(ntr::Framebuffer));
                    dump(side?"b-layers":"a-layers",c->front_layer(),256*192);
                    if(state.has_windows)dump(side?"b-windows":"a-windows",c->window_mask(),256*192);
                }
            }
            r.renderer.finish();
            for(unsigned side=0;side<2;++side){
                const Screen screen=side?Screen::bottom:Screen::top;const auto w=screen_width(screen);const std::size_t bytes=std::size_t(w)*240*3;
                void* rgb=linearMemAlign(bytes,128);
                if(rgb&&r.renderer.capture_rgb8(screen,rgb,bytes)==Error::none){char path[96];
                    std::uint32_t checksum=2166136261u;const auto* pixels=static_cast<const std::uint8_t*>(rgb);
                    for(std::size_t n=0;n<bytes;++n)checksum=(checksum^pixels[n])*16777619u;
                    std::printf("PLAYABLE readback: frame=%llu screen=%u fnv1a=%08lx bytes=%u\n",
                        (unsigned long long)s.frames,side,(unsigned long)checksum,unsigned(bytes));
                    std::snprintf(path,sizeof path,"sdmc:/3ds/sm64ds/shot-%05u-%s.rgb",unsigned(s.frames),side?"bottom":"top");
                    if(FILE* f=std::fopen(path,"wb")){std::fwrite(rgb,1,bytes,f);std::fclose(f);}}
                if(rgb)linearFree(rgb);}
        }
    }
    if(s.frames%120==0)report_playable(r,error,job.three);
    // BUILD134: on the game's core the exact second image cannot wait for the
    // game thread's intermediate display event (the worker would still be
    // drawing and the image was dropped): it follows the interpolated one.
    if(worker->endpoint_pending&&worker->shares_game_core){job.endpoint_only=true;run_job();}
}
void worker_main(void*){
    auto& w=*worker;
    native_worker_thread=true;
    sm64ds_fatal_handlers_install();
    // Azahar 2126.2 does not implement this SVC; only hardware can confirm it.
    std::printf("Fresh native render worker running: reportedCore=%ld\n",(long)svcGetProcessorID());
    for(;;){
        LightEvent_Wait(&w.go);
        if(w.quit)break;
        if(__atomic_exchange_n(&dev_worker_fault,0,__ATOMIC_ACQ_REL)){std::printf("SM64DS_DEV_FAULT: render worker throws\n");throw std::bad_alloc();}
        if(w.pending){__sync_synchronize();w.pending=false;run_job();}
        __sync_synchronize();LightEvent_Signal(&w.done);
    }
}
void wait_worker(){
    if(worker&&worker->busy){LightEvent_Wait(&worker->done);worker->busy=false;if(worker->ppu_pinned){ntr::ppu_pin_candidate_ways(false);worker->ppu_pinned=false;}}
}
bool poll_render_worker(){
    if(!worker||!worker->busy)return true;
    if(!LightEvent_TryWait(&worker->done))return false;
    __sync_synchronize();worker->busy=false;
    if(worker->ppu_pinned){ntr::ppu_pin_candidate_ways(false);worker->ppu_pinned=false;}
    return true;
}
bool render_worker_blocks_update(){return worker&&worker->shares_game_core&&!poll_render_worker();}
bool start_render_worker(){
    if(worker)return true;
    void* storage=std::malloc(sizeof(Worker));if(!storage)return false;
    worker=new(storage)Worker{};
    void* history_storage=std::malloc(sizeof(PoseHistory));
    if(!history_storage){worker->~Worker();std::free(worker);worker=nullptr;return false;}
    worker->history=new(history_storage)PoseHistory{};
    if(const char* option=std::getenv("SM64DS_NATIVE_SYNC_INTERPOLATION"))worker->sync_interpolation=*option!='0';
    LightEvent_Init(&worker->go,RESET_ONESHOT);LightEvent_Init(&worker->done,RESET_ONESHOT);
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    bool is_new=false;APT_CheckNew3DS(&is_new);
    // New 3DS: core 2. Old 3DS: core 1's single application thread when APT
    // granted 80% of it (BUILD124, audio then pumps on core 0); otherwise
    // core 0 below the game thread, audio keeping core 1.
    int core=is_new?2:render_core1_granted.load(std::memory_order_relaxed)?1:0;
    if(const char* c=std::getenv("SM64DS_FRESH_NATIVE_WORKER_CORE"))core=std::atoi(c);
    const auto render_priority=core!=0?priority-1:priority+1;
    worker->thread=threadCreate(worker_main,nullptr,64*1024,render_priority,core,false);
    int used_priority=render_priority;
    if(!worker->thread&&core!=0){core=0;used_priority=priority+1;worker->thread=threadCreate(worker_main,nullptr,64*1024,used_priority,core,false);}
    // The game thread runs on core 0 (exheader ideal processor) at this priority.
    worker->shares_game_core=core==0&&used_priority>priority;worker->core=core;
    if(worker->shares_game_core){worker->raised_priority=priority-1;worker->normal_priority=used_priority;}
    std::printf("Fresh native render worker: core=%d created=%u sharesGameCore=%u audioCore=%d\n",core,unsigned(worker->thread!=nullptr),
        unsigned(worker->shares_game_core),audio_core_used.load(std::memory_order_relaxed));
    if(!worker->thread){
        log_resource_use("render worker failed");
        worker->history->~PoseHistory();std::free(worker->history);worker->~Worker();std::free(worker);worker=nullptr;
        sm64ds_test_mode_boot_failed("no se pudo crear el hilo de render");
        return false;
    }
    log_resource_use("render worker");
    return true;
}
}
void stop_render_worker(){
    if(!worker)return;
    finish_interpolated_frame(true);wait_worker();worker->quit=true;__sync_synchronize();LightEvent_Signal(&worker->go);
    threadJoin(worker->thread,U64_MAX);threadFree(worker->thread);worker->thread=nullptr;
}

void finish_interpolated_frame(bool wait){
    if(!worker)return;
    if(wait)wait_worker();else if(!poll_render_worker())return;
    if(worker->endpoint_pending){
        worker->job.endpoint_only=true;worker->busy=true;worker->pending=true;
        __sync_synchronize();LightEvent_Signal(&worker->go);
        if(wait)wait_worker();
    }
}

namespace {
void lifecycle_hook(APT_HookType type,void*){
    auto& r=*runtime;
    const unsigned begin=type==APTHOOK_ONSUSPEND?1u:type==APTHOOK_ONSLEEP?2u:0u;
    const unsigned end=type==APTHOOK_ONRESTORE?1u:type==APTHOOK_ONWAKEUP?2u:0u;
    if(begin){
        if(!r.lifecycle_pauses){
            r.lifecycle_pause_tick=svcGetSystemTick();
            finish_interpolated_frame(true);r.renderer.finish();
        }
        r.lifecycle_pauses|=begin;
    }else if(end){
        r.lifecycle_pauses&=~end;
        if(!r.lifecycle_pauses&&r.lifecycle_pause_tick){
            sm64ds_native_cadence_pause_ended(r.lifecycle_pause_tick);
            r.lifecycle_pause_tick=0;r.stats.last_tick=0;r.stats.begin_tick=0;
        }
    }
}
}

// BUILD123 diagnostics (fine-profile and test runs): which engine A input
// changed since the previous full top-screen 2D preparation. On hardware the
// HUD power meter re-prepared it on every image (25 ms on the game thread,
// then 11 ms of composition and a 786 KB upload) and timings alone do not
// show which input keeps changing.
void note_engine_a_prepare(std::uint64_t frame,std::uint64_t scene,bool forced){
    struct Region {std::uint32_t address,bytes;const char* name;};
    static constexpr Region regions[]={{0x07000000u,1024u,"oam"},{0x05000200u,512u,"objPal"},{0x05000000u,512u,"bgPal"},
                                       {0x04000000u,0x70u,"regs"},{0x06400000u,32768u,"objVram32k"}};
    static std::uint8_t saved[1024+512+512+0x70+32768];
    static bool have=false;static unsigned printed=0;static std::uint64_t misses=0,last_scene=~0ull;
    if(scene!=last_scene){last_scene=scene;printed=0;have=false;}
    ++misses;char changed[200]="";std::size_t at=0,offset=0;
    for(const auto& g:regions){
        const auto* now=static_cast<const std::uint8_t*>(ntr::host_ptr(g.address));
        if(have){std::size_t first=g.bytes;unsigned bytes=0;
            for(std::size_t i=0;i<g.bytes;++i)if(now[i]!=saved[offset+i]){if(first==g.bytes)first=i;++bytes;}
            if(bytes&&at+48<sizeof changed)at+=std::snprintf(changed+at,sizeof changed-at,"%s%s@%x/%u",at?",":"",g.name,unsigned(first),bytes);}
        std::memcpy(saved+offset,now,g.bytes);offset+=g.bytes;
    }
    const bool first=!have;have=true;
    if(printed<30||misses%240==0){++printed;
        std::printf("PPU A prepare: frame=%llu miss=%llu forced=%u changed=%s\n",(unsigned long long)frame,(unsigned long long)misses,
            unsigned(forced),first?"(first in scene)":at?changed:"none of oam/pal/regs/vram32k");}
}
int present_playable(){
    auto& r=*runtime;
    static bool worker_ready=start_render_worker();
    if(!worker_ready)return 0;
    const auto start=svcGetSystemTick();
    auto& frame=*r.fr;
    const bool capture_head=head_canvas&&head_canvas->requested;
    // Normal presentation has a single in-flight job, never a waiting queue.
    // Keep the latest simulation state moving while the previous image draws.
    // In particular, do not prepare CPU 2D for an image we cannot submit.
    const bool exact_capture=capture_head||native_diagnostics::requested||native_diagnostics::partial_requested;
    const auto display_event=ntr::rt_frame();
    const auto mode=present_mode();
    if(!exact_capture&&r.presented_once&&display_event-r.presented_event<mode.divisor){
        if(frame.seal(true,0)==Error::none)frame.release();
        return 1; // intentional display cap; no game update or sound command is skipped
    }
    if(r.dry!=Runtime::Dry::none){
        // A dry capture holds no models: never publish it. Only a cap-dry
        // update whose display event missed its prediction can arrive here.
        if(frame.seal(true,0)==Error::none)frame.release();
        ++r.unpresented;r.recovery_cut=true;return 1;
    }
    if(exact_capture)wait_worker();
    else if(!poll_render_worker()){
        if(frame.seal(true,0)==Error::none)frame.release();
        ++r.recovery_skips;r.recovery_cut=true;++r.unpresented;++r.busy_drops;
        if(worker->shares_game_core&&worker->job.interpolate)interpolation_missed();
        return 1;
    }
    if(worker->shares_game_core){auto& b=interpolation_budget;
        // The finished job's measured cost (the worker is idle here).
        if(worker->image_ticks)b.image_ticks=b.image_ticks?(b.image_ticks*3+worker->image_ticks)/4:worker->image_ticks;
        if(worker->second_ticks)b.second_ticks=b.second_ticks?(b.second_ticks*3+worker->second_ticks)/4:worker->second_ticks;
        worker->image_ticks=worker->second_ticks=0;
        if(worker->job.interpolate&&++b.streak>=60){b.streak=0;b.backoff=30;}
    }
    if(worker->endpoint_pending){
        // A newer simulation capture supersedes an unsubmitted endpoint.
        // The midpoint job has completed, so its borrowed store is free now.
        r.store[worker->job.store].frame.release();worker->endpoint_pending=false;
        worker->history->reset();r.recovery_cut=true;
    }
    // DS 2D candidates (game memory) are read here, on the game thread.
    const auto disp=std::uint32_t(ntr::io_read(0x04000000,4));
    const Screen three=main_lcd();
    const auto dialogue=hooks::capture_dialogue(frame,three);
    if(dialogue!=Error::none)frame.invalidate(dialogue);
    if(r.native_menu){const auto& effects=hooks::menu_display_snapshot().effect;
        for(std::size_t i=0;i<frame.captured_images();++i){auto& image=r.store[r.cap].images[i];
            const auto& effect=effects[image.screen==three?0:1];
            if(effect.known&&effect.mode==hooks::DisplayEffect::brightness&&effect.first!=63&&(effect.first&image.tone_target))
                image.material.brightness=effect.amount;
        }
    }
    if(fog_experiment&&r.camera_valid)frame.fog(read_fog(),r.camera);
    const auto seal=frame.seal(true,0);
    const bool shown=((disp>>16)&3)==1&&!(disp&0x80);
    const bool geometry=shown&&(disp&8)&&(disp&0x100);
    const bool a_has_2d=!shown||((disp>>8)&0x1e)||!(disp&8);
    // Cache validity belongs to the producer. Never inspect the consumer's
    // flags while it composes the previous job. Pinned ways protect its data;
    // prepare into the other way while its GPU work continues.
    static bool a_cached=false,b_cached=false,last_geometry=false;
    std::optional<ntr::PpuPreparedCandidates> a,b;
    const bool needs_a=capture_head||a_has_2d||(disp&0xe000)||unsigned(ntr::io_read(0x04000050,2));
    if(!r.native_menu&&needs_a&&(!a_cached||last_geometry!=geometry||!ntr::ppu_prepared_inputs_unchanged(ntr::ENGINE_A))){
        if(r.timings)note_engine_a_prepare(r.frames,r.scene,!a_cached||last_geometry!=geometry);
        a.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_A));
    }
    // Authored PICA laboratory: 60 transforms, exact sharp-image LCD parity.
    static const bool gpu_map=[](){const auto* e=std::getenv("SM64DS_NATIVE_GPU_MAP");return !e||*e!='0';}();
    // BUILD123: one failed GPU map publication disabled the GPU map for the
    // rest of the boot (hardware level 24: CPU map, 25 ms + 786 KB per moving
    // image). Retry ~10 s later, three times per scene; a new scene resets.
    if(worker->gpu_map_scene!=r.scene){worker->gpu_map_scene=r.scene;worker->gpu_map_retries=0;worker->gpu_map_retry_at=0;
        worker->gpu_map_failed.store(false,std::memory_order_release);}
    else if(worker->gpu_map_failed.load(std::memory_order_acquire)){
        if(!worker->gpu_map_retry_at)worker->gpu_map_retry_at=r.frames+300;
        else if(r.frames>=worker->gpu_map_retry_at&&worker->gpu_map_retries<3){
            ++worker->gpu_map_retries;worker->gpu_map_retry_at=0;worker->gpu_map_failed.store(false,std::memory_order_release);
            std::printf("Native map: GPU path retry %u\n",worker->gpu_map_retries);
        }
    }
    bool native_map=false;
    if(gpu_map&&!worker->gpu_map_failed.load(std::memory_order_acquire)&&!r.native_menu&&three==Screen::top){
        auto& store=r.store[r.cap];ntr::PpuGpuAffineLayer map{};
        native_map=ntr::ppu_prepare_native_affine(ntr::ENGINE_B,store.map_foreground,map);
        if(native_map){store.map={map.raster,&store.map_foreground.px[0][0],map.raster_generation,
            unsigned(map.width),unsigned(map.height),map.ref_x,map.ref_y,map.pa,map.pb,map.pc,map.pd,map.backdrop,map.wrap};
            // BUILD129: OBJ bands + overlay base, so the upload compares only those.
            const std::uint32_t* base=nullptr;std::uint64_t base_generation=0;
            store.map.foreground_bands=ntr::ppu_native_affine_bands(store.map_foreground,&base,&base_generation);
            store.map.foreground_base=base;store.map.foreground_base_generation=base_generation;}
    }
    // Changed maps, HUD and window animation update at the game cadence.
    if(!native_map&&!r.native_menu&&(!b_cached||!ntr::ppu_prepared_inputs_unchanged(ntr::ENGINE_B)))b.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B));
    a_cached=!r.native_menu&&needs_a;b_cached=!r.native_menu&&!native_map;last_geometry=geometry;
    if(r.frames%120==0)std::printf("Native 2D map: frame=%llu gpu=%u fallback=%u\n",(unsigned long long)r.frames,unsigned(native_map),unsigned(!r.native_menu&&!native_map));
    const int brightness_three=master_brightness(0x0400006c),brightness_other=master_brightness(0x0400106c);
    // Obtain the background before animated OBJ insertion. The final two
    // candidates can both be foreground, hiding the sky for a single pixel
    // and making the entire widescreen strip disappear. Copy only the owned
    // strips here; no BG-cache pointer is shared with the render worker.
    bool sky_valid=false;
    if(!r.native_menu&&three==Screen::bottom){
        const auto bg=ntr::ppu_prepare_background_candidates_cached(ntr::ENGINE_B);
        if(bg&&!bg->gpu_affine&&!bg->state.blank&&!bg->state.has_windows&&!bg->state.window_cut)
            sky_valid=bridge_policy::sky_strips(&bg->front.px[0][0],&bg->back.px[0][0],bg->front_layer(),bg->back_layer(),
                &r.store[r.cap].sky.px[0][0],bg->state.blend,bg->state.brightness,true);
        if(r.timings){static int previous=-1;if(previous!=int(sky_valid)){
            std::printf("Native title background: frame=%llu wide=%u backgroundView=%u affine=%u\n",(unsigned long long)r.frames,unsigned(sky_valid),unsigned(bool(bg)),unsigned(bg&&bg->gpu_affine));
            previous=int(sky_valid);
        }}
    }
    const auto prepared=svcGetSystemTick();
    native_diagnostics::trace.record(ntr::rt_frame(),unsigned(r.serial),unsigned(r.scene));
    // BUILD134: the PPU snapshots advanced above but this image is not sent:
    // prepare both engines again next time (a stale top 2D would stay otherwise).
    if(seal!=Error::none){a_cached=b_cached=false;frame.release();r.unsubmitted_refusal=true;return 1;}
    auto& job=worker->job;
    job.native_2d=r.native_menu;job.tone={};job.geometry_tone=0;
    job.native_map=native_map;
    job.dialogue_window=hooks::dialogue_window();
    if(r.native_menu){const auto& effects=hooks::menu_display_snapshot().effect;
        for(unsigned e=0;e<2;++e)if(effects[e].known&&effects[e].mode==hooks::DisplayEffect::brightness&&effects[e].first==63)
            job.tone[(e==0?three:(three==Screen::top?Screen::bottom:Screen::top))==Screen::top?0:1]=effects[e].amount;
        if(effects[0].known&&effects[0].mode==hooks::DisplayEffect::brightness&&effects[0].first!=63&&(effects[0].first&1))job.geometry_tone=effects[0].amount;
        if(r.frames%120==0){const auto& cells=hooks::menu_oam_statistics();
            std::printf("Native menu coverage: frame=%llu images=%u objCalls=%llu objCaptured=%llu objFailures=%llu effectMain=%u/%u/%d effectSub=%u/%u/%d complete=0\n",
                (unsigned long long)r.frames,unsigned(frame.captured_images()),(unsigned long long)cells.calls,
                (unsigned long long)cells.captured,(unsigned long long)cells.failures,
                unsigned(effects[0].mode),unsigned(effects[0].first),int(effects[0].amount),
                unsigned(effects[1].mode),unsigned(effects[1].first),int(effects[1].amount));}
    }
    static std::uint64_t previous_tick=0;
    const auto tick=ntr::rt_frame();const auto cadence=previous_tick?tick-previous_tick:0;previous_tick=tick;
    static const bool interpolate=[](){const char* e=std::getenv("SM64DS_FRESH_NATIVE_INTERPOLATE");return !e||*e!='0';}();
    job.sync=mode.sync&&worker->sync_interpolation;
    job.keep_history=mode.interpolate&&interpolate;
    job.interpolate=job.keep_history&&cadence==2&&geometry&&!capture_head&&!r.recovery_cut;
    r.recovery_cut=false;
    job.endpoint_only=false;
    job.capture_head=capture_head;if(capture_head)head_canvas->requested=false;
    job.store=r.cap;job.three=three;job.geometry=geometry;job.a_has_2d=needs_a;
    job.dispcnt[0]=disp;job.dispcnt[1]=std::uint32_t(ntr::io_read(0x04001000,4));
    job.a.reset();job.b.reset();if(a)job.a.emplace(*a);if(b)job.b.emplace(*b);
    job.brightness_three=brightness_three;job.brightness_other=brightness_other;
    job.sky_dirty=sky_valid&&(!job.sky_valid||std::memcmp(&r.store[r.cap].sky,&r.store[r.cap^1u].sky,sizeof(ntr::Framebuffer))!=0);
    job.sky_valid=sky_valid;
    job.producer_stats=hooks::statistics();job.skin_ticks=hooks::skin_ticks;job.writers=r.writers;
    job.update_ticks=(r.stats.begin_tick?start-r.stats.begin_tick:0)+(prepared-start);
    // After update_ticks is known: on a shared core the pair must fit.
    if(job.interpolate&&worker->shares_game_core&&!interpolation_fits(job.update_ticks))job.interpolate=false;
    job.source_2d_ticks=prepared-start;job.scene=r.scene;job.frame=r.frames;
    if(r.frames%120==0){sm64ds_ds_census_report(r.frames);const auto& text=hooks::dialogue_statistics();
        std::printf("Native dialogue: starts=%llu characters=%llu lines=%llu scrollSteps=%llu frames=%llu failures=%llu gpuWindow=%u\n",
            (unsigned long long)text.starts,(unsigned long long)text.characters,(unsigned long long)text.lines,
            (unsigned long long)text.scroll_steps,(unsigned long long)text.frames,(unsigned long long)text.failures,unsigned(job.dialogue_window.owned));
    }
    job.selector_language=r.selector_language;r.selector_language=6;
    r.presented_event=display_event;r.presented_once=true;r.unpresented=0;
    worker->busy=true;worker->ppu_pinned=!r.native_menu;if(worker->ppu_pinned)ntr::ppu_pin_candidate_ways(true);worker->pending=true;__sync_synchronize();LightEvent_Signal(&worker->go);
    if(capture_head)wait_worker(); // transition must finish before the game remaps its canvas
    // Next capture goes into the other store (the worker reads this one).
    r.cap^=1u;r.fr=&r.store[r.cap].frame;r.producer.set_frame(*r.fr);
    return 1;
}
namespace {
void report_playable(Runtime& r,Error error,Screen three){
    auto& s=r.stats;const double ms=1000.0/double(SYSCLOCK_ARM11);const auto& job=worker->job;const auto& hooks_stats=job.producer_stats;
    std::printf("Fresh native PLAYABLE: frames=%llu avgFrameMs=%.2f maxFrameMs=%.2f presentMs=%.2f bridge2DMs=%.2f refused=%llu dropped=%llu skipped=%llu models=%llu submitted=%llu missing=%llu lastMissingFile=%lu legacyWords=%llu three=%s occurrences=%u quads=%u status=%s\n",
        static_cast<unsigned long long>(s.frames),s.intervals?s.frame_ticks*ms/s.intervals:0,s.max_frame_ticks*ms,s.present_ticks*ms/120,s.bridge_ticks*ms/120,
        static_cast<unsigned long long>(r.refused),static_cast<unsigned long long>(job.dropped),
        static_cast<unsigned long long>(r.renderer.skipped_occurrences()),static_cast<unsigned long long>(hooks_stats.model_calls),
        static_cast<unsigned long long>(hooks_stats.submitted),static_cast<unsigned long long>(hooks_stats.missing_models),
        static_cast<unsigned long>(hooks_stats.last_missing_file),static_cast<unsigned long long>(job.writers),
        three==Screen::top?"top":"bottom",job.occurrences,job.quads,describe(error));
    std::printf("PLAYABLE timing ms/frame: update=%.2f models=%.2f skin=%.2f bridge2D=%.2f draw=%.2f gpuWait=%.2f upload=%.2f submit=%.2f (update on game thread; the rest on the render worker; timings need SM64DS_FRESH_NATIVE_PROFILE=1)\n",
        s.update_ticks*ms/120,(hooks_stats.producer_ticks-s.model_mark)*ms/120,(job.skin_ticks-s.skin_mark)*ms/120,s.bridge_ticks*ms/120,
        s.draw_ticks*ms/120,s.wait_ticks*ms/120,s.upload_ticks*ms/120,s.submit_ticks*ms/120);
    std::printf("PLAYABLE gpu work per frame: draws=%llu matrixUploads=%llu lightRows=%llu materialBinds=%llu shaderBinds=%llu pipelineChanges=%llu meshBinds=%llu\n",
        (unsigned long long)(s.draws/120),(unsigned long long)(s.matrices/120),(unsigned long long)(s.light_rows/120),(unsigned long long)(s.materials/120),
        (unsigned long long)(s.shader_binds/120),(unsigned long long)(s.pipeline/120),(unsigned long long)(s.mesh_binds/120));
    std::printf("PLAYABLE GPU emission samples: count=%llu meanUs=%.3f baseDraws=%llu (rotating 1/8 draw sample; SDK context plus selected emitter; CPU wall, includes clock overhead; not GPU)\n",
        (unsigned long long)s.sdk_draw_samples,s.sdk_draw_samples?s.sdk_draw_ticks*ms*1000/s.sdk_draw_samples:0,(unsigned long long)s.draws);
    std::printf("PLAYABLE frame histogram: 1vbl=%llu 2vbl=%llu 3vbl=%llu 4+vbl=%llu\n",(unsigned long long)s.vblanks[0],(unsigned long long)s.vblanks[1],(unsigned long long)s.vblanks[2],(unsigned long long)s.vblanks[3]);
    s.sdk_draw_ticks=s.sdk_draw_samples=0;
    s.draws=s.matrices=s.light_rows=s.materials=s.shader_binds=s.pipeline=s.mesh_binds=0;for(auto& v:s.vblanks)v=0;
    s.model_mark=hooks_stats.producer_ticks;s.skin_mark=job.skin_ticks;
    s.update_ticks=s.draw_ticks=s.wait_ticks=s.upload_ticks=s.submit_ticks=0;
    s.intervals=s.frame_ticks=s.present_ticks=s.bridge_ticks=s.max_frame_ticks=0;
}
}
extern "C" int sm64ds_fresh_native_diagnostic_hold(){return runtime&&runtime->diagnostic_continue&&runtime->unsubmitted_refusal;}
extern "C" void sm64ds_fresh_native_shutdown(){
    if(!runtime)return;
    sm64ds_fresh_native_mark_clean_exit();
    // rt_run returns without a single update when its game thread cannot be
    // created (hardware boot 26 of the BUILD 120 tour).
    if(!runtime->frames){log_resource_use("no game updates");sm64ds_test_mode_boot_failed("no se pudo crear el hilo del juego");}
    if(runtime->graphics)aptUnhook(&runtime->lifecycle_cookie);
    if(runtime->open){hooks::end_frame();hooks::menu_oam_end();runtime->fr->seal(false,0);runtime->open=false;}
    hooks::activate(false);
    stop_render_worker();
    if(runtime->graphics){const auto error=runtime->renderer.shutdown();
        std::printf("Fresh native shutdown: %s ownedGpuBytes=%u outputVRAM=%u\n",describe(error),unsigned(runtime->renderer.backend_metrics().owned_gpu_bytes()),unsigned(runtime->renderer.output_vram_bytes()));}
    std::printf("Fresh native total: frames=%llu refused=%llu submitted=%llu\n",static_cast<unsigned long long>(runtime->frames),
        static_cast<unsigned long long>(runtime->refused),static_cast<unsigned long long>(runtime->renderer.metrics().frames));
    std::printf("Fresh native recovery: skippedPresentations=%llu\n",(unsigned long long)runtime->recovery_skips);
    std::fflush(stdout);
    // Hooks are process-lifetime installation. Do not leave their callback
    // context dangling through later original actor teardown or atexit code.
    // Runtime storage retires with the process after graphics is shut down.
    runtime->graphics=false;
    if(runtime->provenance){runtime->provenance->~WriterProvenance();std::free(runtime->provenance);runtime->provenance=nullptr;}
}
#endif
#ifdef SM64DS_FRESH_NATIVE_GAME
// PLAYABLE MODE test aid (opt-in, local diagnostics only): scripted input so
// emulator sessions can reach levels deterministically. Format:
// SM64DS_PLAYABLE_SCRIPT="t:FRAME:X:Y:LEN,k:FRAME:HEXMASK:LEN,..." where
// FRAME counts game updates and X/Y are 3DS lower-screen pixels.
#include <3ds.h>
namespace {
struct ScriptEvent {unsigned frame,length,x,y,keys;bool touch;};
ScriptEvent script[32];unsigned script_count=0;bool script_loaded=false;unsigned script_frame=0;
void load_script(){
    script_loaded=true;const char* text=std::getenv("SM64DS_PLAYABLE_SCRIPT");if(!text)return;
    while(*text&&script_count<32){
        ScriptEvent e{};char kind=*text;unsigned a=0,b=0,c=0,d=0;int used=0;
        if(kind=='t'&&std::sscanf(text,"t:%u:%u:%u:%u%n",&a,&b,&c,&d,&used)==4){e={a,d,b,c,0,true};}
        else if(kind=='k'&&std::sscanf(text,"k:%u:%x:%u%n",&a,&b,&c,&used)==3){e={a,c,0,0,b,false};}
        else break;
        script[script_count++]=e;text+=used;if(*text==',')++text;
    }
    std::printf("Fresh native PLAYABLE script: events=%u\n",script_count);
}
const ScriptEvent* active(bool touch){
    if(!script_loaded)load_script();
    for(unsigned i=0;i<script_count;++i){const auto& e=script[i];
        if(e.touch==touch&&script_frame>=e.frame&&script_frame<e.frame+e.length)return &e;}
    return nullptr;
}
}
extern "C" u32 __real_hidKeysHeld(void);
extern "C" u32 __real_hidKeysDown(void);
extern "C" void __real_hidTouchRead(touchPosition*);
extern "C" void __real_hidScanInput(void);
extern "C" void __real_hidCircleRead(circlePosition*);
extern "C" void __real_hidCstickRead(circlePosition*);
// SELECT+START test mode (test_mode.cpp): it sees the real buttons first, then
// replaces every controller read of the game for the whole run.
namespace {
std::uint32_t test_held=0,test_previous=0;bool test_pad=false;
void refresh_test_pad(){
    std::uint32_t held=0;test_pad=sm64ds_test_mode_keys(&held)!=0;
    if(!test_pad){test_previous=test_held=0;return;}
    int x=0,y=0;
    // libctru derives these direction bits from the same stick positions.
    if(sm64ds_test_mode_circle(&x,&y)){if(x>40)held|=KEY_CPAD_RIGHT;if(x<-40)held|=KEY_CPAD_LEFT;if(y>40)held|=KEY_CPAD_UP;if(y<-40)held|=KEY_CPAD_DOWN;}
    if(sm64ds_test_mode_cstick(&x,&y)){if(x>40)held|=KEY_CSTICK_RIGHT;if(x<-40)held|=KEY_CSTICK_LEFT;if(y>40)held|=KEY_CSTICK_UP;if(y<-40)held|=KEY_CSTICK_DOWN;}
    test_previous=test_held;test_held=held;
}
}
extern "C" void __wrap_hidScanInput(void){
    __real_hidScanInput();
    // Owner buttons, plus an emulator session's scripted keys (PLAYABLE).
    u32 held=__real_hidKeysHeld(),down=__real_hidKeysDown();
    if(const auto* e=active(false)){held|=e->keys;if(script_frame==e->frame)down|=e->keys;}
    sm64ds_test_mode_scan(held,down);
    refresh_test_pad();
}
extern "C" void sm64ds_playable_script_tick(){++script_frame;}
extern "C" u32 __wrap_hidKeysHeld(void){
    if(test_pad)return test_held;
    u32 held=__real_hidKeysHeld();
    if(const auto* e=active(false))held|=e->keys;
    if(active(true))held|=KEY_TOUCH;
    return held;
}
extern "C" u32 __wrap_hidKeysDown(void){
    if(test_pad)return test_held&~test_previous;
    u32 down=__real_hidKeysDown();
    if(const auto* e=active(false))if(script_frame==e->frame)down|=e->keys;
    if(const auto* e=active(true))if(script_frame==e->frame)down|=KEY_TOUCH;
    return down;
}
extern "C" void __wrap_hidTouchRead(touchPosition* out){
    __real_hidTouchRead(out);
    int x=0,y=0;
    if(test_pad&&sm64ds_test_mode_touch(&x,&y)){out->px=u16(x);out->py=u16(y);return;}
    if(const auto* e=active(true)){out->px=u16(e->x);out->py=u16(e->y);}
}
extern "C" void __wrap_hidCircleRead(circlePosition* out){
    int x=0,y=0;
    if(test_pad&&sm64ds_test_mode_circle(&x,&y)){out->dx=s16(x);out->dy=s16(y);return;}
    __real_hidCircleRead(out);
}
extern "C" void __wrap_hidCstickRead(circlePosition* out){
    int x=0,y=0;
    if(test_pad&&sm64ds_test_mode_cstick(&x,&y)){out->dx=s16(x);out->dy=s16(y);return;}
    __real_hidCstickRead(out);
}
// The C-Stick camera reads the New 3DS ir:rst service directly.
extern "C" void __real_irrstCstickRead(circlePosition*);
extern "C" void __wrap_irrstCstickRead(circlePosition* out){
    int x=0,y=0;
    if(test_pad&&sm64ds_test_mode_cstick(&x,&y)){out->dx=s16(x);out->dy=s16(y);return;}
    __real_irrstCstickRead(out);
}


extern "C" int sm64ds_fresh_native_diagnostic_begin(){
    if(!runtime||!runtime->graphics)return 0;
    runtime->diagnostic_pause_tick=svcGetSystemTick();
    finish_interpolated_frame(true);
    std::printf("Native diagnostic drained: frame=%llu scene=%llu linearFree=%u appFree=%u\n",
        (unsigned long long)runtime->frames,(unsigned long long)runtime->scene,unsigned(linearSpaceFree()),
        unsigned(osGetMemRegionFree(MEMREGION_APPLICATION)));
    return runtime->renderer.finish()==Error::none;
}
extern "C" int sm64ds_fresh_native_diagnostic_capture(const char* directory){
    if(!runtime||!worker||!directory||!*directory||std::strlen(directory)>180)return 0;
    // The frame hook has already drained this worker. Game capture cannot
    // resume until this call returns; retained arrays and GPU targets are stable.
    char path[224];bool okay=true;
    std::snprintf(path,sizeof path,"%s/native-frames.csv",directory);
    if(FILE* file=std::fopen(path,"wb")){
        okay=worker->timeline.write_csv(file);if(std::fclose(file)!=0)okay=false;
    }else okay=false;
    bool component=false;
    if(worker->diagnostic_view.serial){
        std::snprintf(path,sizeof path,"%s/native-components.json",directory);
        if(FILE* file=std::fopen(path,"wb")){
            component=write_component_capture(file,worker->diagnostic_view,
                {worker->job.frame,worker->job.writers,runtime->clear_known,!worker->job.native_2d});
            if(std::fclose(file)!=0)component=false;
        }
    }
    std::snprintf(path,sizeof path,"%s/native-display.json",directory);
    if(FILE* file=std::fopen(path,"wb")){
        const bool written=hooks::write_menu_display_capture(file,runtime->frames);
        if(std::fclose(file)!=0||!written)okay=false;
    }else okay=false;
    std::uint32_t hashes[2]{};bool captured[2]{};
    void* pixels=linearMemAlign(400*240*3,128);
    if(pixels){
        for(unsigned side=0;side<2;++side){const auto screen=side?Screen::bottom:Screen::top;
            const auto bytes=std::size_t(screen_width(screen))*240*3;
            if(runtime->renderer.capture_rgb8(screen,pixels,bytes)!=Error::none)continue;
            auto checksum=2166136261u;const auto* data=static_cast<const std::uint8_t*>(pixels);
            for(std::size_t n=0;n<bytes;++n)checksum=(checksum^data[n])*16777619u;
            hashes[side]=checksum;
            std::snprintf(path,sizeof path,"%s/native-%s.rgb",directory,side?"bottom":"top");
            if(FILE* file=std::fopen(path,"wb")){
                captured[side]=std::fwrite(data,1,bytes,file)==bytes;
                if(std::fclose(file)!=0)captured[side]=false;
            }
        }
        linearFree(pixels);
    }
    const auto& gpu=runtime->renderer.backend_metrics();const auto& banks=runtime->renderer.metrics();
    std::snprintf(path,sizeof path,"%s/native-renderer.json",directory);
    if(FILE* file=std::fopen(path,"wb")){
        const int written=std::fprintf(file,
            "{\"format\":\"SM64_NATIVE_DIAGNOSTIC_1\",\"build\":%u,\"tick_frequency\":%u,\"frame\":%llu,\"scene\":%llu,"
            "\"worker_drained\":true,\"gpu_os_dsp_atomic\":false,\"timings\":%s,\"native_menu\":%s,\"component\":%s,"
            "\"timeline_samples\":%u,\"linear_free\":%u,\"app_free\":%u,\"gpu_linear_bytes\":%u,\"gpu_vram_bytes\":%u,"
            "\"bank_loads\":%llu,\"bank_evictions\":%llu,\"bank_load_ticks\":%llu,\"refused\":%llu,\"bank_load_failures\":%llu,\"failed_bank\":%u,\"load_error\":%u,"
            "\"top_rgb\":%s,\"bottom_rgb\":%s,\"rgb_layout\":\"portrait BGR8: (x*240+239-y)*3\",\"rgb_fnv1a\":[%u,%u]}\n",
            sm64ds_build::number,unsigned(SYSCLOCK_ARM11),(unsigned long long)runtime->frames,(unsigned long long)runtime->scene,
            runtime->timings?"true":"false",runtime->native_menu?"true":"false",component?"true":"false",worker->timeline.size(),
            unsigned(linearSpaceFree()),unsigned(osGetMemRegionFree(MEMREGION_APPLICATION)),unsigned(gpu.live_linear_bytes),unsigned(gpu.live_vram_bytes),
            (unsigned long long)banks.loads,(unsigned long long)banks.evictions,(unsigned long long)banks.load_ticks,(unsigned long long)runtime->refused,
            (unsigned long long)banks.load_failures,banks.failed_bank,unsigned(banks.load_error),
            captured[0]?"true":"false",captured[1]?"true":"false",hashes[0],hashes[1]);
        if(std::fclose(file)!=0||written<0)okay=false;
    }else okay=false;
    std::printf("Native diagnostic targets: path=%s metadata=%u top=%u bottom=%u component=%u samples=%u\n",
        directory,unsigned(okay),unsigned(captured[0]),unsigned(captured[1]),unsigned(component),worker->timeline.size());
    runtime->stats.last_tick=0;worker->cost={}; // exclude the deliberate capture pause from frame-time windows
    return okay&&captured[0]&&captured[1];
}
extern "C" void sm64ds_fresh_native_diagnostic_end(){
    if(!runtime)return;
    sm64ds_native_cadence_pause_ended(runtime->diagnostic_pause_tick);runtime->diagnostic_pause_tick=0;
    runtime->stats.last_tick=0;if(worker)worker->cost={};
}

// ---- SELECT+START test reports ---------------------------------------------
namespace {
// 24-bit BMP from the renderer's portrait BGR8 readback ((x*240+239-y)*3),
// built in one heap block: one SD write instead of 240 small ones.
bool write_bmp(const char* path,const std::uint8_t* bgr,unsigned width){
    const unsigned row=(width*3+3)&~3u;const std::size_t size=54+std::size_t(row)*240;
    auto* out=static_cast<std::uint8_t*>(std::malloc(size));if(!out)return false;
    std::memset(out,0,54);out[0]='B';out[1]='M';
    auto put32=[&](unsigned o,std::uint32_t v){out[o]=std::uint8_t(v);out[o+1]=std::uint8_t(v>>8);out[o+2]=std::uint8_t(v>>16);out[o+3]=std::uint8_t(v>>24);};
    put32(2,std::uint32_t(size));put32(10,54);put32(14,40);put32(18,width);put32(22,240);out[26]=1;out[28]=24;put32(34,row*240);
    for(unsigned r=0;r<240;++r){ // BMP rows run bottom-up
        const unsigned y=239-r;auto* line=out+54+std::size_t(r)*row;
        for(unsigned x=0;x<width;++x)std::memcpy(line+x*3,bgr+(std::size_t(x)*240+239-y)*3,3);
        for(unsigned pad=width*3;pad<row;++pad)line[pad]=0;
    }
    bool ok=false;
    if(FILE* f=std::fopen(path,"wb")){ok=std::fwrite(out,1,size,f)==size;ok=std::fclose(f)==0&&ok;}
    std::free(out);return ok;
}
struct Series {double sum=0,max=0;unsigned n=0;void add(double v){sum+=v;if(v>max)max=v;++n;}double mean()const{return n?sum/n:0;}};
}
// BUILD121: a test report holds one drained pause around all of its SD work
// (new folders took ~2.4 s each on the owner's card, then files and the
// result line). Before, the folders were created before the drain: the
// in-flight render job, blocked on its own unbuffered log line, absorbed
// that time and the cadence counted it as lateness.
extern "C" int sm64ds_fresh_native_report_pause(int begin){
    if(!runtime||!worker)return 0;
    // Drained worker and GPU; the pause is excluded from the cadence.
    if(begin){if(sm64ds_fresh_native_diagnostic_begin())return 1;sm64ds_fresh_native_diagnostic_end();return 0;}
    std::fflush(stdout);sm64ds_fresh_native_diagnostic_end();return 1;
}
// Caller holds sm64ds_fresh_native_report_pause(1).
// 2026-11-09: mixer timing of a report window (audio_diagnostics::Timing,
// written by the audio pump and the game thread's lock waits).
namespace {
struct AudioWindow {std::uint64_t tick=0;std::uint32_t at[10]{};};
AudioWindow audio_window;
void audio_counters(std::uint32_t out[10]){
    namespace ad=enhance::audio_diagnostics;const auto& t=ad::timing;
    out[0]=t.pump_us.load(std::memory_order_relaxed);out[1]=t.mix_us.load(std::memory_order_relaxed);
    out[2]=t.wait_us.load(std::memory_order_relaxed);out[3]=t.chunks.load(std::memory_order_relaxed);
    out[4]=t.game_wait_us.load(std::memory_order_relaxed);out[5]=t.game_waits.load(std::memory_order_relaxed);
    out[6]=ad::snapshot().queue_drains;
    out[7]=t.late_wait_us.load(std::memory_order_relaxed);out[8]=t.late_waits.load(std::memory_order_relaxed);
    out[9]=t.deferred_batches.load(std::memory_order_relaxed);
}
}
extern "C" void sm64ds_fresh_native_audio_window(void){
    audio_window.tick=svcGetSystemTick();audio_counters(audio_window.at);
    enhance::audio_diagnostics::timing.chunk_max_us.exchange(0,std::memory_order_relaxed);
    enhance::audio_diagnostics::timing.game_wait_max_us.exchange(0,std::memory_order_relaxed);
}
// BUILD131: "directory" is a file stem (level folder + "/05-correr"); the
// report's files are stem.cuadros.csv, stem.resumen.txt, stem.arriba.bmp,
// stem.abajo.bmp and stem.faltan.csv. One folder per situation cost ~2.4 s
// of SD time each on the owner's card (3.3 s per report on the BUILD128 tour).
extern "C" int sm64ds_fresh_native_test_report(const char* directory,std::uint64_t window_tick,const char* title,char* summary,std::size_t summary_size){
    if(summary&&summary_size)summary[0]=0;
    if(!runtime||!worker||!directory||std::strlen(directory)>150)return 0;
    const double ms=1000.0/double(SYSCLOCK_ARM11);
    char path[200];bool okay=true;
    const auto& tl=worker->timeline;
    // Rows of this situation: presentations that began inside the window.
    unsigned first=tl.size();
    for(unsigned i=0;i<tl.size();++i)if(tl.at(i).start>=window_tick){first=i;break;}
    std::snprintf(path,sizeof path,"%s.cuadros.csv",directory);
    if(FILE* f=std::fopen(path,"wb")){
        static char buffer[16384];std::setvbuf(f,buffer,_IOFBF,sizeof buffer);
        okay=FrameTimeline::write_header(f);
        // Two seconds of context before the window, then the window itself.
        const unsigned context=first>60?first-60:0;
        for(unsigned i=context;i<tl.size()&&okay;++i)okay=FrameTimeline::write_row(f,tl.at(i));
        if(std::fclose(f)!=0)okay=false;
    }else okay=false;
    static double intervals[FrameTimeline::capacity];unsigned count=0;
    Series update,source,producer,skin,work,bridge_s,draw_s,upload,submit,wait,gpu,draws,triangles,quads,matrices,light_rows,upload_kb,materials;
    Series render_cpu,stage[9];unsigned route[6]{};
    std::uint64_t load_ticks=0;unsigned errors=0,endpoints=0;
    for(unsigned i=first;i<tl.size();++i){const auto& r=tl.at(i);
        if(r.error){++errors;continue;}
        if(i>first&&!tl.at(i-1).error&&r.end>tl.at(i-1).end)intervals[count++]=double(r.end-tl.at(i-1).end)*ms;
        endpoints+=r.endpoint;
        if(!r.endpoint){update.add(double(r.update)*ms);source.add(double(r.source_2d)*ms);producer.add(double(r.producer)*ms);skin.add(double(r.skin)*ms);}
        work.add(double(r.end-r.start)*ms);bridge_s.add(double(r.bridge)*ms);draw_s.add(double(r.draw)*ms);
        upload.add(double(r.upload)*ms);submit.add(double(r.submit)*ms);wait.add(double(r.gpu_wait)*ms);gpu.add(r.previous_gpu_us/1000.0);
        draws.add(r.draws);triangles.add(r.triangles);quads.add(r.quads);matrices.add(r.matrices);light_rows.add(r.light_rows);
        upload_kb.add(r.upload_bytes/1024.0);materials.add(r.material_binds);load_ticks+=r.bank_load;
        // Only a worker sharing the game's core loses time to its updates.
        const auto job=r.end-r.start;
        render_cpu.add(double(!worker->shares_game_core?job:job>r.game_overlap?job-r.game_overlap:0)*ms);
        for(unsigned k=0;k<9;++k)stage[k].add(double(r.stages[k])*ms);
        for(unsigned k=0;k<6;++k)route[k]+=(r.route_2d>>k)&1u;
    }
    std::sort(intervals,intervals+count);
    auto pct=[&](unsigned p){return count?intervals[std::min(count-1,(count*p+99)/100-1)]:0.;};
    double span=0;for(unsigned i=0;i<count;++i)span+=intervals[i];
    const double fps=span>0?double(count)*1000.0/span:0;
    unsigned over33=0,over17=0;for(unsigned i=0;i<count;++i){over33+=intervals[i]>34.0;over17+=intervals[i]>17.5;}
    bool is_new=false;APT_CheckNew3DS(&is_new);
    const auto& profile=console_profile::current();
    std::uint64_t dry_in_window=0;
    unsigned heap_free=0;double audio_load=0;
    unsigned missing_models=0;
    std::snprintf(path,sizeof path,"%s.resumen.txt",directory);
    if(FILE* f=std::fopen(path,"wb")){
        static char buffer[8192];std::setvbuf(f,buffer,_IOFBF,sizeof buffer);
        std::fprintf(f,"SM64DS PRUEBA %s\nsituacion: %s\nconsola: %s 3DS, cpu %s, limite %s FPS, antialias %u\n",
            sm64ds_build::label,title?title:"?",is_new?"New":"Old",is_new?"804 MHz":"268 MHz",
            present_mode().lock==enhance::kFpsLock30?"30":present_mode().lock==enhance::kFpsLock60?"60":"sin",unsigned(profile.antialias));
        std::fprintf(f,"\n== imagenes (%u, %u intermedias, %u rechazadas) ==\nfps=%.2f\nintervalo_ms media=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f\nsobre_34ms=%u sobre_17.5ms=%u\n",
            count?count+1:0,endpoints,errors,fps,count?span/count:0,pct(50),pct(95),pct(99),count?intervals[count-1]:0,over33,over17);
        std::fprintf(f,"\n== hilo del juego (por actualizacion, ms) ==\nactualizacion media=%.2f max=%.2f   (logica DS + captura + 2D CPU)\npreparacion_2d media=%.2f max=%.2f\nproductores_3d media=%.2f max=%.2f\nskin media=%.2f max=%.2f\n",
            update.mean(),update.max,source.mean(),source.max,producer.mean(),producer.max,skin.mean(),skin.max);
        std::fprintf(f,"\n== hilo de render (por imagen, ms) ==\ntrabajo media=%.2f max=%.2f\ncomposicion_2d media=%.2f max=%.2f\ndibujo media=%.2f max=%.2f\n  espera_gpu media=%.2f max=%.2f\n  subida media=%.2f max=%.2f\n  envio media=%.2f max=%.2f\ngpu_anterior media=%.2f max=%.2f\ncarga_bancos total=%.1f\n",
            work.mean(),work.max,bridge_s.mean(),bridge_s.max,draw_s.mean(),draw_s.max,wait.mean(),wait.max,upload.mean(),upload.max,
            submit.mean(),submit.max,gpu.mean(),gpu.max,double(load_ticks)*ms);
        // BUILD121: a worker sharing core 0 with the game thread (Old 3DS
        // BUILD123 layout) has wall times that include game updates; this
        // subtracts them. On its own core the work is the estimate.
        std::fprintf(f,"render_cpu_estimado media=%.2f max=%.2f   (trabajo; si comparte nucleo con el juego, menos sus actualizaciones)\n"
            "etapas_dibujo media: validar=%.2f espera=%.2f recursos=%.2f dinamico=%.2f previo=%.2f plan=%.2f geometria=%.2f composicion=%.2f final=%.2f\n"
            "ruta_2d imagenes: menu_nativo=%u cpu2d_arriba=%u preparada_a=%u preparada_b=%u mapa_gpu=%u cielo=%u\n",
            render_cpu.mean(),render_cpu.max,stage[0].mean(),stage[1].mean(),stage[2].mean(),stage[3].mean(),stage[4].mean(),
            stage[5].mean(),stage[6].mean(),stage[7].mean(),stage[8].mean(),route[0],route[1],route[2],route[3],route[4],route[5]);
        std::fprintf(f,"\n== trabajo GPU por imagen ==\ndibujos media=%.0f max=%.0f\ntriangulos media=%.0f max=%.0f\nquads media=%.0f\nmatrices media=%.0f\nfilas_luz media=%.0f\nsubida_kb media=%.1f max=%.1f\nmateriales media=%.0f\n",
            draws.mean(),draws.max,triangles.mean(),triangles.max,quads.mean(),matrices.mean(),light_rows.mean(),upload_kb.mean(),upload_kb.max,materials.mean());
        {unsigned used=0,chunks=0,unclaimed=0;sm64ds_heap_stats(&used,&chunks,&unclaimed);heap_free=chunks+unclaimed;
            std::fprintf(f,"\n== memoria ==\nlinear_libre=%u\napp_libre=%u\nheap_usado=%u\nheap_libre=%u\nheap_libre_bloques=%u\nheap_sin_asignar=%u\n",
                unsigned(linearSpaceFree()),unsigned(osGetMemRegionFree(MEMREGION_APPLICATION)),used,heap_free,chunks,unclaimed);}
        // BUILD121: APPLICATION resource limits (they outlive this process).
        {const auto u=resource_use();
            if(u.ok)std::fprintf(f,"\n== sistema (limites de la aplicacion, en uso/maximo) ==\nhilos=%lld/%lld\neventos=%lld/%lld\nmutex=%lld/%lld\nsemaforos=%lld/%lld\ntemporizadores=%lld/%lld\nmemoria_compartida=%lld/%lld\ncpu_nucleo1=%lld\n",
                u.current[2],u.limit[2],u.current[3],u.limit[3],u.current[4],u.limit[4],u.current[5],u.limit[5],
                u.current[6],u.limit[6],u.current[7],u.limit[7],u.limit[9]);
            // BUILD124: where the render worker and the mixer run.
            std::fprintf(f,"nucleo_render=%d\nnucleo_audio=%d\nrender_comparte_nucleo=%u\n",worker->core,
                audio_core_used.load(std::memory_order_relaxed),unsigned(worker->shares_game_core));}
        // Mixer time in this window (wall time; core 1 is shared with GSP/FS).
        {std::uint32_t now[10],d[10];audio_counters(now);
            const std::uint64_t since=audio_window.tick?audio_window.tick:window_tick;
            for(unsigned i=0;i<10;++i)d[i]=now[i]-audio_window.at[i];
            const double seconds=std::max(1e-3,double(svcGetSystemTick()-since)/double(SYSCLOCK_ARM11));
            audio_load=d[0]/seconds/1e4;
            std::fprintf(f,"\n== audio (ventana de la situacion, tiempo de pared) ==\nmezclador=%s\nmezcla_ms_por_s=%.2f   (mezclador y secuenciador, bajo el cerrojo)\nbombeo_ms_por_s=%.2f   (hilo de audio entero: mezcla, esperas y NDSP)\ncarga_audio=%.1f%%\nbloque_max_ms=%.2f   (64 muestras)\nespera_audio_ms_por_s=%.2f   (el mezclador cede el cerrojo al juego)\njuego_espera_ms total=%.2f max=%.2f esperas=%u   (el juego espera al mezclador)\njuego_espera_tras_imagen_ms total=%.2f esperas=%u lotes_aplazados=%u   (con la imagen ya entregada al render)\ncolas_vacias=%u\n",
                sm64ds_mixer::kernel_name(),d[1]/1000.0/seconds,d[0]/1000.0/seconds,audio_load,
                enhance::audio_diagnostics::timing.chunk_max_us.load(std::memory_order_relaxed)/1000.0,d[2]/1000.0/seconds,
                d[4]/1000.0,enhance::audio_diagnostics::timing.game_wait_max_us.load(std::memory_order_relaxed)/1000.0,d[5],d[7]/1000.0,d[8],d[9],d[6]);}
        // Since the previous report: updates without an image and renderer
        // health (each counter is cumulative in the runtime).
        {static Runtime* owner=nullptr;static std::uint64_t last[12]{};
            const auto& h=hooks::statistics();const auto& p=hooks::particle_statistics();
            const std::uint64_t now[12]={runtime->dry_behind,runtime->dry_cap,runtime->late_skips,runtime->busy_drops,
                h.missing_models,h.producer_failures,h.lifetime_failures,p.failures,runtime->refused,h.dry_calls,runtime->frames,runtime->dry_busy};
            if(owner!=runtime){owner=runtime;for(auto& v:last)v=0;}
            std::uint64_t d[12];for(unsigned i=0;i<12;++i)d[i]=now[i]-last[i];std::memcpy(last,now,sizeof last);
            // BUILD121: every update without an image (BUILD 120 left out busy drops).
            dry_in_window=d[0]+d[2]+d[3]+d[11];
            std::fprintf(f,"\n== actualizaciones sin imagen (desde el informe anterior) ==\nactualizaciones=%llu\nsecas_por_retraso=%llu\nsecas_por_tope=%llu\nsaltadas_tarde=%llu\ndescartadas_render_ocupado=%llu\nsecas_por_render_ocupado=%llu\nmodelos_sin_producir=%llu\n",
                (unsigned long long)d[10],(unsigned long long)d[0],(unsigned long long)d[1],(unsigned long long)d[2],(unsigned long long)d[3],(unsigned long long)d[11],(unsigned long long)d[9]);
            {   // BUILD134: Lock FPS 60 / Unlocked. On a shared core the pair is
                // only attempted when the measured update and both images fit.
                static std::uint64_t ilast[5]{};const auto& b=interpolation_budget;
                const std::uint64_t inow[5]={worker?worker->midpoint_frames:0,worker?worker->endpoint_frames:0,b.allowed,b.over,b.misses};
                static std::uint64_t overlapped_last=0;const auto overlapped_now=worker?worker->overlapped_frames:0;
                const auto overlapped=overlapped_now-overlapped_last;overlapped_last=overlapped_now;
                std::uint64_t id[5];for(unsigned i=0;i<5;++i){id[i]=inow[i]-ilast[i];ilast[i]=inow[i];}
                const double tick_ms=1000.0/double(SYSCLOCK_ARM11);
                std::fprintf(f,"\n== interpolacion (desde el informe anterior) ==\nmodo=%s\nintermedias=%llu\nexactas_tras_intermedia=%llu\nexactas_en_paralelo_con_gpu=%llu\npresupuesto_cabe=%llu\npresupuesto_no_cabe=%llu\nsegunda_imagen_perdida=%llu\ncosto_imagen_ms=%.2f\ncosto_segunda_ms=%.2f\nespera_updates=%u\n",
                    present_mode().lock==enhance::kFpsLock30?"30":present_mode().lock==enhance::kFpsLock60?"60":"sin limite",
                    (unsigned long long)id[0],(unsigned long long)id[1],(unsigned long long)overlapped,(unsigned long long)id[2],(unsigned long long)id[3],(unsigned long long)id[4],
                    b.image_ticks*tick_ms,b.second_ticks*tick_ms,b.hold);
            }
            std::fprintf(f,"\n== salud del render (desde el informe anterior) ==\nmodelos_faltantes=%llu\nfallos_productor=%llu\nfallos_vida_modelo=%llu\nfallos_particulas=%llu (ultimo: %s)\nimagenes_rechazadas=%llu\nultimo_modelo_faltante=%lu\n",
                (unsigned long long)d[4],(unsigned long long)d[5],(unsigned long long)d[6],(unsigned long long)d[7],p.last_issue?p.last_issue:"ok",
                (unsigned long long)d[8],(unsigned long)h.last_missing_file);
            // BUILD131: which capture stage refused the models counted above.
            // The rest had no open image or did not match their bank.
            static std::uint64_t stage_last[8]{};const auto& failed=hooks::capture_failures();
            static constexpr const char* stage_name[8]={"materiales","proyeccion","poses","bases_normales","bases_luz","luces","escena","huesos"};
            std::uint64_t explained=0;bool any=false;
            for(unsigned i=0;i<8;++i){const auto n=failed.stage[i]-stage_last[i];stage_last[i]=failed.stage[i];explained+=n;
                if(n){std::fprintf(f,"%s%s=%llu (%s)",any?", ":"fallos_por_etapa: ",stage_name[i],(unsigned long long)n,describe(failed.last[i]));any=true;}}
            if(d[5]>explained)std::fprintf(f,"%ssin_imagen_abierta_o_modelo_distinto=%llu",any?", ":"fallos_por_etapa: ",(unsigned long long)(d[5]-explained));
            if(any||d[5]>explained)std::fputc('\n',f);
        }
        // BUILD131: the model producers by stage and the vector
        // normalisations, per update since the previous report.
        {static Runtime* owner=nullptr;static std::uint64_t last[9]{};
            const auto& stage=hooks::stage_profile;
            // BUILD134: per update that ran its producers. Dry updates (cap,
            // late, renderer busy) produce nothing; dividing by all updates
            // understated every stage (BUILD 133 reports).
            const auto dry=runtime->dry_behind+runtime->dry_cap+runtime->dry_busy;
            const std::uint64_t now[9]={stage.ticks[0],stage.ticks[1],stage.ticks[2],stage.ticks[3],stage.ticks[4],stage.ticks[5],stage.models,
                sm64ds::render::game::normalizations(),runtime->frames>dry?runtime->frames-dry:0};
            if(owner!=runtime){owner=runtime;for(auto& v:last)v=0;}
            std::uint64_t d[9];for(unsigned i=0;i<9;++i)d[i]=now[i]-last[i];std::memcpy(last,now,sizeof last);
            const double per=d[8]?ms/double(d[8]):0;
            std::fprintf(f,"\n== productores por etapa (ms por actualizacion con captura, desde el informe anterior) ==\nmateriales=%.2f\nposes=%.2f\nbases_normales=%.2f\nbases_luz_y_luces=%.2f\npartes=%.2f\nskin=%.2f\n"
                "modelos_por_actualizacion=%.1f\nnormalizaciones_por_actualizacion=%.1f   (division y raiz exactas por VFP desde BUILD131)\n",
                double(d[0])*per,double(d[1])*per,double(d[2])*per,double(d[3])*per,double(d[4])*per,double(d[5])*per,
                d[8]?double(d[6])/double(d[8]):0,d[8]?double(d[7])/double(d[8]):0);
        }
        // BUILD131: which models were not drawn since the previous report.
        {const hooks::MissingModel* table=nullptr;std::uint32_t overflow=0;missing_models=unsigned(hooks::missing_census(&table,&overflow));
            std::fprintf(f,"\n== modelos sin dibujar (desde el informe anterior; %u distintos%s) ==\n",missing_models,overflow?", tabla llena":"");
            for(unsigned i=0;i<missing_models&&i<12;++i){const auto& m=table[i];
                std::fprintf(f,"llamadas=%lu motivo=%s fichero=%ld huesos=%u materiales=%u texturas=%u material0=%s textura0=%s\n",(unsigned long)m.calls,
                    m.reason==1?"carga_no_vista":m.reason==2?"sin_banco_nativo":"identidad_doble",m.file_id==0xffffffffu?-1L:long(m.file_id),
                    m.bones,m.materials,m.textures,m.material[0]?m.material:"-",m.texture[0]?m.texture:"-");}
            if(missing_models>12)std::fprintf(f,"(%u mas en .faltan.csv)\n",missing_models-12);
        }
        std::fprintf(f,"\n(tiempos de pared con temporizadores finos; las fases se anidan y no se suman)\n");
        if(std::fclose(f)!=0)okay=false;
    }else okay=false;
    bool captured[2]{};
    if(void* pixels=linearMemAlign(400*240*3,128)){
        for(unsigned side=0;side<2;++side){const auto screen=side?Screen::bottom:Screen::top;
            const auto width=screen_width(screen);const auto bytes=std::size_t(width)*240*3;
            if(runtime->renderer.capture_rgb8(screen,pixels,bytes)!=Error::none)continue;
            std::snprintf(path,sizeof path,"%s.%s.bmp",directory,side?"abajo":"arriba");
            captured[side]=write_bmp(path,static_cast<const std::uint8_t*>(pixels),width);
        }
        linearFree(pixels);
    }
    // The whole census as CSV (only when something was missing), then reset
    // so the next report covers its own window.
    if(missing_models){
        const hooks::MissingModel* table=nullptr;std::uint32_t overflow=0;hooks::missing_census(&table,&overflow);
        std::snprintf(path,sizeof path,"%s.faltan.csv",directory);
        if(FILE* f=std::fopen(path,"wb")){
            std::fprintf(f,"llamadas,motivo,fichero,huesos,materiales,texturas,paletas,grupos,material0,textura0,direccion\n");
            for(unsigned i=0;i<missing_models;++i){const auto& m=table[i];
                std::fprintf(f,"%lu,%u,%ld,%u,%u,%u,%u,%u,%s,%s,%08lx\n",(unsigned long)m.calls,unsigned(m.reason),m.file_id==0xffffffffu?-1L:long(m.file_id),
                    m.bones,m.materials,m.textures,m.palettes,m.groups,m.material,m.texture,(unsigned long)reinterpret_cast<std::uintptr_t>(m.file));}
            if(overflow)std::fprintf(f,"%lu,0,-2,0,0,0,0,0,(otros modelos),,00000000\n",(unsigned long)overflow);
            if(std::fclose(f)!=0)okay=false;
        }else okay=false;
    }
    hooks::clear_missing_census();
    if(summary&&summary_size)std::snprintf(summary,summary_size,"fps=%.1f p95=%.1f max=%.1f juego=%.1f render=%.1f gpu=%.1f dibujos=%.0f sin_imagen=%llu heap_mb=%.1f audio=%.1f%% faltan=%u",
        fps,pct(95),count?intervals[count-1]:0,update.mean(),work.mean(),gpu.mean(),draws.mean(),(unsigned long long)dry_in_window,heap_free/1048576.0,audio_load,missing_models);
    return okay&&captured[0]&&captured[1];
}

// ---- BUILD131: deep dump of the test mode (docs/MODO_PRUEBA.md) ----------
// One frame's PICA command list and draw records, the semantic scene, the
// renderer's textures, DS-side video memory and,
// on request, process RAM. BUILD132 excludes hardware register/VRAM reads.
// Read with tools/test_mode_dump.py.
namespace {
struct TestDump {
    static constexpr std::size_t word_capacity=65536,draw_capacity=1024; // the C3D command buffer is 256 KiB
    std::uint32_t* words=nullptr;Citro3DBackend::DrawRecord* draws=nullptr;
} test_dump;
FILE* dump_open(char* path,std::size_t size,const char* directory,const char* name,bool bulk){
    std::snprintf(path,size,"%s/%s",directory,name);
    std::printf("PRUEBA volcado archivo %s\n",name);std::fflush(stdout);
    FILE* f=std::fopen(path,"wb");
    // Bulk payloads go to FS in 64 KiB writes, not through the 1 KiB stdio buffer.
    if(f&&bulk)std::setvbuf(f,nullptr,_IONBF,0);
    return f;
}
}
extern "C" void sm64ds_fresh_native_test_dump_arm(int arm){
    if(!runtime||!worker||!runtime->graphics)return;
    wait_worker(); // the capture state belongs to the render worker while it draws
    auto release=[]{std::free(test_dump.words);std::free(test_dump.draws);test_dump={};};
    if(!arm){runtime->renderer.disarm_frame_capture();release();return;}
    if(!test_dump.words){
        test_dump.words=static_cast<std::uint32_t*>(std::malloc(TestDump::word_capacity*4));
        test_dump.draws=static_cast<Citro3DBackend::DrawRecord*>(std::malloc(TestDump::draw_capacity*sizeof(Citro3DBackend::DrawRecord)));
    }
    if(!test_dump.words||!test_dump.draws){release();std::puts("PRUEBA volcado: sin memoria para la captura de comandos");return;}
    runtime->renderer.arm_frame_capture(test_dump.words,TestDump::word_capacity,test_dump.draws,TestDump::draw_capacity);
}
// flags: 1 whole process memory (memoria.vm), 2 DS video memory,
// 4 reserved (no hardware register reads), 8 the 2D bridge's planes and sprite memory. Caller holds the report pause (worker drained).
extern "C" int sm64ds_fresh_native_test_dump(const char* directory,int flags,char* summary,std::size_t summary_size){
    if(summary&&summary_size)summary[0]=0;
    if(!runtime||!worker||!directory||std::strlen(directory)>150)return 0;
    char path[200];unsigned ok=0,failed=0;const auto begun=svcGetSystemTick();
    const auto done=[&](bool good){good?++ok:++failed;};
    const auto& capture=runtime->renderer.frame_capture();
    const bool have=capture.complete&&test_dump.words&&test_dump.draws;
    unsigned words=0,draws=0;
    if(have){
        words=unsigned(capture.word_count);draws=unsigned(capture.draw_count);
        struct Header {char magic[8];std::uint32_t version,header_bytes,words,words_total,draws,draws_total,draw_bytes,passes;};
        const Header h{{'S','M','6','4','P','I','C','A'},1,sizeof(Header),words,std::uint32_t(capture.word_total),draws,
            std::uint32_t(capture.draw_total),sizeof(Citro3DBackend::DrawRecord),capture.passes};
        if(FILE* f=dump_open(path,sizeof path,directory,"comandos_gpu.bin",true)){
            bool good=std::fwrite(&h,sizeof h,1,f)==1;
            if(draws)good=std::fwrite(test_dump.draws,sizeof(Citro3DBackend::DrawRecord),draws,f)==draws&&good;
            if(words)good=std::fwrite(test_dump.words,4,words,f)==words&&good;
            done(std::fclose(f)==0&&good);
        }else done(false);
        if(FILE* f=dump_open(path,sizeof path,directory,"dibujos.csv",false)){
            static char buffer[8192];std::setvbuf(f,buffer,_IOFBF,sizeof buffer);
            std::fprintf(f,"dibujo,pasada,malla,textura,ancho,alto,formato,primer_indice,indices,iluminado,lote,afin,alfa_textura,mezcla,cara,profundidad,corte_alfa,palabra_comando\n");
            for(unsigned i=0;i<draws;++i){const auto& d=test_dump.draws[i];
                std::fprintf(f,"%u,%u,%lu,%ld,%u,%u,%d,%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%lu\n",i,d.pass,(unsigned long)d.mesh,d.texture==0xffffffffu?-1L:long(d.texture),
                    d.width,d.height,d.format==0xff?-1:int(d.format),(unsigned long)d.first,(unsigned long)d.count,d.flags&1u,(d.flags>>1)&1u,(d.flags>>2)&1u,(d.flags>>3)&1u,
                    d.blend,d.cull,d.depth,d.alpha_cutoff,(unsigned long)d.command_word);}
            done(std::fclose(f)==0);
        }else done(false);
    }
    // The scene the worker drew last: occurrences, parts, materials, quads.
    if(worker->diagnostic_view.serial){
        if(FILE* f=dump_open(path,sizeof path,directory,"escena.json",false)){
            static char buffer[16384];std::setvbuf(f,buffer,_IOFBF,sizeof buffer);
            const bool written=write_component_capture(f,worker->diagnostic_view,{worker->job.frame,worker->job.writers,runtime->clear_known,!worker->job.native_2d});
            done(std::fclose(f)==0&&written);
        }else done(false);
    }
    {   // Renderer resources and GPU memory.
        static Citro3DBackend::TextureInfo textures[256];
        const auto count=runtime->renderer.describe_textures(textures,256);const auto meshes=runtime->renderer.describe_meshes();
        const auto& gpu=runtime->renderer.backend_metrics();const auto& banks=runtime->renderer.metrics();
        if(FILE* f=dump_open(path,sizeof path,directory,"texturas.csv",false)){
            static char buffer[8192];std::setvbuf(f,buffer,_IOFBF,sizeof buffer);
            std::fprintf(f,"ranura,direccion,bytes,ancho,alto,formato,vram,destino_render,inicializada\n");
            for(std::size_t i=0;i<count&&i<256;++i){const auto& x=textures[i];
                std::fprintf(f,"%lu,%08lx,%lu,%u,%u,%u,%u,%u,%u\n",(unsigned long)x.slot,(unsigned long)x.address,(unsigned long)x.bytes,x.width,x.height,x.format,x.vram,x.target,x.initialized);}
            done(std::fclose(f)==0);
        }else done(false);
        if(FILE* f=dump_open(path,sizeof path,directory,"gpu_estado.txt",false)){
            std::fprintf(f,"SM64DS PRUEBA %s: estado del render\n\n== memoria GPU ==\nlineal_libre=%u\nvram_libre=%u\nrender_lineal=%u\nrender_vram=%u\npico_lineal=%u\npico_vram=%u\nvram_salida=%u\n",
                sm64ds_build::label,unsigned(linearSpaceFree()),unsigned(vramSpaceFree()),unsigned(gpu.live_linear_bytes),unsigned(gpu.live_vram_bytes),
                unsigned(gpu.peak_linear_bytes),unsigned(gpu.peak_vram_bytes),unsigned(runtime->renderer.output_vram_bytes()));
            std::fprintf(f,"\n== recursos ==\ntexturas=%u\nmallas=%u\nmallas_lote=%u\nbytes_mallas=%u\nindices=%u\ncargas_banco=%llu\nexpulsiones_banco=%llu\nfallos_carga=%llu\nultimo_banco_fallido=%u\n",
                unsigned(count),meshes.meshes,meshes.batches,unsigned(meshes.bytes),unsigned(meshes.indices),(unsigned long long)banks.loads,
                (unsigned long long)banks.evictions,(unsigned long long)banks.load_failures,banks.failed_bank);
            std::fprintf(f,"\n== contadores acumulados ==\ndibujos=%llu\ntriangulos=%llu\ncambios_malla=%llu\ncambios_material=%llu\nmatrices=%llu\nfilas_luz=%llu\ncambios_shader=%llu\n"
                "cambios_estado=%llu\nrechazados=%llu\nimagenes=%llu\nimagenes_rechazadas=%llu\nocurrencias_saltadas=%llu\n",
                (unsigned long long)gpu.draws,(unsigned long long)gpu.triangles,(unsigned long long)gpu.mesh_binds,(unsigned long long)gpu.material_binds,
                (unsigned long long)gpu.matrix_uploads,(unsigned long long)gpu.lighting_uniform_rows,(unsigned long long)gpu.shader_binds,
                (unsigned long long)gpu.pipeline_changes,(unsigned long long)gpu.rejected,(unsigned long long)banks.frames,(unsigned long long)banks.refused,
                (unsigned long long)runtime->renderer.skipped_occurrences());
            std::fprintf(f,"\n== cuadro capturado ==\ncapturado=%u\npalabras_comando=%u\npalabras_total=%u\ndibujos=%u\ndibujos_total=%u\npasadas=%u\n",unsigned(have),words,
                unsigned(capture.word_total),draws,unsigned(capture.draw_total),capture.passes);
            const auto& view=worker->diagnostic_view;
            std::fprintf(f,"\n== escena ==\nserie=%llu\nocurrencias=%u\npartes=%u\nvertices_quad=%u\nvertices_skin=%u\ncapas_imagen=%u\nestados_luz=%u\nsombras=%u\n",(unsigned long long)view.serial,
                unsigned(view.occurrences),unsigned(view.parts),unsigned(view.quad_vertices),unsigned(view.skin_vertices),unsigned(view.image_layers),unsigned(view.lighting.state_count),unsigned(view.shadows));
            done(std::fclose(f)==0);
        }else done(false);
    }
    // BUILD132: hardware BUILD131 stopped in internal PICA reads, then in
    // the full VRAM read on the next boot. Never probe either in the tour.
    // Keep command-buffer capture and resource metadata above.
    if(FILE* f=dump_open(path,sizeof path,directory,"lecturas_omitidas.txt",false)){
        const bool good=std::fputs("Registros GPU y VRAM fisica omitidos: bloqueos en Old 3DS BUILD131.\n"
            "Comandos GPU, escena, texturas y memoria DS conservados.\n",f)>=0;
        done(std::fclose(f)==0&&good);
    }else done(false);
    if(flags&(2|8)){
        // DS-side video state the 2D bridge still reads: I/O registers of both
        // engines, palettes and OAM, then the mapped VRAM windows. The light
        // form (flag 8 alone) keeps the sprite windows only.
        struct Region {std::uint32_t address,bytes;};
        static constexpr Region all[]={{0x04000000,0x1100},{0x05000000,0x800},{0x07000000,0x800},{0x06400000,0x40000},{0x06600000,0x20000},
            {0x06000000,0x100000},{0x06200000,0xA0000},{0x06800000,0xA4000}};
        const unsigned region_count=(flags&2)?8u:5u;
        if(FILE* f=dump_open(path,sizeof path,directory,"ds_video.bin",true)){
            struct Header {char magic[8];std::uint32_t version,regions;} h{{'S','M','6','4','D','S','V','1'},1,region_count};
            bool good=std::fwrite(&h,sizeof h,1,f)==1&&std::fwrite(all,sizeof(Region),region_count,f)==region_count;
            for(unsigned r=0;r<region_count;++r){const auto& g=all[r];if(!good)break;
                const auto* host=static_cast<const std::uint8_t*>(ntr::host_ptr(g.address));
                if(host)good=std::fwrite(host,1,g.bytes,f)==g.bytes;
                else{std::memset(native_diagnostics::transfer_staging.data(),0,native_diagnostics::transfer_bytes);
                    for(std::uint32_t at=0;at<g.bytes&&good;at+=native_diagnostics::transfer_bytes){
                        const std::size_t n=std::min<std::size_t>(g.bytes-at,native_diagnostics::transfer_bytes);
                        good=std::fwrite(native_diagnostics::transfer_staging.data(),1,n,f)==n;}}
            }
            done(std::fclose(f)==0&&good);
        }else done(false);
        // The three planes the 2D bridge composed last (256x192 ARGB words
        // each): under the 3D image, over it, and the other screen.
        if(bridge){
            const auto& w=*worker;
            struct Header {char magic[8];std::uint32_t version,width,height,planes,valid;} h{{'S','M','6','4','P','L','2','D'},1,ntr::SCREEN_W,ntr::SCREEN_H,3,
                (w.a_valid?1u:0u)|(w.a_valid&&w.front_any?2u:0u)|(w.b_valid?4u:0u)};
            if(FILE* f=dump_open(path,sizeof path,directory,"planos_2d.bin",true)){
                bool good=std::fwrite(&h,sizeof h,1,f)==1;
                for(const auto* plane:{&bridge->back,&bridge->front,&bridge->other})
                    good=good&&std::fwrite(&plane->px[0][0],4,ntr::SCREEN_W*ntr::SCREEN_H,f)==std::size_t(ntr::SCREEN_W*ntr::SCREEN_H);
                done(std::fclose(f)==0&&good);
            }else done(false);
        }
    }
    std::uint64_t memory_bytes=0;
    if(flags&1){
        // The L+R+A dump's own format (SM64VM1): every readable mapping.
        native_diagnostics::Header header;header.scene=unsigned(runtime->scene);header.epoch=unsigned(runtime->frames);header.begin_tick=svcGetSystemTick();
        std::snprintf(path,sizeof path,"%s/memoria.vm",directory);
        std::puts("PRUEBA volcado archivo memoria.vm (sin dispositivos)");std::fflush(stdout);
        done(native_diagnostics::write_memory(path,header,memory_bytes,false));
    }
    if(summary&&summary_size)std::snprintf(summary,summary_size,"archivos=%u fallidos=%u comandos=%u dibujos=%u pasadas=%u memoria_mb=%.1f ms=%.0f",ok,failed,words,draws,
        have?capture.passes:0u,double(memory_bytes)/1048576.0,double(svcGetSystemTick()-begun)*1000.0/double(SYSCLOCK_ARM11));
    runtime->renderer.disarm_frame_capture();
    std::free(test_dump.words);std::free(test_dump.draws);test_dump={};
    return failed==0;
}

extern "C" void __real_func_ov007_020bd4e8();
extern "C" void __wrap_func_ov007_020bd4e8(){
    if(!runtime||!runtime->playable){__real_func_ov007_020bd4e8();return;}
    if(!head_canvas){void* storage=std::malloc(sizeof(HeadCanvas));if(storage)head_canvas=new(storage)HeadCanvas{};}
    if(!head_canvas){runtime->unsubmitted_refusal=true;return;}
    head_canvas->ready=false;head_canvas->requested=true;
}
extern "C" void sm64ds_fresh_native_head_canvas_mode(int mode){
    if(mode==3&&head_canvas&&head_canvas->ready){
        std::memcpy(ntr::host_ptr(0x06008000),head_canvas->pixels.data(),head_canvas->pixels.size()*2);
        head_canvas->ready=false;
        std::puts("Native head canvas: committed bank D BG3 at 06008000");
    }
}


// BUILD115 crash fix. BUILD114 died on Old 3DS hardware in the castle pause:
// Luma recorded a core-1 data abort in the mixer's memcpy into its NDSP
// buffer (0x141c9e00, translation fault). The buffer was valid; the process
// was already exiting. abort() -> _exit -> libctru __appExit ->
// __libctru_exit unmaps the linear heap first, and nothing had stopped the
// core-1 audio thread, which was still rendering into it. devkitARM's
// std::terminate prints nothing, so the reason never reached the log.
//  - userAppExit (libctru __appExit calls it before either heap is freed, on
//    every exit path, HOME close included) stops the audio thread, closes
//    NDSP and waits for the render worker: an abnormal exit returns to HOME.
//  - std::terminate and SIGABRT log the reason, the heap and the code
//    addresses on the failing thread's stack (addr2line with the ELF) and
//    flush the log before anything is torn down.
#include <csignal>
#include <exception>
#include <malloc.h>
#include <unistd.h>
#include <unwind.h>
extern "C" char* fake_heap_end;
extern "C" void sm64ds_present_pipeline_exit_stop(void) __attribute__((weak));
void sm64ds_native_sound_output_close() __attribute__((weak));
extern "C" int sm64ds_test_mode_exit_relaunch(void) __attribute__((weak));
extern "C" bool sm64ds_on_present_worker(void);
// libc heap: bytes in use, free bytes inside the arena, bytes never claimed
// from the heap mapping (sbrk). Free total = the last two.
extern "C" void sm64ds_heap_stats(unsigned* used,unsigned* free_chunks,unsigned* unclaimed){
    const struct mallinfo m=mallinfo();
    char* brk=static_cast<char*>(sbrk(0));
    *used=unsigned(m.uordblks);*free_chunks=unsigned(m.fordblks);
    *unclaimed=(fake_heap_end&&brk!=reinterpret_cast<char*>(-1)&&fake_heap_end>brk)?unsigned(fake_heap_end-brk):0u;
}
namespace {
volatile int fatal_reports=0;
bool clean_exit=false;
volatile int exit_started=0;
void fatal_heap(const char* tag){
    unsigned used=0,chunks=0,unclaimed=0;sm64ds_heap_stats(&used,&chunks,&unclaimed);
    std::printf("%s heap: used=%u free=%u (chunks=%u unclaimed=%u) linearFree=%u\n",tag,used,chunks+unclaimed,chunks,unclaimed,unsigned(linearSpaceFree()));
}
_Unwind_Reason_Code fatal_unwind_step(_Unwind_Context* context,void* argument){
    auto* count=static_cast<unsigned*>(argument);
    if(++*count>24)return _URC_END_OF_STACK;
    std::printf(" %08lx",(unsigned long)_Unwind_GetIP(context));
    return _URC_NO_REASON;
}
// Words on this thread's stack that point into the code mapping: the return
// addresses of the failing call chain (newlib's abort has no unwind table).
void fatal_stack(const char* tag){
    MemInfo code{},stack{};PageInfo page{};
    if(R_FAILED(svcQueryMemory(&code,&page,0x00100000u)))return;
    std::uintptr_t sp;asm volatile("mov %0, sp":"=r"(sp));
    if(R_FAILED(svcQueryMemory(&stack,&page,sp)))return;
    const std::uintptr_t low=code.base_addr,high=code.base_addr+code.size;
    const std::uintptr_t end=std::min<std::uintptr_t>(stack.base_addr+stack.size,sp+8192);
    std::printf("%s stack sp=%08lx code=%08lx-%08lx:",tag,(unsigned long)sp,(unsigned long)low,(unsigned long)high);
    unsigned found=0;
    for(std::uintptr_t a=sp;a+4<=end&&found<48;a+=4){
        const std::uint32_t word=*reinterpret_cast<const volatile std::uint32_t*>(a);
        if(word>=low&&word<high&&!(word&3u)){std::printf(" %08lx",(unsigned long)word);++found;}
    }
    std::printf("\n");
}
const char* fatal_thread(){return native_worker_thread?"render":(sm64ds_on_present_worker()?"audio":"juego");}
[[noreturn]] void fatal_terminate(){
    const char* what="sin excepcion activa";
    if(const std::exception_ptr error=std::current_exception()){
        try{std::rethrow_exception(error);}
        catch(const std::bad_alloc&){what="std::bad_alloc (memoria agotada)";}
        catch(const std::exception& e){what=e.what();}
        catch(...){what="excepcion desconocida";}
    }
    if(__sync_fetch_and_add(&fatal_reports,1)<2){
        std::printf("FATAL: std::terminate hilo=%s: %s\n",fatal_thread(),what);
        fatal_heap("FATAL");
        unsigned frames=0;std::printf("FATAL unwind:");_Unwind_Backtrace(fatal_unwind_step,&frames);std::printf("\n");
        fatal_stack("FATAL");std::fflush(stdout);
    }
    std::abort();
}
void fatal_abort_signal(int){
    if(__sync_fetch_and_add(&fatal_reports,1)<2){
        std::printf("FATAL: abort() hilo=%s\n",fatal_thread());
        fatal_heap("FATAL");fatal_stack("FATAL");std::fflush(stdout);
    }
    // Not the game thread (libctru: no Thread object): exiting from here
    // would unmap the heaps under the still running game thread. Hand the
    // exit to it (it checks at its next update) and stay parked. If it does
    // not start within 3 s it is blocked (e.g. on a lock this thread holds):
    // exit from here then; a blocked thread touches no memory.
    if(threadGetCurrent()){
        __atomic_store_n(&sm64ds_fatal_thread_pending,1,__ATOMIC_RELEASE);
        if(native_worker_thread&&worker)LightEvent_Signal(&worker->done);
        for(unsigned i=0;i<30&&!__atomic_load_n(&exit_started,__ATOMIC_ACQUIRE);++i)svcSleepThread(100000000LL);
        if(__atomic_load_n(&exit_started,__ATOMIC_ACQUIRE))for(;;)svcSleepThread(1000000000LL);
        std::printf("FATAL: the game thread did not take over; exiting from hilo=%s\n",fatal_thread());std::fflush(stdout);
    }
}
}
// newlib keeps signal handlers per thread (one reent each): every thread of
// ours installs it; std::terminate's handler is process-wide.
extern "C" void sm64ds_fatal_handlers_install(void){
    std::signal(SIGABRT,fatal_abort_signal);
    static bool terminate_set=false;
    if(!terminate_set){terminate_set=true;std::set_terminate(fatal_terminate);}
}
extern "C" void userAppExit(void){
    static bool done=false;if(done)return;done=true;
    __atomic_store_n(&exit_started,1,__ATOMIC_RELEASE);
    const bool abnormal=!clean_exit;
    if(abnormal){std::printf("Native exit without runtime shutdown (abort, terminate or HOME close): stopping audio, NDSP and the render worker before the heaps are freed\n");std::fflush(stdout);}
    if(sm64ds_present_pipeline_exit_stop)sm64ds_present_pipeline_exit_stop();
    if(worker&&worker->busy&&!native_worker_thread)LightEvent_WaitTimeout(&worker->done,300000000LL);
    if(sm64ds_native_sound_output_close)sm64ds_native_sound_output_close();
    // The automatic tour continues by itself after a failure: the next boot
    // records this level as crashed and moves on.
    if(abnormal&&sm64ds_test_mode_exit_relaunch&&sm64ds_test_mode_exit_relaunch()){std::printf("Native exit: test tour relaunches the game\n");}
    if(abnormal)std::fflush(stdout);
}
extern "C" void sm64ds_fresh_native_mark_clean_exit(void){clean_exit=true;}

#endif
