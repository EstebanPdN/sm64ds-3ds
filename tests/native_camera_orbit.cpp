#include "sm64ds/camera_orbit.hpp"
#include <cstdint>
#include <limits>
#ifndef CAMERA_ORBIT_ARM
#include <cstdio>
#endif

using namespace sm64ds::camera;
extern "C" { extern std::int16_t data_02082214[8192]; }
namespace {
#define REQUIRE(condition) do { if(!(condition))return __LINE__; } while(false)
Tick frame(std::uint64_t number=1) {
    Tick tick;
    tick.identity={0x08201000,0x08202000,0x00680000,1};
    tick.number=number;tick.elapsed_us=10000;tick.allowed=true;
    tick.target={123456,-234567,345678};tick.seed_radius=0x100000;
    return tick;
}
Config instant() {Config c;c.response_us=0;return c;}
bool same_vector(Vector a,Vector b) {return a.x==b.x && a.y==b.y && a.z==b.z;}
bool same_state(const State& a,const State& b) {
    return a.identity.camera==b.identity.camera && a.identity.target==b.identity.target &&
        a.identity.mode==b.identity.mode && a.identity.generation==b.identity.generation &&
        a.last_tick==b.last_tick && a.yaw_q16==b.yaw_q16 && a.pitch_q16==b.pitch_q16 &&
        a.radius==b.radius && a.yaw_velocity==b.yaw_velocity &&
        a.pitch_velocity==b.pitch_velocity && a.substep_us==b.substep_us &&
        a.has_tick==b.has_tick && a.armed==b.armed;
}
Result step(Orbit& orbit,const Tick& tick) {return orbit.step(tick,data_02082214);}
int test(unsigned index) {
    const auto max=std::numeric_limits<std::int32_t>::max();
    const auto min=std::numeric_limits<std::int32_t>::min();
    if(index==0) {
        Orbit orbit;auto t=frame();
        REQUIRE(step(orbit,t).status==Status::idle);
        REQUIRE(!orbit.state().armed && orbit.state().last_tick==1);
        REQUIRE(same_vector(t.target,Vector{123456,-234567,345678}));
    } else if(index==1) {
        Orbit orbit(instant());auto t=frame();t.stick.x=stick_one;t.elapsed_us=100000;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        unsigned wraps=0;std::uint32_t prior=0;
        for(unsigned i=1;i<=50;++i) {
            t.number=i+1;const auto result=step(orbit,t);
            REQUIRE(result.has_candidate);
            const auto expected=static_cast<std::uint32_t>((2147483648LL*i)/10);
            REQUIRE(orbit.state().yaw_q16==expected);
            REQUIRE(orbit.state().yaw_q16-prior>0);
            if(orbit.state().yaw_q16<prior)++wraps;
            prior=orbit.state().yaw_q16;
        }
        REQUIRE(wraps==2 && orbit.state().yaw_q16==0x80000000u);
    } else if(index==2) {
        Orbit orbit(instant());auto t=frame();t.stick.y=stick_one;t.elapsed_us=100000;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        Result result;
        for(unsigned i=0;i<20;++i){++t.number;result=step(orbit,t);}
        REQUIRE(result.pitch==-0x3c72 && result.eye.y>t.target.y);
        REQUIRE(orbit.state().pitch_velocity==0);
        t.stick.y=-stick_one;++t.number;result=step(orbit,t);
        REQUIRE(result.pitch>-0x3c72);
        for(unsigned i=0;i<20;++i){++t.number;result=step(orbit,t);}
        REQUIRE(result.pitch==0x3c72 && result.eye.y<t.target.y);
    } else if(index==3) {
        Orbit orbit;auto t=frame();t.stick.x=stick_one;t.elapsed_us=100000;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        ++t.number;REQUIRE(step(orbit,t).has_candidate);
        t.stick={};t.elapsed_us=10000;
        for(unsigned i=0;i<40;++i){++t.number;REQUIRE(step(orbit,t).has_candidate);}
        const auto phase=orbit.state().yaw_q16;
        REQUIRE(orbit.state().yaw_velocity==0);
        for(unsigned i=0;i<100;++i){++t.number;REQUIRE(step(orbit,t).has_candidate);}
        REQUIRE(orbit.state().yaw_q16==phase && orbit.state().armed);
    } else if(index==4) {
        Orbit a,b;auto ta=frame(),tb=ta;ta.stick={stick_one,8192};tb.stick=ta.stick;
        REQUIRE(step(a,ta).status==Status::reseeded && step(b,tb).status==Status::reseeded);
        Result ra,rb;
        for(unsigned i=0;i<120;++i) {
            ++ta.number;ta.elapsed_us=i%3==2 ? 16666 : 16667;ra=step(a,ta);
        }
        for(unsigned i=0;i<60;++i) {
            ++tb.number;tb.elapsed_us=i%3==2 ? 33334 : 33333;rb=step(b,tb);
        }
        REQUIRE(a.state().yaw_q16==b.state().yaw_q16);
        REQUIRE(a.state().pitch_q16==b.state().pitch_q16);
        REQUIRE(a.state().yaw_velocity==b.state().yaw_velocity);
        REQUIRE(a.state().pitch_velocity==b.state().pitch_velocity);
        REQUIRE(same_vector(ra.eye,rb.eye));
        ta.stick={-stick_one,0};tb.stick=ta.stick;
        ++ta.number;ta.elapsed_us=1000;ra=step(a,ta);
        ++tb.number;tb.elapsed_us=1000;rb=step(b,tb);
        REQUIRE(a.state().yaw_velocity>0); // Reversal is smoothed, not a sign snap.
        ++ta.number;ta.elapsed_us=100000;ra=step(a,ta);
        ++tb.number;tb.elapsed_us=100000;rb=step(b,tb);
        REQUIRE(a.state().yaw_velocity<0);
        REQUIRE(a.state().yaw_q16==b.state().yaw_q16 && same_vector(ra.eye,rb.eye));
    } else if(index==5) {
        Orbit orbit;auto t=frame();t.stick.x=stick_one;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        ++t.number;REQUIRE(step(orbit,t).has_candidate);const State prior=orbit.state();
        t.stick.x=-stick_one;t.target.x=max;
        REQUIRE(step(orbit,t).status==Status::duplicate && same_state(prior,orbit.state()));
        --t.number;REQUIRE(step(orbit,t).status==Status::stale && same_state(prior,orbit.state()));
    } else if(index==6) {
        Orbit orbit;auto t=frame();t.stick.x=stick_one;
        REQUIRE(step(orbit,t).status==Status::reseeded);++t.number;
        REQUIRE(step(orbit,t).has_candidate);
        t.paused=true;REQUIRE(step(orbit,t).status==Status::duplicate && !orbit.state().armed);
        ++t.number;REQUIRE(step(orbit,t).status==Status::suspended);
        t.paused=false;++t.number;t.elapsed_us=5000000;
        REQUIRE(step(orbit,t).status==Status::invalid_time && !orbit.state().armed);
        ++t.number;t.elapsed_us=16667;t.seed_yaw=1234;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        REQUIRE(orbit.state().yaw_q16==1234u*65536 && !orbit.state().yaw_velocity);
        ++t.number;t.allowed=false;REQUIRE(step(orbit,t).status==Status::suspended);
    } else if(index==7) {
        Orbit orbit;auto t=frame();t.stick.x=stick_one;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        for(unsigned i=0;i<4;++i) {
            ++t.number;REQUIRE(step(orbit,t).has_candidate);
            if(i==0)++t.identity.generation;
            if(i==1)++t.identity.target;
            if(i==2)++t.identity.mode;
            if(i==3)++t.identity.camera;
            ++t.number;t.seed_yaw=static_cast<std::int16_t>(100+i);
            REQUIRE(step(orbit,t).status==Status::reseeded);
            REQUIRE(orbit.state().yaw_q16==(100+i)*65536u && !orbit.state().yaw_velocity);
        }
        ++t.identity.generation;
        REQUIRE(step(orbit,t).status==Status::duplicate && !orbit.state().armed);
        ++t.number;REQUIRE(step(orbit,t).status==Status::reseeded);
        orbit.reset();REQUIRE(!orbit.state().armed && !orbit.state().has_tick);
    } else if(index==8) {
        Orbit a(instant()),b(instant());auto t=frame();t.stick.x=stick_one;
        REQUIRE(step(a,t).status==Status::reseeded && step(b,t).status==Status::reseeded);
        ++t.number;t.elapsed_us=500;REQUIRE(step(a,t).has_candidate);
        REQUIRE(a.state().yaw_q16==0 && a.state().substep_us==500);
        ++t.number;REQUIRE(step(a,t).has_candidate);
        t.elapsed_us=1000;REQUIRE(step(b,t).has_candidate);
        REQUIRE(a.state().yaw_q16==b.state().yaw_q16 && a.state().substep_us==0);
    } else if(index==9) {
        Vector out{31,41,59},before=out;
        REQUIRE(!orbit_position({max,0,0},max,0x4000,0,data_02082214,trig_entries,out));
        REQUIRE(same_vector(out,before));
        REQUIRE(!orbit_position({0,min,0},max,0,0x4000,data_02082214,trig_entries,out));
        REQUIRE(same_vector(out,before));
        Orbit orbit(instant());auto t=frame();t.stick.x=stick_one;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        const auto phase=orbit.state().yaw_q16;
        t.target={max,max,max};++t.number;
        REQUIRE(step(orbit,t).status==Status::invalid_geometry);
        REQUIRE(!orbit.state().armed && orbit.state().yaw_q16==phase);
    } else if(index==10) {
        auto t=frame();t.stick.x=stick_one+1;Orbit orbit;
        REQUIRE(step(orbit,t).status==Status::invalid_input);
        ++t.number;t.stick={min,0};REQUIRE(step(orbit,t).status==Status::invalid_input);
        ++t.number;t.stick={1,0};t.seed_radius=0;
        REQUIRE(step(orbit,t).status==Status::invalid_seed);
        ++t.number;t.seed_radius=1;t.seed_pitch=-32768;
        REQUIRE(step(orbit,t).status==Status::invalid_seed);
        ++t.number;t.seed_pitch=0;t.identity.camera=0;
        REQUIRE(step(orbit,t).status==Status::invalid_identity);
        Config bad=instant();bad.pitch_limit=0x4000;Orbit invalid(bad);
        t=frame();t.stick.x=1;REQUIRE(step(invalid,t).status==Status::invalid_config);
        Vector out{11,22,33};
        REQUIRE(!orbit_position({},1,0,0,nullptr,trig_entries,out));
        REQUIRE(!orbit_position({},1,0,0,data_02082214,trig_entries-1,out));
        std::int16_t corrupted[8192];
        for(unsigned i=0;i<8192;++i)corrupted[i]=data_02082214[i];
        corrupted[1]=4097;
        REQUIRE(!orbit_position({},1,0,0,corrupted,trig_entries,out));
        REQUIRE(same_vector(out,Vector{11,22,33}));
    } else if(index==11) {
        const std::int16_t angles[]={0,0x4000,-32768,-0x4000};
        const Vector expected[]={{0,0,8192},{8192,0,0},{0,0,-8192},{-8192,0,0}};
        Vector out;
        for(unsigned i=0;i<4;++i) {
            REQUIRE(orbit_position({},8192,angles[i],0,data_02082214,trig_entries,out));
            REQUIRE(same_vector(out,expected[i]));
        }
        REQUIRE(orbit_position({},8192,0,-0x2000,data_02082214,trig_entries,out));
        REQUIRE(out.y==5792 && out.z==5792 && out.x==0);
        REQUIRE(orbit_position({},8192,0,0x2000,data_02082214,trig_entries,out));
        REQUIRE(out.y==-5792 && out.z==5792);
    } else if(index==12) {
        Orbit orbit;REQUIRE(!orbit.rebase(0,0));auto t=frame();t.stick.x=stick_one;
        REQUIRE(step(orbit,t).status==Status::reseeded);++t.number;
        REQUIRE(step(orbit,t).has_candidate);
        REQUIRE(orbit.rebase(0x2000,-0x2000));
        const State prior=orbit.state();REQUIRE(!orbit.rebase(0,-32768));
        REQUIRE(same_state(prior,orbit.state()));
        ++t.number;t.stick={};const auto result=step(orbit,t);
        REQUIRE(result.yaw==0x2000 && result.pitch==-0x2000);
        REQUIRE(result.radius==t.seed_radius && !orbit.state().yaw_velocity);
        t.target.x+=11;t.target.y-=22;t.target.z+=33;++t.number;
        const auto followed=step(orbit,t);
        REQUIRE(followed.yaw==result.yaw && followed.pitch==result.pitch);
        REQUIRE(same_vector(followed.eye,Vector{result.eye.x+11,result.eye.y-22,result.eye.z+33}));
    } else if(index==13) {
        struct Guard {std::uint32_t before=0xabc123ef;Orbit orbit;std::uint32_t after=0x87654321;} guard;
        auto t=frame();t.stick={stick_one,stick_one};
        const auto original=t;
        REQUIRE(step(guard.orbit,t).status==Status::reseeded);
        for(unsigned i=0;i<200;++i) {
            ++t.number;t.elapsed_us=10000;
            t.stick={static_cast<std::int32_t>((i*7919)%65537)-32768,
                     static_cast<std::int32_t>((i*1237)%65537)-32768};
            REQUIRE(step(guard.orbit,t).has_candidate);
        }
        REQUIRE(guard.before==0xabc123ef && guard.after==0x87654321);
        REQUIRE(t.identity.camera==original.identity.camera && same_vector(t.target,original.target));
    } else if(index==14) {
        Orbit orbit(instant());auto t=frame();t.stick.x=1;t.elapsed_us=100000;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        for(unsigned i=0;i<20;++i){++t.number;REQUIRE(step(orbit,t).has_candidate);}
        REQUIRE(orbit.state().yaw_q16==2u*65536); // Sub-LUT deflection is retained.
        orbit.reset();t=frame();t.stick.x=stick_one/2;t.elapsed_us=100000;
        REQUIRE(step(orbit,t).status==Status::reseeded);
        for(unsigned i=0;i<10;++i){++t.number;REQUIRE(step(orbit,t).has_candidate);}
        REQUIRE(orbit.state().yaw_q16==0x40000000u);
    } else if(index==15) {
        Config c;c.yaw_rate=0xffffffff;c.pitch_rate=0xffffffff;
        c.response_us=1000000;c.max_elapsed_us=250000;c.pitch_limit=0x3f00;
        Orbit orbit(c);auto t=frame();t.elapsed_us=250000;t.stick={stick_one,-stick_one};
        REQUIRE(step(orbit,t).status==Status::reseeded);++t.number;
        REQUIRE(step(orbit,t).has_candidate);
        REQUIRE(orbit.state().yaw_velocity>0 && orbit.state().yaw_velocity<=0xffffffffLL);
        ++t.number;t.elapsed_us=0;REQUIRE(step(orbit,t).status==Status::invalid_time);
        ++t.number;t.elapsed_us=250001;REQUIRE(step(orbit,t).status==Status::invalid_time);
    } else return 0xffffffff;
    return 0;
}
}
extern "C" std::uint32_t camera_orbit_case(unsigned index) {return static_cast<std::uint32_t>(test(index));}
extern "C" std::uint32_t camera_orbit_geometry(const std::int32_t* in,std::int32_t* out) {
    Vector value{out[0],out[1],out[2]};
    const bool okay=orbit_position({in[0],in[1],in[2]},in[3],
        static_cast<std::int16_t>(in[4]),static_cast<std::int16_t>(in[5]),
        data_02082214,trig_entries,value);
    if(okay){out[0]=value.x;out[1]=value.y;out[2]=value.z;}
    return okay;
}
extern "C" void camera_orbit_test_entry() {}
#ifndef CAMERA_ORBIT_ARM
extern "C" {std::int16_t data_02082214[8192];}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 3;
    const auto count=std::fread(data_02082214,sizeof(std::int16_t),8192,file);std::fclose(file);
    if(count!=8192)return 4;
    for(unsigned i=0;i<16;++i) {
        const auto line=camera_orbit_case(i);
        if(line){std::fprintf(stderr,"camera orbit case %u failed line %u\n",i,line);return 1;}
    }
    std::puts("16 camera orbit cases passed");return 0;
}
#endif
