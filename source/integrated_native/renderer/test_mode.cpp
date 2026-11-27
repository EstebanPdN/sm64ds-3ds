// SELECT+START automated hardware test, after the Worms 3D port's mode.
// SELECT+START (anywhere): a tour of every level. Each level starts from a
// fresh boot through the HOME chainloader, enters with the retail StartFile,
// plays the 24-situation script from its entrance, then visits up to four
// other spots of the level (BUILD131), and writes one report per situation
// under sdmc:/3ds/sm64ds/prueba/NN/LL-level/. The first report of a level
// and of each zone also writes a deep dump (GPU commands, draws, scene,
// textures; DS video memory and process RAM (excluding devices) once per
// level or run). A crash or power-off is recorded and the next launch
// continues with the following level.
// R+SELECT+START (in a level): only the current spot, as in BUILD 113.
// START cancels. Retail APIs only (StartFile, SetRealCharacter). A run plays
// on its own save, prueba/partida.sav (BUILD124); the owner's is untouched.
// BUILD127: a run never sleeps, so it continues with the lid closed (screens
// dark meanwhile); the notification LED turns green when it finishes.
#if defined(SM64DS_FRESH_NATIVE_GAME)&&defined(__3DS__)
#include "test_mode.h"
#include "hang_watchdog.hpp"

