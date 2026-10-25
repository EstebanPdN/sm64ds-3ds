#include "sm64ds/native_camera.hpp"
#include "sm64ds/camera_orbit.hpp"
#include <cstring>
#include <limits>
#include <new>
#include <initializer_list>

extern "C" {
extern unsigned char data_0208715c[],data_0209b008[],data_02082214[];
extern unsigned char data_0209f294,data_0209f2c4,data_0209f20c,data_020a0e40;
extern unsigned char data_0209f49c[],data_0209f49e[];
extern std::int32_t data_0209fc48,data_0209b454;
extern void* data_0209f318;
std::int32_t LenVec3(const void*);
std::int16_t Vec3_HorzAngle(const void*,const void*);
std::int16_t Vec3_VertAngle(const void*,const void*);
std::int32_t sm64ds_original_camera_init(void*);
std::int32_t sm64ds_original_camera_cleanup(void*);
void sm64ds_original_camera_pending_destroy(void*);
}

namespace {
using namespace sm64ds::camera;
struct Publication { Stick stick;std::uint64_t tick=0,vblank=0;bool valid=false; };
struct Lifetime {
    char* self=nullptr;std::uint32_t uid=0,generation=0,target_uid=0;
    std::uintptr_t target=0,state=0;bool ready=false;
};
struct Clock {
    std::uint64_t tick=0,vblank=0;std::uint32_t remainder=0,elapsed=0;
    bool present=false,valid=false;
};
struct Candidate {
    Vector eye;std::uint64_t tick=0;std::uint32_t generation=0;
    bool accepted=false,reseed=false,finish_pending=false;
};
Publication publication;
Lifetime lifetime;
Clock clock;
Candidate candidate;
sm64ds::NativeCameraStats stats;
// Explicit lazy placement construction needs neither .init_array execution
// in the CPU fixture, guard/TLS services nor an allocation.
alignas(Orbit) unsigned char orbit_storage[sizeof(Orbit)];
bool orbit_initialized=false;
Orbit& orbit() {
    if(!orbit_initialized) {::new(static_cast<void*>(orbit_storage)) Orbit{};orbit_initialized=true;}
    return *std::launder(reinterpret_cast<Orbit*>(orbit_storage));
}
template<class T> T get(const void* p,std::size_t offset=0) {
    T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof value);return value;
}
template<class T> void put(void* p,std::size_t offset,T value) {
    std::memcpy(static_cast<unsigned char*>(p)+offset,&value,sizeof value);
}
void disarm() {orbit().reset();candidate={};stats.armed=0;}
void invalidate() {disarm();lifetime={};stats.ready=0;}
bool consume_clock() {
    if(!publication.valid)return false;
    if(clock.present && publication.tick<clock.tick)return false;
    if(clock.present && publication.tick==clock.tick)return clock.valid;
    std::uint64_t delta=0;
    const bool increasing=!clock.present || publication.vblank>=clock.vblank;
    if(clock.present && increasing)delta=publication.vblank-clock.vblank;
    clock.present=true;clock.tick=publication.tick;clock.vblank=publication.vblank;
    clock.valid=increasing && delta>0 && delta<=6;
    if(clock.valid) {
        const auto numerator=std::uint32_t(delta)*1000000u+clock.remainder;
        clock.elapsed=numerator/60u;clock.remainder=numerator%60u;
    } else {clock.elapsed=0;clock.remainder=0;}
    ++stats.consumed_ticks;stats.last_tick=clock.tick;stats.last_vblank=clock.vblank;
    stats.elapsed_us=clock.elapsed;
    return clock.valid;
}
bool normal_state(std::uintptr_t state) {
    const auto base=reinterpret_cast<std::uintptr_t>(data_0209b008);
    if(state<base)return false;
    const auto offset=state-base;
    return offset%16==0 && (offset/16<=8 || offset/16==11);
}
bool globally_suspended() {
    return data_0209fc48 || (data_0209f294|data_0209f2c4|data_0209f20c) ||
           (std::uint32_t(data_0209b454)&0x40000000u);
}
bool admitted(char* self,const void* mode,const void* view,int secondary) {
    if(!lifetime.ready || lifetime.self!=self || data_0209f318!=self ||
       !self || mode!=data_0208715c || view || secondary)return false;
    if(get<std::uint32_t>(self,4)!=lifetime.uid || get<std::uint8_t>(self,0xf))return false;
    const auto target=get<std::uintptr_t>(self,0x110);
    if(!target || get<std::uintptr_t>(self,0x118) ||
       get<std::uint8_t>(reinterpret_cast<const void*>(target),0xf))return false;
    if(globally_suspended() ||
       (get<std::uint32_t>(self,0x154)&0xc018u))return false;
    const auto state=get<std::uintptr_t>(self,0x138);
    if(!normal_state(state))return false;
    // Both original records use their evidenced per-player stride 0x18.
    const auto index=std::size_t(data_020a0e40)*0x18;
    if(data_020a0e40>=4 || ((get<std::uint16_t>(data_0209f49c,index)|
                           get<std::uint16_t>(data_0209f49e,index))&0x4300u) ||
       get<std::uint16_t>(self,0x1a0))return false;
    const auto uid=get<std::uint32_t>(reinterpret_cast<const void*>(target),4);
    if(lifetime.target!=target || lifetime.target_uid!=uid || lifetime.state!=state) {
        disarm();lifetime.target=target;lifetime.target_uid=uid;lifetime.state=state;
    }
    return true;
}
bool distance(Vector eye,Vector target,std::int32_t& value) {
    const std::int64_t x=std::int64_t(eye.x)-target.x,y=std::int64_t(eye.y)-target.y,z=std::int64_t(eye.z)-target.z;
    for(auto component:{x,y,z})if(component<INT32_MIN || component>INT32_MAX)return false;
    const Vector delta{std::int32_t(x),std::int32_t(y),std::int32_t(z)};
    value=LenVec3(&delta);return value>0;
}
void sample_solved(char* self) {
    stats.yaw=get<std::int16_t>(self,0x17c);stats.pitch=get<std::int16_t>(self,0x17e);
    const auto eye=get<Vector>(self,0x8c),target=get<Vector>(self,0x80);
    std::int32_t radius=0;
    if(distance(eye,target,radius))stats.solved_radius=radius;
    stats.armed=orbit().state().armed;stats.desired_radius=orbit().state().radius;
}
}

