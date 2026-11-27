// BUILD137 hang watchdog: see hang_watchdog.hpp.
#if defined(__3DS__)
#include "hang_watchdog.hpp"
#include "build_version.hpp"
#include "../audio_diagnostics.hpp"
#include <3ds.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" {
// game_runtime.cpp: the bytes of the stdout buffer not yet written to the SD.
unsigned sm64ds_log_pending(const char** data) __attribute__((weak));
// game_runtime.cpp: liveness counters (see kRuntimeNames).
unsigned sm64ds_watchdog_runtime_counters(std::uint64_t* out,unsigned capacity) __attribute__((weak));
extern volatile int sm64ds_sound_game_waiting;
extern char __bss_start__[];
}

namespace {
constexpr std::uint64_t kTicksPerMs=SYSCLOCK_ARM11/1000;
constexpr std::uint64_t kPoll=250;                  // ms between checks
constexpr std::uint64_t kDisplayStallMs=6000;       // game runs, no new image
constexpr std::uint64_t kGameStallMs=15000;         // no game update (long loads stay below)
constexpr std::uint64_t kAptStallMs=15000;          // the host never reaches aptMainLoop
constexpr std::uint64_t kHomeExitSeconds=20;        // HOME/POWER unanswered: close to HOME
constexpr unsigned kMaxReports=5;
constexpr std::uint64_t kReportPauseMs=60000;       // a report or dump never takes this long
constexpr unsigned kCrumbs=64;

const char* const kStageNames[SM64DS_WATCH_STAGE_COUNT]={
    "ninguna","host:gancho_de_imagen","host:aptMainLoop","host:presentar","host:audio","host:ritmo","host:suspension_HOME","host:informe",
    "juego:actualizacion","juego:espera_al_render",
    "render:libre","render:trabajo","render:inicio_de_imagen","render:dibujo","render:envio","render:espera_GPU","render:espera_pantalla"};
const char* const kRuntimeNames[]={"actualizaciones_sin_imagen","esperas_de_vida","esperas_de_vida_ms","esperas_agotadas",
    "render_ocupado","imagenes_descartadas_ocupado","escena_runtime","imagenes"};

struct Crumb {std::atomic<std::uint64_t> tick{0};std::atomic<const char*> tag{nullptr};std::atomic<std::uint32_t> a{0},b{0};};
struct Thread {std::atomic<std::uint8_t> stage{0};std::atomic<std::uint64_t> since{0};};
struct State {
    std::atomic<bool> started{false};
    std::atomic<std::uint64_t> boot{0};
    std::atomic<std::uint64_t> updates{0},update_tick{0},presents{0},present_tick{0},apt_enters{0},apt_tick{0};
    std::atomic<unsigned> scene{0};std::atomic<int> menu_state{-1};
    std::atomic<int> paused{0};
    // Report/dump pauses (bounded, kReportPauseMs): their start tick.
    std::atomic<int> report_paused{0};std::atomic<std::uint64_t> report_pause_tick{0};
    std::atomic<std::uint32_t> gpu_stalls{0},gpu_stall_ms{0};
    // Sticky: a GX wait gave up; the backend no longer touches the GPU.
    std::atomic<bool> gpu_hung{false};
    // The render worker was still running when HOME/sleep suspended the game.
    std::atomic<bool> gpu_blocked{false};
    std::atomic<unsigned> hangs{0};
    Thread host,game,worker;
    Crumb crumbs[kCrumbs];std::atomic<unsigned> crumb_next{0};
} g;

std::uint64_t now(){return svcGetSystemTick();}
std::uint64_t ms_since(std::uint64_t tick,std::uint64_t at){return tick&&at>tick?(at-tick)/kTicksPerMs:0;}
void set(Thread& t,unsigned stage){t.stage.store(std::uint8_t(stage<SM64DS_WATCH_STAGE_COUNT?stage:0),std::memory_order_relaxed);t.since.store(now(),std::memory_order_relaxed);}

// ---- Files through FS:USER only (no newlib FILE, no heap) ----
FS_Archive sd=0;bool sd_open=false;
bool open_sd(){
    if(!sd_open)sd_open=R_SUCCEEDED(FSUSER_OpenArchive(&sd,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,"")));
    return sd_open;
}
bool exists(const char* path){
    Handle h=0;if(R_FAILED(FSUSER_OpenFile(&h,sd,fsMakePath(PATH_ASCII,path),FS_OPEN_READ,0)))return false;
    FSFILE_Close(h);return true;
}
// Writes size bytes in 256 KiB pieces; returns the bytes written.
std::uint64_t write_file(const char* path,const void* data,std::uint64_t size){
    Handle h=0;
    if(R_FAILED(FSUSER_OpenFile(&h,sd,fsMakePath(PATH_ASCII,path),FS_OPEN_WRITE|FS_OPEN_CREATE,0)))return 0;
    FSFILE_SetSize(h,0);
    std::uint64_t done=0;const auto* bytes=static_cast<const std::uint8_t*>(data);
    while(done<size){
        const u32 piece=u32(size-done<262144?size-done:262144);u32 written=0;
        if(R_FAILED(FSFILE_Write(h,&written,done,bytes+done,piece,FS_WRITE_FLUSH))||!written)break;
        done+=written;
    }
    FSFILE_Close(h);return done;
}

