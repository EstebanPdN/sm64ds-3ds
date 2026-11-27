#include <array>
#include "citro3d_backend.hpp"
#include "hang_watchdog.hpp"
#include "fixed_uv.hpp"
#include "normal_uv.hpp"
#include "native_quads.hpp"
#include "citro3d_program_cache.hpp"
#include "native_uniforms.hpp"
#include "perf_switch.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits>
extern "C" int sm64ds_gpu_unusable(void) __attribute__((weak));
extern "C" int sm64ds_gpu_hung(void) __attribute__((weak));
namespace {
// BUILD137: no GX work is issued after a GX wait gave up (the GPU is hung for
// the rest of the boot; citro3d ignores the failed wait in most callers).
// While HOME suspended the game with this worker still running, the worker
// waits here until the game resumes instead of abandoning a frame.
bool gpu_usable(){
    if(!sm64ds_gpu_unusable)return true;
    while(sm64ds_gpu_unusable()){if(sm64ds_gpu_hung&&sm64ds_gpu_hung())return false;svcSleepThread(10000000LL);}
    return true;
}
}

namespace sm64ds::render {
namespace {
// CPU->GPU publication of written bytes. GSPGPU_FlushDataCache is an IPC to
// the GSP sysmodule: ~0.33 ms per call on Old 3DS hardware regardless of size
// (measured by the LTTP/Minish ports), several calls per frame here. The
// kernel's store (clean) of the same range gives the GPU the same bytes; the
// writing thread cleans its own core's lines before submission. A refused SVC
// switches permanently to GSP. SM64DS_NATIVE_SVC_CACHE=0 restores GSP.
Result publish_for_gpu(const void* data,std::size_t bytes) noexcept {
    static int mode=-1; // -1 unknown, 1 SVC, 0 GSP
    if(mode<0){const char* option=std::getenv("SM64DS_NATIVE_SVC_CACHE");mode=option&&*option=='0'?0:1;}
    if(mode==1&&bytes){
        if(R_SUCCEEDED(svcStoreProcessDataCache(CUR_PROCESS_HANDLE,u32(reinterpret_cast<std::uintptr_t>(data)),u32(bytes))))return 0;
        mode=0;std::puts("Native cache publication: SVC refused; using GSP");
    }
    return GSPGPU_FlushDataCache(data,u32(bytes));
}
class Timed {
    std::uint64_t& total_;
    bool enabled_;
    std::uint64_t start_;
public:
    Timed(std::uint64_t& total, bool enabled) noexcept
        : total_(total), enabled_(enabled), start_(enabled ? svcGetSystemTick() : 0) {}
    ~Timed() { if (enabled_) total_ += svcGetSystemTick()-start_; }
};
}
Error Citro3DBackend::init(const void* bytes, std::size_t count, std::size_t budget, bool timings,bool prefer_affine) noexcept {
    if (ready_) return reject(Error::invalid_phase);
    const auto* uniform_ranges=std::getenv("SM64DS_NATIVE_UNIFORM_RANGES");
    sm64ds_native_uniform_ranges(!uniform_ranges||*uniform_ranges!='0');
    std::printf("Native uniform ranges: blocks=%u sdk=1.7.1\n",unsigned(!uniform_ranges||*uniform_ranges!='0'));
    const auto* draw_packets=std::getenv("SM64DS_NATIVE_DRAW_PACKETS");packed_draws_=!draw_packets||*draw_packets!='0';
    std::printf("Native indexed packets: enabled=%u words=30\n",unsigned(packed_draws_));
    if (!bytes || (reinterpret_cast<std::uintptr_t>(bytes)&3) || count < 16 ||
        count > std::numeric_limits<u32>::max() || budget < 256) return reject(Error::invalid_data);
    shader_ = DVLB_ParseFile(static_cast<u32*>(const_cast<void*>(bytes)), u32(count));
    if (!shader_) return reject(Error::out_of_memory);
    if (!shader_->numDVLE || R_FAILED(shaderProgramInit(&program_)) ||
        R_FAILED(shaderProgramSetVsh(&program_, &shader_->DVLE[0]))) {
        shaderProgramFree(&program_); DVLB_Free(shader_); shader_ = nullptr;
        return reject(Error::gpu_failure);
    }
    matrix_uniform_ = shaderInstanceGetUniformLocation(program_.vertexShader, "mvp");
    uv_uniform_ = shaderInstanceGetUniformLocation(program_.vertexShader, "uv_transform");
    tint_uniform_ = shaderInstanceGetUniformLocation(program_.vertexShader, "material_tint");
    fixed_uv_uniform_=shaderInstanceGetUniformLocation(program_.vertexShader,"integer_uv");
    fixed_uv_switch_=shaderInstanceGetUniformLocation(program_.vertexShader,"use_fixed_uv");
    normal_uv_uniform_=shaderInstanceGetUniformLocation(program_.vertexShader,"integer_uv");
    normal_uv_switch_=shaderInstanceGetUniformLocation(program_.vertexShader,"use_normal_uv");
    light_normal_uniform_=shaderInstanceGetUniformLocation(program_.vertexShader,"light_normal");
    light_direction_uniform_=shaderInstanceGetUniformLocation(program_.vertexShader,"light_direction");
    light_colour_uniform_=shaderInstanceGetUniformLocation(program_.vertexShader,"light_colour");
    light_material_uniform_=shaderInstanceGetUniformLocation(program_.vertexShader,"light_material");
    shininess_uniform_=shaderInstanceGetUniformLocation(program_.vertexShader,"shininess");
    light_loop_uniform_=shaderInstanceGetUniformLocation(program_.vertexShader,"light_loop");
    lighting_switch_=shaderInstanceGetUniformLocation(program_.vertexShader,"use_lighting");
    lights_switch_=shaderInstanceGetUniformLocation(program_.vertexShader,"has_lights");
    specular_switch_=shaderInstanceGetUniformLocation(program_.vertexShader,"use_specular");
    shininess_switch_=shaderInstanceGetUniformLocation(program_.vertexShader,"use_shininess");
    toon_switch_=shaderInstanceGetUniformLocation(program_.vertexShader,"use_toon");
    grey_switch_=shaderInstanceGetUniformLocation(program_.vertexShader,"use_grey");
    const auto rows_valid=[](int first,int count){return first>=0&&first<=C3D_FVUNIF_COUNT-count;};
    const auto bool_valid=[](int id){return id>=0x68&&id<0x78;};
    if (matrix_uniform_ < 0 || matrix_uniform_ > C3D_FVUNIF_COUNT-4 ||
        uv_uniform_ < 0 || uv_uniform_ > C3D_FVUNIF_COUNT-2 || tint_uniform_ < 0 ||
        tint_uniform_ >= C3D_FVUNIF_COUNT || fixed_uv_uniform_<0 || fixed_uv_uniform_>C3D_FVUNIF_COUNT-7 ||
        fixed_uv_switch_<0x68 || fixed_uv_switch_>=0x78 || normal_uv_uniform_<0 || normal_uv_uniform_>C3D_FVUNIF_COUNT-16 ||
        normal_uv_switch_<0x68 || normal_uv_switch_>=0x78 ||
        !rows_valid(light_normal_uniform_,12)||!rows_valid(light_direction_uniform_,4)||
        !rows_valid(light_colour_uniform_,4)||!rows_valid(light_material_uniform_,4)||!rows_valid(shininess_uniform_,32)||
        light_loop_uniform_<0x60||light_loop_uniform_>=0x64||!bool_valid(lighting_switch_)||
        !bool_valid(lights_switch_)||!bool_valid(specular_switch_)||!bool_valid(shininess_switch_)||!bool_valid(toon_switch_)||!bool_valid(grey_switch_)) {
        shaderProgramFree(&program_); DVLB_Free(shader_); shader_ = nullptr;
        return reject(Error::invalid_data);
    }
    if(prefer_affine&&shader_->numDVLE>1){
        if(R_FAILED(shaderProgramInit(&affine_program_))||
           R_FAILED(shaderProgramSetVsh(&affine_program_,&shader_->DVLE[1]))||
           shaderInstanceGetUniformLocation(affine_program_.vertexShader,"mvp")!=matrix_uniform_||
           shaderInstanceGetUniformLocation(affine_program_.vertexShader,"uv_transform")!=uv_uniform_||
           shaderInstanceGetUniformLocation(affine_program_.vertexShader,"material_tint")!=tint_uniform_||
           affine_program_.vertexShader->boolUniformMask||affine_program_.vertexShader->intUniformMask||
           program_.vertexShader->boolUniformMask||program_.vertexShader->intUniformMask){
            shaderProgramFree(&affine_program_);affine_program_={};
            shaderProgramFree(&program_);DVLB_Free(shader_);shader_=nullptr;return reject(Error::invalid_data);
        }
        // The linked entry shares dynamic rows0..81. Its private constants
        // must stay above them so a program bind cannot clobber cached lights.
        for(unsigned i=0;i<affine_program_.vertexShader->numFloat24Uniforms;++i)
            if(affine_program_.vertexShader->float24Uniforms[i].id<82){
                shaderProgramFree(&affine_program_);affine_program_={};
                shaderProgramFree(&program_);DVLB_Free(shader_);shader_=nullptr;return reject(Error::invalid_data);
            }
        affine_ready_=true;
    }
    if(shader_->numDVLE>2){
        if(R_FAILED(shaderProgramInit(&batch_program_))||R_FAILED(shaderProgramSetVsh(&batch_program_,&shader_->DVLE[2]))||
           matrix_uniform_!=0||shaderInstanceGetUniformLocation(batch_program_.vertexShader,"mvp")!=0||
           shaderInstanceGetUniformLocation(batch_program_.vertexShader,"shininess")!=47||
           batch_program_.vertexShader->numFloat24Uniforms!=program_.vertexShader->numFloat24Uniforms||
           batch_program_.vertexShader->boolUniformMask||batch_program_.vertexShader->intUniformMask){
            shaderProgramFree(&batch_program_);batch_program_={};
            if(affine_ready_)shaderProgramFree(&affine_program_);
            affine_program_={};affine_ready_=false;
            shaderProgramFree(&program_);DVLB_Free(shader_);shader_=nullptr;return reject(Error::invalid_data);
        }
        for(unsigned i=0;i<batch_program_.vertexShader->numFloat24Uniforms;++i)
            if(batch_program_.vertexShader->float24Uniforms[i].id<79||
               batch_program_.vertexShader->float24Uniforms[i].id!=program_.vertexShader->float24Uniforms[i].id||
               std::memcmp(batch_program_.vertexShader->float24Uniforms[i].data,
                   program_.vertexShader->float24Uniforms[i].data,sizeof(float24Uniform_s::data))){
                shaderProgramFree(&batch_program_);batch_program_={};
                if(affine_ready_)shaderProgramFree(&affine_program_);
                affine_program_={};affine_ready_=false;
                shaderProgramFree(&program_);DVLB_Free(shader_);shader_=nullptr;return reject(Error::invalid_data);
            }
        batch_uv_uniform_=32;batch_tint_uniform_=34;batch_ready_=true;
    }
    if (!C3D_TexInit(&unused_sampler_, 8, 8, GPU_RGBA8)) {
        if(batch_ready_)shaderProgramFree(&batch_program_);
        batch_program_={};batch_ready_=false;
        if(affine_ready_)shaderProgramFree(&affine_program_);
        affine_program_={};affine_ready_=false;
        shaderProgramFree(&program_); DVLB_Free(shader_); shader_ = nullptr;
        return reject(Error::out_of_memory);
    }
    std::memset(unused_sampler_.data, 255, 256);
    C3D_TexSetFilter(&unused_sampler_, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&unused_sampler_, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    if (R_FAILED(publish_for_gpu(unused_sampler_.data, 256))) {
        C3D_TexDelete(&unused_sampler_); unused_sampler_ = {};
        if(batch_ready_)shaderProgramFree(&batch_program_);
        batch_program_={};batch_ready_=false;
        if(affine_ready_)shaderProgramFree(&affine_program_);
        affine_program_={};affine_ready_=false;
        shaderProgramFree(&program_); DVLB_Free(shader_); shader_ = nullptr;
        return reject(Error::gpu_failure);
    }
    budget_ = budget; timings_ = timings; live_ = peak_ = vram_ = peak_vram_ = 0; account(256);
    // BUILD134 exact cost cuts: one mode for the whole session.
    cuts_=perf::cuts();
    lighting_cache_=LightingCache{};lighting_cache_.set_reference(!cuts_);
    sm64ds_native_uniform_residency(cuts_);
    C3D_TexEnvInit(&neutral_stage_);
    shadow_={};raw_state_=cuts_&&packed_draws_&&citro3d_shadow(shadow_,&unused_sampler_);
    bound_layout_=0;buffers_resident_=aux_units_pending_=aux_units_valid_=false;deltas_.count=0;
    std::printf("Native cost cuts: enabled=%u registerDeltas=%u (sdmc:/3ds/sm64ds/sin-recortes.txt = BUILD 133 paths)\n",
        unsigned(cuts_),unsigned(raw_state_));
    const auto* fence=std::getenv("SM64DS_NATIVE_PRESENT_FENCE");
    present_fence_=!fence||*fence!='0';
    std::printf("Native LCD pacing: pendingSwapFence=%u\n",unsigned(present_fence_));
    program_cache_valid_=true;ready_ = true; return Error::none;
}
// ---- BUILD134: second image built while the GPU draws the first ----------
// citro3d 1.7.1 (pinned): the context starts with its GX queue, and the frame
// API refuses to run outside C3D_FrameBegin/End, so the three frame calls a
// pass needs are restated here on public SDK functions.
extern "C" {
extern unsigned char __C3D_Context[];
bool C3Di_SplitFrame(u32** buffer,u32* words);
}
namespace {
constexpr std::size_t overlap_words=256*1024/4; // same capacity as C3D_Init
gxCmdQueue_s* sdk_queue() noexcept {return reinterpret_cast<gxCmdQueue_s*>(__C3D_Context);}
}
bool Citro3DBackend::overlap_available() noexcept {
    if(overlap_checked_)return overlap_ok_;
    overlap_checked_=true;
    const auto* queue=sdk_queue();
    // The SDK queue as C3D_Init leaves it; anything else is not the layout this code knows.
    const bool known=queue->maxEntries==32&&queue->entries&&queue->callback&&queue->numEntries<=32;
    if(known)overlap_buffer_=static_cast<std::uint32_t*>(linearAlloc(overlap_words*4));
    overlap_ok_=known&&overlap_buffer_;
    std::printf("Native overlapped second image: available=%u sdkQueue=%u buffer=%p\n",unsigned(overlap_ok_),unsigned(known),static_cast<void*>(overlap_buffer_));
    return overlap_ok_;
}
Error Citro3DBackend::begin_overlapped() noexcept {
    if(!ready_||state_.phase()!=FrameState::Phase::idle||!overlap_available())return reject(Error::invalid_phase);
    if(!gpu_usable())return reject(Error::gpu_failure);
    sdk_sample_phase_=(sdk_sample_phase_+1)&7u;
    stats_ = {}; stats_.live_linear_bytes = live_-vram_; stats_.peak_linear_bytes = peak_;
    stats_.live_vram_bytes=vram_;stats_.peak_vram_bytes=peak_vram_;
    // GX requests of this image (command lists, clears) are recorded, in
    // order; the SDK queue keeps running the first image meanwhile.
    overlap_queue_={};overlap_queue_.entries=overlap_entries_;overlap_queue_.maxEntries=32;
    GX_BindQueue(&overlap_queue_);
    GPUCMD_SetBuffer(overlap_buffer_,overlap_words,0);
    overlap_=true;
    state_.begin();
    stats_.previous_gpu_queue_ms = C3D_GetDrawingTime();
    active_colour_=nullptr;active_depth_=false;
    bound_mesh_ = {}; material_valid_ = matrix_valid_ = lighting_valid_ = general_uv_valid_ = vertex_material_valid_ = false;
    palette_matrix_valid_=palette_basis_valid_=0;lighting_cache_.invalidate();
    return Error::none;
}
Error Citro3DBackend::end_overlapped() noexcept {
    if(!overlap_)return reject(Error::invalid_phase);
    if(!gpu_usable()){state_.end();overlap_queue_.numEntries=0;return reject(Error::gpu_failure);}
    const bool ended=ready_&&state_.end();
    {Timed time(stats_.submit_ticks, timings_);
     u32* buffer=nullptr;u32 words=0;
     if(overlap_queue_.numEntries<overlap_queue_.maxEntries&&C3Di_SplitFrame(&buffer,&words))GX_ProcessCommandList(buffer,words*4,GX_CMDLIST_FLUSH);
     GPUCMD_SetBuffer(nullptr,0,0);
     GX_BindQueue(sdk_queue());overlap_=false;}
    return ended?Error::none:reject(Error::invalid_phase);
}
void Citro3DBackend::discard_overlapped() noexcept {overlap_queue_.numEntries=0;}
// BUILD137: hang watchdog hooks (weak: labs link the backend without it).
extern "C" void sm64ds_watchdog_worker_stage(unsigned) __attribute__((weak));
extern "C" void sm64ds_watchdog_gpu_stall(std::uint32_t) __attribute__((weak));
namespace {
void watch_stage(unsigned stage){if(sm64ds_watchdog_worker_stage)sm64ds_watchdog_worker_stage(stage);}
// Raises the calling thread to `raised` (when it runs below it) and restores
// its own previous priority afterwards: safe on the game and host threads too.
struct LockPriority {
    s32 saved=-1;
    explicit LockPriority(int raised){
        if(raised<0)return;s32 current=0;
        if(R_SUCCEEDED(svcGetThreadPriority(&current,CUR_THREAD_HANDLE))&&current>raised&&
           R_SUCCEEDED(svcSetThreadPriority(CUR_THREAD_HANDLE,raised)))saved=current;
    }
    ~LockPriority(){if(saved>=0)svcSetThreadPriority(CUR_THREAD_HANDLE,saved);}
    LockPriority(const LockPriority&)=delete;LockPriority& operator=(const LockPriority&)=delete;
};
// A present fence waits while GSP still owns the previous swap; GSP clears it
// at the next VBlank. At most one second (60 refreshes): a longer wait means
// the display stopped, which the watchdog records instead of hanging here.
void wait_present_fence(){
    watch_stage(SM64DS_WATCH_WORKER_PRESENT_WAIT);
    const auto start=svcGetSystemTick();
    while(gspIsPresentPending(GSP_SCREEN_TOP)||gspIsPresentPending(GSP_SCREEN_BOTTOM)){
        if(svcGetSystemTick()-start>SYSCLOCK_ARM11){if(sm64ds_watchdog_gpu_stall)sm64ds_watchdog_gpu_stall(1000);break;}
        gspWaitForAnyEvent();
    }
}
}
Error Citro3DBackend::submit_overlapped(C3D_RenderTarget* target,bool fence,int raised_priority,int normal_priority) noexcept {
    if(!ready_||overlap_||state_.phase()!=FrameState::Phase::idle||!target||!target->linked)return reject(Error::invalid_phase);
    if(!gpu_usable()){overlap_queue_.numEntries=0;return reject(Error::gpu_failure);}
    Timed time(stats_.wait_ticks, timings_);
    // BUILD140: the tail (GPU and LCD waits) may overlap the next game
    // update; its ends are measured at the raised priority, so the game
    // thread cannot stretch them.
    stats_.tail_begin_tick=svcGetSystemTick();
    __atomic_store_n(&second_tail_,true,__ATOMIC_RELEASE);
    const auto tail_end=[&](){stats_.tail_end_tick=svcGetSystemTick();stats_.blocked_ticks+=stats_.tail_end_tick-stats_.tail_begin_tick;};
    auto* queue=sdk_queue();
    // These waits are blocked time (the game's core stays free); the raised
    // priority only lets the few instructions between them run on time.
    if(raised_priority>=0)svcSetThreadPriority(CUR_THREAD_HANDLE,raised_priority);
    watch_stage(SM64DS_WATCH_WORKER_GPU_WAIT);
    // A bounded wait that gave up (hung GPU): leave without touching the queue.
    const auto give_up=[&](){overlap_queue_.numEntries=0;tail_end();if(raised_priority>=0)svcSetThreadPriority(CUR_THREAD_HANDLE,normal_priority);return reject(Error::gpu_failure);};
    if(!gxCmdQueueWait(queue,-1))return give_up(); // first image drawn; the SDK's callback swapped it in
    if(unsigned(queue->numEntries)+overlap_queue_.numEntries+1u>queue->maxEntries){
        gxCmdQueueStop(queue);gxCmdQueueClear(queue);gxCmdQueueRun(queue);
    }
    for(unsigned i=0;i<overlap_queue_.numEntries;++i)gxCmdQueueAdd(queue,&overlap_entries_[i]);
    overlap_queue_.numEntries=0;
    if(!gxCmdQueueWait(queue,-1))return give_up(); // this image drawn into its render target
    // The LCD still shows the buffer this image will be copied into until
    // the first image's swap happens: copy only after that refresh. (Checked
    // here, milliseconds after the SDK's queue callback requested that swap.)
    if(fence)wait_present_fence();
    watch_stage(SM64DS_WATCH_WORKER_GPU_WAIT);
    C3D_FrameBufTransfer(&target->frameBuf,target->screen,target->side,target->transferFlags);
    if(!gxCmdQueueWait(queue,-1))return give_up();
    gfxScreenSwapBuffers(target->screen,false);
    tail_end(); // still at the raised priority
    if(raised_priority>=0)svcSetThreadPriority(CUR_THREAD_HANDLE,normal_priority);
    return Error::none;
}
Error Citro3DBackend::begin(bool pace) noexcept {
    if (!ready_ || state_.phase() != FrameState::Phase::idle) return reject(Error::invalid_phase);
    if(!gpu_usable())return reject(Error::gpu_failure);
    sdk_sample_phase_=(sdk_sample_phase_+1)&7u;
    stats_ = {}; stats_.live_linear_bytes = live_-vram_; stats_.peak_linear_bytes = peak_;
    stats_.live_vram_bytes=vram_;stats_.peak_vram_bytes=peak_vram_;
    {
        Timed time(stats_.wait_ticks, timings_);
        // The SDK deletes targets only outside C3D frames and drains their
        // pending queue itself. Ordinary resources retire after FrameBegin.
        if(retirements_)textures_.each([&](Texture h,TextureResource& r){if(r.retired&&r.target)destroy(h);});
        // First drain the GPU queue: its completion callback publishes the
        // previous LCD swaps. Then wait only while GSP still owns those swaps.
        // SYNCDRAW always asks for a NEW VBlank at entry, discarding a refresh
        // already consumed during CPU preparation and quantizing pairs to 40Hz.
        watch_stage(SM64DS_WATCH_WORKER_FRAME_BEGIN);
        const auto blocked_start=svcGetSystemTick();
        LockPriority boost(lock_priority_); // BUILD137: GX queue lock (see set_lock_priority)
        if (!C3D_FrameBegin(pace&&!present_fence_ ? C3D_FRAME_SYNCDRAW : 0)) return reject(Error::gpu_failure);
        if(pace&&present_fence_)wait_present_fence();
        stats_.blocked_ticks+=svcGetSystemTick()-blocked_start;
        watch_stage(SM64DS_WATCH_WORKER_DRAW);
    }
    state_.begin();
    if(capture_.armed)capture_.base=gpuCmdBuf;
    if (retirements_) {
        meshes_.each([&](Mesh h, MeshResource& r) { if(r.retired) destroy(h); });
        textures_.each([&](Texture h, TextureResource& r) { if(r.retired) destroy(h); });
        retirements_=false;
    }
    stats_.previous_gpu_queue_ms = C3D_GetDrawingTime();
    active_colour_=nullptr;active_depth_=false;
    bound_mesh_ = {}; material_valid_ = matrix_valid_ = lighting_valid_ = general_uv_valid_ = vertex_material_valid_ = false;
    palette_matrix_valid_=palette_basis_valid_=0;lighting_cache_.invalidate();
    return Error::none;
}
void Citro3DBackend::account(std::size_t bytes,bool vram) noexcept {
    live_ += bytes;if(vram)vram_+=bytes;
    peak_=std::max(peak_,live_-vram_);peak_vram_=std::max(peak_vram_,vram_);
    stats_.live_linear_bytes=live_-vram_;stats_.peak_linear_bytes=peak_;
    stats_.live_vram_bytes=vram_;stats_.peak_vram_bytes=peak_vram_;
    ++stats_.resource_creates;
}
Error Citro3DBackend::create_mesh(const MeshView& source, Mesh& out) noexcept {
    if (!ready_ || !state_.can_mutate()) return reject(Error::invalid_phase);
    Timed time(stats_.upload_ticks, timings_);
    UvDomain domain;const auto check = validate(source,&domain);
    if (check != Error::none) return reject(check);
    return upload_mesh({reinterpret_cast<const std::uint8_t*>(source.vertices),source.vertex_count*sizeof(Vertex),
        reinterpret_cast<const std::uint8_t*>(source.indices),source.index_count*sizeof(std::uint16_t)},domain,out);
}
Error Citro3DBackend::create_mesh_bytes(const MeshBytes& source, Mesh& out) noexcept {
    if (!ready_ || !state_.can_mutate()) return reject(Error::invalid_phase);
    Timed time(stats_.upload_ticks, timings_);
    UvDomain domain;const auto check=validate(source,&domain);
    if (check != Error::none) return reject(check);
    return upload_mesh(source,domain,out);
}
Error Citro3DBackend::create_mesh_group(const MeshGroups& groups,unsigned index,Mesh& out) noexcept {
    if(!ready_||!state_.can_mutate())return reject(Error::invalid_phase);
    if(index>=groups.size())return reject(Error::invalid_data);
    Timed time(stats_.upload_ticks,timings_);
    const auto& g=groups.group(index);
    const MeshBytes shape{nullptr,g.vertex_bytes,nullptr,g.index_bytes,g.normals};
    return upload_mesh(shape,g.uv,out,&groups,index);
}
Error Citro3DBackend::create_quad_stream(std::size_t capacity,Mesh& out,bool palette) noexcept {
    if(!ready_||!state_.can_mutate())return reject(Error::invalid_phase);
    Timed time(stats_.upload_ticks,timings_);
    if(!capacity||capacity>max_frame_quads)return reject(Error::invalid_data);
    const auto handle=meshes_.available();if(!handle)return reject(Error::capacity);
    const auto stride=palette?sizeof(BatchVertex):sizeof(Vertex);
    const auto index_at=(capacity*4*stride+127)&~std::size_t(127);
    const auto bytes=index_at+((capacity*6*sizeof(std::uint16_t)+127)&~std::size_t(127));
    if(bytes>budget_-live_)return reject(Error::budget);
    MeshResource next;next.storage=linearMemAlign(bytes,128);if(!next.storage)return reject(Error::out_of_memory);
    std::memset(next.storage,0,index_at);
    auto* indices=reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(next.storage)+index_at);
    constexpr unsigned pattern[]={0,1,2,0,2,3};
    for(std::size_t q=0;q<capacity;++q)for(unsigned i=0;i<6;++i)indices[q*6+i]=std::uint16_t(q*4+pattern[i]);
    next.indices=indices;next.bytes=bytes;next.quad_capacity=capacity;next.batch=palette;BufInfo_Init(&next.binding);
    if(BufInfo_Add(&next.binding,next.storage,stride,palette?5:3,palette?0x43210:0x210)<0||
       R_FAILED(publish_for_gpu(next.storage,bytes))){linearFree(next.storage);return reject(Error::gpu_failure);}
    next.index_configuration=(osConvertVirtToPhys(indices)-next.binding.base_paddr)|0x80000000u;
    meshes_.commit(handle,next);out=handle;account(bytes);stats_.mesh_upload_bytes+=bytes;return Error::none;
}
Error Citro3DBackend::update_quad_stream(Mesh handle,const Vertex* vertices,std::size_t count,const std::uint8_t* selectors,const UvDomain* validated) noexcept {
    if(!ready_||!state_.can_mutate())return reject(Error::invalid_phase);
    Timed time(stats_.upload_ticks,timings_);
    auto* mesh=meshes_.get(handle);if(!mesh||mesh->retired)return reject(Error::stale_handle);
    if(!mesh->quad_capacity||count>mesh->quad_capacity*4)return reject(Error::invalid_data);
    UvDomain domain;
    if(validated)domain=*validated; // BUILD134: the frame boundary already read every vertex
    else{const auto error=validate_quads(vertices,count,&domain);if(error!=Error::none)return reject(error);}
    if(selectors){if(!mesh->batch)return reject(Error::invalid_data);
        for(std::size_t i=0;i<count;++i)if(selectors[i]>=8)return reject(Error::invalid_data);}
    const auto bytes=count*(mesh->batch?sizeof(BatchVertex):sizeof(Vertex));
    if(mesh->batch){auto* output=static_cast<BatchVertex*>(mesh->storage);
        for(std::size_t i=0;i<count;++i){output[i].source={vertices[i],0,0,0,0};
            const auto rows=packed_palette_rows(selectors?selectors[i]:0);
            std::memcpy(output[i].rows.data(),&rows,sizeof(rows));}}
    else std::memcpy(mesh->storage,vertices,bytes);
    if(R_FAILED(publish_for_gpu(mesh->storage,bytes))){mesh->count=0;return reject(Error::gpu_failure);}
    mesh->count=count/4*6;mesh->vertex_bytes=bytes;mesh->uv=domain;stats_.mesh_upload_bytes+=bytes;return Error::none;
}
Error Citro3DBackend::create_skin_stream(std::size_t capacity,Mesh& out,bool palette) noexcept {
    if(!ready_||!state_.can_mutate())return reject(Error::invalid_phase);
    if(!capacity||capacity>65535||capacity%3)return reject(Error::invalid_data);
    const auto handle=meshes_.available();if(!handle)return reject(Error::capacity);
    const auto stride=palette?sizeof(BatchVertex):sizeof(NormalVertex);
    const auto index_at=(capacity*stride+127)&~std::size_t(127);
    const auto bytes=index_at+((capacity*sizeof(std::uint16_t)+127)&~std::size_t(127));
    if(bytes>budget_-live_)return reject(Error::budget);
    MeshResource next;next.storage=linearMemAlign(bytes,128);if(!next.storage)return reject(Error::out_of_memory);
    std::memset(next.storage,0,index_at);
    auto* indices=reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(next.storage)+index_at);
    for(std::size_t i=0;i<capacity;++i)indices[i]=std::uint16_t(i);
    next.indices=indices;next.bytes=bytes;next.quad_capacity=capacity;next.normals=true;next.batch=palette;BufInfo_Init(&next.binding);
    if(BufInfo_Add(&next.binding,next.storage,stride,palette?5:4,palette?0x43210:0x3210)<0||
       R_FAILED(publish_for_gpu(next.storage,bytes))){linearFree(next.storage);return reject(Error::gpu_failure);}
    next.index_configuration=(osConvertVirtToPhys(indices)-next.binding.base_paddr)|0x80000000u;
    meshes_.commit(handle,next);out=handle;account(bytes);stats_.mesh_upload_bytes+=bytes;return Error::none;
}
Error Citro3DBackend::update_skin_stream(Mesh handle,const NormalVertex* vertices,std::size_t count,const std::uint8_t* selectors) noexcept {
    if(!ready_||!state_.can_mutate())return reject(Error::invalid_phase);
    // BUILD134: this scope was missing, so the reported upload time left the
    // skin stream out (the quad stream above always had it).
    Timed time(stats_.upload_ticks,timings_);
    auto* mesh=meshes_.get(handle);if(!mesh||mesh->retired||!mesh->normals)return reject(Error::stale_handle);
    if(count>mesh->quad_capacity||count%3||(count&&!vertices))return reject(Error::invalid_data);
    UvDomain domain;mesh->batch_source_valid=true;
    // The stream owns immutable identity indices; do not decode/rescan them
    // through the unaligned file loader on every pose. Validate current typed
    // vertex values once, retaining the existing lenient fallback semantics.
    if(!cuts_&&count&&validate(vertices,count,&domain)!=Error::none){domain={};mesh->batch_source_valid=false;}
    if(selectors){if(!mesh->batch)return reject(Error::invalid_data);
        for(std::size_t i=0;i<count;++i)if(selectors[i]>=8)return reject(Error::invalid_data);}
    const auto bytes=count*(mesh->batch?sizeof(BatchVertex):sizeof(NormalVertex));
    if(cuts_){
        // BUILD134: validate while writing the GPU records, one read of the
        // vertices instead of two (same checks, same domain, same bytes).
        const bool valid=mesh->batch?pack_skin_vertices(vertices,count,selectors,static_cast<BatchVertex*>(mesh->storage),domain):
            copy_skin_vertices(vertices,count,static_cast<NormalVertex*>(mesh->storage),domain);
        if(!valid){domain={};mesh->batch_source_valid=false;}
    }else if(mesh->batch){auto* output=static_cast<BatchVertex*>(mesh->storage);
        for(std::size_t i=0;i<count;++i){output[i].source=vertices[i];
            const auto rows=packed_palette_rows(selectors?selectors[i]:0);
            std::memcpy(output[i].rows.data(),&rows,sizeof(rows));}}
    else if(bytes)std::memcpy(mesh->storage,vertices,bytes);
    if(bytes&&R_FAILED(publish_for_gpu(mesh->storage,bytes))){mesh->count=0;return reject(Error::gpu_failure);}
    mesh->count=count;mesh->vertex_bytes=bytes;mesh->uv=domain;stats_.mesh_upload_bytes+=bytes;return Error::none;
}
Error Citro3DBackend::create_resident_batch(const BatchGeometry& geometry,unsigned first,unsigned parts,Mesh& output,BatchRange* ranges) noexcept {
    if(!can_upload()||!batch_ready_)return reject(Error::invalid_phase);
    if(!parts||parts>8||first>geometry.parts()||parts>geometry.parts()-first||!ranges)return reject(Error::invalid_data);
    std::size_t vertices=0,indices=0;
    for(unsigned p=0;p<parts;++p){const auto source=geometry.source(first+p);
        if(source.vertex_count()>65535-vertices||source.index_count()>65535-indices)return reject(Error::capacity);
        vertices+=source.vertex_count();indices+=source.index_count();}
    const auto handle=meshes_.available();if(!handle)return reject(Error::capacity);
    const auto index_at=(vertices*sizeof(BatchVertex)+127)&~std::size_t(127);
    const auto bytes=index_at+((indices*2+127)&~std::size_t(127));
    if(bytes>budget_-live_)return reject(Error::budget);
    MeshResource next;next.storage=linearMemAlign(bytes,128);if(!next.storage)return reject(Error::out_of_memory);
    auto* output_vertices=static_cast<BatchVertex*>(next.storage);
    auto* output_indices=reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(next.storage)+index_at);
    std::size_t v=0,n=0;UvDomain domain;
    for(unsigned p=0;p<parts;++p){const auto source=geometry.source(first+p);
        std::memcpy(output_vertices+v,source.vertices(),source.vertex_count()*sizeof(BatchVertex));
        for(std::size_t i=0;i<source.vertex_count();++i){auto& vertex=output_vertices[v+i];
            for(unsigned row=0;row<4;++row)vertex.rows[row]=std::uint8_t(p*4+row);
            domain.include(vertex.source.base.u,vertex.source.base.v);}
        for(std::size_t i=0;i<source.index_count();++i)output_indices[n+i]=std::uint16_t(v+source.indices()[i]);
        ranges[p]={handle,std::uint32_t(n),std::uint32_t(source.index_count()),std::uint8_t(p)};
        v+=source.vertex_count();n+=source.index_count();
    }
    next.bytes=bytes;next.vertex_bytes=vertices*sizeof(BatchVertex);next.count=indices;
    next.indices=output_indices;next.uv=domain;next.batch=next.normals=true;BufInfo_Init(&next.binding);
    if(BufInfo_Add(&next.binding,next.storage,sizeof(BatchVertex),5,0x43210)<0||R_FAILED(publish_for_gpu(next.storage,bytes))){
        linearFree(next.storage);for(unsigned p=0;p<parts;++p)ranges[p]={};return reject(Error::gpu_failure);}
    next.index_configuration=(osConvertVirtToPhys(next.indices)-next.binding.base_paddr)|0x80000000u;
    meshes_.commit(handle,next);output=handle;account(bytes);stats_.mesh_upload_bytes+=bytes;return Error::none;
}
Error Citro3DBackend::draw_batch(const NativeBatchPlan& plan,unsigned index) noexcept {
    const auto* span=plan.span(index);if(!span)return reject(Error::invalid_phase);
    const LightingInput input{span->lighting.mask?span->lights:nullptr,{},span->lighting};
    // A palette draw takes its matrices from the span; this one is never read.
    static constexpr Matrix unused{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    return draw_impl(span->mesh,span->material,unused,span->first,span->count,span->lit?&input:nullptr,span);
}
Error Citro3DBackend::upload_mesh(const MeshBytes& source,const UvDomain& domain,Mesh& out,const MeshGroups* groups,unsigned group) noexcept {
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "PICA native payload byte order");
    const auto handle = meshes_.available();
    if (!handle) return reject(Error::capacity);
    const std::size_t index_offset=(source.vertex_bytes+127)&~std::size_t(127);
    const std::size_t bytes=index_offset+((source.index_bytes+127)&~std::size_t(127));
    if (bytes > budget_-live_) return reject(Error::budget);
    MeshResource next{};
    next.storage = linearMemAlign(bytes, 128);
    if (!next.storage) return reject(Error::out_of_memory);
    auto* indices = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(next.storage)+index_offset);
    if(groups)groups->copy(group,next.storage,indices);
    else{
        std::memcpy(next.storage, source.vertices, source.vertex_bytes);
        std::memcpy(indices, source.indices, source.index_bytes);
    }
    next.indices = indices; next.count = source.index_bytes/2; next.bytes = bytes;next.vertex_bytes=source.vertex_bytes;next.uv=domain;next.normals=source.normals;
    BufInfo_Init(&next.binding);
    if (BufInfo_Add(&next.binding,next.storage,source.normals?sizeof(NormalVertex):sizeof(Vertex),source.normals?4:3,source.normals?0x3210:0x210) < 0 ||
        R_FAILED(publish_for_gpu(next.storage, bytes))) {
        linearFree(next.storage); return reject(Error::gpu_failure);
    }
    next.index_configuration=(osConvertVirtToPhys(indices)-next.binding.base_paddr)|0x80000000u;
    meshes_.commit(handle, next); out = handle; account(bytes);
    stats_.mesh_upload_bytes += bytes;
    return Error::none;
}
Error Citro3DBackend::create_texture(unsigned w, unsigned h, const void* data, std::size_t bytes, Texture& out,TextureFormat format) noexcept {
    if (!ready_ || !state_.can_mutate()) return reject(Error::invalid_phase);
    Timed time(stats_.upload_ticks, timings_);
    const auto expected=texture_bytes(w,h,format);
    if (!data || !expected || bytes!=expected) return reject(Error::invalid_data);
    const auto handle = textures_.available();
    if (!handle) return reject(Error::capacity);
    if (bytes > budget_-live_) return reject(Error::budget);
    TextureResource next{};
    if (!C3D_TexInit(&next.gpu,w,h,format==TextureFormat::alpha4?GPU_A4:GPU_RGBA8)) return reject(Error::out_of_memory);
    next.bytes = bytes;
    std::memcpy(next.gpu.data, data, bytes);
    C3D_TexSetFilter(&next.gpu, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&next.gpu, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    if (R_FAILED(publish_for_gpu(next.gpu.data, bytes))) {
        C3D_TexDelete(&next.gpu); return reject(Error::gpu_failure);
    }
    textures_.commit(handle, next); out = handle; account(bytes);
    stats_.texture_upload_bytes += bytes;
    return Error::none;
}
Error Citro3DBackend::upload_interface(const InterfaceImage& image,Texture& handle,InterfaceCrop& crop) noexcept {
    if(!ready_||!state_.can_mutate())return reject(Error::invalid_phase);
    Timed time(stats_.upload_ticks,timings_);
    InterfaceCrop next;auto error=interface_crop(image,next);if(error!=Error::none)return reject(error);
    auto* resource=handle?textures_.get(handle):nullptr;
    if(handle&&(!resource||resource->retired))return reject(Error::stale_handle);
    if(resource&&(resource->target||resource->gpu.fmt!=GPU_RGBA8))return reject(Error::invalid_data);
    if(resource&&(next.empty()||resource->gpu.width!=next.texture_width||resource->gpu.height!=next.texture_height)){
        error=destroy(handle);if(error!=Error::none)return error;handle={};resource=nullptr;
    }
    if(next.empty()){crop=next;return Error::none;}
    const auto bytes=std::size_t(next.texture_width)*next.texture_height*4;
    TextureResource fresh;Texture reserved{};
    if(!resource){
        reserved=textures_.available();if(!reserved)return reject(Error::capacity);
        if(bytes>budget_-live_)return reject(Error::budget);
        if(!C3D_TexInit(&fresh.gpu,next.texture_width,next.texture_height,GPU_RGBA8))return reject(Error::out_of_memory);
        fresh.bytes=bytes;C3D_TexSetFilter(&fresh.gpu,GPU_NEAREST,GPU_NEAREST);
        C3D_TexSetWrap(&fresh.gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);resource=&fresh;
    }
    error=tile_interface(image,next,resource->gpu.data,bytes);
    if(error==Error::none&&R_FAILED(publish_for_gpu(resource->gpu.data,bytes)))error=Error::gpu_failure;
    if(error!=Error::none){if(reserved)C3D_TexDelete(&fresh.gpu);return reject(error);}
    if(reserved){textures_.commit(reserved,fresh);handle=reserved;account(bytes);}
    crop=next;stats_.texture_upload_bytes+=bytes;return Error::none;
}
Error Citro3DBackend::upload_frame_image(const std::uint32_t* argb,Texture& handle,bool sharp,std::uint32_t* shadow,bool* changed_out,std::uint32_t candidates) noexcept {
    if(!ready_||!state_.can_mutate())return reject(Error::invalid_phase);
    if(!argb)return reject(Error::invalid_data);
    Timed time(stats_.upload_ticks,timings_);
    // Sharp sampling (same as the maintained renderer): a 2x nearest copy
    // (512x384 in 512x512) sampled with LINEAR filtering when scaled 1.25x.
    const unsigned texture=sharp?512u:256u;const std::size_t bytes=std::size_t(texture)*texture*4;
    auto* resource=handle?textures_.get(handle):nullptr;
    if(handle&&(!resource||resource->retired||resource->target||resource->gpu.fmt!=GPU_RGBA8||resource->gpu.width!=texture))
        return reject(Error::stale_handle);
    const bool fresh_resource=!resource;
    if(!resource){
        const auto reserved=textures_.available();if(!reserved)return reject(Error::capacity);
        if(bytes>budget_-live_)return reject(Error::budget);
        TextureResource fresh;
        if(!C3D_TexInit(&fresh.gpu,texture,texture,GPU_RGBA8))return reject(Error::out_of_memory);
        fresh.bytes=bytes;const auto filter=sharp?GPU_LINEAR:GPU_NEAREST;C3D_TexSetFilter(&fresh.gpu,filter,filter);
        C3D_TexSetWrap(&fresh.gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        std::memset(fresh.gpu.data,0,bytes);
        textures_.commit(reserved,fresh);handle=reserved;account(bytes);resource=textures_.get(handle);
    }
    auto* words=static_cast<std::uint32_t*>(resource->gpu.data);
    if(shadow&&!fresh_resource){
        // Re-tile and publish only the 8-row bands that differ from the
        // pixels this texture already holds (the caller's shadow copy). A
        // HUD digit or the map arrow no longer rewrites the whole plane.
        constexpr std::size_t band_pixels=std::size_t(256)*frame_band_rows;
        unsigned run=frame_bands;std::size_t published=0;
        const auto publish=[&](unsigned first,unsigned end)->bool{
            // Higher bands sit lower in memory: [band(end-1), band(first)+words).
            const auto low=frame_band_word(sharp,end-1),high=frame_band_word(sharp,first)+frame_band_words(sharp);
            published+=(high-low)*4;
            return R_SUCCEEDED(publish_for_gpu(words+low,(high-low)*4));
        };
        for(unsigned band=0;band<=frame_bands;++band){
            const bool changed=band<frame_bands&&((candidates>>band)&1u)&&
                std::memcmp(argb+band*band_pixels,shadow+band*band_pixels,band_pixels*4)!=0;
            if(changed){
                const auto row=band*frame_band_rows;
                if(sharp)tile_frame_rows_2x(argb,words,row,row+frame_band_rows);else tile_frame_rows_1x(argb,words,row,row+frame_band_rows);
                std::memcpy(shadow+band*band_pixels,argb+band*band_pixels,band_pixels*4);
                if(run==frame_bands)run=band;
            }else if(run!=frame_bands){if(!publish(run,band))return reject(Error::gpu_failure);run=frame_bands;}
        }
        stats_.texture_upload_bytes+=published;if(changed_out)*changed_out=published!=0;return Error::none;
    }
    if(sharp)tile_frame_image_2x(argb,words);
    else tile_frame_image_1x(argb,words);
    if(shadow)std::memcpy(shadow,argb,std::size_t(256)*192*4);
    // Bottom 128 texture rows are initialized once and never rewritten.
    const std::size_t padding=std::size_t(texture)*(texture-(sharp?384u:192u))*4;
    const auto offset=fresh_resource?0:padding,changed=bytes-offset;
    if(R_FAILED(publish_for_gpu(static_cast<std::uint8_t*>(resource->gpu.data)+offset,changed)))return reject(Error::gpu_failure);
    stats_.texture_upload_bytes+=changed;if(changed_out)*changed_out=true;return Error::none;
}
Error Citro3DBackend::upload_map_image(const std::uint32_t* image,unsigned width,unsigned height,Texture& handle) noexcept {
    if(!ready_||!state_.can_mutate())return reject(Error::invalid_phase);
    if(!image||!texture_dimensions(width,height)||width>512||height>512)return reject(Error::invalid_data);
    Timed time(stats_.upload_ticks,timings_);
    auto* resource=handle?textures_.get(handle):nullptr;
    if(handle&&(!resource||resource->retired||resource->target))return reject(Error::stale_handle);
    if(resource&&(resource->gpu.width!=width||resource->gpu.height!=height)){
        const auto error=destroy(handle);if(error!=Error::none)return error;handle={};resource=nullptr;
    }
    const std::size_t bytes=std::size_t(width)*height*4;
    if(!resource){
        const auto slot=textures_.available();if(!slot)return reject(Error::capacity);
        if(bytes>budget_-live_)return reject(Error::budget);
        TextureResource next;
        if(!C3D_TexInit(&next.gpu,width,height,GPU_RGBA8))return reject(Error::out_of_memory);
        next.bytes=bytes;next.gpu.border=0;
        C3D_TexSetFilter(&next.gpu,GPU_NEAREST,GPU_NEAREST);
        textures_.commit(slot,next);handle=slot;account(bytes);resource=textures_.get(handle);
    }
    auto* tiled=static_cast<std::uint32_t*>(resource->gpu.data);
    // BUILD142: one 8-texel run per tile row with a Morton table (a 512x512
    // layer is 1 MiB; the per-texel index math cost the Old 3DS several ms).
    // Same texel order as the per-texel formula.
    static const auto order=[](){std::array<std::uint8_t,64> t{};
        for(unsigned gy=0;gy<8;++gy)for(unsigned x=0;x<8;++x)
            t[gy*8+x]=std::uint8_t((x&1)|((gy&1)<<1)|((x&2)<<1)|((gy&2)<<2)|((x&4)<<2)|((gy&4)<<3));
        return t;}();
    const unsigned tiles_x=width/8;
    for(unsigned gy=0;gy<height;++gy){
        const auto* row=image+std::size_t(height-1-gy)*width;
        const auto* m=&order[(gy&7)*8];
        auto* out=tiled+std::size_t(gy/8)*tiles_x*64;
        for(unsigned tx=0;tx<tiles_x;++tx,row+=8,out+=64){
            for(unsigned k=0;k<8;++k){const auto pixel=row[k];out[m[k]]=(pixel<<8)|(pixel>>24);}
        }
    }
    if(R_FAILED(publish_for_gpu(resource->gpu.data,bytes)))return reject(Error::gpu_failure);
    stats_.texture_upload_bytes+=bytes;return Error::none;
}
Error Citro3DBackend::destroy(Mesh handle) noexcept {
    if (!ready_ || !state_.can_mutate()) return reject(Error::invalid_phase);
    auto* mesh = meshes_.get(handle);
    if (!mesh) return reject(Error::stale_handle);
    live_ -= mesh->bytes; linearFree(mesh->storage); meshes_.release(handle);
    stats_.live_linear_bytes = live_-vram_; ++stats_.resource_destroys; return Error::none;
}
Error Citro3DBackend::create_surface(unsigned width,unsigned height,bool depth,Texture& out,bool linear,bool vram) noexcept {
    if(!ready_||state_.phase()!=FrameState::Phase::idle)return reject(Error::invalid_phase);
    Timed time(stats_.upload_ticks,timings_);
    if(!texture_dimensions(width,height))return reject(Error::invalid_data);
    const auto handle=textures_.available();if(!handle)return reject(Error::capacity);
    const std::size_t bytes=std::size_t(width)*height*(depth?8:4);
    if(bytes>budget_-live_)return reject(Error::budget);
    TextureResource next;
    if(!(vram?C3D_TexInitVRAM(&next.gpu,width,height,GPU_RGBA8):C3D_TexInit(&next.gpu,width,height,GPU_RGBA8)))return reject(Error::out_of_memory);
    C3D_TexSetFilter(&next.gpu,linear?GPU_LINEAR:GPU_NEAREST,linear?GPU_LINEAR:GPU_NEAREST);
    C3D_TexSetWrap(&next.gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    next.target=C3D_RenderTargetCreateFromTex(&next.gpu,GPU_TEXFACE_2D,0,
        depth?C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8):C3D_DEPTHTYPE(-1));
    if(!next.target){C3D_TexDelete(&next.gpu);return reject(Error::out_of_memory);}
    next.bytes=bytes;next.vram=vram;textures_.commit(handle,next);out=handle;account(bytes,vram);target_surface_count_=0;return Error::none;
}
C3D_RenderTarget* Citro3DBackend::surface(Texture handle) const noexcept {
    const auto* resource=ready_?textures_.get(handle):nullptr;
    return resource&&!resource->retired?resource->target:nullptr;
}
Error Citro3DBackend::destroy(Texture handle) noexcept {
    if(!ready_||state_.can_draw())return reject(Error::invalid_phase);
    auto* texture = textures_.get(handle);
    if(state_.phase()==FrameState::Phase::idle){
        if(!texture||!texture->target)return reject(Error::invalid_phase);
    }else if(texture&&texture->target)return reject(Error::invalid_phase);
    if (!texture) return reject(Error::stale_handle);
    if(texture->target){
        target_surface_count_=0;
        if(active_colour_==texture->gpu.data){active_colour_=nullptr;active_depth_=false;}
        // A hung GPU may still write the target: leak it rather than free it.
        if(!gpu_usable())return reject(Error::gpu_failure);
        C3D_RenderTargetDelete(texture->target);if(texture->vram)vram_-=texture->bytes;
    }
    live_ -= texture->bytes; C3D_TexDelete(&texture->gpu); textures_.release(handle);
    stats_.live_linear_bytes=live_-vram_;stats_.live_vram_bytes=vram_;
    ++stats_.resource_destroys;return Error::none;
}
Error Citro3DBackend::retire(Mesh handle) noexcept {
    // shutdown() already releases every resource; a surviving asset can then
    // discard its metadata without issuing any SDK work.
    if (!ready_) return Error::none;
    auto* mesh=meshes_.get(handle);
    if (!mesh) return reject(Error::stale_handle);
    mesh->retired=true;retirements_=true;return Error::none;
}
Error Citro3DBackend::retire(Texture handle) noexcept {
    if (!ready_) return Error::none;
    auto* texture=textures_.get(handle);
    if (!texture) return reject(Error::stale_handle);
    texture->retired=true;retirements_=true;return Error::none;
}
Error Citro3DBackend::pass(C3D_RenderTarget* target, bool clear, std::uint32_t rgba,const Viewport* area) noexcept {
    if(!gpu_usable())return reject(Error::gpu_failure); // BUILD137: no GX clear while HOME owns the GPU
    if (!ready_ || !target || !target->frameBuf.colorBuf ||
        state_.phase() == FrameState::Phase::idle) return reject(Error::invalid_phase);
    if(target->frameBuf.colorFmt!=GPU_RB_RGBA8&&target->frameBuf.colorFmt!=GPU_RB_RGB8)return reject(Error::unsupported);
    if(area&&(!area->width||!area->height||area->x>target->frameBuf.width||area->y>target->frameBuf.height||
       area->width>target->frameBuf.width-area->x||area->height>target->frameBuf.height-area->y))return reject(Error::invalid_data);
    TextureResource* surface_resource=nullptr;bool known=false;
    for(unsigned i=0;i<target_surface_count_&&!known;++i)
        if(target_surfaces_[i].target==target){surface_resource=target_surfaces_[i].resource;known=true;}
    if(!known){
        textures_.each([&](Texture,TextureResource& r){if(r.target==target)surface_resource=&r;});
        if(target_surface_count_<8)target_surfaces_[target_surface_count_++]={target,surface_resource};
    }
    if(surface_resource){
        if(surface_resource->retired)return reject(Error::stale_handle);
        if(!surface_resource->initialized&&!clear)return reject(Error::invalid_data);
    }
    Timed time(stats_.submit_ticks, timings_);
    // A clear queues a memory fill immediately. First publish any preceding
    // draw fragment, even when the next pass reuses the same render target.
    if(overlap_){
        // Recorded image: room for this pass's command list and its clear.
        if(overlap_queue_.numEntries+3u>overlap_queue_.maxEntries)return reject(Error::capacity);
        if(clear&&state_.can_draw()){u32* buffer=nullptr;u32 words=0;
            if(C3Di_SplitFrame(&buffer,&words))GX_ProcessCommandList(buffer,words*4,GX_CMDLIST_FLUSH);}
        target->used=true;C3D_SetFrameBuf(&target->frameBuf);
        C3D_SetViewport(0,0,target->frameBuf.width,target->frameBuf.height);
    }else{
    if (clear && state_.can_draw()) C3D_FrameSplit(GX_CMDLIST_FLUSH);
    if (!C3D_FrameDrawOn(target)) return reject(Error::gpu_failure);
    }
    if(capture_.armed&&!overlap_)++capture_.passes; // BUILD135: a dump describes one normal frame
    // BUILD134: the first pass of an image states everything, as BUILD 133
    // did for every pass; a later pass skips what provably has not changed.
    const bool first_pass=state_.can_mutate(),restate=!raw_state_||first_pass;
    state_.pass();
    if(area)C3D_SetViewport(area->x,area->y,area->width,area->height);
    active_colour_=target->frameBuf.colorBuf;active_depth_=target->frameBuf.depthBuf!=nullptr;
    if (clear) C3D_RenderTargetClear(target,active_depth_?C3D_CLEAR_ALL:C3D_CLEAR_COLOR,
        target->frameBuf.colorFmt==GPU_RB_RGB8?rgba>>8:rgba,0);
    // Keep the last entry point across passes/frames. Rebinding the general
    // kernel here would force a redundant switch before every sprite pass.
    // Binding the program already bound made the SDK send its 12 constants
    // and the boolean word again (74 command words). With the cuts they are
    // sent once per image, here; native_uniforms.cpp skips them afterwards
    // while the GPU still holds them, also across batch/rigid switches.
    if(cuts_&&first_pass)sm64ds_native_uniform_forget();
    auto* program=batch_bound_?&batch_program_:affine_bound_?&affine_program_:&program_;C3D_BindProgram(program);++stats_.shader_binds;
    if(!citro3d_program_cached(program)){
        // Do not submit against or later free a program whose SDK cache ABI
        // cannot be established. Keep its metadata alive until process exit.
        program_cache_valid_=false;return reject(Error::unsupported);
    }
    auto* attributes = C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributes, 2, GPU_UNSIGNED_BYTE, 4);
    // Stages 2..5 are only ever neutral: touching one makes the SDK send it
    // again (6 words each). Stages 0/1 are rewritten by the first draw anyway.
    for (unsigned i = 0; i < 6; ++i)
        if(restate||i<2||std::memcmp(shadow_.stage+i,&neutral_stage_,sizeof(C3D_TexEnv)))C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both, 0);
    C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);fog_ready_=false;
    C3D_LightEnvBind(nullptr);
    C3D_FragOpMode(GPU_FRAGOPMODE_GL);
    // SDK 1.7.1 dereferences unit 1/2 descriptors before checking for null.
    // These valid neutral bindings are not sampled by the rigid shader's TEV.
    // BUILD134: a later pass decides at its first draw, where BUILD 133's
    // bind was emitted, whether the units need sending again (draw_impl).
    if(restate){C3D_TexBind(1, &unused_sampler_); C3D_TexBind(2, &unused_sampler_);}
    aux_units_pending_=raw_state_;
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0xff, 0xff);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    bound_shadow_stage_=0;
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    C3D_DepthMap(true, -1.0f, 0.0f);
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_EarlyDepthTest(false, GPU_EARLYDEPTH_GREATER, 0);
    bound_mesh_ = {}; material_valid_ = matrix_valid_ = lighting_valid_ = general_uv_valid_ = vertex_material_valid_ = false;
    palette_matrix_valid_=palette_basis_valid_=0;lighting_cache_.invalidate();
    bound_layout_=3;buffers_resident_=false;deltas_.count=0;
    if(surface_resource&&clear)surface_resource->initialized=true;
    return Error::none;
}
Error Citro3DBackend::draw(Mesh handle, const Material& material, const Matrix& mvp,
                          std::size_t first, std::size_t count,const LightingInput* lighting) noexcept {
    return draw_impl(handle,material,mvp,first,count,lighting,nullptr);
}
void Citro3DBackend::fog(const float* table,std::uint32_t colour) noexcept {
    if(!ready_||!state_.can_draw())return;
    fog_ready_=table!=nullptr;material_valid_=false; // the next draw selects its fog mode again
    if(!table)return;
    FogLut_FromArray(&fog_table_,table);C3D_FogLutBind(&fog_table_);C3D_FogColor(colour);
}
Error Citro3DBackend::draw_shadow(Mesh handle,const Matrix& mvp,bool mask,std::uint8_t reference) noexcept {
    if(!active_depth_||!reference)return reject(Error::invalid_data);
    // Untextured: the vertex colour. The alpha test runs before the stencil,
    // and every volume's alpha is above zero.
    Material material;material.blend=Blend::alpha;material.depth_write=false;
    material.cull=mask?Cull::front:Cull::back;
    shadow_stage_=mask?1:2;shadow_reference_=reference;
    const auto error=draw_impl(handle,material,mvp,0,0,nullptr,nullptr);
    shadow_stage_=0;
    return error;
}
void Citro3DBackend::upload_matrix(unsigned location,const Matrix& matrix) noexcept {
    auto* target=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,location,4);
    for(unsigned row=0;row<4;++row){
        const auto& r=matrix.row[row];
        target[row]=FVec4_New(r[0],r[1],r[2],r[3]);
    }
    if(eye_!=0){
        // Horizontal is the second clip row on the portrait render target.
        // Far geometry separates by 3.6 pixels per eye at full slider.
        float x[4];
        for(unsigned c=0;c<4;++c)x[c]=matrix.row[1][c]+eye_*(.018f*matrix.row[3][c]+.144f*matrix.row[2][c]);
        target[1]=FVec4_New(x[0],x[1],x[2],x[3]);
    }
}
Error Citro3DBackend::draw_impl(Mesh handle,const Material& material,const Matrix& mvp,
    std::size_t first,std::size_t count,const LightingInput* lighting,const BatchSpan* batch) noexcept {
    if (!ready_ || !state_.can_draw()) return reject(Error::invalid_phase);
    // Keep room for a complete dirty-context/shader upload plus finalization.
    // The caller's command allocation is finite. Refuse before any GPU writes
    // instead of overflowing it; scene presentation can discard this target.
    if(gpuCmdBufOffset>gpuCmdBufSize||gpuCmdBufSize-gpuCmdBufOffset<8192)return reject(Error::capacity);
    Timed time(stats_.submit_ticks, timings_);
    deltas_.count=0; // nothing below may return between a register delta and its draw
    auto* mesh = meshes_.get(handle);
    if (!mesh || mesh->retired) return reject(Error::stale_handle);
    if(batch&&(!mesh->batch||!mesh->batch_source_valid))return reject(Error::invalid_data);
    auto* texture = material.texture ? textures_.get(material.texture) : nullptr;
    if (material.texture && (!texture || texture->retired)) return reject(Error::stale_handle);
    if(texture&&texture->target&&!texture->initialized)return reject(Error::invalid_data);
    if(texture&&texture->gpu.data==active_colour_)return reject(Error::invalid_phase);
    if(!active_depth_&&(material.depth_test||material.depth_write))return reject(Error::invalid_data);
    if (!count) count = mesh->count;
    if (!count || (!batch&&(!valid(material) || !finite(mvp))) || first > mesh->count ||
        count > mesh->count-first || first%3 || count%3 ||
        (material.uv_mode==UvMode::fixed && (!texture||!mesh->uv.accepts(material.fixed_uv.width,material.fixed_uv.height))) ||
        (material.uv_mode==UvMode::normal && (!texture||!normal_uv_compatible(handle,material.normal_uv()))))
        return reject(Error::invalid_data);
    if(lighting&&!mesh->normals)return reject(Error::invalid_data);
    const bool affine=!batch&&affine_ready_&&!lighting&&material.uv_mode==UvMode::affine;
    const bool change_program=bool(batch)!=batch_bound_||affine!=affine_bound_;
    // The batch palette aliases ordinary matrix/normal/UV rows. Invalidate
    // residency before preparing changes, so this draw restores every alias.
    if(change_program&&(batch||batch_bound_)){
        lighting_cache_.invalidate();palette_matrix_valid_=palette_basis_valid_=0;
    }
    LightingChanges light_changes;
    if(lighting){
        const auto error=lighting_cache_.prepare(*lighting,light_changes,bool(batch));
        if(error!=Error::none)return reject(error);
    }
    // Validate the complete draw before changing cached GPU state.
    if(change_program){
        auto* program=batch?&batch_program_:affine?&affine_program_:&program_;C3D_BindProgram(program);
        // BUILD134: prepare() above marked this draw's rows resident; they
        // were never uploaded.
        if(!citro3d_program_cached(program)){program_cache_valid_=false;lighting_cache_.invalidate();return reject(Error::unsupported);}
        // Binding a vertex program aliases float rows, not the fragment
        // sampler/TEV/depth/blend state. Restore only its vertex uniforms.
        if(batch||batch_bound_)vertex_material_valid_=matrix_valid_=lighting_valid_=general_uv_valid_=false;
        batch_bound_=bool(batch);affine_bound_=affine;++stats_.shader_binds;
        // Shared uniform locations and nonoverlapping constants were checked
        // at init. Neither entry has default bool/int writes. Preserve the
        // lighting values across sprite draws instead of repacking/reuploading.
    }
    // The short entry does not consume these bools. Its material can change
    // while the general entry's UV controls remain untouched. Track the actual
    // general-kernel state independently, including when the next lit affine
    // material is identical to the intervening unlit affine material.
    if(!batch&&!affine&&(!general_uv_valid_||general_uv_mode_!=material.uv_mode)){
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,fixed_uv_switch_,material.uv_mode==UvMode::fixed);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,normal_uv_switch_,material.uv_mode==UvMode::normal);
        general_uv_mode_=material.uv_mode;general_uv_valid_=true;
    }
    if(!affine&&(!lighting_valid_||lighting_enabled_!=bool(lighting))){
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,lighting_switch_,bool(lighting));
        lighting_enabled_=bool(lighting);lighting_valid_=true;++stats_.lighting_state_changes;
    }
    if(lighting){
        const auto& packed=lighting_cache_.uniforms();
        stats_.lighting_preparations+=light_changes.packed;
        const auto upload=[&](int first,const auto& rows){
            for(unsigned i=0;i<rows.size();++i){const auto& r=rows[i];
                C3D_FVUnifSet(GPU_VERTEX_SHADER,first+i,r[0],r[1],r[2],r[3]);}
            stats_.lighting_uniform_rows+=rows.size();
        };
        if(!batch&&light_changes.normal)upload(light_normal_uniform_,packed.normal);
        if(light_changes.direction)upload(light_direction_uniform_,packed.direction);
        if(light_changes.colour)upload(light_colour_uniform_,packed.colour);
        if(light_changes.material)upload(light_material_uniform_,packed.material);
        if(light_changes.shininess)upload(shininess_uniform_,packed.shininess);
        if(light_changes.switches){
            C3D_BoolUnifSet(GPU_VERTEX_SHADER,lights_switch_,bool(packed.count));
            C3D_BoolUnifSet(GPU_VERTEX_SHADER,specular_switch_,packed.specular);
            C3D_BoolUnifSet(GPU_VERTEX_SHADER,shininess_switch_,packed.table);
            C3D_BoolUnifSet(GPU_VERTEX_SHADER,toon_switch_,packed.toon);
            C3D_BoolUnifSet(GPU_VERTEX_SHADER,grey_switch_,packed.grey);
            ++stats_.lighting_state_changes;
        }
        if(light_changes.count){
            C3D_IVUnifSet(GPU_VERTEX_SHADER,light_loop_uniform_,packed.count?packed.count-1:0,0,1,0);
            ++stats_.lighting_state_changes;
        }
    }
    if (bound_mesh_ != handle) {
        // BUILD134: BUILD 133 declared the loaders (12 command words) and the
        // whole 12-buffer table (40) for every mesh. The loaders depend only
        // on the layout: 3, 4 or 5 attributes. Every mesh is buffer 0 over
        // the same base with buffers 1..11 unused, so after the pass's first
        // complete table only buffer 0's three registers change (4 words).
        const std::uint8_t layout=mesh->batch?5:mesh->normals?4:3;
        if(!raw_state_||bound_layout_!=layout){
            auto* attributes=C3D_GetAttrInfo();AttrInfo_Init(attributes);
            AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3);AttrInfo_AddLoader(attributes,1,GPU_FLOAT,2);
            AttrInfo_AddLoader(attributes,2,GPU_UNSIGNED_BYTE,4);
            if(mesh->normals||mesh->batch)AttrInfo_AddLoader(attributes,3,GPU_SHORT,4);
            if(mesh->batch)AttrInfo_AddLoader(attributes,4,GPU_UNSIGNED_BYTE,4);
            bound_layout_=layout;
        }
        if(raw_state_&&buffers_resident_&&mesh->binding.bufCount==1&&shadow_.buffers->bufCount==1&&
           mesh->binding.base_paddr==shadow_.buffers->base_paddr){
            const auto& buffer=mesh->binding.buffers[0];
            shadow_.buffers->buffers[0]=buffer;deltas_.buffer0(buffer.offset,buffer.flags[0],buffer.flags[1]);
        }else{C3D_SetBufInfo(&mesh->binding);buffers_resident_=raw_state_&&mesh->binding.bufCount==1;}
        bound_mesh_ = handle; ++stats_.mesh_binds;
    }
    // Stencil stage of this draw: a shadow pass (1/2), or owner marking (3)
    // for a polygon-ID-1 material while the image draws shadow volumes.
    const std::uint8_t stage=shadow_stage_?shadow_stage_:(owner_marking_&&active_depth_&&material.shadow_id)?3:0;
    const std::uint8_t stage_reference=shadow_stage_?shadow_reference_:(stage?0x80:0);
    const bool shadow_changed=bound_shadow_stage_!=stage||(stage&&bound_shadow_reference_!=stage_reference);
    if (!material_valid_ || !vertex_material_valid_ || shadow_changed || !(bound_material_ == material)) {
        // BUILD134: one changed effect field made the SDK send its whole
        // block again (34 command words). Write the field into the SDK's
        // copy and send its own register(s). delta is null while the SDK has
        // the block pending (first draw of a pass, applet restore): that
        // re-send carries the value.
        pica::Deltas* const delta=raw_state_&&!(citro3d_dirty()&Citro3DShadow::effect_block)?&deltas_:nullptr;
        const bool mode_changed=!material_valid_||!vertex_material_valid_||bound_material_.uv_mode!=material.uv_mode;
        const unsigned sampled_width=texture?texture->gpu.width:0,sampled_height=texture?texture->gpu.height:0;
        const bool uv_changed=mode_changed || (material.uv_mode==UvMode::fixed?
            !(bound_material_.fixed_uv==material.fixed_uv)||bound_sample_width_!=sampled_width||bound_sample_height_!=sampled_height:
            material.uv_mode==UvMode::normal?
            !(bound_material_.normal_uv()==material.normal_uv())||bound_sample_width_!=sampled_width||bound_sample_height_!=sampled_height:
            bound_material_.uv!=material.uv);
        if(uv_changed&&material.uv_mode==UvMode::fixed){
            FixedUvUniforms packed;
            pack_fixed_uv(material.fixed_uv,texture->gpu.width,texture->gpu.height,packed);
            for(unsigned i=0;i<packed.row.size();++i){const auto& r=packed.row[i];
                C3D_FVUnifSet(GPU_VERTEX_SHADER,fixed_uv_uniform_+i,r[0],r[1],r[2],r[3]);}
        }else if(uv_changed&&material.uv_mode==UvMode::normal){
            NormalUvUniforms packed;pack_normal_uv(material.normal_uv(),texture->gpu.width,texture->gpu.height,packed);
            for(unsigned i=0;i<packed.row.size();++i){const auto& r=packed.row[i];
                C3D_FVUnifSet(GPU_VERTEX_SHADER,normal_uv_uniform_+i,r[0],r[1],r[2],r[3]);}
        }else if(uv_changed){
            const auto location=batch?batch_uv_uniform_:uv_uniform_;
            C3D_FVUnifSet(GPU_VERTEX_SHADER,location,material.uv[0],material.uv[1],material.uv[2],0);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,location+1,material.uv[3],material.uv[4],material.uv[5],0);
        }
        if(uv_changed)++stats_.uv_uploads;
        if(!material_valid_||!vertex_material_valid_||bound_material_.tint!=material.tint){
            C3D_FVUnifSet(GPU_VERTEX_SHADER,batch?batch_tint_uniform_:tint_uniform_,float((material.tint>>24)&255)/255.f,
            float((material.tint>>16)&255)/255.f,float((material.tint>>8)&255)/255.f,float(material.tint&255)/255.f);
            ++stats_.tint_uploads;
        }
        auto* sampler=texture ? &texture->gpu : &unused_sampler_;
        const auto wrapping=[](Wrap w){return w==Wrap::repeat?GPU_REPEAT:w==Wrap::mirror?GPU_MIRRORED_REPEAT:w==Wrap::transparent_border?GPU_CLAMP_TO_BORDER:GPU_CLAMP_TO_EDGE;};
        if(!material_valid_||bound_material_.texture!=material.texture||bound_material_.wrap_s!=material.wrap_s||bound_material_.wrap_t!=material.wrap_t){
            C3D_TexSetWrap(sampler,wrapping(material.wrap_s),wrapping(material.wrap_t));
        // The following DrawElements publishes this sampler's state to the
        // command buffer before another material can change its metadata.
            C3D_TexBind(0,sampler);++stats_.sampler_binds;
        }
        const bool alpha_mask=texture&&texture->gpu.fmt==GPU_A4;
        if(!material_valid_||bool(bound_material_.texture)!=bool(material.texture)||bound_material_.sample_alpha!=material.sample_alpha||bound_alpha_mask_!=alpha_mask){
            auto* env = C3D_GetTexEnv(0); C3D_TexEnvInit(env);
            // A4 supplies coverage only. RGB always comes from the glyph's
            // vertex/tint colour, independent of the sampler's unused RGB.
            const bool sampled_colour=texture&&!alpha_mask;
            C3D_TexEnvSrc(env,C3D_RGB,sampled_colour?GPU_TEXTURE0:GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env,C3D_RGB,sampled_colour?GPU_MODULATE:GPU_REPLACE);
            const bool sampled_alpha=texture&&material.sample_alpha;
            C3D_TexEnvSrc(env,C3D_Alpha,sampled_alpha?GPU_TEXTURE0:GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env,C3D_Alpha,sampled_alpha?GPU_MODULATE:GPU_REPLACE);
            bound_alpha_mask_=alpha_mask;
        }
        if(!material_valid_||bound_material_.brightness!=material.brightness){
            auto* tone=C3D_GetTexEnv(1);C3D_TexEnvInit(tone);
            if(material.brightness){
                const auto strength=unsigned(material.brightness<0?-material.brightness:material.brightness);
                C3D_TexEnvSrc(tone,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_CONSTANT);
                C3D_TexEnvOpRgb(tone,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
                C3D_TexEnvFunc(tone,C3D_RGB,GPU_INTERPOLATE);
                C3D_TexEnvColor(tone,(material.brightness>0?0xffffffu:0u)|(((strength*255+8)/16)<<24));
            }
            ++stats_.pipeline_changes;
        }
        if(!material_valid_||bound_material_.cull!=material.cull){
            const auto mode=material.cull == Cull::none ? GPU_CULL_NONE :
                material.cull == Cull::back ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW;
            if(raw_state_)pica::cull(*shadow_.effect,mode,delta);else C3D_CullFace(mode);
            ++stats_.pipeline_changes;
        }
        if(!material_valid_||bound_material_.depth_test!=material.depth_test||bound_material_.depth_write!=material.depth_write||
           (bound_material_.blend==Blend::highlight)!=(material.blend==Blend::highlight)||bound_shadow_stage_!=stage){
            // The shadow mask writes neither colour nor depth: only the stencil.
            const auto function=material.blend==Blend::highlight?GPU_EQUAL:GPU_GREATER;
            const auto mask=stage==1?GPU_WRITEMASK(0):material.depth_write ? GPU_WRITE_ALL : GPU_WRITE_COLOR;
            if(raw_state_)pica::depth_test(*shadow_.effect,material.depth_test,function,mask,delta);
            else C3D_DepthTest(material.depth_test,function,mask);
            ++stats_.pipeline_changes;
        }
        if(shadow_changed){
            // Stages: 1 volume mask (bit 0 on depth fail), 2 darken where the
            // stencil is exactly 1 (an owner's bit 7 makes it 0x81: skipped),
            // 3 owner mark (bit 7 where the owner is visible).
            const bool enable=stage!=0;
            const auto function=stage==2?GPU_EQUAL:GPU_ALWAYS;
            const int reference=enable?stage_reference:0;
            const auto depth_fail=stage==1?GPU_STENCIL_REPLACE:stage==2?GPU_STENCIL_ZERO:GPU_STENCIL_KEEP;
            const auto pass=stage==2?GPU_STENCIL_ZERO:stage==3?GPU_STENCIL_REPLACE:GPU_STENCIL_KEEP;
            const unsigned write_mask=stage==3?0x80u:stage?0x01u:0xffu;
            if(raw_state_)pica::stencil(*shadow_.effect,enable,function,unsigned(reference),0xff,write_mask,GPU_STENCIL_KEEP,depth_fail,pass,delta);
            else{
                C3D_StencilTest(enable,function,reference,0xff,write_mask);
                C3D_StencilOp(GPU_STENCIL_KEEP,depth_fail,pass);
            }
            bound_shadow_stage_=stage;bound_shadow_reference_=stage_reference;++stats_.pipeline_changes;
        }
        if(!material_valid_||bound_material_.fog!=material.fog){
            // The table index is the window depth as stored (1 nearest).
            C3D_FogGasMode(material.fog&&fog_ready_?GPU_FOG:GPU_NO_FOG,GPU_PLAIN_DENSITY,false);++stats_.pipeline_changes;
        }
        if(!material_valid_||bound_material_.alpha_cutoff!=material.alpha_cutoff){
            if(raw_state_)pica::alpha_test(*shadow_.effect,true,GPU_GREATER,material.alpha_cutoff,delta);
            else C3D_AlphaTest(true, GPU_GREATER, material.alpha_cutoff);
            ++stats_.pipeline_changes;
        }
        if(!material_valid_||bound_material_.blend!=material.blend){
            const auto source_colour=material.blend == Blend::alpha ? GPU_SRC_ALPHA : GPU_ONE;
            const auto destination=material.blend==Blend::highlight?GPU_ONE:material.blend != Blend::opaque ? GPU_ONE_MINUS_SRC_ALPHA : GPU_ZERO;
            const auto source_alpha=material.blend==Blend::highlight?GPU_ZERO:GPU_ONE;
            if(raw_state_)pica::alpha_blend(*shadow_.effect,GPU_BLEND_ADD,GPU_BLEND_ADD,source_colour,destination,source_alpha,destination,delta);
            else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,source_colour,destination,source_alpha,destination);
            ++stats_.pipeline_changes;
        }
        bound_sample_width_=sampled_width;bound_sample_height_=sampled_height;
        bound_material_ = material; material_valid_ = vertex_material_valid_ = true; ++stats_.material_binds;
    }
    if(batch){
        for(unsigned pose=0;pose<8;++pose)if(batch->used&(1u<<pose)){
            const auto bit=std::uint8_t(1u<<pose);
            const Matrix& matrix=*batch->palette[pose]; // the plan's leased frame
            if(!(palette_matrix_valid_&bit)||std::memcmp(&palette_matrix_[pose],&matrix,sizeof(Matrix))){
                upload_matrix(matrix_uniform_+pose*4,matrix);
                palette_matrix_[pose]=matrix;palette_matrix_valid_|=bit;++stats_.matrix_uploads;
            }
            if(batch->lit&&batch->lighting.mask&&(!(palette_basis_valid_&bit)||palette_basis_[pose]!=batch->basis[pose])){
                auto* target=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,shininess_uniform_+pose*3,3);
                for(unsigned row=0;row<3;++row){const auto& r=batch->basis[pose][row];target[row]=FVec4_New(float(r[0]),float(r[1]),float(r[2]),0);}
                palette_basis_[pose]=batch->basis[pose];palette_basis_valid_|=bit;stats_.lighting_uniform_rows+=3;
            }
        }
    }else if (!matrix_valid_ || std::memcmp(&bound_matrix_, &mvp, sizeof(mvp))) {
        upload_matrix(matrix_uniform_,mvp);
        bound_matrix_ = mvp; matrix_valid_ = true; ++stats_.matrix_uploads;
    }
    if(aux_units_pending_){
        // First draw of a pass with register deltas. BUILD 133 sent units
        // 1/2 here, with the neutral texture's parameters as of this draw
        // (an untextured material's wrap mode is written into them above).
        aux_units_pending_=false;
        constexpr std::uint32_t both=Citro3DShadow::texture1|Citro3DShadow::texture2;
        const bool pending=(citro3d_dirty()&both)==both; // the SDK sends them with this draw
        if(shadow_.texture[1]!=&unused_sampler_||shadow_.texture[2]!=&unused_sampler_||
           (!pending&&(!aux_units_valid_||aux_units_param_!=unused_sampler_.param))){
            C3D_TexBind(1,&unused_sampler_);C3D_TexBind(2,&unused_sampler_);
        }
        aux_units_param_=unused_sampler_.param;aux_units_valid_=true;
    }
    {   // Rotate the sampled ordinal each frame: cover all draws without
        // adding two clock SVCs to every draw. This measures CPU SDK work,
        // including context/uniform emission, not GPU execution time.
        const bool sample=timings_&&((stats_.draws+sdk_sample_phase_)&7u)==0;
        Timed sdk(stats_.sdk_draw_ticks,sample);
        if(packed_draws_){
            draw_packet_.range(mesh->index_configuration+std::uint32_t(first*2),std::uint32_t(count));
            citro3d_submit_indexed_packet(draw_packet_,deltas_.words.data(),deltas_.count);deltas_.count=0;
        }else C3D_DrawElements(GPU_TRIANGLES, int(count), C3D_UNSIGNED_SHORT, mesh->indices+first);
        stats_.sdk_draw_samples+=sample;
    }
    if(capture_.armed&&!overlap_){
        if(capture_.draws&&capture_.draw_count<capture_.draw_capacity){auto& d=capture_.draws[capture_.draw_count++];
            d={std::uint32_t(handle.slot),material.texture?std::uint32_t(material.texture.slot):0xffffffffu,std::uint32_t(first),std::uint32_t(count),
               std::uint32_t((capture_.base&&capture_.base<=gpuCmdBuf?std::size_t(gpuCmdBuf-capture_.base):0u)+gpuCmdBufOffset),std::uint16_t(texture?texture->gpu.width:0),std::uint16_t(texture?texture->gpu.height:0),
               std::uint8_t(texture?texture->gpu.fmt:0xff),std::uint8_t((lighting?1u:0u)|(batch?2u:0u)|(affine?4u:0u)|(material.sample_alpha?8u:0u)),
               std::uint8_t(material.blend),std::uint8_t(material.cull),std::uint8_t((material.depth_test?1u:0u)|(material.depth_write?2u:0u)),
               std::uint8_t(capture_.passes),std::uint16_t(material.alpha_cutoff)};}
        ++capture_.draw_total;
    }
    ++stats_.draws; stats_.triangles += count/3;
    stats_.lit_draws+=lighting!=nullptr;
    stats_.affine_draws+=affine;
    return Error::none;
}
Error Citro3DBackend::end() noexcept {
    if (!ready_ || !state_.end()) return reject(Error::invalid_phase);
    // The frame is abandoned (citro3d stays in it; begin() refuses until the
    // GPU is usable again, which a hung GPU never is in this boot).
    if(!gpu_usable())return reject(Error::gpu_failure);
    Timed time(stats_.submit_ticks, timings_);
    if(capture_.armed){
        // Every list of this frame is still in the command buffer: each
        // split (a cleared pass) only moves libctru's base pointer forward
        // until the next FrameBegin, so the frame runs from the base seen
        // at begin() to the current offset.
        const std::uint32_t* base=capture_.base&&capture_.base<=gpuCmdBuf?capture_.base:gpuCmdBuf;
        capture_.word_total=std::size_t(gpuCmdBuf-base)+gpuCmdBufOffset;
        capture_.word_count=capture_.word_total<capture_.word_capacity?capture_.word_total:capture_.word_capacity;
        if(capture_.word_count)std::memcpy(capture_.words,base,capture_.word_count*4);
        capture_.armed=false;capture_.complete=true;
    }
    if(late_fence_){
        // The copy to the LCD buffer is queued by FrameEnd: the buffer it
        // writes is still on screen while the previous swap is pending.
        const auto start=svcGetSystemTick();
        wait_present_fence();
        const auto waited=svcGetSystemTick()-start;stats_.blocked_ticks+=waited;
        if(timings_){stats_.wait_ticks+=waited;stats_.submit_ticks-=waited;}
    }
    // All mutated resources were explicitly flushed during upload. This flag
    // flushes commands and avoids a whole-linear-heap flush on every frame.
    watch_stage(SM64DS_WATCH_WORKER_SUBMIT);
    {LockPriority boost(lock_priority_);C3D_FrameEnd(GX_CMDLIST_FLUSH);}
    return Error::none;
}
std::size_t Citro3DBackend::describe_textures(TextureInfo* out,std::size_t capacity) const noexcept {
    std::size_t count=0;unsigned slot=0;
    const_cast<Citro3DBackend*>(this)->textures_.each([&](Texture handle,TextureResource& r){
        slot=handle.slot;
        if(out&&count<capacity)out[count]={slot,std::uint32_t(reinterpret_cast<std::uintptr_t>(r.gpu.data)),std::uint32_t(r.bytes),
            std::uint16_t(r.gpu.width),std::uint16_t(r.gpu.height),std::uint8_t(r.gpu.fmt),std::uint8_t(r.vram),
            std::uint8_t(r.target!=nullptr),std::uint8_t(r.initialized)};
        ++count;});
    return count;
}
Citro3DBackend::MeshTotals Citro3DBackend::describe_meshes() const noexcept {
    MeshTotals totals;
    const_cast<Citro3DBackend*>(this)->meshes_.each([&](Mesh,MeshResource& r){
        ++totals.meshes;totals.batches+=r.batch;totals.bytes+=r.bytes;totals.indices+=r.count;});
    return totals;
}
Error Citro3DBackend::shutdown() noexcept {
    if (!ready_) return Error::none;
    if(!program_cache_valid_)return reject(Error::unsupported);
    if (state_.phase() != FrameState::Phase::idle) return reject(Error::invalid_phase);
    // RenderTargetDelete forbids an open SDK frame. It drains pending uses;
    // ordinary texture/mesh storage is reclaimed by the subsequent fence.
    textures_.each([&](Texture h,TextureResource& r){if(r.target)destroy(h);});
    const auto begun = begin(false);
    if (begun != Error::none) return begun;
    meshes_.each([&](Mesh h, MeshResource&) { destroy(h); });
    textures_.each([&](Texture h, TextureResource&) { destroy(h); });
    C3D_TexDelete(&unused_sampler_); unused_sampler_ = {}; live_ = vram_ = 0;
    stats_.live_linear_bytes=stats_.live_vram_bytes=0;active_colour_=nullptr;active_depth_=false;
    end();
    // begin() above drained all previous work. Drop only our matching cache
    // reference before freeing metadata; otherwise a later BindProgram reads
    // a freed shader, including across C3D_Fini/Init on the detected SDK.
    citro3d_forget_program(&program_);
    citro3d_forget_program(&affine_program_);
    citro3d_forget_program(&batch_program_);
    if(batch_ready_)shaderProgramFree(&batch_program_);
    batch_program_={};batch_ready_=batch_bound_=false;
    if(affine_ready_)shaderProgramFree(&affine_program_);
    affine_program_={};affine_ready_=affine_bound_=false;
    shaderProgramFree(&program_); DVLB_Free(shader_); shader_ = nullptr;
    program_ = {}; ready_ = false; retirements_=false;
    return Error::none;
}
}
