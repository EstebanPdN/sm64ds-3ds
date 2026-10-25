#include "sm64ds/native_runtime.hpp"
#include "sm64ds/native_platform.hpp"
#include <cstdint>

// The original boot worker is observed through Start/Join, not its stack.
// Native file I/O is synchronous. Defer its actual callback until the game's
// first Join poll, preserving the pending return that causes BootScene to load
// its message bank after the worker. Never signal completion without running it.
namespace {
using Work = void (*)(int);
Work pending=nullptr;
int argument=0;
bool running=false;
}
extern "C" {
extern std::uint8_t data_0209d4f8;
extern std::uintptr_t data_0209d518;

int func_0201a244(Work function,int value,int priority,void*,unsigned stack_size) {
    if(!function || priority<0 || priority>31 || !stack_size)
        sm64ds_native_missing("Invalid native original worker request");
    if(data_0209d4f8) {function(value);return 1;}
    if(pending || running)sm64ds_native_missing("Native worker ownership mismatch");
    data_0209d4f8=1;data_0209d518=0;
    pending=function;argument=value;
    return 0;
}
int func_0201a1bc() {
    if(!data_0209d4f8)return 1;
    if(!pending || running)sm64ds_native_missing("Native worker join without pending callback");
    const auto function=pending;pending=nullptr;running=true;
    function(argument);
    running=false;
    if(!sm64ds::native_platform_ok() || sm64ds::native_platform_is_waiting())
        sm64ds_native_missing("Original worker requires an additional asynchronous service");
    data_0209d4f8=0;
    return 1;
}
}