namespace sm64ds {
void native_camera_reset() {
    invalidate();publication={};clock={};stats={};
}
void native_camera_publish(sm64ds::Stick stick,std::uint64_t tick,std::uint64_t vblank) {
    // Native HID normalization already bounds these values; invalid/NaN input
    // is rejected before conversion rather than cast to an integer.
    if(!(stick.x>=-1.f && stick.x<=1.f && stick.y>=-1.f && stick.y<=1.f)) {
        sm64ds_native_camera_publish_q15(32769,32769,tick,vblank);return;
    }
    const auto convert=[](float value) {
        return std::int32_t(value*32768.f+(value>=0.f ? .5f : -.5f));
    };
    sm64ds_native_camera_publish_q15(convert(stick.x),convert(stick.y),tick,vblank);
}
const NativeCameraStats& native_camera_stats() {return stats;}
}

extern "C" void sm64ds_native_camera_publish_q15(std::int32_t x,std::int32_t y,
    std::uint64_t tick,std::uint64_t vblank) {
    ++stats.publications;
    // First publication owns a tick; a duplicate never changes its duration or
    // stick sample. Stale publication disarms without rewinding the clock.
    if(publication.valid && tick<=publication.tick) {
        if(tick<publication.tick)disarm();return;
    }
    publication={{x,y},tick,vblank,tick!=0};
    // Behavior can omit its state/body during a pause or scripted view. Stamp
    // every actual input-ready tick here, so a later body does not integrate
    // elapsed VBlanks from those omitted calls. prepare reuses this stamp.
    if(!consume_clock() || globally_suspended())disarm();
}
extern "C" void sm64ds_native_camera_reset() {sm64ds::native_camera_reset();}
extern "C" const sm64ds::NativeCameraStats* sm64ds_native_camera_stats() {return &stats;}

