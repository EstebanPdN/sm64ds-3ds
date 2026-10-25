#include "sm64ds/camera_orbit.hpp"
#include <limits>

namespace sm64ds::camera {
namespace {
constexpr std::int64_t angle_fraction=65536;
constexpr std::uint32_t substep_us=1000;

bool same_identity(const Identity& a,const Identity& b) {
    return a.camera==b.camera && a.target==b.target && a.mode==b.mode &&
           a.generation==b.generation;
}
bool valid_identity(const Identity& identity) {
    return identity.camera && identity.target && identity.mode && identity.generation;
}
bool valid_config(const Config& config) {
    return config.yaw_rate && config.pitch_rate && config.response_us<=1000000 &&
           config.max_elapsed_us>=substep_us && config.max_elapsed_us<=250000 &&
           config.pitch_limit>0 && config.pitch_limit<=0x3f00;
}
std::int64_t floor_div(std::int64_t value,std::int64_t divisor) {
    const auto quotient=value/divisor;
    return quotient-(value%divisor<0 ? 1 : 0);
}
std::int32_t multiply_fx12(std::int32_t a,std::int32_t b) {
    // All callers bound b to [-4096,4096]; the product and +0x800 fit s64.
    return static_cast<std::int32_t>(floor_div(std::int64_t(a)*b+0x800,4096));
}
std::int16_t signed_angle(std::uint32_t value) {
    const auto bits=static_cast<std::uint16_t>(value);
    return static_cast<std::int16_t>(bits<0x8000 ? std::int32_t(bits) : std::int32_t(bits)-65536);
}
std::int16_t pitch_angle(std::int32_t value) {
    return static_cast<std::int16_t>(floor_div(value,angle_fraction));
}
bool valid_pitch(std::int16_t pitch,std::int32_t limit) {
    return std::int32_t(pitch)>=-limit && std::int32_t(pitch)<=limit;
}
std::int64_t approach_velocity(std::int64_t value,std::int64_t desired,
                               std::uint32_t response) {
    if(!response)return desired;
    const auto difference=desired-value;
    auto delta=difference*substep_us/(std::int64_t(response)+substep_us);
    // Finish quantized convergence rather than retaining a one-bit velocity.
    if(!delta && difference)delta=difference>0 ? 1 : -1;
    return value+delta;
}
bool add_component(std::int32_t a,std::int32_t b,std::int32_t& out) {
    const auto sum=std::int64_t(a)+b;
    if(sum<std::numeric_limits<std::int32_t>::min() ||
       sum>std::numeric_limits<std::int32_t>::max())return false;
    out=static_cast<std::int32_t>(sum);
    return true;
}
}

bool orbit_position(Vector target,std::int32_t radius,
                    std::int16_t yaw,std::int16_t pitch,
                    const std::int16_t* lut,std::size_t entries,Vector& out) {
    if(!lut || entries!=trig_entries || radius<=0)return false;
    const auto yi=(std::uint16_t(yaw)>>4)*2;
    const auto pi=(std::uint16_t(pitch)>>4)*2;
    const std::int32_t sy=lut[yi],cy=lut[yi+1],sp=lut[pi],cp=lut[pi+1];
    const std::int32_t selected[]={sy,cy,sp,cp};
    for(auto value:selected)if(value<-4096 || value>4096)return false;
    const auto horizontal=multiply_fx12(radius,cp);
    const Vector delta{multiply_fx12(horizontal,sy),multiply_fx12(radius,-sp),
                       multiply_fx12(horizontal,cy)};
    Vector candidate;
    if(!add_component(target.x,delta.x,candidate.x) ||
       !add_component(target.y,delta.y,candidate.y) ||
       !add_component(target.z,delta.z,candidate.z))return false;
    out=candidate;
    return true;
}

void Orbit::reset() {
    state_={};yaw_remainder_=pitch_remainder_=0;neutral_us_=0;
}
void Orbit::disarm() {
    state_.armed=false;state_.yaw_velocity=state_.pitch_velocity=0;
    state_.substep_us=0;yaw_remainder_=pitch_remainder_=0;neutral_us_=0;
}
bool Orbit::rebase(std::int16_t yaw,std::int16_t pitch) {
    if(!state_.armed || !valid_pitch(pitch,config_.pitch_limit))return false;
    state_.yaw_q16=std::uint32_t(std::uint16_t(yaw))*65536u;
    state_.pitch_q16=static_cast<std::int32_t>(std::int64_t(pitch)*angle_fraction);
    state_.yaw_velocity=state_.pitch_velocity=0;
    state_.substep_us=0;yaw_remainder_=pitch_remainder_=0;neutral_us_=0;
    return true;
}

Result Orbit::step(const Tick& tick,const std::int16_t* lut,std::size_t entries) {
    Result result;
    // A changed lifetime must invalidate even if a second dispatch occurs in
    // the same tick. Keep the global tick stamp to prevent double integration.
    if(!same_identity(tick.identity,state_.identity)) {
        disarm();state_.identity=tick.identity;
    }
    if(tick.paused || !tick.allowed)disarm();
    if(state_.has_tick && tick.number<=state_.last_tick) {
        result.status=tick.number==state_.last_tick ? Status::duplicate : Status::stale;
        return result;
    }
    state_.has_tick=true;state_.last_tick=tick.number;
    if(!valid_identity(tick.identity)) {
        disarm();result.status=Status::invalid_identity;return result;
    }
    if(tick.paused || !tick.allowed) {
        disarm();result.status=Status::suspended;return result;
    }
    if(!valid_config(config_)) {
        disarm();result.status=Status::invalid_config;return result;
    }
    if(tick.stick.x<-stick_one || tick.stick.x>stick_one ||
       tick.stick.y<-stick_one || tick.stick.y>stick_one) {
        disarm();result.status=Status::invalid_input;return result;
    }
    if(!tick.elapsed_us || tick.elapsed_us>config_.max_elapsed_us) {
        disarm();result.status=Status::invalid_time;return result;
    }
    if(!state_.armed) {
        if(!tick.stick.x && !tick.stick.y) {
            result.status=Status::idle;return result;
        }
        if(tick.seed_radius<=0 || !valid_pitch(tick.seed_pitch,config_.pitch_limit)) {
            result.status=Status::invalid_seed;return result;
        }
        Vector check;
        if(!orbit_position(tick.target,tick.seed_radius,tick.seed_yaw,tick.seed_pitch,
                           lut,entries,check)) {
            result.status=Status::invalid_geometry;return result;
        }
        state_.armed=true;state_.radius=tick.seed_radius;
        rebase(tick.seed_yaw,tick.seed_pitch);
        // No duration predating camera ownership is integrated on acquisition.
        result.status=Status::reseeded;return result;
    }

    // Work on a copy so geometry overflow/bad LUT cannot partly advance pose.
    Orbit next=*this;
    const auto desired_yaw=std::int64_t(tick.stick.x)*config_.yaw_rate/stick_one;
    const auto desired_pitch=-std::int64_t(tick.stick.y)*config_.pitch_rate/stick_one;
    const auto elapsed=next.state_.substep_us+tick.elapsed_us;
    next.state_.substep_us=elapsed%substep_us;
    const auto limit=std::int64_t(config_.pitch_limit)*angle_fraction;
    for(std::uint32_t i=0;i<elapsed/substep_us;++i) {
        next.state_.yaw_velocity=approach_velocity(next.state_.yaw_velocity,desired_yaw,config_.response_us);
        next.state_.pitch_velocity=approach_velocity(next.state_.pitch_velocity,desired_pitch,config_.response_us);
        if(!tick.stick.x && !tick.stick.y) {
            if(next.neutral_us_<6*config_.response_us)next.neutral_us_+=substep_us;
            if(next.neutral_us_>=6*config_.response_us) {
                next.state_.yaw_velocity=next.state_.pitch_velocity=0;
                next.yaw_remainder_=next.pitch_remainder_=0;
            }
        } else next.neutral_us_=0;
        auto numerator=next.state_.yaw_velocity*substep_us+next.yaw_remainder_;
        const auto yaw_delta=numerator/1000000;
        next.yaw_remainder_=numerator%1000000;
        next.state_.yaw_q16+=static_cast<std::uint32_t>(yaw_delta);
        numerator=next.state_.pitch_velocity*substep_us+next.pitch_remainder_;
        const auto pitch_delta=numerator/1000000;
        next.pitch_remainder_=numerator%1000000;
        auto pitch=std::int64_t(next.state_.pitch_q16)+pitch_delta;
        if(pitch>limit || pitch<-limit) {
            pitch=pitch>limit ? limit : -limit;
            next.state_.pitch_velocity=0;next.pitch_remainder_=0;
        }
        next.state_.pitch_q16=static_cast<std::int32_t>(pitch);
    }
    result.yaw=signed_angle(next.state_.yaw_q16>>16);
    result.pitch=pitch_angle(next.state_.pitch_q16);
    result.radius=next.state_.radius;
    if(!orbit_position(tick.target,result.radius,result.yaw,result.pitch,lut,entries,result.eye)) {
        result={};result.status=Status::invalid_geometry;
        disarm();return result;
    }
    *this=next;result.has_candidate=true;result.status=Status::candidate;
    return result;
}
}
