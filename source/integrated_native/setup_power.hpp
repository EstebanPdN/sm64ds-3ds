#pragma once

namespace setup_power {
// Stack-owned for setup only. Service failures never stop file extraction;
// destruction balances only references and backlight changes we actually own.
template<class Service> class BasicGuard {
    Service service_{};
    bool prior_sleep_, shell_service_=false, lcd_service_=false, led_service_=false;
    bool display_allowed_=true, darkened_=false, ready_=false, led_on_=false;
public:
    BasicGuard():prior_sleep_(service_.sleep_allowed()) {
        service_.allow_sleep(false);
        shell_service_=service_.open_shell();
        lcd_service_=service_.open_lcd();
        refresh();
    }
    BasicGuard(const BasicGuard&)=delete;
    BasicGuard& operator=(const BasicGuard&)=delete;
    ~BasicGuard() {
        if(led_on_)service_.notification(false);
        if(led_service_)service_.close_led();
        if(darkened_)service_.backlights(true);
        if(lcd_service_)service_.close_lcd();
        if(shell_service_)service_.close_shell();
        service_.allow_sleep(prior_sleep_);
    }
    bool refresh() {
        bool open=true;
        if(shell_service_&&service_.shell_open(open)){
            display_allowed_=open;
            if(lcd_service_){
                if(!open&&!darkened_)darkened_=service_.backlights(false);
                else if(open&&darkened_&&service_.backlights(true))darkened_=false;
            }
            if(open&&led_on_&&service_.notification(false))led_on_=false;
            if(!open&&ready_&&led_service_&&!led_on_)led_on_=service_.notification(true);
        }
        return display_allowed_;
    }
    void completed() {
        ready_=true;
        if(!led_service_)led_service_=service_.open_led();
        refresh();
    }
};
} // namespace setup_power

#ifdef __3DS__
#include <3ds.h>
namespace setup_power {
struct CtrService {
    bool sleep_allowed()const{return aptIsSleepAllowed();}
    void allow_sleep(bool value)const{aptSetSleepAllowed(value);}
    bool open_shell()const{return R_SUCCEEDED(ptmuInit());}
    void close_shell()const{ptmuExit();}
    bool open_lcd()const{return R_SUCCEEDED(gspLcdInit());}
    void close_lcd()const{gspLcdExit();}
    bool shell_open(bool& open)const{u8 raw=1;const Result result=PTMU_GetShellState(&raw);open=raw!=0;return R_SUCCEEDED(result);}
    bool backlights(bool on)const{return R_SUCCEEDED(on?GSPLCD_PowerOnAllBacklights():GSPLCD_PowerOffAllBacklights());}
    bool open_led()const{return R_SUCCEEDED(mcuHwcInit());}
    void close_led()const{mcuHwcExit();}
    bool notification(bool on)const {
        InfoLedPattern pattern{};
        pattern.delay=1;pattern.loopDelay=on?0:0xff;
        // 16 bright and 16 dark steps at 1/16 s give a calm 2-second blink.
        if(on)for(unsigned i=0;i<16;++i)pattern.greenPattern[i]=0xff;
        return R_SUCCEEDED(MCUHWC_SetInfoLedPattern(&pattern));
    }
};
using Guard=BasicGuard<CtrService>;
// Root setup owns the pointed-to guard, never a graphics/session object.
inline Guard* active_guard=nullptr;
inline bool display_allowed(){return !active_guard||active_guard->refresh();}
class Scope {
    Guard guard_;
    Guard* prior_=active_guard;
public:
    Scope(){active_guard=&guard_;}
    ~Scope(){active_guard=prior_;}
    // Called only after all files and audio are ready. Keep the light notice
    // owned until reopening, so no detached thread or lingering LED is needed.
    bool completed() {
        guard_.completed();
        while(!guard_.refresh()){
            if(!aptMainLoop())return false;
            svcSleepThread(50000000);
        }
        return !aptShouldClose();
    }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
} // namespace setup_power
#endif