extern "C" int sm64ds_native_camera_prepare(char* self,const void* mode,
    const void* view,int secondary,std::int32_t minimum,std::int32_t maximum) {
    const bool time_valid=consume_clock();
    if(!time_valid || !admitted(self,mode,view,secondary) || minimum<=0 || maximum<minimum) {
        disarm();++stats.blocked;return 0;
    }
    if(!orbit().state().armed && !publication.stick.x && !publication.stick.y) {
        candidate={};stats.status=std::uint32_t(Status::idle);return 0;
    }
    const auto target=get<Vector>(self,0x80),eye=get<Vector>(self,0x8c);
    std::int32_t radius=0;
    if(!distance(eye,target,radius)) {disarm();++stats.blocked;return 0;}
    if(radius<minimum)radius=minimum;else if(radius>maximum)radius=maximum;
    Tick input;
    input.identity={std::uint32_t(reinterpret_cast<std::uintptr_t>(self)),
        std::uint32_t(lifetime.target),std::uint32_t(reinterpret_cast<std::uintptr_t>(mode)),lifetime.generation};
    input.number=publication.tick;input.elapsed_us=clock.elapsed;input.stick=publication.stick;
    input.target=target;input.seed_yaw=Vec3_HorzAngle(&target,&eye);
    input.seed_pitch=Vec3_VertAngle(&target,&eye);input.seed_radius=radius;input.allowed=true;
    auto result=orbit().step(input,reinterpret_cast<const std::int16_t*>(data_02082214));
    stats.status=std::uint32_t(result.status);stats.armed=orbit().state().armed;
    stats.desired_radius=orbit().state().radius;
    if(result.status==Status::reseeded) {
        ++stats.reseeds;candidate={{},publication.tick,lifetime.generation,false,true,true};return 0;
    }
    if(result.status==Status::duplicate) {
        ++stats.duplicate_updates;
        if(candidate.tick!=publication.tick || candidate.generation!=lifetime.generation ||
           !orbit().state().armed)return 0;
        if(!candidate.accepted) {candidate.finish_pending=true;return 0;}
        result.yaw=std::int16_t(std::uint16_t(orbit().state().yaw_q16>>16));
        const auto pitch=orbit().state().pitch_q16;
        result.pitch=std::int16_t(pitch/65536-(pitch%65536<0 ? 1 : 0));
        result.radius=orbit().state().radius;result.has_candidate=true;
    }
    if(!result.has_candidate) {candidate={};return 0;}
    auto effective_radius=result.radius;
    if(effective_radius<minimum)effective_radius=minimum;
    else if(effective_radius>maximum)effective_radius=maximum;
    Vector position;
    if(!orbit_position(target,effective_radius,result.yaw,result.pitch,
        reinterpret_cast<const std::int16_t*>(data_02082214),trig_entries,position)) {
        disarm();++stats.blocked;return 0;
    }
    put(self,0x8c,position);put(self,0x17c,result.yaw);
    candidate={position,publication.tick,lifetime.generation,true,false,true};
    ++stats.manual_updates;return 1;
}
extern "C" int sm64ds_native_camera_height_range(char*,std::int32_t floor,std::int32_t ceiling) {
    if(floor>ceiling) {++stats.inverted_intervals;disarm();return 0;}
    return 1;
}
extern "C" void sm64ds_native_camera_finish(char* self,int manual) {
    if(lifetime.self!=self || !lifetime.ready)return;
    // A scripted/secondary/no-target branch can bypass prepare entirely.
    // Only a prepare in THIS dispatch authorizes a final solved-pose sample;
    // an accepted candidate from an earlier duplicate call is insufficient.
    if(!candidate.finish_pending) {disarm();return;}
    candidate.finish_pending=false;
    if(!manual) {
        if(candidate.reseed && candidate.tick==publication.tick) {
            if(!orbit().rebase(get<std::int16_t>(self,0x17c),get<std::int16_t>(self,0x17e)))disarm();
            candidate.reseed=false;
        } else if(!(candidate.tick==publication.tick && orbit().state().armed &&
                    stats.status==std::uint32_t(Status::duplicate))) {
            disarm();return;
        }
    } else if(candidate.accepted) {
        const auto solved=get<Vector>(self,0x8c);
        if(solved.x!=candidate.eye.x || solved.y!=candidate.eye.y || solved.z!=candidate.eye.z) {
            ++stats.constrained;
            if(!orbit().rebase(get<std::int16_t>(self,0x17c),get<std::int16_t>(self,0x17e)))disarm();
        }
    }
    sample_solved(self);
}

extern "C" std::int32_t _ZN6Camera13InitResourcesEv(void* self) {
    invalidate();
    const auto result=sm64ds_original_camera_init(self);
    if(result==1) {
        auto generation=stats.generation+1;
        if(!generation)generation=1;
        stats.generation=generation;stats.ready=1;
        lifetime={static_cast<char*>(self),get<std::uint32_t>(self,4),generation,0,0,0,true};
    }
    return result;
}
extern "C" std::int32_t _ZN6Camera16CleanupResourcesEv(void* self) {
    if(lifetime.self==self)invalidate();
    return sm64ds_original_camera_cleanup(self);
}
extern "C" void _ZN6Camera16OnPendingDestroyEv(void* self) {
    if(lifetime.self==self)invalidate();
    sm64ds_original_camera_pending_destroy(self);
}
static_assert(sizeof(sm64ds::NativeCameraStats)==112,"diagnostic stats ARM32 ABI");
