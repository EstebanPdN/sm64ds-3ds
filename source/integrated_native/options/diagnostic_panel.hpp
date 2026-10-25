#pragma once
#include <cstdint>
namespace enhance {
struct DiagnosticPanel {
    bool visible=false;
    static constexpr uint32_t select=4;
    void update(bool enabled,uint32_t down){
        if(!enabled)visible=false;
        else if(down&select)visible=!visible;
    }
    // Telemetry never owns B, the D-pad, the shoulder buttons or movement.
    static uint32_t game_keys(bool enabled,uint32_t held){return enabled?held&~select:held;}
};
}
