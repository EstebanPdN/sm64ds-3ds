#pragma once
// Automated hardware test, modeled on the Worms 3D port's SELECT+START mode.
// SELECT+START starts a tour of every level: each level starts from a fresh
// boot (HOME chainloader restart), enters through the retail StartFile and
// plays one fixed script from its entrance, writing a report per situation.
// BUILD131: the tour then visits up to four other spots of the level (live
// actors standing on ground, far from the entrance and from each other) and
// measures three situations at each, so a level is no longer judged by its
// entrance alone. R+SELECT+START keeps the BUILD 113 mode: the current spot.
// This header is the platform-free part: script and level tables, pad
// synthesis, state file and the run state machine (host tests use it).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace sm64ds::test_mode {
// libctru KEY_* bits; the DS game sees A/B/SELECT/START/D-pad/R/L/X/Y as-is.
constexpr std::uint32_t kA=1u<<0,kB=1u<<1,kSelect=1u<<2,kStart=1u<<3,kR=1u<<8,kL=1u<<9,kX=1u<<10,kY=1u<<11,kTouch=1u<<20;
constexpr unsigned kUpdatesPerSecond=30;  // original game logic cadence

struct Tap {std::uint32_t keys=0;std::uint8_t at=0,length=0,every=0;}; // every=0: once
// pause: the game's pause menu is expected to take control away; the step
// clock keeps running and START closes the menu (A and B do not).
enum class StepKind:std::uint8_t{move,pause};
struct Step {
    const char* id;      // folder slug
    const char* name;    // on-screen label (ASCII: the HUD font has no accents)
    std::uint16_t length;// game updates
    std::uint32_t hold;  // keys held for the whole step
    std::int8_t sx,sy,cx,cy; // Circle Pad / C-Stick, -100..100 (+y = forward)
    std::uint8_t touch;  // 0 none, 1 left camera arrow, 2 right camera arrow
    std::uint8_t spin;   // updates per Circle Pad revolution, 0 = fixed direction
    Tap tap,tap2;
    std::int16_t report; // update of the report inside the step, -1 = none
    bool home;           // return to the starting spot first
    const char* why;     // report title
    StepKind kind=StepKind::move;
    std::uint8_t zone=0; // 1..kMaxZones: played at that zone spot (tour only)
};
constexpr unsigned kMaxZones=4;
// One fixed script for every level. Moves alternate with returns to the
// starting spot so runs stay where they began; reports are taken mid-action.
inline constexpr Step kScript[]={
    {"quieto","quieto",105,0,0,0,0,0,0,0,{},{},90,true,"Quieto, camara fija"},
    {"camara-izquierda","camara izquierda",90,0,0,0,0,0,1,0,{},{},75,false,"Girando la camara a la izquierda (flecha tactil)"},
    {"camara-derecha","camara derecha",90,0,0,0,0,0,2,0,{},{},75,false,"Girando la camara a la derecha (flecha tactil)"},
    {"andar","andar",75,0,0,45,0,0,0,0,{},{},60,true,"Andando"},
    {"correr","correr",66,0,0,100,0,0,0,0,{},{},50,true,"Corriendo"},
    {"correr-curva","correr en curva",90,0,0,100,0,0,0,72,{},{},75,true,"Corriendo en circulo"},
    {"correr-camara","correr y camara",75,0,0,100,0,0,2,0,{},{},60,true,"Corriendo y girando la camara"},
    {"salto","salto",45,0,0,0,0,0,0,0,{kA,0,6,0},{},12,true,"Saltando (en el aire)"},
    {"triple-salto","triple salto",90,0,0,100,0,0,0,0,{kA,20,4,20},{},68,true,"Triple salto corriendo"},
    {"golpes","golpes",60,0,0,0,0,0,0,0,{kB,0,3,9},{},22,true,"Golpes con B"},
    {"golpe-carrera","golpe corriendo",50,0,0,100,0,0,0,0,{kB,25,3,0},{},32,true,"Golpe en carrera (B corriendo)"},
    {"salto-largo","salto largo",60,0,0,100,0,0,0,0,{kR,24,6,0},{kA,26,4,0},34,true,"Salto largo (R+A corriendo)"},
    {"voltereta","voltereta",50,kR,0,0,0,0,0,0,{kA,15,4,0},{},24,true,"Voltereta hacia atras (R+A)"},
    {"pausa","pausa",90,0,0,0,0,0,0,0,{kStart,0,3,0},{kStart,60,3,0},40,true,"Menu de pausa (START)",StepKind::pause},
    {"golpe-suelo","golpe al suelo",60,0,0,0,0,0,0,0,{kA,0,5,0},{kR,14,4,0},22,true,"Golpe al suelo (A y R en el aire)"},
    {"girar-sitio","girar en el sitio",75,0,0,60,0,0,0,30,{},{},60,true,"Girando sobre si mismo"},
    {"camara-l","camara detras",45,0,0,0,0,0,0,0,{kL,0,4,0},{},15,false,"Camara detras del personaje (L)"},
    {"camara-cstick","camara c-stick",75,0,0,0,100,0,0,0,{},{},60,false,"Camara con C-Stick (solo New 3DS)"},
    {"correr-atras","correr hacia la camara",66,0,0,-100,0,0,0,0,{},{},50,true,"Corriendo hacia la camara"},
    {"quieto-final","quieto final",105,0,0,0,0,0,0,0,{},{},90,true,"Quieto al final"},
    // BUILD131: more of the move set (R is crouch; R+B while running slides).
    {"agachado","agachado",50,kR,0,0,0,0,0,0,{},{},32,true,"Agachado (R)"},
    {"gatear","gatear",70,kR,0,45,0,0,0,0,{},{},52,true,"Gateando (R y adelante)"},
    {"barrido","barrido",60,0,0,100,0,0,0,0,{kR,24,10,0},{kB,27,3,0},36,true,"Barrido (R+B corriendo)"},
    {"patada-salto","patada en salto",50,0,0,0,0,0,0,0,{kA,0,5,0},{kB,10,3,0},16,true,"Patada en el aire (A y B)"},
    // Zone spots: the first step of each zone goes to its spot (home), the
    // other two stay there. A level with fewer spots skips the rest.
#define SM64DS_TEST_ZONE(n) \
    {"zona" #n "-quieto","zona " #n " quieto",75,0,0,0,0,0,0,0,{},{},60,true,"Zona " #n ": quieto en otro punto del nivel",StepKind::move,n}, \
    {"zona" #n "-camara","zona " #n " camara",120,0,0,0,0,0,2,0,{},{},100,false,"Zona " #n ": vuelta de camara",StepKind::move,n}, \
    {"zona" #n "-correr","zona " #n " correr",90,0,0,100,0,0,0,72,{},{},75,false,"Zona " #n ": corriendo en circulo",StepKind::move,n}
    SM64DS_TEST_ZONE(1),SM64DS_TEST_ZONE(2),SM64DS_TEST_ZONE(3),SM64DS_TEST_ZONE(4),
#undef SM64DS_TEST_ZONE
};
constexpr unsigned kSteps=sizeof(kScript)/sizeof(kScript[0]);
constexpr unsigned count_reports(bool zones=true){unsigned n=0;for(const auto& s:kScript)n+=s.report>=0&&(zones||!s.zone);return n;}
constexpr unsigned first_zone_step(){unsigned n=0;for(const auto& s:kScript){if(s.zone)break;++n;}return n;}
// Every step before kBaseSteps belongs to the entrance script.
constexpr unsigned kBaseSteps=first_zone_step();
constexpr unsigned kMaxReports=count_reports(),kBaseReports=count_reports(false);
constexpr bool zones_are_last(){for(unsigned i=kBaseSteps;i<kSteps;++i)if(!kScript[i].zone||kScript[i].zone>kMaxZones)return false;return true;}
static_assert(kBaseReports==24&&kMaxReports==24+3*kMaxZones&&zones_are_last(),"24 entrance situations, then three per zone");
static_assert(kScript[kBaseSteps].home&&kScript[kBaseSteps].zone==1,"a zone starts by going to its spot");
// The first level of a tour also reports the title screen (Mario's face).
inline constexpr Step kTitleStep{"titulo","titulo",0,0,0,0,0,0,0,0,{},{},0,false,"Pantalla de titulo (cara de Mario)"};

// ---- Levels of the tour ----
// Level IDs and their order follow the ROM's own tables (arm9: overlay
// 8+id per level, SUBLEVEL_LEVEL_TABLE). 0 is a test map and 41 has no
// course. The character rotates so every body appears in every kind of map.
constexpr std::uint8_t kMario=0,kLuigi=1,kWario=2,kYoshi=3;
struct Level {std::uint8_t id;const char* slug;std::uint8_t character;};
inline constexpr Level kTour[]={
    {1,"jardines-castillo",kYoshi},{2,"castillo-planta-baja",kMario},{3,"patio-castillo",kLuigi},
    {4,"sotano-castillo",kWario},{5,"castillo-planta-alta",kYoshi},
    {6,"bob-omb",kMario},{7,"fortaleza-whomp",kLuigi},{8,"bahia-jolly-roger",kWario},{9,"barco-jolly-roger",kMario},
    {10,"montana-helada",kYoshi},{11,"tobogan-helado",kMario},{12,"mansion-boo",kLuigi},{13,"cueva-laberinto",kWario},
    {14,"lava-letal",kMario},{15,"volcan",kYoshi},{16,"arenas-movedizas",kLuigi},{17,"piramide",kWario},
    {18,"muelle",kMario},{19,"tierra-nieve",kYoshi},{20,"iglu",kLuigi},{21,"mundo-agua",kWario},
    {22,"montana-alta",kMario},{23,"tobogan-montana",kLuigi},{24,"isla-grande",kYoshi},{25,"isla-pequena",kWario},
    {26,"isla-interior",kMario},{27,"reloj",kLuigi},{28,"arcoiris",kWario},
    {29,"secreto-29",kMario},{30,"secreto-30",kYoshi},{31,"torre-ala",kMario},{32,"secreto-32",kLuigi},
    {33,"secreto-33",kWario},{34,"sobre-arcoiris",kMario},
    {35,"bowser-oscuro",kMario},{36,"bowser-oscuro-jefe",kMario},{37,"bowser-fuego",kLuigi},
    {38,"bowser-fuego-jefe",kLuigi},{39,"bowser-cielo",kWario},{40,"bowser-cielo-jefe",kWario},
    {42,"secreto-42",kYoshi},{43,"secreto-43",kMario},
    {44,"jefe-goomba",kYoshi},{45,"jefe-goomba-arena",kYoshi},{46,"gran-boo",kMario},{47,"gran-boo-espejo",kMario},
    {48,"jefe-frio",kLuigi},{49,"jefe-frio-arena",kLuigi},{50,"sala-juegos",kYoshi},{51,"castillo-51",kMario},
};
constexpr unsigned kTourLevels=sizeof(kTour)/sizeof(kTour[0]);
static_assert(kTourLevels==50,"levels 1-51 except 41");
inline const char* character_name(int c){return c==kMario?"Mario":c==kLuigi?"Luigi":c==kWario?"Wario":c==kYoshi?"Yoshi":"?";}

// The synthetic controller for one game update. Units are libctru's raw
// Circle Pad (about +-156) and C-Stick (about +-146) ranges.
struct Pad {
    std::uint32_t held=0;
    int sx=0,sy=0,cx=0,cy=0;
    bool touch=false;int tx=0,ty=0;
};
// Camera arrows of native_gameplay_camera (32x32 at (10,198) and (278,198)).
constexpr int kLeftArrowX=26,kRightArrowX=294,kArrowY=214;
inline bool tap_active(const Tap& t,unsigned u){
    if(!t.keys||!t.length||u<t.at)return false;
    const unsigned since=u-t.at;
    return t.every?since%t.every<t.length:since<t.length;
}
inline Pad pad_for(const Step& s,unsigned u){
    Pad p;p.held=s.hold;
    if(tap_active(s.tap,u))p.held|=s.tap.keys;
    if(tap_active(s.tap2,u))p.held|=s.tap2.keys;
    int x=s.sx,y=s.sy;
    if(s.spin){
        // 16-direction revolution, deterministic integer table (sin*100).
        static constexpr int kSin[16]={0,38,71,92,100,92,71,38,0,-38,-71,-92,-100,-92,-71,-38};
        const unsigned i=(u*16u/s.spin)%16u;const int m=s.sy?(s.sy<0?-s.sy:s.sy):100;
        x=kSin[i]*m/100;y=kSin[(i+4)%16]*m/100;
    }
    p.sx=x*150/100;p.sy=y*150/100;p.cx=s.cx*140/100;p.cy=s.cy*140/100;
    if(s.touch){p.touch=true;p.tx=s.touch==1?kLeftArrowX:kRightArrowX;p.ty=kArrowY;p.held|=kTouch;}
    return p;
}

// ---- State kept on SD between the chord and the restarted boots ----
enum class Phase:std::uint8_t{none,pending,running,final_};
enum class Mode:std::uint8_t{spot,tour};
struct Spot {
    int level=-1,area=-1,character=0;
    std::int32_t x=0,y=0,z=0;std::int16_t angle=0;
    bool position=false;
};
// Tour results carried across boots (tenths keep the file integer-only).
struct Totals {unsigned complete=0,incomplete=0,crashed=0,fps_tenths=0,fps_levels=0,worst_p95_tenths=0;};
// BUILD124: a tour level whose script killed the player (lava, a fall) after
// it ran is entered again from a fresh boot and continues after the fatal
// situation; this keeps what its earlier boots measured. BUILD123 lost 20 of
// 50 levels that way (lava 5/20, Bowser fights 4-11/20).
constexpr unsigned kMaxReentries=5;
// zone_reports: how many of "reports" came from zone steps (BUILD131).
struct Resume {unsigned step=0,reports=0,reentries=0,failed=0,fps_hundredths=0,fps_reports=0,p95_tenths=0,zone_reports=0;};
struct State {unsigned run=0;Phase phase=Phase::none;Mode mode=Mode::spot;unsigned index=0;Spot spot;Totals totals;Resume resume;};
inline const char* phase_name(Phase p){return p==Phase::pending?"pendiente":p==Phase::running?"en_curso":p==Phase::final_?"final":"ninguna";}
inline int format_state(const State& s,char* out,std::size_t size){
    return std::snprintf(out,size,"SM64DS_PRUEBA_V2\ncarpeta=%u\nfase=%s\nmodo=%s\nindice=%u\nnivel=%d\narea=%d\npersonaje=%d\nposicion=%d\nx=%ld\ny=%ld\nz=%ld\nangulo=%d\n"
        "completos=%u\nincompletos=%u\ncolgados=%u\nfps_decimas=%u\nfps_niveles=%u\np95_peor_decimas=%u\n"
        "paso=%u\ninformes=%u\nreentradas=%u\nfallidos=%u\nfps_nivel_centesimas=%u\nfps_nivel_informes=%u\np95_nivel_decimas=%u\ninformes_zona=%u\n",
        s.run,phase_name(s.phase),s.mode==Mode::tour?"recorrido":"punto",s.index,s.spot.level,s.spot.area,s.spot.character,
        int(s.spot.position),long(s.spot.x),long(s.spot.y),long(s.spot.z),int(s.spot.angle),
        s.totals.complete,s.totals.incomplete,s.totals.crashed,s.totals.fps_tenths,s.totals.fps_levels,s.totals.worst_p95_tenths,
        s.resume.step,s.resume.reports,s.resume.reentries,s.resume.failed,s.resume.fps_hundredths,s.resume.fps_reports,s.resume.p95_tenths,
        s.resume.zone_reports);
}
// Strict: any malformed or out-of-range field rejects the whole file, so a
// damaged SD state can never make the game warp somewhere arbitrary. V1
// (BUILD 113, spot only) is still read.
inline bool parse_state(const char* text,std::size_t size,State& out){
    if(!text||size<16||size>2048)return false;
    const bool v1=!std::strncmp(text,"SM64DS_PRUEBA_V1\n",17),v2=!std::strncmp(text,"SM64DS_PRUEBA_V2\n",17);
    if(!v1&&!v2)return false;
    State s;bool have_run=false,have_phase=false,have_level=false,have_mode=v1;
    const char* p=text+17;const char* end=text+size;
    while(p<end&&*p){
        const char* nl=static_cast<const char*>(std::memchr(p,'\n',std::size_t(end-p)));if(!nl)nl=end;
        char line[64];const std::size_t n=std::size_t(nl-p);if(n>=sizeof line)return false;
        std::memcpy(line,p,n);line[n]=0;p=nl<end?nl+1:end;
        if(!n)continue;
        char* eq=std::strchr(line,'=');if(!eq)return false;*eq=0;const char* v=eq+1;char* stop=nullptr;
        if(!std::strcmp(line,"fase")){
            if(!std::strcmp(v,"pendiente"))s.phase=Phase::pending;else if(!std::strcmp(v,"en_curso"))s.phase=Phase::running;
            else if(!std::strcmp(v,"final"))s.phase=Phase::final_;else return false;have_phase=true;continue;
        }
        if(!std::strcmp(line,"modo")){
            if(v1)return false;
            if(!std::strcmp(v,"recorrido"))s.mode=Mode::tour;else if(!std::strcmp(v,"punto"))s.mode=Mode::spot;else return false;
            have_mode=true;continue;
        }
        const long value=std::strtol(v,&stop,10);if(stop==v||*stop)return false;
        auto& t=s.totals;
        if(!std::strcmp(line,"carpeta")){if(value<1||value>99)return false;s.run=unsigned(value);have_run=true;}
        else if(!std::strcmp(line,"nivel")){if(value<-1||value>=64)return false;s.spot.level=int(value);have_level=true;}
        else if(!std::strcmp(line,"area")){if(value<-1||value>=16)return false;s.spot.area=int(value);}
        else if(!std::strcmp(line,"personaje")){if(value<0||value>3)return false;s.spot.character=int(value);}
        else if(!std::strcmp(line,"posicion"))s.spot.position=value!=0;
        else if(!std::strcmp(line,"x"))s.spot.x=std::int32_t(value);
        else if(!std::strcmp(line,"y"))s.spot.y=std::int32_t(value);
        else if(!std::strcmp(line,"z"))s.spot.z=std::int32_t(value);
        else if(!std::strcmp(line,"angulo")){if(value<-32768||value>32767)return false;s.spot.angle=std::int16_t(value);}
        else if(!v2)return false;
        else if(!std::strcmp(line,"indice")){if(value<0||value>long(kTourLevels))return false;s.index=unsigned(value);}
        else if(!std::strcmp(line,"completos")){if(value<0||value>long(kTourLevels))return false;t.complete=unsigned(value);}
        else if(!std::strcmp(line,"incompletos")){if(value<0||value>long(kTourLevels))return false;t.incomplete=unsigned(value);}
        else if(!std::strcmp(line,"colgados")){if(value<0||value>long(kTourLevels))return false;t.crashed=unsigned(value);}
        else if(!std::strcmp(line,"fps_niveles")){if(value<0||value>long(kTourLevels))return false;t.fps_levels=unsigned(value);}
        else if(!std::strcmp(line,"fps_decimas")){if(value<0||value>long(kTourLevels)*1200)return false;t.fps_tenths=unsigned(value);}
        else if(!std::strcmp(line,"p95_peor_decimas")){if(value<0||value>100000)return false;t.worst_p95_tenths=unsigned(value);}
        else if(!std::strcmp(line,"paso")){if(value<0||value>=long(kSteps))return false;s.resume.step=unsigned(value);}
        else if(!std::strcmp(line,"informes")){if(value<0||value>long(kMaxReports))return false;s.resume.reports=unsigned(value);}
        else if(!std::strcmp(line,"reentradas")){if(value<0||value>long(kMaxReentries))return false;s.resume.reentries=unsigned(value);}
        else if(!std::strcmp(line,"fallidos")){if(value<0||value>long(kMaxReports))return false;s.resume.failed=unsigned(value);}
        else if(!std::strcmp(line,"fps_nivel_centesimas")){if(value<0||value>long(kMaxReports)*12000)return false;s.resume.fps_hundredths=unsigned(value);}
        else if(!std::strcmp(line,"fps_nivel_informes")){if(value<0||value>long(kMaxReports))return false;s.resume.fps_reports=unsigned(value);}
        else if(!std::strcmp(line,"p95_nivel_decimas")){if(value<0||value>100000)return false;s.resume.p95_tenths=unsigned(value);}
        else if(!std::strcmp(line,"informes_zona")){if(value<0||value>long(kMaxReports-kBaseReports))return false;s.resume.zone_reports=unsigned(value);}
        else return false;
    }
    if(!have_run||!have_phase||!have_level||!have_mode)return false;
    if(s.mode==Mode::spot&&(s.spot.level<0||s.index))return false;
    // Only a tour resumes, and never with more reports than steps before it.
    if((s.mode!=Mode::tour&&(s.resume.step||s.resume.reports||s.resume.reentries))||s.resume.reports>s.resume.step||
       s.resume.zone_reports>s.resume.reports)return false;
    if(s.mode==Mode::tour&&s.index>=kTourLevels&&s.phase!=Phase::final_)return false;
    out=s;return true;
}
// The level a tour state points at, entered at its own entrance.
inline Spot tour_spot(unsigned index){Spot s;if(index<kTourLevels){s.level=kTour[index].id;s.character=kTour[index].character;}return s;}

// ---- Run state machine (one call per original game update) ----
struct Observation {
    unsigned scene=0;       // 1 title, 3 stage
    unsigned updates=0;     // original game updates since boot
    int level=-1,area=-1,character=-1;
    bool player=false,control=false,body=false,dialogue=false,paused=false;
    // Stage::IsPauseDisabled (player byte 0x709). Usually only during entry
    // animations, but the castle courtyard keeps it set from StartFile on.
    bool pause_locked=false;
    std::int32_t x=0,y=0,z=0;std::int16_t angle=0;
};
// reenter (tour): end this boot; the next one enters the level again and
// continues from resume_step() (BUILD124).
// zones (tour, BUILD131): the caller answers with Runner::set_zones before
// the next update.
enum class Action:std::uint8_t{none,start_file,set_character,teleport,report,finish,reenter,zones};
// window: the measured interval of the next report opens on this update.
struct Command {Action action=Action::none;Pad pad;bool inject=false,window=false;};
class Runner {
public:
    enum class Stage:std::uint8_t{idle,title,stage,character,settle,run,done};
    static constexpr unsigned kTitleUpdates=750;     // spot mode: the verified benchmark's title delay
    static constexpr unsigned kTitleWait=150;        // tour: updates on the title before StartFile
    static constexpr unsigned kTitleReportAt=120;    // tour, first level: the title report
    static constexpr unsigned kStageTimeout=90*kUpdatesPerSecond;
    // A zone is far from where the camera was: BUILD131's first run took the
    // zone's first picture before the camera had reached the character.
    static constexpr unsigned kSettle=45,kHomeSettle=30,kZoneSettle=90;
    static constexpr unsigned kNoControlSkip=20*kUpdatesPerSecond;
    static constexpr unsigned kPauseClose=10*kUpdatesPerSecond;
    static constexpr unsigned kPauseLockGrace=10*kUpdatesPerSecond;
    static constexpr unsigned kFirstStartTap=3*kUpdatesPerSecond; // longer than any entry lock
    static constexpr unsigned kTitleReport=kSteps+1; // report_step_ sentinel
    // spot.position: teleport there before the script (spot mode). Without
    // it the settled level entrance becomes the spot.
    // resume (tour): the step to run first, the reports earlier boots of this
    // level wrote and the re-entries they used.
    void start(const Spot& spot,bool tour=false,bool title_report=false,const Resume& resume=Resume{}){
        *this=Runner{};spot_=spot;tour_=tour;title_report_=title_report;stage_=Stage::title;
        zones_set_=!tour; // a spot run measures its own spot only
        if(tour&&resume.step<kSteps){step_=resume.step;reports_=resume.reports;reentries_=resume.reentries;zone_reports_=resume.zone_reports;}
    }
    // Answer to Action::zones: the spots of this level, at most kMaxZones.
    void set_zones(const Spot* spots,unsigned count){
        zones_=count<kMaxZones?count:kMaxZones;for(unsigned i=0;i<zones_;++i)zone_[i]=spots[i];zones_set_=true;
    }
    unsigned zones()const{return zones_;}
    bool zones_known()const{return zones_set_;}
    unsigned zone_reports()const{return zone_reports_;}
    unsigned base_reports()const{return reports_-zone_reports_;}
    // Why the zone visits stopped early (a death there never fails the level).
    const char* zone_failure()const{return zone_failure_;}
    // Where the teleport being issued goes: a zone spot or the home spot.
    const Spot& target()const{const unsigned z=step_<kSteps?kScript[step_].zone:0;return z&&z<=zones_?zone_[z-1]:spot_;}
    unsigned resume_step()const{return resume_step_;}
    unsigned reentries()const{return reentries_;}
    // Updates on which START was held to close a menu behind a pause lock,
    // and whether a lock ended right after one (the courtyard's save menu).
    unsigned menu_closes()const{return menu_closes_;}
    bool menu_closed()const{return menu_closed_;}
    Stage stage()const{return stage_;}
    unsigned step()const{return step_;}
    unsigned reports()const{return reports_;}
    unsigned retries()const{return retries_;}
    bool teleported()const{return teleported_;}
    bool character_kept()const{return character_kept_;}
    bool pause_lock_ignored()const{return lock_ignored_;}
    bool finished()const{return finished_;}
    const char* failure()const{return failure_;}
    // Updates from StartFile to the first controllable update in the level.
    unsigned load_updates()const{return load_updates_;}
    const Spot& home()const{return spot_;}
    const Step* current()const{return step_<kSteps?&kScript[step_]:nullptr;}
    // The step that emitted the last Action::report (it may already be over).
    const Step* report_step()const{return report_step_==kTitleReport?&kTitleStep:report_step_<kSteps?&kScript[report_step_]:nullptr;}
    bool title_report()const{return report_step_==kTitleReport;}
    void teleport_result(bool ok){
        if(step_<kSteps&&kScript[step_].zone){zone_refused_=!ok;return;}
        teleported_=ok;home_ok_=ok;
    }
    // Development runs (env.txt) may stop the entrance script after its first
    // n situations; the zones still follow.
    void limit_steps(unsigned n){base_limit_=n&&n<kBaseSteps?n:kBaseSteps;}
    unsigned expected_base_reports()const{unsigned n=0;for(unsigned i=0;i<base_limit_;++i)n+=kScript[i].report>=0;return n;}
    unsigned expected_zone_reports()const{unsigned n=0;for(unsigned i=kBaseSteps;i<kSteps;++i)n+=kScript[i].report>=0&&kScript[i].zone<=(zones_set_?zones_:kMaxZones);return n;}
    unsigned expected_reports()const{return expected_base_reports()+expected_zone_reports();}
    void cancel(const char* why){failure_=why;stage_=Stage::done;}
    // Call once per original game update (repeated updates are ignored).
    Command update(const Observation& seen){
        Command c;
        if(stage_==Stage::idle||stage_==Stage::done)return c;
        c.inject=true; // during a run the owner's buttons never reach the game
        if(have_last_&&seen.updates==last_)return held_; // same update, same pad
        have_last_=true;last_=seen.updates;
        // A pause lock that outlasts the grace period (with no dialogue,
        // cutscene or menu) is the level's own state, not an entry animation.
        Observation o=seen;
        // BUILD124: the castle courtyard's entrance opens the pause screen's
        // save menu (Save & Continue / Save & Quit / Continue) and holds this
        // lock; BUILD123 measured 13 situations of that menu (15 FPS on the
        // Old 3DS, also in Azahar) until the pause step's START closed it.
        // START every 2 s of the lock closes it; with pause disabled START
        // cannot open a pause menu, so a genuine lock just waits as before.
        // BUILD129: the first tap waits 3 s of lock and only before the
        // script runs. BUILD128 tapped at 1 s, one update before the ~31-update
        // entry lock of 38/50 levels ended: a race that could open the pause
        // menu, and the reason "menu cerrado con START" was a false alarm.
        bool start_tap=false;
        if(o.control&&o.pause_locked&&!lock_ignored_){
            if(!lock_since_){lock_since_=o.updates+1;tapped_at_=0;}
            const unsigned age=o.updates+1-lock_since_;
            const bool entering=stage_==Stage::stage||stage_==Stage::character||stage_==Stage::settle;
            if(entering&&age>=kFirstStartTap&&age%(2*kUpdatesPerSecond)>=kUpdatesPerSecond&&age%(2*kUpdatesPerSecond)<kUpdatesPerSecond+3){
                start_tap=true;tapped_at_=o.updates;++menu_closes_;
            }
            if(age>=kPauseLockGrace)lock_ignored_=true;
        }else if(!o.pause_locked){
            // A lock that ends right after one of its START taps was a menu
            // START closed (entry locks end by themselves long before a tap).
            if(lock_since_&&tapped_at_&&o.updates-tapped_at_<=20)menu_closed_=true;
            lock_since_=0;tapped_at_=0;
        }
        // A pause outside the script's pause step (a START that landed just
        // as a lock ended) is closed like a stray pause during the script.
        if(o.paused&&stage_!=Stage::run&&stage_!=Stage::title&&o.updates%20==0)start_tap=true;
        if(o.pause_locked&&!lock_ignored_)o.control=false;
        // Load time ends at the first controllable update, lock or not.
        if(stage_==Stage::stage&&!load_updates_&&seen.scene==3&&seen.level==spot_.level&&seen.player&&seen.control)
            load_updates_=seen.updates-start_update_;
        const bool in_stage=o.scene==3&&o.level==spot_.level&&o.player;
        switch(stage_){
        case Stage::title:
            if(o.scene==3&&o.level==spot_.level){stage_=Stage::stage;since_=o.updates;break;}
            if(o.scene!=1)break;
            if(!title_seen_){title_seen_=true;title_since_=o.updates;}
            if(title_report_&&!title_reported_){
                if(!window_&&o.updates-title_since_>=kTitleReportAt/2){window_=true;c.window=true;}
                if(o.updates-title_since_>=kTitleReportAt){title_reported_=true;window_=false;report_step_=kTitleReport;c.action=Action::report;}
                break;
            }
            if(tour_?o.updates-title_since_>=kTitleWait:o.updates>=kTitleUpdates){
                c.action=Action::start_file;stage_=Stage::stage;since_=start_update_=o.updates;
            }
            break;
        case Stage::stage:
            if(in_stage&&o.control){
                stage_=Stage::character;since_=o.updates;idle_=waited_=0;
                if(o.character!=spot_.character)c.action=Action::set_character;
            }else if(o.updates-since_>kStageTimeout){
                // BUILD131 tour, castle basement: the game never gave control
                // back after a zone teleport. That costs the zone, not the level.
                c=step_<kSteps&&kScript[step_].zone&&zone_teleport_issued_?exit_level("el juego no devolvio el control en la zona"):fail("no se pudo entrar en el nivel");
                break;
            }
            else if(o.scene==3&&o.level!=spot_.level&&o.player){c=exit_level("el juego entro en otro nivel");break;}
            else if(o.scene==3&&o.player&&o.dialogue&&(o.updates-since_)%15==0)c.pad.held=kA; // level intro text
            break;
        case Stage::character:
            if(!in_stage){stage_=Stage::stage;since_=o.updates;break;}
            if(!o.control){
                // The course intro talks (Bob-omb Battlefield does): advance
                // it with A; time without control is not a character failure.
                ++idle_;if(o.dialogue&&o.updates%15==0)c.pad.held=kA;
                if(idle_>90*kUpdatesPerSecond)c=fail("dialogo o escena sin terminar");
                break;
            }
            ++waited_;
            if(o.character==spot_.character&&o.body){settle(o);break;}
            // Retry the original switch while controllable (it may have been
            // issued just before a cutscene took control away). A level that
            // refuses the body keeps its own character: the script still runs.
            if(o.character!=spot_.character&&waited_%(3*kUpdatesPerSecond)==0)c.action=Action::set_character;
            if(waited_>20*kUpdatesPerSecond){character_kept_=true;settle(o);}
            break;
        case Stage::settle:
            if(!in_stage){c=lost(o);break;}
            if(!o.control){since_=o.updates;if(o.dialogue&&o.updates%15==0)c.pad.held=kA;break;}
            if(camera_<4){c.pad.held=kL;++camera_;} // camera behind the character after arriving
            if(o.updates-since_<settle_)break;
            if(!spot_.position){
                // Tour: the settled entrance becomes the spot every "home"
                // step returns to (same area only, as the teleport requires).
                spot_.area=o.area;spot_.x=o.x;spot_.y=o.y;spot_.z=o.z;spot_.angle=o.angle;spot_.position=true;home_ok_=true;
            }
            if(const unsigned zone=kScript[step_].zone){
                // Ask for the spots when the first zone is due: the actors
                // are those alive now. Fewer spots or a refused teleport
                // skip to the next zone.
                if(!zones_set_){c.action=Action::zones;break;}
                if(zone>zones_||zone_refused_){zone_refused_=false;skip_zone(o);break;}
                // A boot that resumes at a zone has not gone to its spot yet.
                if(!ran_&&!zone_teleport_issued_)home_pending_=true;
                if(home_pending_)zone_teleport_issued_=true;
            }
            if(home_pending_){home_pending_=false;c.action=Action::teleport;since_=o.updates;settle_=kScript[step_].zone?kZoneSettle:kHomeSettle;camera_=0;break;}
            stage_=Stage::run;step_start_=o.updates;no_control_=0;window_=false;ran_=true;step_started_=true;
            c.pad=pad_for(kScript[step_],0);
            break;
        case Stage::run:{
            if(!in_stage){c=lost(o);break;}
            const Step& s=kScript[step_];
            if(s.kind==StepKind::pause){
                // The menu takes control away by design: the clock runs on.
                const unsigned u=o.updates-step_start_;
                c.pad=pad_for(s,u);
                report_window(s,u,c);
                if(u+1>=s.length){
                    if(!o.paused&&o.control){next_step(o);break;}
                    // Still paused (an early START is ignored while the menu
                    // opens): START again until it closes.
                    const unsigned extra=u+1-s.length;
                    if(extra%20==0)c.pad.held|=kStart;
                    if(extra>kPauseClose)c=fail("el menu de pausa no se cerro");
                }
                break;
            }
            if(!o.control){
                // Dialogue/cutscene: advance text with A; the step clock waits.
                if(!no_control_)no_control_=o.updates;
                if(o.dialogue&&!o.paused&&(o.updates-no_control_)%15==0)c.pad.held=kA;
                if(o.paused&&(o.updates-no_control_)%20==0)c.pad.held=kStart; // close a stray pause
                if(o.updates-no_control_>kNoControlSkip)next_step(o);
                break;
            }
            if(no_control_){step_start_+=o.updates-no_control_;no_control_=0;}
            const unsigned u=o.updates-step_start_;
            c.pad=pad_for(s,u);
            report_window(s,u,c);
            if(u+1>=s.length)next_step(o);
            break;}
        default:break;
        }
        if(start_tap&&stage_!=Stage::done)c.pad.held|=kStart;
        held_=c;held_.action=Action::none;held_.window=false;
        return c;
    }
private:
    void settle(const Observation& o){
        stage_=Stage::settle;since_=o.updates;settle_=kSettle;home_pending_=spot_.position;camera_=0;
    }
    void report_window(const Step& s,unsigned u,Command& c){
        const unsigned open=s.report>=0&&unsigned(s.report)<20?unsigned(s.report)/2:10u;
        if(s.report>=0&&!window_&&u>=open){window_=true;c.window=true;}
        if(s.report>=0&&!reported_&&u>=unsigned(s.report)){reported_=true;++reports_;zone_reports_+=s.zone!=0;report_step_=step_;c.action=Action::report;}
    }
    // Leave the current zone: its remaining steps are not played.
    void skip_zone(const Observation& o){
        const unsigned zone=kScript[step_].zone;
        while(step_<kSteps&&kScript[step_].zone&&(kScript[step_].zone==zone||kScript[step_].zone>zones_))++step_;
        reported_=false;window_=false;no_control_=0;zone_teleport_issued_=false;
        if(step_>=kSteps){stage_=Stage::done;finished_=true;return;}
        stage_=Stage::settle;since_=o.updates;settle_=kHomeSettle;home_pending_=true;camera_=4;step_started_=false;
    }
    // During a run the owner's buttons never reach the game, the last update included.
    Command fail(const char* why){failure_=why;stage_=Stage::done;Command c;c.inject=true;c.action=Action::finish;return c;}
    // The game left the level (a death sends the player back to the castle).
    // A tour level that already ran its script is entered again by the next
    // boot after the fatal step; anything else ends the level as before.
    // BUILD129: an exit noticed while settling home for a step that never
    // started belongs to the previous step; the next boot runs the unrun step
    // instead of skipping it (BUILD128 lost "quieto-final" in 7 levels).
    // BUILD131: a death at a zone spot abandons that zone (the spot itself
    // may be the danger) and never fails the level: its entrance script is
    // complete by then.
    Command exit_level(const char* why){
        unsigned resume=step_started_?step_+1:step_;
        // A zone whose teleport was issued is abandoned whole; one that was
        // only being prepared (the exit belongs to the previous step) is kept.
        const unsigned zone=step_<kSteps&&(step_started_||zone_teleport_issued_)?kScript[step_].zone:0;
        if(zone)for(resume=step_;resume<kSteps&&kScript[resume].zone==zone;)++resume;
        else if(resume>=base_limit_&&resume<kBaseSteps)resume=kBaseSteps;
        const bool can=tour_&&ran_&&reentries_<kMaxReentries&&resume<kSteps;
        if(!can&&zone){zone_failure_=why;stage_=Stage::done;finished_=true;Command c;c.inject=true;c.action=Action::finish;return c;}
        if(!can)return fail(why);
        failure_=why;stage_=Stage::done;resume_step_=resume;Command c;c.inject=true;c.action=Action::reenter;return c;
    }
    Command lost(const Observation& o){
        // Death or respawn inside the same level: wait for control again and
        // resume the current step. Another level/scene ends the run cleanly.
        if(++retries_>3){
            if(step_<kSteps&&kScript[step_].zone){zone_failure_="salio del nivel demasiadas veces";stage_=Stage::done;finished_=true;
                Command c;c.inject=true;c.action=Action::finish;return c;}
            return fail("salio del nivel demasiadas veces");
        }
        if(o.scene==3&&o.level!=spot_.level&&o.player)return exit_level("salio del nivel (caida o derrota)");
        stage_=o.scene==1?Stage::title:Stage::stage;since_=o.updates;
        Command c;c.inject=true;return c;
    }
    void next_step(const Observation& o){
        ++step_;reported_=false;window_=false;no_control_=0;
        if(step_>=base_limit_&&step_<kBaseSteps)step_=kBaseSteps; // development limit: on to the zones
        if(step_>=kSteps){stage_=Stage::done;finished_=true;return;}
        // A zone step always passes through settle (spots, teleport, skips).
        const auto& next=kScript[step_];
        if(next.zone?next.home||!zones_set_||next.zone>zones_:next.home&&home_ok_){
            if(next.home)zone_teleport_issued_=false;
            stage_=Stage::settle;since_=o.updates;settle_=kHomeSettle;home_pending_=next.home;camera_=4;step_started_=false;return;}
        step_start_=o.updates+1;step_started_=true;
    }
    Spot spot_{};Stage stage_=Stage::idle;
    unsigned since_=0,settle_=0,step_=0,step_start_=0,no_control_=0,reports_=0,retries_=0,last_=0,camera_=0,report_step_=kSteps;
    unsigned idle_=0,waited_=0; // character stage: updates without / with control
    unsigned title_since_=0,start_update_=0,load_updates_=0,base_limit_=kBaseSteps;
    Spot zone_[kMaxZones]{};unsigned zones_=0,zone_reports_=0;bool zones_set_=false,zone_refused_=false,zone_teleport_issued_=false;
    const char* zone_failure_=nullptr;
    bool tour_=false,title_report_=false,title_seen_=false,title_reported_=false;
    bool reported_=false,window_=false,home_pending_=false,teleported_=false,home_ok_=false,finished_=false,have_last_=false;
    bool character_kept_=false,lock_ignored_=false,ran_=false,menu_closed_=false,step_started_=false;
    unsigned lock_since_=0,reentries_=0,resume_step_=0,menu_closes_=0,tapped_at_=0;
    Command held_{};
    const char* failure_=nullptr;
};
// Folder names: "05-correr", "06-bob-omb".
inline int report_folder(char* out,std::size_t size,unsigned index,const Step& s){return std::snprintf(out,size,"%02u-%s",index,s.id);}
inline int level_folder(char* out,std::size_t size,unsigned index){
    return index<kTourLevels?std::snprintf(out,size,"%02u-%s",unsigned(kTour[index].id),kTour[index].slug):std::snprintf(out,size,"nivel-%u",index);
}
}