// ---- Report text (integers only: snprintf never allocates for them) ----
char text[16384];
std::size_t used=0;
void line(const char* format,...) __attribute__((format(printf,1,2)));
void line(const char* format,...){
    if(used>=sizeof text-1)return;
    va_list args;va_start(args,format);
    const int n=std::vsnprintf(text+used,sizeof text-used,format,args);va_end(args);
    if(n>0)used+=std::size_t(n)<sizeof text-used?std::size_t(n):sizeof text-used-1;
}
void thread_line(const char* name,const Thread& t,std::uint64_t at){
    const unsigned stage=t.stage.load(std::memory_order_relaxed);
    line("%s=%s desde_hace_ms=%llu\n",name,kStageNames[stage<SM64DS_WATCH_STAGE_COUNT?stage:0],(unsigned long long)ms_since(t.since.load(),at));
}

// One hang report: text, log tail and global memory. Returns its number.
unsigned write_report(const char* reason,std::uint64_t at,bool with_memory=true){
    if(!open_sd())return 0;
    FSUSER_CreateDirectory(sd,fsMakePath(PATH_ASCII,"/3ds/sm64ds/cuelgues"),0);
    char path[96];unsigned number=0;
    for(unsigned n=1;n<100;++n){std::snprintf(path,sizeof path,"/3ds/sm64ds/cuelgues/cuelgue-%02u.txt",n);if(!exists(path)){number=n;break;}}
    if(!number)return 0;
    used=0;
    line("SM64DS %s: CUELGUE DETECTADO\nmotivo=%s\n",sm64ds_build::label,reason);
    line("segundos_desde_el_arranque=%llu\n",(unsigned long long)(ms_since(g.boot.load(),at)/1000));
    bool is_new=false;APT_CheckNew3DS(&is_new);line("consola=%s 3DS\n",is_new?"New":"Old");
    line("escena=%u estado_menu=%d\n",g.scene.load(),g.menu_state.load());
    line("actualizaciones_del_juego=%llu ultima_hace_ms=%llu\n",(unsigned long long)g.updates.load(),(unsigned long long)ms_since(g.update_tick.load(),at));
    line("imagenes_terminadas=%llu ultima_hace_ms=%llu\n",(unsigned long long)g.presents.load(),(unsigned long long)ms_since(g.present_tick.load(),at));
    line("entradas_aptMainLoop=%llu ultima_hace_ms=%llu\n",(unsigned long long)g.apt_enters.load(),(unsigned long long)ms_since(g.apt_tick.load(),at));
    line("HOME_o_POWER_pendiente=%u\n",unsigned(aptShouldJumpToHome()));
    line("pausa=%d\n",g.paused.load());
    thread_line("hilo_principal",g.host,at);thread_line("hilo_del_juego",g.game,at);thread_line("hilo_de_render",g.worker,at);
    line("gpu_esperas_agotadas=%lu ultima_ms=%lu gpu_colgada=%u gpu_bloqueada=%u\n",(unsigned long)g.gpu_stalls.load(),(unsigned long)g.gpu_stall_ms.load(),
        unsigned(g.gpu_hung.load()),unsigned(g.gpu_blocked.load()));
    line("gsp_presentacion_pendiente_arriba=%u abajo=%u\n",unsigned(gspIsPresentPending(GFX_TOP)),unsigned(gspIsPresentPending(GFX_BOTTOM)));
    {namespace ad=enhance::audio_diagnostics;const auto& t=ad::timing;
     line("audio: juego_esperando=%d mezcla_us=%lu bloques=%lu bloque_max_us=%lu espera_juego_us=%lu esperas_juego=%lu lotes_diferidos=%lu\n",
        int(sm64ds_sound_game_waiting),(unsigned long)t.mix_us.load(),(unsigned long)t.chunks.load(),(unsigned long)t.chunk_max_us.load(),
        (unsigned long)t.game_wait_us.load(),(unsigned long)t.game_waits.load(),(unsigned long)t.deferred_batches.load());}
    if(sm64ds_watchdog_runtime_counters){std::uint64_t c[8]{};const unsigned n=sm64ds_watchdog_runtime_counters(c,8);
        for(unsigned i=0;i<n&&i<8;++i)line("runtime_%s=%llu\n",kRuntimeNames[i],(unsigned long long)c[i]);}
    {s32 priority=0;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);line("prioridad_vigilante=0x%lx\n",(long)priority);}
    line("\n== ultimas migas de pan (mas antigua primero) ==\n");
    const unsigned next=g.crumb_next.load();
    for(unsigned i=0;i<kCrumbs;++i){const auto& c=g.crumbs[(next+i)%kCrumbs];const char* tag=c.tag.load();if(!tag)continue;
        line("hace_ms=%llu %s %lu %lu\n",(unsigned long long)ms_since(c.tick.load(),at),tag,(unsigned long)c.a.load(),(unsigned long)c.b.load());}
    const char* log=nullptr;const unsigned pending=sm64ds_log_pending?sm64ds_log_pending(&log):0;
    line("\nlog_pendiente_bytes=%u (cuelgue-%02u-log.txt)\n",pending,number);
    // Global memory: the mapped block holding .data and .bss (game globals).
    MemInfo info{};PageInfo page{};std::uint64_t memory=0;
    if(with_memory&&R_SUCCEEDED(svcQueryMemory(&info,&page,u32(reinterpret_cast<std::uintptr_t>(__bss_start__))))){
        memory=info.size>(32u<<20)?(32u<<20):info.size;
        line("memoria: base=0x%08lx bytes=%llu (cuelgue-%02u-memoria.bin, sin cabecera)\n",(unsigned long)info.base_addr,(unsigned long long)memory,number);
    }
    write_file(path,text,used);
    if(pending&&log){std::snprintf(path,sizeof path,"/3ds/sm64ds/cuelgues/cuelgue-%02u-log.txt",number);write_file(path,log,pending);}
    if(memory){std::snprintf(path,sizeof path,"/3ds/sm64ds/cuelgues/cuelgue-%02u-memoria.bin",number);
        write_file(path,reinterpret_cast<const void*>(std::uintptr_t(info.base_addr)),memory);}
    return number;
}
void led_red(){
    if(R_FAILED(mcuHwcInit()))return;
    InfoLedPattern pattern{};pattern.delay=1;pattern.loopDelay=0;
    std::memset(pattern.redPattern,0xff,sizeof pattern.redPattern);
    MCUHWC_SetInfoLedPattern(&pattern);mcuHwcExit();
}

