// Host checks for the SELECT+START test mode: state file, synthetic pad, a
// simulated tour level (title report, entrance as spot, pause menu) and the
// spot mode (title -> level -> character -> spot -> script).
#include "../../source/integrated_native/renderer/test_mode_script.hpp"
#include <cassert>
#include <cstring>
#include <string>
using namespace sm64ds::test_mode;
namespace {
struct Counts {unsigned start_files=0,characters=0,teleports=0,reports=0,windows=0,title_reports=0,starts=0,finishes=0,reenters=0;bool allow_end=false;
    unsigned zones=0,zone_requests=0,zone_teleports=0,zone_reports=0;};
// Zone spot n of the simulated level (BUILD131).
Spot zone_spot(unsigned n){return Spot{-1,0,0,std::int32_t(50000+1000*n),200,std::int32_t(-70000-n),0,true};}
void act(Runner& r,const Command& c,Counts& n){
    assert(c.inject);
    n.windows+=c.window;
    switch(c.action){
    case Action::start_file:++n.start_files;break;
    case Action::set_character:++n.characters;break;
    case Action::teleport:{++n.teleports;
        // A zone step goes to its own spot, every other step to the home spot.
        const unsigned zone=r.current()?r.current()->zone:0;
        if(zone){assert(zone<=n.zones&&r.target().x==zone_spot(zone-1).x&&r.target().z==zone_spot(zone-1).z);++n.zone_teleports;}
        else assert(r.target().x==r.home().x&&r.target().z==r.home().z);
        r.teleport_result(true);break;}
    case Action::report:assert(r.report_step());if(r.title_report())++n.title_reports;else{++n.reports;n.zone_reports+=r.report_step()->zone!=0;}break;
    case Action::zones:{Spot spots[kMaxZones];for(unsigned i=0;i<kMaxZones;++i)spots[i]=zone_spot(i);
        assert(!r.zones_known());r.set_zones(spots,n.zones);++n.zone_requests;break;}
    case Action::finish:assert(n.allow_end);++n.finishes;break;
    case Action::reenter:assert(n.allow_end);++n.reenters;break;
    default:break;
    }
}
// One boot of the lava level (tour index 13, id 14): the player dies during
// step `die_at` (or never, die_at >= kSteps) and the game returns to the castle.
// enter_castle: the game leaves for the castle right after StartFile instead.
Runner lava_boot(const Resume& resume,unsigned die_at,Counts& c,bool enter_castle=false,bool while_settling=false){
    Runner r;r.start(tour_spot(13),true,false,resume);c.allow_end=true;
    Observation o;o.scene=1;unsigned died=0;
    for(unsigned u=0;u<40000&&r.stage()!=Runner::Stage::done;++u){
        o.updates=u;
        if(c.start_files&&o.scene==1){o.scene=3;o.level=enter_castle?2:14;o.player=true;o.control=true;o.character=0;o.body=true;}
        if(!died&&r.stage()==(while_settling?Runner::Stage::settle:Runner::Stage::run)&&r.step()==die_at&&(!while_settling||r.reports())){o.player=false;died=u;}
        if(died&&!o.player&&u>died+40){o.level=2;o.player=true;} // back in the castle
        act(r,r.update(o),c);
    }
    return r;
}
}
int main(){
    // State round trip (V2) and strict rejection.
    State s;s.run=7;s.phase=Phase::pending;s.mode=Mode::spot;s.spot={6,2,1,-123456,7890,42,-1234,true};
    char text[640];int n=format_state(s,text,sizeof text);assert(n>0&&std::size_t(n)<sizeof text);
    State back;assert(parse_state(text,std::size_t(n),back));
    assert(back.run==7&&back.phase==Phase::pending&&back.mode==Mode::spot&&back.spot.level==6&&back.spot.area==2&&back.spot.character==1);
    assert(back.spot.x==-123456&&back.spot.y==7890&&back.spot.z==42&&back.spot.angle==-1234&&back.spot.position);
    State t;t.run=3;t.phase=Phase::running;t.mode=Mode::tour;t.index=17;t.spot=tour_spot(17);
    t.totals={12,3,2,3561,15,412};
    n=format_state(t,text,sizeof text);assert(n>0&&std::size_t(n)<sizeof text);
    assert(parse_state(text,std::size_t(n),back));
    assert(back.mode==Mode::tour&&back.index==17&&back.spot.level==kTour[17].id&&back.phase==Phase::running);
    assert(back.totals.complete==12&&back.totals.incomplete==3&&back.totals.crashed==2&&back.totals.fps_tenths==3561&&
        back.totals.fps_levels==15&&back.totals.worst_p95_tenths==412);
    // BUILD 113 spot state files are still read.
    const char* v1="SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\nnivel=6\narea=0\npersonaje=0\nposicion=1\nx=1\ny=2\nz=3\nangulo=4\n";
    assert(parse_state(v1,std::strlen(v1),back)&&back.mode==Mode::spot&&back.spot.position&&back.spot.z==3);
    const char* bad[]={"SM64DS_PRUEBA_V3\ncarpeta=1\nfase=pendiente\nnivel=6\n",
        "SM64DS_PRUEBA_V1\ncarpeta=0\nfase=pendiente\nnivel=6\n","SM64DS_PRUEBA_V1\ncarpeta=1\nfase=otra\nnivel=6\n",
        "SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\nnivel=99\n","SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\n",
        "SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\nnivel=6\nextra=1\n","SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\nnivel=6x\n",
        "SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\nnivel=6\npersonaje=4\n",
        "SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nnivel=6\n",   // V1 has no mode
        "SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\nnivel=6\nindice=2\n",       // nor an index
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nnivel=6\n",                 // V2 needs a mode
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=otro\nnivel=6\n",
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nindice=50\nnivel=-1\n",
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nindice=2\nnivel=-1\ncolgados=51\n",
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=punto\nnivel=-1\n",
        // BUILD124 resume fields: in range, tour only, never more reports than steps.
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nindice=2\nnivel=-1\npaso=36\n",
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nindice=2\nnivel=-1\npaso=30\ninformes=3\ninformes_zona=4\n",
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nindice=2\nnivel=-1\npaso=4\ninformes=5\n",
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nindice=2\nnivel=-1\nreentradas=6\n",
        "SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=punto\nnivel=6\npaso=3\n",
        "SM64DS_PRUEBA_V1\ncarpeta=1\nfase=pendiente\nnivel=6\npaso=3\n"};
    for(const char* b:bad){State r;assert(!parse_state(b,std::strlen(b),r));}
    {
        State q;q.run=2;q.phase=Phase::pending;q.mode=Mode::tour;q.index=13;q.spot=tour_spot(13);
        q.resume={26,25,2,1,14875,5,823,1};
        n=format_state(q,text,sizeof text);assert(n>0&&std::size_t(n)<sizeof text);
        State r;assert(parse_state(text,std::size_t(n),r));
        assert(r.resume.step==26&&r.resume.reports==25&&r.resume.reentries==2&&r.resume.failed==1&&
            r.resume.fps_hundredths==14875&&r.resume.fps_reports==5&&r.resume.p95_tenths==823&&r.resume.zone_reports==1);
    }
    // Tour table: unique IDs 1..51 except 41, valid characters, short slugs.
    bool seen[64]{};
    for(const auto& level:kTour){assert(level.id>=1&&level.id<=51&&level.id!=41&&!seen[level.id]);seen[level.id]=true;
        assert(level.character<=3&&std::strlen(level.slug)<24);for(const char* c=level.slug;*c;++c)assert((*c>='a'&&*c<='z')||(*c>='0'&&*c<='9')||*c=='-');}
    char folder[48];level_folder(folder,sizeof folder,5);assert(!std::strcmp(folder,"06-bob-omb"));
    // Pad synthesis: taps, repetition, spin, touch arrows and the pause taps.
    const Step& jump=kScript[7];assert(!std::strcmp(jump.id,"salto"));
    assert(pad_for(jump,0).held&kA&&pad_for(jump,5).held&kA&&!(pad_for(jump,6).held&kA));
    const Step& punches=kScript[9];assert(!std::strcmp(punches.id,"golpes"));
    assert(pad_for(punches,0).held&kB&&!(pad_for(punches,3).held&kB)&&pad_for(punches,9).held&kB);
    const Step& camera=kScript[1];const auto cam=pad_for(camera,10);
    assert(cam.touch&&cam.tx==kLeftArrowX&&cam.ty==kArrowY&&(cam.held&kTouch));
    const Step& spin=kScript[15];assert(spin.spin);
    bool moved[4]{};for(unsigned u=0;u<spin.spin;++u){const auto p=pad_for(spin,u);moved[(p.sx>0)+2*(p.sy>0)]=true;}
    assert(moved[0]&&moved[1]&&moved[2]&&moved[3]);
    unsigned pause_index=kSteps;for(unsigned i=0;i<kSteps;++i)if(kScript[i].kind==StepKind::pause)pause_index=i;
    assert(pause_index<kSteps);const Step& pause=kScript[pause_index];
    assert(pad_for(pause,0).held==kStart&&!pad_for(pause,10).held&&pad_for(pause,60).held==kStart&&!(pad_for(pause,63).held&kStart));
    for(const auto& step:kScript){assert(step.report<int(step.length)-1);assert(std::strlen(step.name)<36);
        for(const char* c=step.name;*c;++c)assert(*c>=32&&*c<127);}

    // Simulated tour level: title report, StartFile, an intro dialogue, a
    // character that never switches, the entrance as spot, a pause menu.
    {
        Runner r;r.start(tour_spot(5),true,true);
        Observation o;o.scene=0;o.level=1;
        Counts c;c.zones=kMaxZones;unsigned presses=0,entered=0,pause_opened=0,pause_closed=0,u=0;bool paused=false;
        for(;u<30000&&!r.finished();++u){
            o.updates=u;
            if(u==40)o.scene=1; // boot logo, then the title
            if(c.start_files&&o.scene==1&&!entered){o.scene=3;o.level=6;o.player=true;o.control=true;o.character=3;o.area=0;o.body=true;
                o.x=1000;o.y=2000;o.z=3000;o.angle=512;entered=u;}
            if(entered&&u>entered+3&&presses<3){o.control=false;o.dialogue=true;}
            else if(entered&&presses>=3&&!paused){o.control=true;o.dialogue=false;}
            const auto cmd=r.update(o);
            if(cmd.action==Action::start_file)assert(u>=40+Runner::kTitleWait&&c.title_reports==1);
            if(o.dialogue&&(cmd.pad.held&kA))++presses;
            // The pause menu: START opens it, the second START closes it.
            if(r.stage()==Runner::Stage::run&&r.step()==pause_index&&(cmd.pad.held&kStart)&&!paused&&!pause_opened){paused=true;++pause_opened;o.paused=true;o.control=false;}
            else if(paused&&(cmd.pad.held&kStart)&&u>entered+100&&pause_opened&&r.step()==pause_index){
                // The second tap lands while the menu still opens: ignored once.
                static bool ignored=false;
                if(!ignored){ignored=true;}else{paused=false;o.paused=false;o.control=true;++pause_closed;}
            }
            act(r,cmd,c);
        }
        assert(r.finished()&&!r.failure()&&c.title_reports==1&&c.start_files==1&&c.reports==kMaxReports);
        // BUILD131: the spots are asked for once, each zone is entered once
        // and gives three reports after the 24 of the entrance.
        assert(c.zone_requests==1&&c.zone_teleports==kMaxZones&&c.zone_reports==3*kMaxZones&&r.zone_reports()==3*kMaxZones);
        assert(r.base_reports()==kBaseReports&&r.expected_reports()==kMaxReports&&!r.zone_failure());
        assert(r.character_kept()&&c.characters>=6); // retried every 3 s, then kept Yoshi
        assert(r.home().position&&r.home().x==1000&&r.home().angle==512&&c.teleports>=10);
        assert(pause_opened==1&&pause_closed==1&&r.load_updates()>0);
        // The title report and each situation open their measurement window once.
        assert(c.windows==kMaxReports+1);
    }
    // Spot mode: the BUILD 113 simulated run, unchanged.
    Runner r;Spot spot{6,0,0,100,200,300,90,true};r.start(spot);
    Observation o;o.scene=1;o.level=1;
    unsigned updates=0,deaths=0,presses=0;Counts c;bool done=false;
    unsigned entered=0,switched=0;
    for(;updates<20000&&!done;++updates){
        o.updates=updates;
        if(c.start_files&&o.scene==1&&updates>800){o.scene=3;o.level=6;o.player=true;o.control=true;o.character=2;o.area=0;o.body=false;entered=updates;}
        // Course intro: control returns briefly, then a dialogue needs three A.
        if(entered&&updates>entered+3&&presses<3){o.control=false;o.dialogue=true;}
        else if(entered&&presses>=3){o.control=true;o.dialogue=false;}
        // The first switch is lost inside the dialogue; a retry must land.
        if(c.characters>=2&&!switched)switched=updates;
        if(switched&&updates>switched+20){o.character=0;o.body=true;}
        // One death in the middle of the script: player missing for 40 updates.
        if(c.reports==8&&!deaths&&r.stage()==Runner::Stage::run){o.player=false;++deaths;}
        if(deaths==1&&!o.player&&updates%40==0){o.player=true;}
        const auto cmd=r.update(o);
        if(o.dialogue&&(cmd.pad.held&kA))++presses;
        if(r.update(o).action!=Action::none)assert(false); // same update twice: no repeated action
        act(r,cmd,c);
        done=r.finished();
    }
    // A spot run measures its own spot: no zones are asked for or visited.
    assert(done&&c.start_files==1&&c.characters>=2&&presses>=3&&c.reports==kBaseReports&&r.reports()==kBaseReports);
    assert(c.zone_requests==0&&c.zone_teleports==0&&r.expected_reports()==kBaseReports);
    assert(!r.character_kept()&&r.teleported());
    // The step interrupted by the death restarts and reopens its window once.
    assert(c.windows==kBaseReports+1);
    assert(c.teleports>=10&&r.retries()==1&&!r.failure());
    // A level exit ends the run cleanly instead of warping again.
    Runner exit;exit.start(spot);Observation e;e.scene=3;e.level=6;e.player=true;e.control=true;e.character=0;e.body=true;
    for(unsigned u=0;u<300;++u){e.updates=u;exit.update(e);}
    e.level=1;e.updates=300;const auto cmd=exit.update(e);
    assert(cmd.action==Action::finish&&exit.failure()&&exit.stage()==Runner::Stage::done);
    // A pause menu that never closes ends the level instead of hanging.
    Runner stuck;stuck.start(tour_spot(5),true,false);Observation p;p.scene=1;
    for(unsigned u=0;u<20000&&stuck.stage()!=Runner::Stage::done;++u){
        p.updates=u;
        const auto k=stuck.update(p);
        if(k.action==Action::start_file){p.scene=3;p.level=6;p.player=true;p.control=true;p.character=0;p.body=true;}
        if(stuck.stage()==Runner::Stage::run&&stuck.step()==pause_index&&(k.pad.held&kStart)){p.paused=true;p.control=false;}
    }
    assert(stuck.stage()==Runner::Stage::done&&stuck.failure()&&!std::strcmp(stuck.failure(),"el menu de pausa no se cerro"));
    // A level whose player keeps the pause lock (the castle courtyard): the
    // script runs after the grace period instead of timing out at the entry.
    {
        Runner r;r.start(tour_spot(2),true,false);Observation o;o.scene=1;Counts c;
        for(unsigned u=0;u<30000&&!r.finished()&&r.stage()!=Runner::Stage::done;++u){
            o.updates=u;
            if(c.start_files&&o.scene==1){o.scene=3;o.level=3;o.player=true;o.control=true;o.pause_locked=true;o.character=1;o.body=true;}
            const auto cmd=r.update(o);act(r,cmd,c);
        }
        assert(r.finished()&&!r.failure()&&r.pause_lock_ignored()&&c.reports==kBaseReports);
        assert(r.menu_closes()>0&&!r.menu_closed()); // START was tried; a genuine lock ignores it
    }
    // BUILD124: the courtyard's lock is its stray save menu; START closes it
    // (BUILD129: first tap after 3 s of lock) and the script then runs unlocked.
    {
        Runner r;r.start(tour_spot(2),true,false);Observation o;o.scene=1;Counts c;unsigned entered=0,closed=0;
        for(unsigned u=0;u<30000&&!r.finished()&&r.stage()!=Runner::Stage::done;++u){
            o.updates=u;
            if(c.start_files&&o.scene==1){o.scene=3;o.level=3;o.player=true;o.control=true;o.pause_locked=true;o.character=1;o.body=true;entered=u;}
            const auto cmd=r.update(o);act(r,cmd,c);
            if(o.pause_locked&&(cmd.pad.held&kStart)){o.pause_locked=false;closed=u;}
        }
        assert(r.finished()&&!r.failure()&&!r.pause_lock_ignored()&&c.reports==kBaseReports);
        assert(closed>entered&&closed-entered>=3*kUpdatesPerSecond&&closed-entered<=4*kUpdatesPerSecond&&r.menu_closes()>=1&&r.menu_closes()<=3);
    }
    // An entry lock that ends by itself is never tapped (BUILD129: 2 s < first
    // tap) nor a closed menu, and a pause opened while settling is closed.
    {
        Runner r;r.start(tour_spot(3),true,false);Observation o;o.scene=1;Counts c;unsigned entered=0,paused_at=0,resumed=0;
        for(unsigned u=0;u<30000&&!r.finished()&&r.stage()!=Runner::Stage::done;++u){
            o.updates=u;
            if(c.start_files&&o.scene==1){o.scene=3;o.level=4;o.player=true;o.control=true;o.pause_locked=true;o.character=2;o.body=true;entered=u;}
            if(entered&&o.pause_locked&&u==entered+60)o.pause_locked=false;          // 2 s: before the first tap
            if(entered&&!paused_at&&r.stage()==Runner::Stage::settle){o.paused=true;o.control=false;paused_at=u;}
            const auto cmd=r.update(o);act(r,cmd,c);
            if(o.paused&&(cmd.pad.held&kStart)){o.paused=false;o.control=true;resumed=u;}
        }
        assert(r.finished()&&!r.failure()&&!r.menu_closed()&&r.menu_closes()==0&&c.reports==kBaseReports); // no tap
        assert(paused_at&&resumed>paused_at&&resumed-paused_at<=20);
    }
    // BUILD124: lava kills the player during step 4 (before its report); the
    // next boot enters again, skips the fatal step and keeps four reports.
    {
        Counts c;const auto first=lava_boot(Resume{},4,c);
        assert(c.reenters==1&&c.finishes==0&&c.reports==4&&first.reports()==4&&first.resume_step()==5);
        assert(!first.finished()&&first.failure()&&!std::strcmp(first.failure(),"el juego entro en otro nivel"));
        Counts d;const auto second=lava_boot(Resume{first.resume_step(),first.reports(),1},kSteps,d);
        assert(second.finished()&&!second.failure()&&d.reenters==0&&d.finishes==0);
        assert(d.reports==kBaseSteps-5&&second.reports()==kBaseSteps-1&&second.reentries()==1);
        // The last allowed re-entry dies again: the level ends instead.
        Counts e;const auto last=lava_boot(Resume{7,6,kMaxReentries},9,e);
        assert(e.finishes==1&&e.reenters==0&&last.failure()&&last.reports()==8);
        // A level that leaves before its script ran is not re-entered.
        Counts f;const auto never=lava_boot(Resume{},kSteps,f,true);
        assert(f.finishes==1&&f.reenters==0&&never.reports()==0&&never.failure());
        // BUILD129: an exit noticed while settling home for the last step
        // (BUILD128 lost "quieto-final" in 7 levels) re-enters and runs it.
        assert(kScript[kBaseSteps-1].home);
        Counts g;const auto settling=lava_boot(Resume{},kBaseSteps-1,g,false,true);
        assert(g.reenters==1&&g.finishes==0&&settling.resume_step()==kBaseSteps-1&&settling.reports()==kBaseSteps-1);
        Counts h;const auto rerun=lava_boot(Resume{settling.resume_step(),settling.reports(),1},kSteps,h);
        assert(rerun.finished()&&!rerun.failure()&&h.reports==1&&rerun.reports()==kBaseSteps);
        // Spot runs never resume.
        Runner spot_run;spot_run.start(spot,false,false,Resume{5,4,1});assert(spot_run.step()==0&&spot_run.reports()==0);
    }
    // BUILD131 zones.
    {
        // A level with two spots plays two zones and still ends complete.
        Counts a;a.zones=2;const auto two=lava_boot(Resume{},kSteps,a);
        assert(two.finished()&&!two.failure()&&!two.zone_failure()&&a.reports==kBaseReports+6&&a.zone_teleports==2&&two.zones()==2);
        assert(two.expected_reports()==kBaseReports+6&&two.base_reports()==kBaseReports);
        // A level without spots asks once and ends after the entrance script.
        Counts b;const auto none=lava_boot(Resume{},kSteps,b);
        assert(none.finished()&&!none.failure()&&b.zone_requests==1&&b.zone_teleports==0&&b.reports==kBaseReports);
        // A death at zone 2 (its second step) abandons that zone only: the
        // next boot continues at zone 3, going to its spot first.
        Counts c;c.zones=4;const auto died=lava_boot(Resume{},kBaseSteps+4,c);
        assert(c.reenters==1&&c.finishes==0&&died.resume_step()==kBaseSteps+6&&died.reports()==kBaseReports+4&&died.zone_reports()==4);
        Counts d;d.zones=4;const auto after=lava_boot(Resume{died.resume_step(),died.reports(),1,0,0,0,0,died.zone_reports()},kSteps,d);
        assert(after.finished()&&!after.failure()&&d.zone_requests==1&&d.zone_teleports==2&&d.reports==6&&d.zone_reports==6);
        assert(after.reports()==kBaseReports+10&&after.zone_reports()==10);
        // A death at a zone with no re-entry left ends the level without
        // failing it: the entrance script was complete.
        Counts e;e.zones=4;const auto last=lava_boot(Resume{kBaseSteps,kBaseReports,kMaxReentries},kBaseSteps+1,e);
        assert(e.finishes==1&&e.reenters==0&&last.finished()&&!last.failure()&&last.zone_failure()&&last.zone_reports()==1);
        // The development limit shortens the entrance script, not the zones.
        Runner r;r.start(tour_spot(5),true,false);r.limit_steps(2);Observation o;o.scene=1;Counts f;f.zones=1;f.allow_end=true;
        for(unsigned u=0;u<20000&&r.stage()!=Runner::Stage::done;++u){o.updates=u;
            if(f.start_files&&o.scene==1){o.scene=3;o.level=6;o.player=true;o.control=true;o.character=0;o.body=true;}
            act(r,r.update(o),f);}
        assert(r.finished()&&!r.failure()&&f.reports==2+3&&f.zone_reports==3&&r.expected_reports()==5);
        // The game never returns to the level after going to zone 1 (castle
        // basement in the first BUILD131 tour): the zone is abandoned and the
        // next boot continues at zone 2; the level is not failed.
        Runner q;q.start(tour_spot(5),true,false);q.limit_steps(2);Observation p;p.scene=1;Counts g;g.zones=2;g.allow_end=true;
        for(unsigned u=0;u<40000&&q.stage()!=Runner::Stage::done;++u){p.updates=u;
            if(g.start_files&&p.scene==1){p.scene=3;p.level=6;p.player=true;p.control=true;p.character=0;p.body=true;}
            if(g.zone_teleports)p.player=false;
            act(q,q.update(p),g);}
        assert(g.reenters==1&&g.finishes==0&&q.resume_step()==kBaseSteps+3&&g.reports==2&&g.zone_reports==0);
    }
    std::printf("test mode script: %u levels in the tour, %u reports per level, spot run %u updates\n",kTourLevels,kMaxReports,updates);
}