#ifdef SM64DS_PUBLIC_BETA
// The HID wrappers use the real controls whenever these providers return zero.
// The weak boot-resume and chainloader hooks stay unbound in this build.
namespace { constexpr bool g_quit = false; }
extern "C" void sm64ds_test_mode_boot() {}
extern "C" void sm64ds_test_mode_boot_failed(const char*) {}
extern "C" void sm64ds_test_mode_scan(std::uint32_t, std::uint32_t) {}
extern "C" void sm64ds_test_mode_update() {}
extern "C" int sm64ds_test_mode_keys(std::uint32_t*) { return 0; }
extern "C" int sm64ds_test_mode_circle(int*, int*) { return 0; }
extern "C" int sm64ds_test_mode_cstick(int*, int*) { return 0; }
extern "C" int sm64ds_test_mode_touch(int*, int*) { return 0; }
extern "C" std::uint32_t sm64ds_test_mode_strip(const std::uint32_t**) { return 0; }
extern "C" std::uint32_t sm64ds_test_mode_panel(const std::uint32_t**) { return 0; }
#else
#include "test_mode_script.hpp"
#include "test_mode_journey.hpp"
#include "build_version.hpp"
#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#if __has_include("../enhance_overlay.h")
#include "../enhance_overlay.h"
#include "../native_lower_painter.hpp"
#define SM64DS_TEST_MODE_TEXT 1
#endif
extern "C" {
unsigned sm64ds_render60_scene() __attribute__((weak));
}
namespace tm_=sm64ds::test_mode;
namespace {
constexpr const char* kBase="sdmc:/3ds/sm64ds/prueba";
constexpr const char* kStatePath="sdmc:/3ds/sm64ds/prueba/estado.txt";
constexpr const char* kRunSave="sdmc:/3ds/sm64ds/prueba/partida.sav";
// BUILD131 deep dumps. ram.txt: the owner asks for the whole process memory
// in every level's dump (about 80 MB each) instead of the first level only.
constexpr const char* kRamEveryLevel="sdmc:/3ds/sm64ds/prueba/ram.txt";
constexpr int kDumpRam=1,kDumpVideo=2,kDump2D=8;
bool g_dump_due=false;int g_dump_flags=0;unsigned g_dumps=0;
struct ZoneNote {std::uint16_t actor=0;std::uint32_t unique=0;std::int32_t distance=0;} g_zone_note[tm_::kMaxZones];
tm_::State g_state;
tm_::Runner g_runner;
tm_::JourneyRunner g_journey; // BUILD137 player journey (L+SELECT+START)
bool g_active=false,g_quit=false,g_finished_written=false,g_final_panel=false;
unsigned g_updates=0;
std::uint64_t g_window_tick=0,g_run_started=0,g_start_file_tick=0,g_load_ms=0;
tm_::Command g_command;
bool g_have_command=false;
// Published images: the game writes slot (revision+1)&1 while the worker may
// still upload slot revision&1. Status changes are seconds apart, far longer
// than one upload, so a slot is never rewritten while it is being read.
std::uint32_t g_strip[2][256*16],g_panel[2][320*240];
std::atomic<std::uint32_t> g_strip_revision{0},g_panel_revision{0};
char g_status[48]="";
// This level (tour) or this run (spot): average FPS and worst p95.
float g_fps_sum=0;unsigned g_fps_reports=0;float g_worst_p95=0;
unsigned g_failed_reports=0; // missing screenshots/files or no image measured

// BUILD127: an unattended run owns the console's power. From its first boot
// until START on the final panel the console never sleeps (__appInit below
// covers the first APT wakeup of every relaunched level), the screens go
// dark while the lid is closed, and the notification LED turns green when
// the run finishes or red when the tour cannot continue. A refused service
// only loses that comfort, never the run.
struct Power {
    enum Led {off,green,red};
    bool shell=false,lcd=false,led=false;std::atomic<bool> dark{false};
    // BUILD129: the BUILD128 tour polled the shell from the game thread every
    // 15 updates; 92 of its 95 game stalls of 100-314 ms were those IPC
    // round trips (ptm/gsp answer on the system core). A low-priority thread
    // on the game's core polls twice a second instead and only runs while
    // the game thread waits for its frame.
    Thread watcher=nullptr;LightLock lock;
    void begin(){
        aptSetSleepAllowed(false);
        if(!shell)shell=R_SUCCEEDED(ptmuInit());
        if(!lcd)lcd=R_SUCCEEDED(gspLcdInit());
        if(shell&&lcd&&!watcher){LightLock_Init(&lock);watcher=threadCreate(watch,this,4096,0x3e,-2,true);}
        std::printf("PRUEBA power: sleep refused=%u shell=%u lcd=%u watcher=%u\n",unsigned(!aptIsSleepAllowed()),unsigned(shell),
            unsigned(lcd),unsigned(watcher!=nullptr));
    }
    // Dark screens while the lid is closed (runs until the process exits).
    // The first poll of each boot applies the lid state unconditionally: a
    // relaunched level starts with whatever the previous boot and
    // setup_power's guard left (lit with the lid closed, or dark with it open).
    static void watch(void* self){
        auto& power=*static_cast<Power*>(self);bool known=false;
        for(;;){
            svcSleepThread(500000000LL);
            u8 open=1;if(R_FAILED(PTMU_GetShellState(&open)))continue;
            LightLock_Lock(&power.lock);
            if(!open&&(!known||!power.dark)){if(R_SUCCEEDED(GSPLCD_PowerOffAllBacklights())){power.dark=true;known=true;}}
            else if(open&&(!known||power.dark)){if(R_SUCCEEDED(GSPLCD_PowerOnAllBacklights())){power.dark=false;known=true;}}
            LightLock_Unlock(&power.lock);
        }
    }
    void screens_on(){
        if(!watcher){if(dark&&R_SUCCEEDED(GSPLCD_PowerOnAllBacklights()))dark=false;return;}
        LightLock_Lock(&lock);if(dark&&R_SUCCEEDED(GSPLCD_PowerOnAllBacklights()))dark=false;LightLock_Unlock(&lock);
    }
    // Solid colour (all 32 steps lit, looping) or off (dark, played once),
    // the pattern form setup_power's completion notice uses.
    // Logged and flushed: the LED is the run's only signal with the lid closed.
    void light(Led colour){
        if(!led){const Result init=mcuHwcInit();led=R_SUCCEEDED(init);
            if(!led){std::printf("PRUEBA notification LED unavailable: mcu::HWC %08lx\n",(unsigned long)init);std::fflush(stdout);return;}}
        InfoLedPattern pattern{};pattern.delay=1;pattern.loopDelay=colour==off?0xff:0;
        if(colour!=off)std::memset(colour==green?pattern.greenPattern:pattern.redPattern,0xff,sizeof pattern.greenPattern);
        const Result result=MCUHWC_SetInfoLedPattern(&pattern);
        std::printf("PRUEBA notification LED %s result=%08lx\n",colour==green?"green":colour==red?"red":"off",(unsigned long)result);
        std::fflush(stdout);
    }
} g_power;

bool tour(){return g_state.mode==tm_::Mode::tour;}
bool journey(){return g_state.mode==tm_::Mode::journey;}
// The run is over (final panel shown, waiting for START).
bool run_over(){return journey()?g_finished_written:g_runner.stage()==tm_::Runner::Stage::done;}
void run_dir(char* out,std::size_t size){std::snprintf(out,size,"%s/%02u",kBase,g_state.run);}
// Folder of this level's situations (tour) or of the spot run.
void level_dir(char* out,std::size_t size){
    char run[64],level[48];run_dir(run,sizeof run);
    if(tour())tm_::level_folder(level,sizeof level,g_state.index);
    else std::snprintf(level,sizeof level,"%s",journey()?"jugador":"informes");
    std::snprintf(out,size,"%s/%s",run,level);
}
bool write_state(){
    char text[640];const int n=tm_::format_state(g_state,text,sizeof text);if(n<=0||std::size_t(n)>=sizeof text)return false;
    // Replace through a temporary file so a power cut never leaves half a state.
    char temporary[96];std::snprintf(temporary,sizeof temporary,"%s.tmp",kStatePath);
    FILE* f=std::fopen(temporary,"wb");if(!f)return false;
    const bool ok=std::fwrite(text,1,std::size_t(n),f)==std::size_t(n);
    if(std::fclose(f)!=0||!ok){std::remove(temporary);return false;}
    std::remove(kStatePath);return std::rename(temporary,kStatePath)==0;
}
void clear_state(){std::remove(kStatePath);}
void result_line(const char* text){
    char dir[64],path[96];run_dir(dir,sizeof dir);std::snprintf(path,sizeof path,"%s/resultado.txt",dir);
    if(FILE* f=std::fopen(path,"ab")){std::fputs(text,f);std::fputc('\n',f);std::fclose(f);}
}
void publish_strip(const char* text){
    if(!std::strcmp(text,g_status))return;
    std::snprintf(g_status,sizeof g_status,"%s",text);
#ifdef SM64DS_TEST_MODE_TEXT
    auto* strip=g_strip[(g_strip_revision.load(std::memory_order_relaxed)+1)&1];
    std::memset(strip,0,sizeof g_strip[0]);
    enhance::draw_game_text(strip,256,16,0,0,g_status,true);
    g_strip_revision.fetch_add(1,std::memory_order_release);
#endif
}
void publish_panel(bool cancelled,const char* failure){
#ifdef SM64DS_TEST_MODE_TEXT
    auto* panel=g_panel[(g_panel_revision.load(std::memory_order_relaxed)+1)&1];
    struct Canvas {std::uint32_t* p;void pixel(int x,int y,std::uint32_t c){if(x>=0&&y>=0&&x<320&&y<240)p[y*320+x]=c;}} canvas{panel};
    std::memset(panel,0,sizeof g_panel[0]);
    native_lower::frame(canvas,20,32,280,176);
    char line[64];
    if(tour()){
        const auto& t=g_state.totals;
        native_lower::centered(canvas,44,cancelled?"RECORRIDO CANCELADO":"RECORRIDO FINALIZADO",0xff8d4300,true);
        std::snprintf(line,sizeof line,"Niveles: %u completos de %u",t.complete,tm_::kTourLevels);native_lower::centered(canvas,74,line,0xff3c3c3c);
        std::snprintf(line,sizeof line,"Incompletos %u  Cuelgues %u",t.incomplete,t.crashed);native_lower::centered(canvas,94,line,0xff3c3c3c);
        if(t.fps_levels){std::snprintf(line,sizeof line,"FPS medio %.1f  p95 peor %.1f ms",t.fps_tenths/10.0/t.fps_levels,t.worst_p95_tenths/10.0);
            native_lower::centered(canvas,114,line,0xff3c3c3c);}
    }else if(journey()){
        native_lower::centered(canvas,44,cancelled?"RECORRIDO CANCELADO":g_journey.failure()?"RECORRIDO INCOMPLETO":"RECORRIDO COMPLETO",0xff8d4300,true);
        std::snprintf(line,sizeof line,"Informes: %u de %u",g_journey.reports(),tm_::kJourneyPhase1Reports);native_lower::centered(canvas,76,line,0xff3c3c3c);
        std::snprintf(line,sizeof line,"Cuelgues detectados: %u",sm64ds_watchdog_hangs());native_lower::centered(canvas,96,line,0xff3c3c3c);
        if(g_fps_reports){std::snprintf(line,sizeof line,"FPS medio %.1f  p95 peor %.1f ms",g_fps_sum/g_fps_reports,g_worst_p95);native_lower::centered(canvas,116,line,0xff3c3c3c);}
    }else{
        native_lower::centered(canvas,44,cancelled?"PRUEBA CANCELADA":"PRUEBA FINALIZADA",0xff8d4300,true);
        std::snprintf(line,sizeof line,"Informes: %u de %u",g_runner.reports(),g_runner.expected_reports());native_lower::centered(canvas,76,line,0xff3c3c3c);
        if(g_fps_reports){std::snprintf(line,sizeof line,"FPS medio %.1f  p95 peor %.1f ms",g_fps_sum/g_fps_reports,g_worst_p95);native_lower::centered(canvas,96,line,0xff3c3c3c);}
    }
    if(failure)native_lower::centered(canvas,136,failure,0xff9c1c1c);
    std::snprintf(line,sizeof line,"Resultados: 3ds/sm64ds/prueba/%02u",g_state.run);native_lower::centered(canvas,158,line,0xff3c3c3c);
    native_lower::centered(canvas,184,"START: reiniciar el juego",0xff8d4300);
    g_panel_revision.fetch_add(1,std::memory_order_release);
#endif
    (void)cancelled;(void)failure;
}
// Restart through the HOME Menu chainloader (real console). Emulators may
// only exit; the next manual launch continues from the saved state.
void restart(){aptSetChainloaderToSelf();g_quit=true;}
// -1: no source file; 0: copy failed; 1: copied.
int copy_file(const char* from,const char* to){
    FILE* in=std::fopen(from,"rb");if(!in)return -1;
    FILE* out=std::fopen(to,"wb");if(!out){std::fclose(in);return 0;}
    static char buffer[4096];std::size_t n;bool ok=true;
    while((n=std::fread(buffer,1,sizeof buffer,in))>0)ok=std::fwrite(buffer,1,n,out)==n&&ok;
    ok=std::fclose(out)==0&&ok;std::fclose(in);return ok?1:0;
}
// BUILD115: the boot that died left its log as *-anterior (game_entry
// renames it before opening a new one); keep it beside that level's reports.
bool keep_previous_log(){
    static const char* const names[]={"sdmc:/3ds/sm64ds/native-test-anterior.log","sdmc:/3ds/sm64ds/log-anterior.txt","sdmc:/3ds/sm64ds/user-test-anterior.log"};
    char dir[96],path[128];level_dir(dir,sizeof dir);mkdir(dir,0777);
    std::snprintf(path,sizeof path,"%s/registro_cuelgue.txt",dir);
    for(const char* name:names){const int copied=copy_file(name,path);if(copied>=0)return copied==1;}
    return false;
}
// BUILD124: the run's own save. The BUILD123 hardware tour played on the
// owner's file: with that progress the castle courtyard opened the
// course-clear save menu (14 of its 20 situations measured the menu, 15 FPS)
// and the script's A presses could save over it. A tour starts every level
// from a new file, as each Azahar session does; a spot run keeps the copy
// of the owner's save taken at the chord. Read lazily by save_cart.cpp.
void use_run_save(){
    if(tour()||journey())std::remove(kRunSave); // a journey starts a new game on File A
    setenv("SM64DS_SAVE_PATH",kRunSave,1);
}

unsigned current_scene(){return sm64ds_render60_scene?sm64ds_render60_scene():0;}
tm_::Observation observe(){
    tm_::Observation o;o.updates=g_updates;o.scene=current_scene();
    Sm64dsTestPlayer p;sm64ds_test_game_observe(o.scene,&p);
    o.level=p.level;o.player=p.present;o.dialogue=p.dialogue;o.paused=p.paused;
    if(p.present){o.character=p.character;o.area=p.area;o.x=p.x;o.y=p.y;o.z=p.z;o.angle=std::int16_t(p.angle);o.body=p.body;o.control=p.control;o.pause_locked=p.pause_locked;}
    return o;
}
bool teleport(){
    const auto& s=g_runner.target(); // the home spot or the zone being entered
    if(!s.position)return false;
    if(!sm64ds_test_game_teleport(current_scene(),s.area,s.x,s.y,s.z,s.angle)){std::printf("PRUEBA teleport refused (area %d)\n",s.area);return false;}
    return true;
}
// BUILD131: other spots of this level. Candidates are live actors of the
// player's area that stand on ground (the game's own floor probe), far from
// the entrance; farthest-point sampling keeps up to four of them apart from
// the entrance and from each other. Distances are whole units (20.12 >> 12).
void choose_zones(){
    static Sm64dsTestActor actors[512];
    struct Candidate {tm_::Spot spot;std::uint16_t actor;std::uint32_t unique;long long nearest;bool used;};
    static Candidate candidates[512];
    constexpr long long kApart=1200;                       // from the entrance and between zones
    constexpr std::int32_t kBelow=400<<12,kAbove=120<<12;  // floor this close to the actor's own height
    constexpr long long kStandOff=220;                     // beside the actor, toward the entrance
    constexpr std::int32_t kStep=150<<12;                  // floor there within this of the actor's floor
    unsigned limit=tm_::kMaxZones;
    if(const char* e=std::getenv("SM64DS_PRUEBA_ZONAS"))limit=std::min<unsigned>(tm_::kMaxZones,unsigned(std::strtoul(e,nullptr,10)));
    const auto& home=g_runner.home();const auto o=observe();
    const unsigned total=sm64ds_test_game_actors(actors,512);unsigned count=0,grounded=0;
    const auto apart=[](const tm_::Spot& a,const tm_::Spot& b){
        const long long dx=(a.x-b.x)>>12,dy=(a.y-b.y)>>12,dz=(a.z-b.z)>>12;return dx*dx+dy*dy+dz*dz;};
    if(home.position&&o.player&&limit)for(unsigned i=0;i<total&&i<512;++i){const auto& a=actors[i];
        if(a.player||a.alive==2||(a.area>=0&&a.area!=home.area))continue;
        std::int32_t floor=0;
        if(!sm64ds_test_game_floor(a.x,a.y,a.z,&floor)||floor<a.y-kBelow||floor>a.y+kAbove)continue;
        ++grounded;
        // Stand beside the actor, kStandOff units toward the entrance, on a
        // floor at about the same height: landing on an enemy or inside an
        // object would test the damage animation, not the place.
        const long long hx=(home.x-a.x)>>12,hz=(home.z-a.z)>>12;long long length=0;
        while((length+1)*(length+1)<=hx*hx+hz*hz)length+=hx*hx+hz*hz>(length+64)*(length+64)?64:1;
        if(length<kApart)continue;
        tm_::Spot s=home;s.x=a.x+std::int32_t(hx*kStandOff/length)*4096;s.z=a.z+std::int32_t(hz*kStandOff/length)*4096;
        std::int32_t beside=0;
        if(!sm64ds_test_game_floor(s.x,floor+kAbove,s.z,&beside)||beside<floor-kStep||beside>floor+kStep)continue;
        s.y=beside;
        const long long d=apart(s,home);
        if(d<kApart*kApart)continue;
        candidates[count++]={s,a.id,a.unique,d,false};
    }
    tm_::Spot spots[tm_::kMaxZones];unsigned chosen=0;
    while(chosen<limit){
        int best=-1;
        for(unsigned i=0;i<count;++i)if(!candidates[i].used&&candidates[i].nearest>=kApart*kApart&&(best<0||candidates[i].nearest>candidates[best].nearest))best=int(i);
        if(best<0)break;
        auto& c=candidates[best];c.used=true;spots[chosen]=c.spot;
        long long root=0;while((root+1)*(root+1)<=apart(c.spot,home))root+=apart(c.spot,home)>(root+64)*(root+64)?64:1;
        g_zone_note[chosen]={c.actor,c.unique,std::int32_t(root)};
        std::printf("PRUEBA zona %u: actor %u unico %lu en %ld,%ld,%ld a %lld unidades de la entrada\n",chosen+1,unsigned(c.actor),(unsigned long)c.unique,
            long(c.spot.x>>12),long(c.spot.y>>12),long(c.spot.z>>12),root);
        ++chosen;
        for(unsigned i=0;i<count;++i)if(!candidates[i].used)candidates[i].nearest=std::min(candidates[i].nearest,apart(candidates[i].spot,c.spot));
    }
    g_runner.set_zones(spots,chosen);
    char line[200];std::snprintf(line,sizeof line,"   -- zonas: %u elegidas de %u candidatos (%u actores, %u sobre suelo en el area %d)",chosen,count,total,grounded,home.area);
    result_line(line);std::printf("PRUEBA %s\n",line);
}
// Player state appended to a report's summary (no extra file on the SD).
void append_game_state(const char* stem){
    char path[176];std::snprintf(path,sizeof path,"%s.resumen.txt",stem);
    FILE* f=std::fopen(path,"ab");if(!f)return;
    const auto o=observe();Sm64dsTestPlayerState s;const bool have=sm64ds_test_game_player_state(o.scene,&s)!=0;
    std::fprintf(f,"\n== estado del juego ==\nescena=%u\nnivel=%d\narea=%d\nactualizaciones=%u\npersonaje=%d (%s)\ncontrol=%u\ndialogo=%u\npausa=%u\npausa_bloqueada=%u\n",
        o.scene,o.level,o.area,o.updates,o.character,tm_::character_name(o.character),unsigned(o.control),unsigned(o.dialogue),unsigned(o.paused),unsigned(o.pause_locked));
    if(o.player){
        // From above the feet: a probe that starts on the floor itself misses it.
        std::int32_t floor=0;const bool grounded=sm64ds_test_game_floor(o.x,o.y+(100<<12),o.z,&floor)!=0;
        std::fprintf(f,"posicion=%ld,%ld,%ld   (20.12; unidades = valor/4096)\nangulo=%d\nsuelo_debajo=%s%ld\n",long(o.x),long(o.y),long(o.z),int(o.angle),grounded?"":"ninguno ",long(floor));
    }
    if(have)std::fprintf(f,"velocidad_horizontal=%ld\nvelocidad_vertical=%ld\nsalud=%d\nbajo_agua=%d\nagua_poco_profunda=%d\nen_el_aire=%d\nclase_suelo=%d\nestado=%08lx\nbanderas_estado=%04x\ngorra=%04x\ntemporizador_poder=%u\n",
        long(s.horizontal_speed),long(s.vertical_speed),s.health,s.underwater,s.shallow_water,s.in_air,s.floor_class,(unsigned long)s.state,s.state_flags,s.cap_flags,s.powerup_timer);
    std::fprintf(f,"actores_vivos=%u\n",sm64ds_test_game_actors(nullptr,0));
    std::fclose(f);
}
// BUILD131: the deep dump that follows the first report of a level and of
// each zone. Files: docs/MODO_PRUEBA.md; reader: tools/test_mode_dump.py.
void write_dump(const char* dir,const tm_::Step& step){
    char folder[128];
    if(step.zone)std::snprintf(folder,sizeof folder,"%s/volcado-zona%u",dir,unsigned(step.zone));else std::snprintf(folder,sizeof folder,"%s/volcado",dir);
    mkdir(folder,0777);
    int flags=g_dump_flags;
    std::printf("PRUEBA volcado %s flags=%d\n",folder,flags);std::fflush(stdout); // attributable if the console stops here
    char summary[160]="";
    const int ok=sm64ds_fresh_native_test_dump(folder,flags,summary,sizeof summary);
    // Every live actor: what the level holds around this spot.
    char path[176];std::snprintf(path,sizeof path,"%s/actores.csv",folder);
    static Sm64dsTestActor actors[512];
    const unsigned total=sm64ds_test_game_actors(actors,512);const auto o=observe();
    if(FILE* f=std::fopen(path,"wb")){
        static char buffer[8192];std::setvbuf(f,buffer,_IOFBF,sizeof buffer);
        std::fprintf(f,"id_actor,unico,area,estado_vida,jugador,x,y,z,distancia_al_jugador\n");
        for(unsigned i=0;i<total&&i<512;++i){const auto& a=actors[i];
            const long long dx=(a.x-o.x)>>12,dy=(a.y-o.y)>>12,dz=(a.z-o.z)>>12;long long d2=dx*dx+dy*dy+dz*dz,d=0;
            while((d+1)*(d+1)<=d2)d+=d2>(d+64)*(d+64)?64:1; // integer square root, no libm on this path
            std::fprintf(f,"%u,%lu,%d,%u,%u,%ld,%ld,%ld,%lld\n",unsigned(a.id),(unsigned long)a.unique,int(a.area),unsigned(a.alive),unsigned(a.player),long(a.x),long(a.y),long(a.z),o.player?d:-1LL);}
        std::fclose(f);
    }
    char line[320];std::snprintf(line,sizeof line,"      volcado%s %s%s actores=%u",step.zone?" de zona":"",ok?"":"FALLO ",summary,total);
    result_line(line);std::printf("PRUEBA %s\n",line);
    ++g_dumps;
}
void write_report(){
    const tm_::Step* step=g_runner.report_step();if(!step)return;
    char dir[96],folder[48],path[160];
    if(g_runner.title_report()){run_dir(dir,sizeof dir);std::snprintf(folder,sizeof folder,"00-%s",step->id);}
    else{level_dir(dir,sizeof dir);tm_::report_folder(folder,sizeof folder,g_runner.reports(),*step);}
    // BUILD131: a file stem; the report's files are LL-level/SS-step.<kind>.
    std::snprintf(path,sizeof path,"%s/%s",dir,folder);
    char summary[256]="";
    // A zone's title says where it is: the actor the spot was taken from.
    char title[160];
    if(step->zone&&step->zone<=g_runner.zones()){const auto& z=g_runner.target();const auto& note=g_zone_note[step->zone-1];
        std::snprintf(title,sizeof title,"%s (junto al actor %u, a %ld unidades de la entrada: %ld,%ld,%ld)",step->why,unsigned(note.actor),long(note.distance),
            long(z.x>>12),long(z.y>>12),long(z.z>>12));}
    else std::snprintf(title,sizeof title,"%s",step->why);
    const auto begun=svcGetSystemTick();
    // BUILD121: every SD write of the report happens inside the drained
    // pause that the frame cadence excludes; no render job is in flight.
    const bool paused=sm64ds_fresh_native_report_pause(1)!=0;
    int ok=0;
    if(paused){
        mkdir(dir,0777);ok=sm64ds_fresh_native_test_report(path,g_window_tick,title,summary,sizeof summary);
        if(!g_runner.title_report())append_game_state(path);
    }
    char line[400];
    if(g_runner.title_report())std::snprintf(line,sizeof line,"   titulo %s%s",ok?"":"FALLO ",summary);
    else std::snprintf(line,sizeof line,"   %02u %-22s %s%s",g_runner.reports(),step->id,ok?"":"FALLO ",summary);
    result_line(line);
    if(!ok)++g_failed_reports;
    float fps=0,p95=0;
    if(const char* f=std::strstr(summary,"fps="))fps=std::strtof(f+4,nullptr);
    if(const char* q=std::strstr(summary,"p95="))p95=std::strtof(q+4,nullptr);
    if(fps>0&&!g_runner.title_report()){g_fps_sum+=fps;++g_fps_reports;if(p95>g_worst_p95)g_worst_p95=p95;}
    std::printf("PRUEBA report %s %s ok=%d writeMs=%.1f %s\n",folder,step->id,ok,
        double(svcGetSystemTick()-begun)*1000.0/double(SYSCLOCK_ARM11),summary);
    if(g_dump_due){
        g_dump_due=false;
        if(paused&&!g_runner.title_report())write_dump(dir,*step);else sm64ds_fresh_native_test_dump_arm(0);
    }
    if(paused)sm64ds_fresh_native_report_pause(0); // flushes the log, ends the pause
}
// BUILD137: one journey step: CSV, summary and screenshots, plus the game state.
// BUILD140: true when the renderer's failed bank-load count grew since the
// previous report (the summary carries the count since boot).
bool new_bank_failures(const char* summary){
    static unsigned long long seen=0;
    const char* at=std::strstr(summary,"cargas_fallidas=");if(!at)return false;
    const unsigned long long now=std::strtoull(at+16,nullptr,10);
    const bool grew=now>seen;seen=now;return grew;
}
void write_journey_report(){
    const auto& step=g_journey.report_step();
    char dir[96],path[160];level_dir(dir,sizeof dir);
    std::snprintf(path,sizeof path,"%s/%02u-%s",dir,g_journey.reports(),step.id);
    char summary[256]="";const auto begun=svcGetSystemTick();
    const bool paused=sm64ds_fresh_native_report_pause(1)!=0;int ok=0;
    if(paused){mkdir(dir,0777);ok=sm64ds_fresh_native_test_report(path,g_window_tick,step.why,summary,sizeof summary);append_game_state(path);}
    // BUILD140: a model bank that failed to load (missing terrain, sky or
    // body on screen) fails the step, so the run is not reported complete.
    const bool banks_failed=new_bank_failures(summary);
    char line[400];std::snprintf(line,sizeof line,"   %02u %-16s %s%s",g_journey.reports(),step.id,!ok?"FALLO ":banks_failed?"MODELOS SIN CARGAR ":"",summary);
    result_line(line);if(!ok||banks_failed)++g_failed_reports;
    float fps=0,p95=0;
    if(const char* f=std::strstr(summary,"fps="))fps=std::strtof(f+4,nullptr);
    if(const char* q=std::strstr(summary,"p95="))p95=std::strtof(q+4,nullptr);
    // A step measured across a scene load (the letter) keeps its p95 out of the run's worst.
    if(fps>0){g_fps_sum+=fps;++g_fps_reports;if(!step.load&&p95>g_worst_p95)g_worst_p95=p95;}
    std::printf("PRUEBA jugador %s ok=%d writeMs=%.1f %s\n",step.id,ok,double(svcGetSystemTick()-begun)*1000.0/double(SYSCLOCK_ARM11),summary);
    if(g_dump_due){g_dump_due=false;
        if(paused){char folder[176];std::snprintf(folder,sizeof folder,"%s-gpu",path);mkdir(folder,0777);
            char gpu_summary[160]="";const int good=sm64ds_fresh_native_test_dump(folder,0,gpu_summary,sizeof gpu_summary);
            std::snprintf(line,sizeof line,"      gpu %s%s",good?"":"FALLO ",gpu_summary);result_line(line);}
        else sm64ds_fresh_native_test_dump_arm(0);}
    if(paused)sm64ds_fresh_native_report_pause(0);
}
void final_screen(bool cancelled,const char* failure){
    g_final_panel=true;
    publish_panel(cancelled,failure);
    // The 256-pixel strip fits about 16 title-font characters; details
    // (and the START instruction) are on the lower panel.
    publish_strip(cancelled?"CANCELADA":tour()?"RECORRIDO FIN":"PRUEBA FIN");
}
// Tour: one level is over (complete, incomplete or cancelled).
void level_line(const char* verdict){
    const auto& level=tm_::kTour[g_state.index<tm_::kTourLevels?g_state.index:0];
    char line[480];
    char failed[32]="";if(g_failed_reports)std::snprintf(failed,sizeof failed," (%u fallidos)",g_failed_reports);
    char again[48]="";if(g_runner.reentries())std::snprintf(again,sizeof again,", reentradas %u",g_runner.reentries());
    if(g_runner.menu_closed())std::strncat(again,", menu cerrado con START",sizeof again-std::strlen(again)-1);
    // BUILD131: the entrance script decides the verdict; the zones are extra.
    char zones[96]="";
    if(g_runner.zones_known())std::snprintf(zones,sizeof zones,", zonas %u (informes %u/%u)%s%s",g_runner.zones(),g_runner.zone_reports(),
        g_runner.expected_zone_reports(),g_runner.zone_failure()?" interrumpidas: ":"",g_runner.zone_failure()?g_runner.zone_failure():"");
    std::snprintf(line,sizeof line,"== %02u/%02u nivel %u %s (%s%s): %s, informes %u/%u%s%s%s, carga %.1f s, FPS medio %.1f, peor p95 %.1f ms%s%s",
        g_state.index+1,tm_::kTourLevels,unsigned(level.id),level.slug,tm_::character_name(level.character),
        g_runner.character_kept()?", sin cambiar":g_runner.pause_lock_ignored()?", sin pausa":"",verdict,g_runner.base_reports(),g_runner.expected_base_reports(),
        failed,zones,again,double(g_load_ms)/1000.0,
        g_fps_reports?g_fps_sum/g_fps_reports:0.f,g_worst_p95,g_runner.failure()?": ":"",g_runner.failure()?g_runner.failure():"");
    result_line(line);std::printf("PRUEBA %s\n",line);
}
void tour_summary(bool cancelled){
    const auto& t=g_state.totals;char line[256];
    const double seconds=double(svcGetSystemTick()-g_run_started)/double(SYSCLOCK_ARM11);
    std::snprintf(line,sizeof line,"RECORRIDO %s: completos %u, incompletos %u, cuelgues %u de %u niveles; FPS medio %.1f, peor p95 %.1f ms (ultimo arranque %.0f s)",
        cancelled?"CANCELADO":"FINALIZADO",t.complete,t.incomplete,t.crashed,tm_::kTourLevels,
        t.fps_levels?t.fps_tenths/10.0/t.fps_levels:0.0,t.worst_p95_tenths/10.0,seconds);
    result_line(line);std::printf("PRUEBA %s\n",line);
}
void finish(bool cancelled){
    if(g_finished_written)return;
    g_finished_written=true;
    if(tour()){
        auto& t=g_state.totals;
        if(!cancelled){
            const bool complete=!g_runner.failure()&&g_runner.base_reports()==g_runner.expected_base_reports()&&!g_failed_reports;
            level_line(complete?"COMPLETO":"INCOMPLETO");
            ++(complete?t.complete:t.incomplete);
            if(g_fps_reports){t.fps_tenths+=unsigned(g_fps_sum/g_fps_reports*10.f+.5f);++t.fps_levels;}
            t.worst_p95_tenths=std::max(t.worst_p95_tenths,unsigned(g_worst_p95*10.f+.5f));
            ++g_state.index;g_state.resume={};
            if(g_state.index<tm_::kTourLevels){
                // Next level from a fresh boot: same memory, same title path.
                g_state.phase=tm_::Phase::pending;
                if(write_state()){publish_strip("SIGUIENTE NIVEL");restart();return;}
                result_line("RECORRIDO DETENIDO: no se pudo escribir el estado en la SD");
            }
        }else level_line("CANCELADO");
        tour_summary(cancelled);clear_state();
        const bool stopped=!cancelled&&g_state.index<tm_::kTourLevels;
        g_power.light(cancelled?Power::off:stopped?Power::red:Power::green);
        final_screen(cancelled,stopped?"no se pudo continuar":nullptr);
        return;
    }
    if(journey()){
        char line[420];const double seconds=double(svcGetSystemTick()-g_run_started)/double(SYSCLOCK_ARM11);
        // Failed report files make the run incomplete too (missing evidence).
        const char* verdict=cancelled?"CANCELADO":g_journey.failure()||g_failed_reports?"INCOMPLETO":"COMPLETO";
        std::snprintf(line,sizeof line,"RECORRIDO DE JUGADOR %s tras %.0f s: informes %u/%u%s, textos avanzados %u, saltos %u, punto de ruta %u/%u, "
            "cuelgues detectados por el vigilante %u, FPS medio %.1f, peor p95 %.1f ms%s%s",
            verdict,seconds,g_journey.reports(),tm_::kJourneyPhase1Reports,g_failed_reports?" (con fallos de informe)":"",g_journey.texts(),g_journey.stuck_jumps(),
            g_journey.waypoint(),g_journey.route_points(),sm64ds_watchdog_hangs(),g_fps_reports?g_fps_sum/g_fps_reports:0.f,g_worst_p95,
            g_journey.failure()?": ":"",g_journey.failure()?g_journey.failure():"");
        result_line(line);std::printf("PRUEBA %s\n",line);
        clear_state();
        g_power.light(cancelled?Power::off:g_journey.failure()||g_failed_reports||sm64ds_watchdog_hangs()?Power::red:Power::green);
        final_screen(cancelled,g_journey.failure());
        return;
    }
    char line[256];
    const double seconds=double(svcGetSystemTick()-g_run_started)/double(SYSCLOCK_ARM11);
    if(cancelled)std::snprintf(line,sizeof line,"CANCELADA con START tras %.0f s, informes %u",seconds,g_runner.reports());
    else std::snprintf(line,sizeof line,"%s: %.0f s, informes %u/%u, reintentos %u, punto exacto %s, FPS medio %.1f, peor p95 %.1f ms%s%s",
        g_runner.failure()?"INCOMPLETA":"COMPLETA",seconds,g_runner.reports(),g_runner.expected_reports(),g_runner.retries(),
        g_runner.teleported()?"si":"no",g_fps_reports?g_fps_sum/g_fps_reports:0.f,g_worst_p95,
        g_runner.failure()?": ":"",g_runner.failure()?g_runner.failure():"");
    result_line(line);
    // The next boot is the ordinary game; results stay in prueba/NN.
    clear_state();
    g_power.light(cancelled?Power::off:Power::green);
    final_screen(cancelled,g_runner.failure());
    std::printf("PRUEBA finished: %s\n",line);
}
// BUILD124: the script killed the player after the level ran (lava, a fall);
// a fresh boot enters it again and continues after the fatal situation,
// keeping this level's reports and averages. Without a writable state the
// level ends here, as before.
void reenter_level(){
    if(g_finished_written)return;
    auto& r=g_state.resume;
    const auto* fatal=g_runner.resume_step()?&tm_::kScript[g_runner.resume_step()-1]:nullptr;
    char line[240];
    std::snprintf(line,sizeof line,"   -- %s durante \"%s\": reentrada %u de %u, sigue en \"%s\"",g_runner.failure(),
        fatal?fatal->id:"?",r.reentries+1,tm_::kMaxReentries,tm_::kScript[g_runner.resume_step()].id);
    r.step=g_runner.resume_step();r.reports=g_runner.reports();r.zone_reports=g_runner.zone_reports();++r.reentries;r.failed=g_failed_reports;
    r.fps_hundredths=unsigned(g_fps_sum*100.f+.5f);r.fps_reports=g_fps_reports;r.p95_tenths=unsigned(g_worst_p95*10.f+.5f);
    g_state.phase=tm_::Phase::pending;
    if(write_state()){g_finished_written=true;result_line(line);std::printf("PRUEBA %s\n",line);publish_strip("REENTRANDO");restart();return;}
    finish(false);
}
// Start the level the state points at (this boot owns it from now on).
bool begin_level(){
    g_state.phase=tm_::Phase::running;
    if(!write_state()){std::puts("PRUEBA: state not writable; run skipped");return false;}
    use_run_save();
    g_power.begin();
    if(!tour()||(!g_state.index&&!g_state.resume.reentries))g_power.light(Power::off);
    // Fine renderer/producer timers for the report CSVs (cost ~1 ms/frame).
    setenv("SM64DS_FRESH_NATIVE_PROFILE","1",1);
    g_active=true;g_run_started=svcGetSystemTick();
    // env.txt (development .3dsx only; the CIA never reads it): fewer steps.
    unsigned steps=0;if(const char* e=std::getenv("SM64DS_PRUEBA_PASOS"))steps=unsigned(std::strtoul(e,nullptr,10));
    bool is_new=false;APT_CheckNew3DS(&is_new);
    char line[256];
    if(journey()){
        g_journey.start();
        std::snprintf(line,sizeof line,"recorrido de jugador %s, %s 3DS: titulo, menu, Adventure, seleccion de partida, partida nueva en File A, intro completa, "
            "jardines hasta la puerta del castillo (cerrada: la llave la tiene el conejo); un informe por paso; cuelgues en 3ds/sm64ds/cuelgues",
            sm64ds_build::label,is_new?"New":"Old");
        result_line(line);std::printf("PRUEBA boot: %s\n",line);
        publish_strip("JUGADOR");
        return true;
    }
    if(tour()){
        const auto spot=tm_::tour_spot(g_state.index);
        // No title report: in PLAYABLE mode the title publishes too few
        // measurable images (most of its frames stay unpublished).
        // BUILD124: a re-entered level continues with its earlier boots' data.
        const auto& resume=g_state.resume;
        g_runner.start(spot,true,false,resume);g_runner.limit_steps(steps);
        g_fps_sum=float(resume.fps_hundredths)/100.f;g_fps_reports=resume.fps_reports;
        g_worst_p95=float(resume.p95_tenths)/10.f;g_failed_reports=resume.failed;
        if(resume.reentries)std::printf("PRUEBA tour level %u re-entered (%u/%u) at step %u, %u reports kept\n",
            g_state.index+1,resume.reentries,tm_::kMaxReentries,resume.step,resume.reports);
        if(g_state.index==0){
            std::snprintf(line,sizeof line,"recorrido automatico %s, %s 3DS: %u niveles, %u situaciones en la entrada de cada nivel y 3 en cada una de hasta %u zonas, volcados por nivel y zona, partida nueva en cada nivel",
                sm64ds_build::label,is_new?"New":"Old",tm_::kTourLevels,tm_::kBaseReports,tm_::kMaxZones);
            result_line(line);
        }
        std::printf("PRUEBA tour level %u/%u id=%d character=%d\n",g_state.index+1,tm_::kTourLevels,spot.level,spot.character);
    }else{
        g_runner.start(g_state.spot);g_runner.limit_steps(steps);
        std::snprintf(line,sizeof line,"prueba automatica %s, %s 3DS: nivel %d area %d personaje %d punto %s (%ld,%ld,%ld)",
            sm64ds_build::label,is_new?"New":"Old",g_state.spot.level,g_state.spot.area,g_state.spot.character,
            g_state.spot.position?"guardado":"entrada",long(g_state.spot.x),long(g_state.spot.y),long(g_state.spot.z));
        result_line(line);std::printf("PRUEBA boot: %s\n",line);
    }
    publish_strip("PRUEBA");
    return true;
}
}
// userAppExit on an abnormal exit (abort, terminate): keep the unattended
// tour going. The next boot records this level as crashed and continues.
// Not when HOME asked the game to close.
extern "C" int sm64ds_test_mode_exit_relaunch(){
    if(!g_active||!tour()||g_runner.stage()==tm_::Runner::Stage::done||aptShouldClose())return 0;
    aptSetChainloaderToSelf();return 1;
}
// BUILD121: boot 25 of the BUILD 120 tour could not create the render worker
// and boot 26 not even the game thread; the system needed a reboot. Keep the
// level for the next boot (not counted as crashed) and return to HOME instead
// of relaunching into the same failure.
extern "C" void sm64ds_test_mode_boot_failed(const char* what){
    if(!g_active||!tour()||g_finished_written)return;
    g_finished_written=true;
    const auto& level=tm_::kTour[g_state.index<tm_::kTourLevels?g_state.index:0];char line[320];
    std::snprintf(line,sizeof line,"== %02u/%02u nivel %u %s: NO ARRANCO, %s. Reinicia la consola y abre el juego: el recorrido repetira este nivel",
        g_state.index+1,tm_::kTourLevels,unsigned(level.id),level.slug,what?what:"fallo de arranque");
    result_line(line);std::printf("PRUEBA %s\n",line);
    g_state.phase=tm_::Phase::pending;
    if(!write_state())result_line("RECORRIDO DETENIDO: no se pudo escribir el estado en la SD");
    g_runner.cancel("sin hilos");
    g_power.screens_on();g_power.light(Power::red);
    g_quit=true; // HOME, no chainloader: only a reboot frees the system
}
// A run is pending or in progress while its state file exists (it is removed
// when the run ends or is cancelled). setup_power waits for the lid only
// outside runs.
extern "C" int sm64ds_test_mode_pending(void){struct stat st;return stat(kStatePath,&st)==0;}
// BUILD127: libctru's aptInit asks APT to sleep if the shell is closed right
// after the first wakeup, with sleep allowed by default, so a tour level
// relaunched with the lid closed would sleep before main. Refuse sleep at once
// (nothing blocks in between; APT's query is answered by libctru's event
// thread, lower priority on this core), then allow it again unless a test
// run is in progress. Otherwise libctru's own __appInit, step by step.
extern "C" void __attribute__((weak)) userAppInit(void);
extern "C" void __appInit(void){
    srvInit();aptInit();
    aptSetSleepAllowed(false);
    hidInit();fsInit();archiveMountSdmc();
    if(!sm64ds_test_mode_pending())aptSetSleepAllowed(true);
    if(&userAppInit)userAppInit();
}
extern "C" void sm64ds_test_mode_boot(){
    char text[2049];FILE* f=std::fopen(kStatePath,"rb");if(!f)return;
    const std::size_t n=std::fread(text,1,sizeof text-1,f);std::fclose(f);text[n]=0;
    tm_::State state;
    if(!tm_::parse_state(text,n,state)){std::puts("PRUEBA: invalid state file removed");clear_state();aptSetSleepAllowed(true);return;}
    g_state=state;
    if(state.phase==tm_::Phase::running){
        // The previous boot died during the run (crash, HOME, power): record it.
        if(journey()){
            const bool kept=keep_previous_log();
            char line[300];std::snprintf(line,sizeof line,"RECORRIDO DE JUGADOR NO TERMINO: el juego se cerro, se colgo o se apago durante el recorrido (%s; informes del vigilante en 3ds/sm64ds/cuelgues)",
                kept?"registro del arranque anterior: jugador/registro_cuelgue.txt":"sin registro del arranque anterior");
            result_line(line);clear_state();aptSetSleepAllowed(true);return;
        }
        if(!tour()){result_line("NO TERMINO: el juego se cerro o fallo durante la prueba (ver log)");clear_state();aptSetSleepAllowed(true);return;}
        const auto& level=tm_::kTour[g_state.index];char line[240];
        const bool kept=keep_previous_log();
        std::snprintf(line,sizeof line,"== %02u/%02u nivel %u %s (%s): CUELGUE O CIERRE, el juego no termino este nivel (%s)",
            g_state.index+1,tm_::kTourLevels,unsigned(level.id),level.slug,tm_::character_name(level.character),
            kept?"registro en la carpeta del nivel: registro_cuelgue.txt":"ver luma/dumps y el log");
        result_line(line);++g_state.totals.crashed;++g_state.index;g_state.resume={};
        if(g_state.index>=tm_::kTourLevels){g_power.begin();tour_summary(false);clear_state();g_active=true;g_runner.cancel("fin");
            g_power.light(Power::green);final_screen(false,nullptr);return;}
        // Continue with the next level in this boot.
    }else if(state.phase!=tm_::Phase::pending){clear_state();aptSetSleepAllowed(true);return;}
    if(!begin_level())aptSetSleepAllowed(true);
}
extern "C" void sm64ds_test_mode_scan(std::uint32_t held,std::uint32_t down){
    const std::uint32_t chord=tm_::kSelect|tm_::kStart;
    const bool chord_now=(held&chord)==chord&&(down&chord);
    if(g_active){
        if(run_over()){
            if(g_final_panel&&(down&tm_::kStart)){g_power.light(Power::off);g_power.screens_on();restart();}
            return;
        }
        // START cancels; the final panel stays until START restarts the game.
        if(down&tm_::kStart){if(!journey())g_runner.cancel("cancelada con START");finish(true);}
        return;
    }
    // BUILD137: a SELECT+START without L or R waits up to 12 scans for one of
    // them to join, so the order of the presses does not start the wrong run.
    static unsigned chord_wait=0;
    if(g_quit||(!chord_now&&!chord_wait))return;
    const bool spot=(held&tm_::kR)!=0;
    // L+SELECT+START, the player journey (anywhere).
    const bool walk=!spot&&(held&tm_::kL)!=0;
    if(!spot&&!walk&&(chord_wait?++chord_wait<=12:(chord_wait=1,true)))return;
    chord_wait=0;
    Sm64dsTestPlayer p;sm64ds_test_game_observe(current_scene(),&p);
    if(spot&&!p.present){std::puts("PRUEBA: R+SELECT+START ignored outside a level");return;}
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/sm64ds",0777);mkdir(kBase,0777);
    unsigned run=1;
    for(;run<100;++run){char dir[64];std::snprintf(dir,sizeof dir,"%s/%02u",kBase,run);struct stat st;if(stat(dir,&st)!=0)break;}
    if(run>=100){std::puts("PRUEBA: 99 runs already on SD");return;}
    g_state={};g_state.run=run;g_state.phase=tm_::Phase::pending;
    if(walk){g_state.mode=tm_::Mode::journey;g_state.index=0;}
    else if(spot){
        auto& s=g_state.spot;g_state.mode=tm_::Mode::spot;
        s.level=p.level;s.area=p.area;s.character=p.character>=0&&p.character<4?p.character:0;
        s.x=p.x;s.y=p.y;s.z=p.z;s.angle=std::int16_t(p.angle);s.position=true;
    }else{g_state.mode=tm_::Mode::tour;g_state.index=0;g_state.spot=tm_::tour_spot(0);}
    char dir[64];run_dir(dir,sizeof dir);mkdir(dir,0777);
    // BUILD124: a spot run plays on a copy of the owner's save (use_run_save).
    std::remove(kRunSave);
    if(spot)if(const char* save=std::getenv("SM64DS_SAVE_PATH"))if(copy_file(save,kRunSave)==0){
        std::remove(kRunSave);std::puts("PRUEBA: save copy failed; the spot run starts from a new file");}
    if(!write_state()){std::puts("PRUEBA: could not write state");return;}
    std::printf("PRUEBA: %s run=%u; restarting\n",walk?"L+SELECT+START journey":spot?"R+SELECT+START spot":"SELECT+START tour",run);
    publish_strip("REINICIANDO");
    restart();
}
// BUILD129: BUILD128 kept simulating and rendering at full load for 66 min
// after the green LED, lid closed. The finished run parks: muted, one update
// every 50 ms (250 ms with the lid closed) while the final panel waits for
// START, which restarts into the ordinary game.
static void park(){
    static bool logged=false;
    if(!logged){logged=true;ndspSetMasterVol(0.f);std::puts("PRUEBA fin: juego en reposo hasta START");std::fflush(stdout);}
    svcSleepThread(g_power.dark?250000000LL:50000000LL);
}
extern "C" int sm64ds_title_menu_state() __attribute__((weak));
extern "C" void sm64ds_test_mode_update(){
    ++g_updates;
    // Development (env.txt, .3dsx only): the player's state every second, to
    // take positions for the journey script (SM64DS_PRUEBA_POSICION=1).
    {static const bool trace=std::getenv("SM64DS_PRUEBA_POSICION")!=nullptr;
     if(trace&&g_updates%30==0){const auto o=observe();
        std::printf("PRUEBA posicion: escena=%u menu=%d nivel=%d area=%d jugador=%u control=%u dialogo=%u pausa=%u x=%ld y=%ld z=%ld angulo=%d\n",
            o.scene,sm64ds_title_menu_state?sm64ds_title_menu_state():-1,o.level,o.area,unsigned(o.player),unsigned(o.control),unsigned(o.dialogue),unsigned(o.paused),
            long(o.x>>12),long(o.y>>12),long(o.z>>12),int(o.angle));
        // SM64DS_PRUEBA_POSICION=2: also the live actors near the player every 3 s.
        if(trace&&std::getenv("SM64DS_PRUEBA_POSICION")[0]=='2'&&o.player&&g_updates%90==0){
            static Sm64dsTestActor actors[512];const unsigned total=sm64ds_test_game_actors(actors,512);
            for(unsigned i=0;i<total&&i<512;++i){const auto& a=actors[i];
                const long long dx=(a.x-o.x)>>12,dz=(a.z-o.z)>>12;if(dx*dx+dz*dz>4000LL*4000LL||a.player)continue;
                std::printf("PRUEBA actor: id=%u unico=%lu area=%d vivo=%u x=%ld y=%ld z=%ld\n",unsigned(a.id),(unsigned long)a.unique,int(a.area),unsigned(a.alive),
                    long(a.x>>12),long(a.y>>12),long(a.z>>12));}
        }}}
    if(!g_active){g_have_command=false;return;}
    if(journey()){
        // Development (.3dsx with SM64DS_PRUEBA_RUTA=1 in env.txt; the CIA
        // never reads env.txt, so a stray file cannot change a console run):
        // prueba/ruta.txt overrides the castle route, one "x z radius" line
        // per point (whole units, radius > 0), read when the walk starts.
        {static bool route_read=false;
         if(!route_read&&g_journey.phase()==tm_::JourneyRunner::Phase::walk){route_read=true;
            if(FILE* f=std::getenv("SM64DS_PRUEBA_RUTA")?std::fopen("sdmc:/3ds/sm64ds/prueba/ruta.txt","r"):nullptr){
                tm_::Waypoint points[tm_::JourneyRunner::kMaxRoute];unsigned n=0;long x,z,r;
                while(n<tm_::JourneyRunner::kMaxRoute&&std::fscanf(f,"%ld %ld %ld",&x,&z,&r)==3)if(r>0)points[n++]={std::int32_t(x),std::int32_t(z),std::int32_t(r)};
                std::fclose(f);if(n){g_journey.set_route(points,n);char line[96];
                    std::snprintf(line,sizeof line,"   -- ruta de desarrollo prueba/ruta.txt: %u puntos (no la ruta normal)",n);result_line(line);std::printf("PRUEBA %s\n",line);}
            }}}
        if(g_final_panel&&run_over())park();
        if(run_over()){g_have_command=true;g_command={};g_command.inject=true;return;}
        tm_::JourneyObservation seen;seen.game=observe();seen.menu=sm64ds_title_menu_state?sm64ds_title_menu_state():-1;
        const auto c=g_journey.update(seen);
        g_command={};g_command.pad=c.pad;g_command.inject=c.inject;g_have_command=c.inject;
        // BUILD138: every journey step also records the GPU command list of
        // one image of its window (comandos_gpu.bin): the menu, the file
        // selector and the opening get a PICA stream that
        // tools/check_pica_hardware.py can check against the console's rules.
        if(c.window){g_window_tick=svcGetSystemTick();sm64ds_fresh_native_audio_window();g_dump_flags=0;g_dump_due=true;sm64ds_fresh_native_test_dump_arm(1);}
        if(c.action==tm_::JourneyAction::report)write_journey_report();
        if(c.action==tm_::JourneyAction::finish||g_journey.finished()){finish(false);return;}
        char status[48];std::snprintf(status,sizeof status,"JUGADOR %02u/%02u",g_journey.reports(),tm_::kJourneyPhase1Reports);publish_strip(status);
        return;
    }
    if(g_final_panel&&g_runner.stage()==tm_::Runner::Stage::done)park();
    if(g_runner.stage()==tm_::Runner::Stage::idle||g_runner.stage()==tm_::Runner::Stage::done){g_have_command=g_runner.stage()==tm_::Runner::Stage::done;g_command={};g_command.inject=true;return;}
    const auto o=observe();
    g_command=g_runner.update(o);g_have_command=g_command.inject;
    if(g_command.window){
        // BUILD131: the first situation of a level (first boot of it) and of
        // each zone ends with a deep dump; its GPU frame is captured from
        // the start of the window. START cancels before any of this.
        if(const auto* step=g_runner.current()){
            const bool level_start=g_runner.step()==0&&!g_state.resume.reentries;
            const bool zone_start=step->zone&&step->home;
            if((level_start||zone_start)&&g_runner.stage()==tm_::Runner::Stage::run){
                g_dump_flags=kDump2D;
                if(level_start){
                    g_dump_flags|=kDumpVideo;
                    struct stat st;
                    if(!tour()||g_state.index==0||stat(kRamEveryLevel,&st)==0)g_dump_flags|=kDumpRam;
                }
                g_dump_due=true;sm64ds_fresh_native_test_dump_arm(1);
            }
        }
        g_window_tick=svcGetSystemTick();sm64ds_fresh_native_audio_window();
    }
    switch(g_command.action){
    case tm_::Action::zones:choose_zones();break;
    case tm_::Action::start_file:{
        const int level=tour()?tm_::tour_spot(g_state.index).level:g_state.spot.level;
        sm64ds_test_game_start_file(level,!tour()&&g_state.spot.position?g_state.spot.area:-1);g_start_file_tick=svcGetSystemTick();g_load_ms=0;
        std::printf("PRUEBA StartFile level=%d\n",level);
        break;}
    case tm_::Action::set_character:sm64ds_test_game_set_character(g_runner.home().character);break;
    case tm_::Action::teleport:{
        const bool ok=teleport();g_runner.teleport_result(ok);
        std::printf("PRUEBA teleport ok=%d\n",int(ok));
        break;}
    case tm_::Action::report:write_report();break;
    case tm_::Action::finish:finish(false);break;
    case tm_::Action::reenter:reenter_level();return;
    default:break;
    }
    // Wall time from StartFile to the first controllable update (loads).
    if(g_start_file_tick&&!g_load_ms&&g_runner.load_updates())g_load_ms=(svcGetSystemTick()-g_start_file_tick)*1000/SYSCLOCK_ARM11;
    if(g_runner.stage()==tm_::Runner::Stage::done){finish(false);return;}
    char status[48];
    const auto stage=g_runner.stage();
    if(tour())std::snprintf(status,sizeof status,"NIVEL %02u %02u/%02u",g_state.index+1,g_runner.reports(),g_runner.expected_reports());
    else if(stage==tm_::Runner::Stage::run&&g_runner.current())std::snprintf(status,sizeof status,"PRUEBA %02u/%02u",g_runner.reports(),g_runner.expected_reports());
    else std::snprintf(status,sizeof status,"PRUEBA");
    publish_strip(status);
}
extern "C" int sm64ds_test_mode_keys(std::uint32_t* held){
    if(!g_active||!g_have_command)return 0;
    // START is still read for cancellation through sm64ds_test_mode_scan.
    *held=g_command.pad.held;return 1;
}
extern "C" int sm64ds_test_mode_circle(int* x,int* y){if(!g_active||!g_have_command)return 0;*x=g_command.pad.sx;*y=g_command.pad.sy;return 1;}
extern "C" int sm64ds_test_mode_cstick(int* x,int* y){if(!g_active||!g_have_command)return 0;*x=g_command.pad.cx;*y=g_command.pad.cy;return 1;}
extern "C" int sm64ds_test_mode_touch(int* x,int* y){
    if(!g_active||!g_have_command)return 0;
    if(g_command.pad.touch){*x=g_command.pad.tx;*y=g_command.pad.ty;}else{*x=0;*y=0;}
    return 1;
}
extern "C" std::uint32_t sm64ds_test_mode_strip(const std::uint32_t** pixels){
    const auto r=g_strip_revision.load(std::memory_order_acquire);if(r)*pixels=g_strip[r&1];return r;
}
extern "C" std::uint32_t sm64ds_test_mode_panel(const std::uint32_t** pixels){
    const auto r=g_panel_revision.load(std::memory_order_acquire);if(r)*pixels=g_panel[r&1];return r;
}
#endif // SM64DS_PUBLIC_BETA

// The game loop ends like a HOME close, so the chainloader relaunches us.
extern "C" bool __real_aptMainLoop(void);
// BUILD137: the hang watchdog sees every entry (HOME, POWER and sleep are
// only answered here, on the host thread).
extern "C" bool __wrap_aptMainLoop(void){
    sm64ds_watchdog_host_stage(SM64DS_WATCH_HOST_APT);
    const bool run=!g_quit&&__real_aptMainLoop();
    sm64ds_watchdog_host_stage(SM64DS_WATCH_HOST_HOOK);
    return run;
}
#endif