void watch(void*){
    bool episode=false;unsigned reports=0;std::uint64_t home_since=0;
    std::uint64_t last_updates=0,last_presents=0,updates_at_present=0,last_poll=now();
    for(;;){
        svcSleepThread(s64(kPoll*1000000ull));
        const auto at=now();
        // The whole process was paused (Rosalina menu, debugger): every age
        // jumped by the pause. Restart the clocks instead of reporting.
        if(ms_since(last_poll,at)>2000){
            sm64ds_watchdog_crumb("vigilante: proceso pausado (ms)",std::uint32_t(ms_since(last_poll,at)),0);
            g.update_tick.store(g.update_tick.load()?at:0);g.present_tick.store(g.present_tick.load()?at:0);g.apt_tick.store(g.apt_tick.load()?at:0);
            if(g.report_paused.load())g.report_pause_tick.store(at);
            home_since=0;last_poll=at;continue;
        }
        last_poll=at;
        const auto updates=g.updates.load(),presents=g.presents.load();
        if(presents!=last_presents){last_presents=presents;updates_at_present=updates;}
        last_updates=updates;(void)last_updates;
        if(g.paused.load()){episode=false;home_since=0;continue;}
        // A report or dump pause that lasts too long is itself a hang (its
        // drain and readbacks wait on the render worker and the GPU).
        if(g.report_paused.load()&&ms_since(g.report_pause_tick.load(),at)<kReportPauseMs){episode=false;home_since=0;continue;}
        const char* reason=nullptr;
        const auto update_age=ms_since(g.update_tick.load(),at),present_age=ms_since(g.present_tick.load(),at),apt_age=ms_since(g.apt_tick.load(),at);
        // A GX wait that gave up (refused jobs still count as finished images).
        if(g.report_paused.load())reason="un informe o volcado no termina (espera al render o a la GPU)";
        else if(g.gpu_hung.load())reason="la GPU no responde (2 s sin completar trabajo de GX)";
        else if(g.present_tick.load()&&present_age>kDisplayStallMs&&updates-updates_at_present>=60)reason="la imagen no cambia aunque el juego avanza (render detenido o sin CPU)";
        else if(g.update_tick.load()&&update_age>kGameStallMs)reason="el juego no avanza (hilo del juego detenido)";
        else if(g.apt_tick.load()&&apt_age>kAptStallMs)reason="el sistema (HOME/POWER) no se atiende";
        // HOME or POWER pressed and never answered: close to the HOME Menu.
        const bool home=aptShouldJumpToHome()&&g.apt_tick.load()&&apt_age>2000;
        if(home){if(!home_since)home_since=at;}else home_since=0;
        if(!reason&&home&&ms_since(home_since,at)>5000)reason="HOME o POWER pulsado y sin respuesta";
        if(!reason){if(episode)sm64ds_watchdog_crumb("vigilante: recuperado",0,0);episode=false;continue;}
        if(!episode&&reports<kMaxReports){
            episode=true;++reports;g.hangs.fetch_add(1);
            sm64ds_watchdog_crumb("vigilante: cuelgue",unsigned(update_age),unsigned(present_age));
            write_report(reason,at);led_red();
            // Writing the report (about 19 MB on the console's SD) takes
            // seconds; it is not a process pause, so it must not reset the
            // clocks, the HOME timer or the report-pause deadline.
            last_poll=now();
        }
        static const bool exit_allowed=[](){if(!open_sd())return true;return !exists("/3ds/sm64ds/cuelgue-sin-salida.txt");}();
        if(exit_allowed&&home_since&&ms_since(home_since,at)>kHomeExitSeconds*1000){
            // The game can no longer hand the console to the system: end the
            // process so the HOME Menu takes over (no power-off needed).
            write_report("cierre de emergencia: HOME o POWER sin respuesta",now(),false); // no second 19 MB dump before leaving
            sm64ds_watchdog_emergency_exit();
        }
    }
}
}

