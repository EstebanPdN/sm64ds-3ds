#pragma once
// BUILD137 player journey (L+SELECT+START): the game played from the title
// like a player would, on a new save. Title (touch), main menu (Adventure),
// file selector (File A: new game), the whole opening (the letter and the
// cutscene are watched, text is only advanced when it waits for A), then the
// castle grounds on foot to the castle door, the castle hall, and the
// dialogues on the way. One report (screenshots, FPS, audio) per step; the
// hang watchdog (hang_watchdog.hpp) writes its own files if anything stops.
// Platform-free: the host tests drive JourneyRunner with synthetic frames.
#include "test_mode_script.hpp"
#include <cmath>
#include <cstdint>
namespace sm64ds::test_mode {

struct JourneyObservation {
    Observation game;   // scene, level, player, control, dialogue, position...
    int menu=-1;        // title overlay state (11 star, 10/1 logo, 0 menus, 6 fade), -1 outside
};
// One step of the journey: its report folder, title and the reason it exists.
// load: the window spans a scene load, so its p95 is not the run's worst.
struct JourneyStep {const char* id;const char* name;const char* why;bool load=false;};
inline constexpr JourneyStep kJourney[]={
    {"titulo","titulo","Pantalla de titulo (TOUCH TO START)"},
    {"menu","menu principal","Menu principal con la cara de Mario"},
    {"seleccion","seleccion de partida","Seleccion de partida tras Adventure (el cuelgue de BUILD 52-136)"},
    {"intro-carta","intro carta","Intro: la carta de Peach (se mide durante la carga de los jardines)",true},
    {"intro-castillo","intro castillo","Intro: la camara sobre el castillo"},
    {"control","control","Primer momento con control (Yoshi frente al castillo)"},
    {"caminando","caminando","Caminando hacia el castillo"},
    {"puerta","puerta","Llegada a la puerta del castillo"},
    // A new game finds the castle door locked: the key is with a rabbit in the
    // gardens (not chased yet). Phase 1 ends pushing against the door.
    {"puerta-cerrada","puerta cerrada","Fin de la fase 1: la puerta del castillo, cerrada hasta atrapar al conejo de la llave"},
    // Only when the door opens (a save that already has the key).
    {"castillo","castillo","Dentro del castillo (vestibulo)"},
    {"castillo-final","castillo final","Fin del recorrido: vestibulo tras los dialogos"},
};
// Reports of a complete phase 1 run (title to the locked door).
constexpr unsigned kJourneyPhase1Reports=9;
constexpr unsigned kJourneySteps=sizeof(kJourney)/sizeof(kJourney[0]);
enum JourneyStepId:unsigned{jTitle,jMenu,jSelect,jLetter,jCastleIntro,jControl,jWalking,jDoor,jLocked,jHall,jHallEnd};
// Touch targets on the lower screen (pixels), as a player taps them.
constexpr int kTouchStartX=160,kTouchStartY=120;  // TOUCH TO START
constexpr int kAdventureX=160,kAdventureY=214;    // Adventure button
constexpr int kFileAX=60,kFileAY=80;              // File A
// Castle grounds (level 1) route, whole units (20.12 >> 12), x/z only.
// Measured in Azahar (BUILD 137): Yoshi starts at (-1200, 6800) facing -z;
// the moat fence is at z ~ 1950; the bridge leads to the door, whose two
// leaves are actors at (+-75, 784, -1050). The last point lies behind the
// door: reaching the one before it is the arrival, then the walk pushes on.
struct Waypoint {std::int32_t x,z;std::int32_t radius;};
inline constexpr Waypoint kCastleRoute[]={{-1200,3600,250},{-250,2500,250},{-120,700,220},{-120,-700,200},{-120,-1400,80}};
constexpr unsigned kCastleRoutePoints=sizeof(kCastleRoute)/sizeof(kCastleRoute[0]);
constexpr int kCastleLevel=1,kHallLevel=2;

// What the runner asks the platform side to do for this update.
enum class JourneyAction:std::uint8_t{none,report,finish};
struct JourneyCommand {JourneyAction action=JourneyAction::none;Pad pad;bool inject=false,window=false;};

// DS angle units: 0x10000 per turn. Atan2 of a world direction on x/z.
inline int ds_angle(double dx,double dz){return int(std::lround(std::atan2(dx,dz)*32768.0/3.14159265358979323846))&0xffff;}
inline int wrap_angle(int a){a&=0xffff;return a>=0x8000?a-0x10000:a;}

class JourneyRunner {
public:
    enum class Phase:std::uint8_t{idle,title,menu,select,opening,control,walk,door,hall,done};
    static constexpr unsigned kDoorPush=6*kUpdatesPerSecond;   // walking into the locked door
    static constexpr unsigned kWait=4*kUpdatesPerSecond;       // calm before each report
    static constexpr unsigned kTitleSettle=6*kUpdatesPerSecond; // boot preloads before the title report
    static constexpr unsigned kTitleTimeout=90*kUpdatesPerSecond;
    static constexpr unsigned kMenuTimeout=60*kUpdatesPerSecond;
    static constexpr unsigned kOpeningTimeout=240*kUpdatesPerSecond;
    static constexpr unsigned kWalkTimeout=150*kUpdatesPerSecond;
    static constexpr unsigned kHallTime=20*kUpdatesPerSecond;
    static constexpr unsigned kControlTimeout=90*kUpdatesPerSecond;
    static constexpr unsigned kTextEvery=45;                    // A on a waiting text, like a reader (1.5 s)
    void start(){*this=JourneyRunner{};phase_=Phase::title;}
    // Development (.3dsx): another castle route, at most kMaxRoute points.
    static constexpr unsigned kMaxRoute=16;
    void set_route(const Waypoint* points,unsigned count){
        route_count_=count<kMaxRoute?count:kMaxRoute;for(unsigned i=0;i<route_count_;++i)route_[i]=points[i];
    }
    unsigned route_points()const{return route_count_;}
    Phase phase()const{return phase_;}
    bool finished()const{return phase_==Phase::done;}
    const char* failure()const{return failure_;}
    unsigned reports()const{return reports_;}
    // The step of the last JourneyAction::report.
    const JourneyStep& report_step()const{return kJourney[report_<kJourneySteps?report_:0];}
    unsigned waypoint()const{return waypoint_;}
    unsigned stuck_jumps()const{return jumps_;}
    unsigned texts()const{return texts_;}
    JourneyCommand update(const JourneyObservation& seen){
        JourneyCommand c;
        if(phase_==Phase::idle||phase_==Phase::done)return c;
        c.inject=true;
        if(have_last_&&seen.game.updates==last_)return held_;
        have_last_=true;last_=seen.game.updates;
        const auto& o=seen.game;const unsigned u=o.updates;
        if(!phase_since_)phase_since_=u;
        const unsigned age=u-phase_since_;
        switch(phase_){
        case Phase::title:
            // The star screen (state 11) after boot; report, then touch it.
            if(o.scene!=1){if(age>kTitleTimeout)c=fail("no aparecio la pantalla de titulo");break;}
            if(seen.menu==11||seen.menu<0){
                // 6 s later than the other reports: the boot preload job keeps
                // the first images back (the New 3DS title report measured none).
                const unsigned shown=age>kTitleSettle?age-kTitleSettle:0;
                if(!report_once(jTitle,shown,c)&&tap_due(shown))tap(c,kTouchStartX,kTouchStartY);
            }else if(seen.menu==0||seen.menu==1||seen.menu==10)next(Phase::menu,u);
            if(age>kTitleTimeout)c=fail("el titulo no respondio al toque");
            break;
        case Phase::menu:
            // Main menu with Mario's face (state 0): report, then Adventure.
            if(seen.menu==6){next(Phase::select,u);break;}
            if(seen.menu==0){
                if(!menu_since_)menu_since_=u;
                const unsigned shown=u-menu_since_;
                if(!report_once(jMenu,shown,c)&&tap_due(shown))tap(c,kAdventureX,kAdventureY);
            }
            if(age>kMenuTimeout)c=fail("Adventure no abrio la seleccion de partida");
            break;
        case Phase::select:
            // Fade to white (6), then the file selector (0): the BUILD 52-136
            // hang. Report it once it has run kWait, then File A (new game).
            // File A starts the opening: the letter can still come in the
            // title scene (state 11 again) before the castle grounds load.
            if(o.scene==3||(file_tapped_&&seen.menu==11)){next(Phase::opening,u);break;}
            if(seen.menu==0){
                if(!select_since_)select_since_=u;
                const unsigned shown=u-select_since_;
                if(!report_once(jSelect,shown,c)&&tap_due(shown)){tap(c,kFileAX,kFileAY);file_tapped_=true;}
            }
            if(age>kMenuTimeout)c=fail("la partida nueva no empezo");
            break;
        case Phase::opening:
            // The whole opening, watched: only a text that waits for A gets it.
            if(o.scene!=3&&o.scene!=1){if(age>kOpeningTimeout)c=fail("la intro no termino");break;}
            if(!letter_done_&&age>=3*kUpdatesPerSecond){if(report_now(jLetter,c))letter_done_=true;break;}
            if(!castle_done_&&age>=40*kUpdatesPerSecond){if(report_now(jCastleIntro,c))castle_done_=true;break;}
            if(o.player&&o.control&&o.level==kCastleLevel&&age>=10*kUpdatesPerSecond){
                if(!control_since_)control_since_=u;
                if(u-control_since_>=2*kUpdatesPerSecond){next(Phase::control,u);control_since_=0;break;}
            }else control_since_=0;
            read_text(o,c);
            if(age>kOpeningTimeout)c=fail("la intro no devolvio el control");
            break;
        case Phase::control:{
            // Lakitu talks first: read it. The report waits for kWait of
            // uninterrupted control (no picture is measured during the text).
            if(age>kControlTimeout){c=fail("no volvio el control tras los textos");break;}
            read_text(o,c);
            if(!o.control||o.dialogue){control_since_=0;break;}
            if(!control_since_)control_since_=u;
            const unsigned free=u-control_since_;
            if(!report_once(jControl,free,c)&&free>=kWait+30)next(Phase::walk,u);
            break;}
        case Phase::walk:{
            if(o.scene==3&&o.level==kHallLevel){next(Phase::hall,u);break;}
            // Every way out of this phase is bounded by kWalkTimeout (also
            // while a text or a cutscene holds the control).
            if(age>kWalkTimeout){c=fail(o.scene!=3||o.level!=kCastleLevel?"salio del jardin sin entrar al castillo":"no llego a la puerta del castillo");break;}
            if(o.scene!=3||o.level!=kCastleLevel)break;
            if(!o.control){read_text(o,c);break;}
            // The pad keeps steering while a report measures the walk.
            steer(o,c);
            if(!walking_done_&&age>=8*kUpdatesPerSecond){if(report_now(jWalking,c))walking_done_=true;break;}
            if(waypoint_>=route_count_-1&&!door_done_){
                if(report_now(jDoor,c)){door_done_=true;next(Phase::door,u);break;}
            }
            break;}
        case Phase::door:
            // Keep walking into the door: it opens only with the rabbit's key.
            if(o.scene==3&&o.level==kHallLevel){next(Phase::hall,u);break;}
            if(age>kControlTimeout){c=fail("la puerta no respondio");break;}
            if(!o.control){read_text(o,c);break;}
            steer(o,c);
            if(age>=kDoorPush&&report_now(jLocked,c)){phase_=Phase::done;c.action=JourneyAction::report;}
            break;
        case Phase::hall:
            if(o.scene!=3||o.level!=kHallLevel){if(age>kWalkTimeout)c=fail("salio del castillo");break;}
            read_text(o,c);
            if(!hall_done_&&o.control&&age>=kWait){if(report_now(jHall,c))hall_done_=true;break;}
            if(age>=kHallTime&&o.control&&!o.dialogue){if(report_now(jHallEnd,c)){phase_=Phase::done;c.action=JourneyAction::report;}}
            if(age>kHallTime*3)c=fail("los dialogos del castillo no terminaron");
            break;
        default:break;
        }
        held_=c;held_.action=JourneyAction::none;held_.window=false;
        return c;
    }
private:
    // A report: the measurement window opens kWait/2 updates before it.
    bool report_once(unsigned step,unsigned age,JourneyCommand& c){
        if(reported_[step])return false;
        if(age>=kWait/2&&!window_){window_=true;c.window=true;}
        if(age>=kWait){reported_[step]=true;window_=false;report_=step;++reports_;c.action=JourneyAction::report;return true;}
        return true; // still waiting for its report: do nothing else yet
    }
    // A report at the current moment (window opened at the first call).
    bool report_now(unsigned step,JourneyCommand& c){
        if(reported_[step])return true;
        if(!now_since_){now_since_=last_;window_=true;c.window=true;return false;}
        if(last_-now_since_>=kWait/2){reported_[step]=true;window_=false;now_since_=0;report_=step;++reports_;c.action=JourneyAction::report;return true;}
        return false;
    }
    // A tap 1 s after the report, again every 5 s while nothing answers.
    static bool tap_due(unsigned age){return age>=kWait+30&&(age-kWait-30)%(5*kUpdatesPerSecond)<6;}
    void tap(JourneyCommand& c,int x,int y){c.pad.touch=true;c.pad.tx=x;c.pad.ty=y;c.pad.held|=kTouch;}
    void read_text(const Observation& o,JourneyCommand& c){
        if(o.dialogue&&!o.paused){if(!text_since_)text_since_=o.updates;
            if((o.updates-text_since_)%kTextEvery==kTextEvery-1){c.pad.held|=kA;++texts_;}}
        else text_since_=0;
    }
    static bool near(const Observation& o,const Waypoint& w,std::int32_t radius){
        const double dx=double(o.x>>12)-w.x,dz=double(o.z>>12)-w.z;return dx*dx+dz*dz<=double(radius)*radius;
    }
    // Closed-loop steering, independent of the camera: the Circle Pad angle
    // is corrected by the difference between the direction to the waypoint
    // and the direction the player actually moved since the last update.
    void steer(const Observation& o,JourneyCommand& c){
        const unsigned before=waypoint_;
        while(waypoint_<route_count_&&near(o,route_[waypoint_],route_[waypoint_].radius))++waypoint_;
        if(waypoint_!=before){best_=1e18;best_at_=o.updates;} // progress is measured per point
        // Past the last point: keep walking toward it (into the door).
        const auto& w=route_[waypoint_<route_count_?waypoint_:route_count_-1];
        const double px=double(o.x>>12),pz=double(o.z>>12);
        const int want=ds_angle(w.x-px,w.z-pz);
        if(have_prev_){
            const double mx=px-prev_x_,mz=pz-prev_z_;
            // The game maps the pad to the world through the camera with a
            // reflection (Azahar, BUILD 137: up -> -z, right -> +x, so
            // world = camera - pad): the pad turns against the error.
            if(mx*mx+mz*mz>4.0){const int moved=ds_angle(mx,mz);stick_-=wrap_angle(want-moved)/2;}
            // No progress for 3 s: jump (a fence or a step) and keep going.
            const double d=(w.x-px)*(w.x-px)+(w.z-pz)*(w.z-pz);
            if(d<best_-400.0){best_=d;best_at_=o.updates;}
            else if(o.updates-best_at_>3*kUpdatesPerSecond){best_at_=o.updates;++jumps_;jump_until_=o.updates+6;stick_+=0x2000;}
        }else{best_=1e18;best_at_=o.updates;}
        prev_x_=px;prev_z_=pz;have_prev_=true;
        const double a=double(stick_&0xffff)*3.14159265358979323846/32768.0;
        c.pad.sx=int(std::lround(std::sin(a)*150.0));c.pad.sy=int(std::lround(std::cos(a)*150.0));
        if(o.updates<jump_until_)c.pad.held|=kA;
    }
    JourneyCommand fail(const char* why){failure_=why;phase_=Phase::done;JourneyCommand c;c.inject=true;c.action=JourneyAction::finish;return c;}
    void next(Phase p,unsigned u){phase_=p;phase_since_=u;window_=false;now_since_=0;}
    static_assert(kCastleRoutePoints==5,"route initializer below");
    Waypoint route_[kMaxRoute]={kCastleRoute[0],kCastleRoute[1],kCastleRoute[2],kCastleRoute[3],kCastleRoute[4]};
    unsigned route_count_=kCastleRoutePoints;
    bool file_tapped_=false;
    Phase phase_=Phase::idle;
    unsigned menu_since_=0,last_=0,phase_since_=0,select_since_=0,control_since_=0,text_since_=0,now_since_=0,reports_=0,report_=0;
    bool have_last_=false,window_=false,letter_done_=false,castle_done_=false,walking_done_=false,door_done_=false,hall_done_=false;
    bool reported_[kJourneySteps]{};
    unsigned waypoint_=0,jumps_=0,texts_=0,best_at_=0,jump_until_=0;
    int stick_=0;double prev_x_=0,prev_z_=0,best_=1e18;bool have_prev_=false;
    JourneyCommand held_{};
    const char* failure_=nullptr;
};
}
