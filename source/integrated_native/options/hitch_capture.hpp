#pragma once
#include <cstdint>

namespace profile_probe {
// Diagnostic-only, game-thread ownership. At most two automatic captures per
// process; ignore settling, menus and modal control. Capture I/O explicitly
// resets the interval, so a long real gameplay stall remains observable.
class HitchCapture {
    uint64_t previous_=0,last_capture_=0;
    unsigned stable_=0,slow_=0,captures_=0,target_fps_=60;
public:
    static constexpr uint64_t hz=268111856;
    void reset_interval(){previous_=0;stable_=slow_=0;}
    unsigned captures()const{return captures_;}
    static constexpr unsigned threshold_ms(unsigned target_fps){return target_fps==30?50:25;}
    bool observe(uint64_t now,bool controllable,unsigned target_fps=60){
        target_fps=target_fps==30?30:60;
        if(target_fps_!=target_fps){target_fps_=target_fps;reset_interval();}
        if(!controllable){reset_interval();return false;}
        if(!previous_ || now<=previous_){previous_=now;return false;}
        const uint64_t elapsed=now-previous_;previous_=now;
        if(stable_<120){++stable_;return false;}
        const uint64_t threshold=hz*threshold_ms(target_fps_)/1000;
        slow_=elapsed>threshold?slow_+1:0;
        if(captures_>=2 || (last_capture_ && now-last_capture_<10*hz))return false;
        if(slow_<3 && elapsed<=2*threshold)return false;
        ++captures_;last_capture_=now;slow_=0;return true;
    }
};
}