// Close through APT first (the HOME Menu expects that handshake), on a helper
// thread because the stuck host may hold APT's lock; the raw process exit
// follows if the close has not ended the process within 1.5 s.
// The helper thread exists from the watchdog's start: creating it at the
// emergency would allocate its stack under a heap lock a stuck thread may hold.
static LightEvent close_go;static bool close_ready=false;
extern "C" void sm64ds_watchdog_emergency_exit(void){
    sm64ds_watchdog_crumb("vigilante: cierre de emergencia",0,0);
    if(close_ready)LightEvent_Signal(&close_go);
    svcSleepThread(1500000000LL);
    svcExitProcess();
}
// A report or dump that is still writing pushes its 60 s deadline forward.
extern "C" void sm64ds_watchdog_report_progress(){if(g.report_paused.load())g.report_pause_tick.store(now());}
extern "C" void sm64ds_watchdog_report_pause(int paused){
    if(paused){if(g.report_paused.fetch_add(1)==0)g.report_pause_tick.store(now());}
    else if(g.report_paused.load()>0&&g.report_paused.fetch_sub(1)==1){const auto t=now();
        g.update_tick.store(g.update_tick.load()?t:0);g.present_tick.store(g.present_tick.load()?t:0);g.apt_tick.store(g.apt_tick.load()?t:0);}
}
extern "C" void sm64ds_watchdog_start(){
    bool expected=false;if(!g.started.compare_exchange_strong(expected,true))return;
    g.boot.store(now());
    // Above the game (0x32), the render worker and libctru's APT thread (0x31)
    // on the application core; it sleeps between checks.
    if(!threadCreate(watch,nullptr,16*1024,0x2c,-2,true)){g.started.store(false);return;}
    LightEvent_Init(&close_go,RESET_ONESHOT);
    close_ready=threadCreate([](void*){LightEvent_Wait(&close_go);APT_PrepareToCloseApplication(true);APT_CloseApplication(nullptr,0,0);},
        nullptr,8192,0x2b,-2,true)!=nullptr;
}
extern "C" void sm64ds_watchdog_game_update(unsigned scene){
    g.updates.fetch_add(1,std::memory_order_relaxed);g.update_tick.store(now(),std::memory_order_relaxed);
    if(g.scene.exchange(scene,std::memory_order_relaxed)!=scene)sm64ds_watchdog_crumb("escena",scene,0);
    set(g.game,SM64DS_WATCH_GAME_UPDATE);
}
extern "C" void sm64ds_watchdog_presented(){g.presents.fetch_add(1,std::memory_order_relaxed);g.present_tick.store(now(),std::memory_order_relaxed);}
extern "C" void sm64ds_watchdog_host_stage(unsigned stage){
    if(stage==SM64DS_WATCH_HOST_APT){g.apt_enters.fetch_add(1,std::memory_order_relaxed);g.apt_tick.store(now(),std::memory_order_relaxed);}
    set(g.host,stage);
}
extern "C" void sm64ds_watchdog_game_stage(unsigned stage){set(g.game,stage);}
extern "C" void sm64ds_watchdog_worker_stage(unsigned stage){set(g.worker,stage);}
extern "C" void sm64ds_watchdog_pause(int paused){
    if(paused)g.paused.fetch_add(1);else if(g.paused.load()>0)g.paused.fetch_sub(1);
    // Times measured across a pause restart from its end.
    if(!paused){const auto t=now();g.update_tick.store(t);g.present_tick.store(g.present_tick.load()?t:0);g.apt_tick.store(t);}
}
extern "C" void sm64ds_watchdog_crumb(const char* tag,std::uint32_t a,std::uint32_t b){
    auto& c=g.crumbs[g.crumb_next.fetch_add(1,std::memory_order_relaxed)%kCrumbs];
    c.tag.store(nullptr,std::memory_order_relaxed);c.tick.store(now(),std::memory_order_relaxed);
    c.a.store(a,std::memory_order_relaxed);c.b.store(b,std::memory_order_relaxed);c.tag.store(tag,std::memory_order_release);
}
extern "C" void sm64ds_watchdog_gpu_stall(std::uint32_t milliseconds){
    g.gpu_stalls.fetch_add(1);g.gpu_stall_ms.store(milliseconds);sm64ds_watchdog_crumb("GPU: espera agotada (ms)",milliseconds,0);
}
extern "C" void sm64ds_watchdog_menu_state(int state){
    if(g.menu_state.exchange(state,std::memory_order_relaxed)!=state)sm64ds_watchdog_crumb("menu: estado",unsigned(state),0);
}
extern "C" unsigned sm64ds_watchdog_hangs(){return g.hangs.load();}

