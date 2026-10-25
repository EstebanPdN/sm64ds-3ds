#include "setup_power.hpp"
#include "loading_locale.hpp"
#include <cassert>
#include <initializer_list>
#include <cstring>
#include <cstdio>
#include <stdexcept>

struct Model {
    bool sleep=true,shell=true,shell_ok=true,lcd_ok=true,led_ok=true,read_ok=true,write_ok=true;
    int shell_refs=0,lcd_refs=0,led_refs=0,off=0,on=0,notice_on=0,notice_off=0,sleep_writes=0;
};
static Model model;
struct Service {
    bool sleep_allowed(){return model.sleep;}
    void allow_sleep(bool value){model.sleep=value;++model.sleep_writes;}
    bool open_shell(){if(!model.shell_ok)return false;++model.shell_refs;return true;}
    void close_shell(){--model.shell_refs;}
    bool open_lcd(){if(!model.lcd_ok)return false;++model.lcd_refs;return true;}
    void close_lcd(){--model.lcd_refs;}
    bool shell_open(bool& value){value=model.shell;return model.read_ok;}
    bool backlights(bool on){if(!model.write_ok)return false;if(on)++model.on;else ++model.off;return true;}
    bool open_led(){if(!model.led_ok)return false;++model.led_refs;return true;}
    void close_led(){--model.led_refs;}
    bool notification(bool on){if(!model.write_ok)return false;if(on)++model.notice_on;else ++model.notice_off;return true;}
};
using Guard=setup_power::BasicGuard<Service>;
static void closed(){assert(model.shell_refs==0&&model.lcd_refs==0&&model.led_refs==0);}
int main(){
    for(bool prior:{false,true}){
        model={};model.sleep=prior;
        {Guard guard;assert(!model.sleep);for(int i=0;i<5000;++i)assert(guard.refresh());
            assert(model.shell_refs==1&&model.lcd_refs==1&&model.led_refs==0);
            model.shell=false;assert(!guard.refresh());assert(model.off==1);
            for(int i=0;i<5000;++i)assert(!guard.refresh());assert(model.off==1);
            guard.completed();assert(model.notice_on==1&&model.led_refs==1);
            guard.refresh();assert(model.notice_on==1);
            model.shell=true;assert(guard.refresh());assert(model.on==1&&model.notice_off==1);
        }closed();assert(model.sleep==prior&&model.sleep_writes==2);
    }
    model={};{Guard guard;guard.completed();assert(model.notice_on==0);}closed();
    model={};model.shell=false;{Guard guard;assert(model.off==1);}closed();assert(model.on==1&&model.notice_on==0);
    model={};model.shell=false;{Guard guard;guard.completed();}closed();assert(model.notice_off==1);
    model={};try{Guard guard;throw std::runtime_error("cancel or preparation failure");}catch(const std::runtime_error&){}closed();assert(model.sleep);
    for(unsigned mask=0;mask<8;++mask){model={};model.shell_ok=mask&1;model.lcd_ok=mask&2;model.led_ok=mask&4;
        {Guard guard;model.shell=false;guard.refresh();guard.completed();model.shell=true;guard.refresh();}closed();assert(model.sleep);}
    model={};{Guard guard;model.read_ok=false;model.shell=false;assert(guard.refresh());model.read_ok=true;
        model.write_ok=false;assert(!guard.refresh());assert(model.off==0);model.write_ok=true;guard.refresh();assert(model.off==1);
        guard.completed();model.shell=true;model.write_ok=false;guard.refresh();assert(model.notice_off==0);
        model.write_ok=true;guard.refresh();assert(model.notice_off==1&&model.on==1);}closed();
    using namespace loading_screen;
    assert(!std::strcmp(translated(Language::English,Label::LongWait),"This will take forever."));
    for(unsigned i=0;i<language_count;++i){assert(*translated(Language(i),Label::LongWait));assert(*translated(Language(i),Label::AlmostThere));}
    std::puts("{\"status\":\"passed\",\"sleep_policies\":2,\"shell_refreshes\":20000,\"service_failure_combinations\":8,\"locales\":6,\"scope\":\"Mock services exercise ownership, shell display suppression, completion notice and exact prior sleep restoration; physical lid and LED unverified\"}");
}
