#include "sm64ds/native_alarm.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/native_runtime.hpp"
#include <array>
#include <cstring>

// Native monotonic timer service. Game watchdog callbacks retain their own
// original elapsed-time checks. Polling once per presentation replaces the
// DS timer interrupt; it never invents completion or runs a callback early.
namespace {
using Callback=void (*)(void*);
struct Alarm {void* object{};std::uint64_t due{};Callback callback{};void* argument{};};
std::array<Alarm,16> alarms;
void clear_record(void* object) {
    const std::uint32_t zero=0;
    for(unsigned offset:{0u,0x1cu,0x20u})
        std::memcpy(static_cast<std::uint8_t*>(object)+offset,&zero,4);
}
void cancel(void* object) {
    if(!object)sm64ds_native_missing("Native alarm has no original record");
    for(auto& alarm:alarms)if(alarm.object==object)alarm={};
    clear_record(object);
}
void schedule(void* object,std::uint64_t delay,Callback callback,void* argument) {
    if(!object || !callback)sm64ds_native_missing("Invalid original alarm request");
    std::uint32_t occupied=0;std::memcpy(&occupied,object,4);
    if(occupied)sm64ds_native_missing("Original alarm is already scheduled");
    for(auto& alarm:alarms)if(!alarm.object) {
        const auto due=sm64ds::native_platform_ticks()+delay;
        alarm={object,due,callback,argument};
        clear_record(object);
        std::memcpy(object,&callback,4);
        std::memcpy(static_cast<std::uint8_t*>(object)+4,&argument,4);
        // Original SDK deadline starts at byte 0x0c, not GCC's s64 alignment.
        std::memcpy(static_cast<std::uint8_t*>(object)+0xc,&due,8);
        return;
    }
    sm64ds_native_missing("Native alarm capacity exhausted");
}
}
namespace sm64ds {
void native_alarm_poll() {
    const auto now=native_platform_ticks();
    // Snapshot expired work. A callback may safely reschedule its own record;
    // a newly scheduled callback waits for the next presentation boundary.
    std::array<Alarm,16> ready{};unsigned count=0;
    for(auto& alarm:alarms)if(alarm.object && alarm.due<=now) {
        ready[count++]=alarm;clear_record(alarm.object);alarm={};
    }
    for(unsigned i=0;i<count;++i)ready[i].callback(ready[i].argument);
}
}
extern "C" {
extern std::uint16_t data_020a6440;
void func_02059950(void* object) {cancel(object);}
void func_0201aaa4(void* object) {cancel(object);}
void func_020599e8(void* object,long long delay,Callback callback,void* argument) {
    schedule(object,std::uint64_t(delay),callback,argument);
}
int func_0201aa50(void* object,std::uint32_t lo,std::uint32_t hi,Callback callback,void* argument) {
    if(!data_020a6440)return 0;
    schedule(object,std::uint64_t(lo)|(std::uint64_t(hi)<<32),callback,argument);
    return 1;
}
}