// ---- Bounded GPU waits (link option --wrap=gxCmdQueueWait) ----
// libctru's gxCmdQueueWait(-1) loops on gspWaitForAnyEvent until the GX queue
// stops running; a GX command that never completes on hardware would block
// the render worker, our HOME/sleep hook and citro3d's own one for ever.
// An infinite wait is now 100 ms slices for at most kGpuLimitMs (a frame's
// GX work takes milliseconds, so a healthy GPU returns in the first slice and
// nothing changes in normal play). When the limit passes the GPU is declared
// hung for the rest of the boot: the wait reports false and the backend stops
// issuing GX work (sm64ds_gpu_unusable), because citro3d ignores that result
// in most callers and would otherwise queue into a running queue or free
// targets the GPU still writes. The picture stays frozen, the game, HOME and
// the watchdog report keep working. No exemption while paused: citro3d's own
// HOME hook runs after ours and must stay bounded too.
extern "C" bool __real_gxCmdQueueWait(gxCmdQueue_s* queue,s64 timeout);
// The bound counts 100 ms slices WITHOUT queue progress, not wall time: a
// process paused by the Rosalina menu or a debugger stretches one slice but
// is not a hung GPU (review of BUILD 137).
extern "C" bool __wrap_gxCmdQueueWait(gxCmdQueue_s* queue,s64 timeout){
    if(timeout>=0||g.gpu_hung.load(std::memory_order_relaxed))return __real_gxCmdQueueWait(queue,timeout>=0?timeout:0);
    constexpr unsigned kStalledSlices=20; // 2 s of a GPU that completes nothing
    const auto slice=s64(SYSCLOCK_ARM11/10);const auto start=now();
    unsigned stalled=0;std::uint32_t last=queue?(std::uint32_t(queue->curEntry)<<16|queue->lastEntry):0;
    for(;;){
        if(__real_gxCmdQueueWait(queue,slice))return true;
        const std::uint32_t mark=queue?(std::uint32_t(queue->curEntry)<<16|queue->lastEntry):0;
        if(mark!=last){last=mark;stalled=0;continue;}
        if(++stalled>=kStalledSlices){g.gpu_hung.store(true);sm64ds_watchdog_gpu_stall(std::uint32_t(ms_since(start,now())));return false;}
    }
}
extern "C" int sm64ds_gpu_hung(void){return g.gpu_hung.load(std::memory_order_relaxed);}
extern "C" int sm64ds_gpu_unusable(void){return g.gpu_hung.load(std::memory_order_relaxed)||g.gpu_blocked.load(std::memory_order_relaxed);}
extern "C" void sm64ds_gpu_block(int blocked){g.gpu_blocked.store(blocked!=0);sm64ds_watchdog_crumb(blocked?"GPU: bloqueada (render vivo en HOME)":"GPU: desbloqueada",0,0);}
#endif
