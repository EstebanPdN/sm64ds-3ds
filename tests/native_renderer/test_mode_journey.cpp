// Host checks for the BUILD137 player journey (L+SELECT+START): title touch,
// Adventure, File A, the watched opening, Lakitu's text, the walk along the
// castle route with closed-loop steering, the castle hall and its reports.
#include "../../source/integrated_native/renderer/test_mode_journey.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <string_view>
using namespace sm64ds::test_mode;
namespace {
struct Sim {
    JourneyObservation seen;unsigned reports=0,windows=0,taps=0,a_presses=0;bool finished=false;
    JourneyCommand step(JourneyRunner& r){
        ++seen.game.updates;const auto c=r.update(seen);
        assert(c.inject);windows+=c.window;
        if(c.action==JourneyAction::report)++reports;
        if(c.action==JourneyAction::finish||r.finished())finished=true;
        if(c.pad.touch)++taps;
        if(c.pad.held&kA)++a_presses;
        return c;
    }
    void run(JourneyRunner& r,unsigned n){for(unsigned i=0;i<n&&!finished;++i)step(r);}
};
// The whole journey with the camera at a fixed yaw (radians) during the walk.
void journey(double camera,bool open){
    JourneyRunner r;r.start();Sim s;
    // Title: star screen (11). It is reported, then touched.
    s.seen.game.scene=1;s.seen.menu=11;
    s.run(r,JourneyRunner::kTitleSettle+4*kUpdatesPerSecond+40);
    assert(s.reports==1&&r.report_step().id==std::string_view("titulo")&&s.taps>0&&s.windows==1);
    // The touch leads through the logo states to the main menu (0).
    s.seen.menu=10;s.run(r,30);s.seen.menu=1;s.run(r,10);s.seen.menu=0;
    const unsigned taps_before_menu=s.taps;
    s.run(r,4*kUpdatesPerSecond+40);
    assert(s.reports==2&&r.report_step().id==std::string_view("menu")&&s.taps>taps_before_menu);
    // Adventure: fade to white (6), then the file selector (0).
    s.seen.menu=6;s.run(r,20);assert(r.phase()==JourneyRunner::Phase::select);
    s.seen.menu=0;const unsigned taps_before_file=s.taps;
    s.run(r,4*kUpdatesPerSecond+40);
    assert(s.reports==3&&r.report_step().id==std::string_view("seleccion")&&s.taps>taps_before_file);
    // File A: the letter (state 11 again in the title scene), then the castle grounds.
    s.seen.menu=11;s.run(r,2);assert(r.phase()==JourneyRunner::Phase::opening);
    s.seen.menu=-1;s.seen.game.scene=3;s.seen.game.level=1;s.seen.game.player=true;s.seen.game.control=false;
    const unsigned a_before_opening=s.a_presses;
    s.run(r,60*kUpdatesPerSecond);
    assert(s.reports==5&&s.a_presses==a_before_opening); // watched: no A without a waiting text
    // A text that waits for A gets one press every 1.5 s, never every update.
    s.seen.game.dialogue=true;const unsigned a0=s.a_presses;s.run(r,3*JourneyRunner::kTextEvery);
    assert(s.a_presses-a0==3);s.seen.game.dialogue=false;
    // Control at Yoshi's start: report, then the walk along the route.
    s.seen.game.control=true;s.seen.game.x=-1200*4096;s.seen.game.z=6800*4096;
    s.run(r,3*kUpdatesPerSecond);assert(r.phase()==JourneyRunner::Phase::control);
    // Lakitu's text interrupts control: the report waits for it to end.
    s.seen.game.control=false;s.seen.game.dialogue=true;s.run(r,2*kUpdatesPerSecond);assert(s.reports==5);
    s.seen.game.control=true;s.seen.game.dialogue=false;
    s.run(r,4*kUpdatesPerSecond+40);
    assert(r.phase()==JourneyRunner::Phase::walk&&s.reports==6);
    // Walk: a player whose motion follows the Circle Pad rotated by an
    // unknown camera yaw (here 90 degrees). The steering must reach the end.
    double x=-1200,z=6800;bool door=false;
    for(unsigned i=0;i<150*kUpdatesPerSecond&&r.phase()==JourneyRunner::Phase::walk;++i){
        const auto c=s.step(r);
        // The game's mapping (measured in Azahar): world = camera - pad.
        const double a=camera-std::atan2(double(c.pad.sx),double(c.pad.sy));
        const double speed=std::sqrt(double(c.pad.sx)*c.pad.sx+double(c.pad.sy)*c.pad.sy)>10?25.0:0.0;
        x+=std::sin(a)*speed;z+=std::cos(a)*speed;
        s.seen.game.x=std::int32_t(x)*4096;s.seen.game.z=std::int32_t(z)*4096;
        if(r.report_step().id==std::string_view("puerta"))door=true;
        // The door opens only with the key: then it takes the player into the hall.
        if(door&&open&&z<-700){s.seen.game.level=2;}
        if(r.phase()==JourneyRunner::Phase::door&&!open)break;
    }
    assert(!r.failure()&&door&&r.waypoint()>=r.route_points()-1);
    if(!open){
        // A new game: the locked door ends phase 1 with its own report.
        assert(r.phase()==JourneyRunner::Phase::door);
        s.run(r,JourneyRunner::kDoorPush+JourneyRunner::kWait+10);
        assert(s.finished&&!r.failure()&&s.reports==kJourneyPhase1Reports);
        assert(r.report_step().id==std::string_view("puerta-cerrada"));
        return;
    }
    for(unsigned i=0;i<10*kUpdatesPerSecond&&r.phase()!=JourneyRunner::Phase::hall;++i){s.step(r);s.seen.game.level=2;}
    assert(r.phase()==JourneyRunner::Phase::hall);
    // The hall: a report, then the end report after the dialogues.
    s.run(r,40*kUpdatesPerSecond);
    assert(s.finished&&!r.failure()&&s.reports==kJourneyPhase1Reports+1);
}
}
int main(){
    const double pi=3.14159265358979323846;
    for(const double camera:{pi,pi/2,0.0,-3*pi/4})journey(camera,false);
    journey(pi,true); // a save with the key: the door opens into the hall
    // A title that never answers fails with a reason instead of waiting for ever.
    JourneyRunner stuck;stuck.start();Sim t;t.seen.game.scene=1;t.seen.menu=11;
    t.run(stuck,JourneyRunner::kTitleTimeout+10);
    assert(t.finished&&stuck.failure());
    // Development route override.
    JourneyRunner custom;custom.start();const Waypoint pts[2]={{0,0,100},{10,10,100}};custom.set_route(pts,2);
    assert(custom.route_points()==2);
    return 0;
}
