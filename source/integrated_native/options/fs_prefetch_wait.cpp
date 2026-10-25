// Keep libctru's typedefs out of the original FS ABI translation unit.
#include <3ds.h>
#include <atomic>
namespace {
LightEvent changed;
std::atomic<bool> ready{false};
}
extern "C" void sm64ds_prefetch_wait_init(){
    LightEvent_Init(&changed,RESET_STICKY);ready.store(true,std::memory_order_release);
}
extern "C" void sm64ds_prefetch_notify(){
    if(ready.load(std::memory_order_acquire))LightEvent_Signal(&changed);
}
extern "C" void sm64ds_prefetch_wait_begin(){LightEvent_Clear(&changed);}
extern "C" void sm64ds_prefetch_wait(){LightEvent_Wait(&changed);}
// Star select level preload (hal/fs.cpp): the game thread reads and frees the
// level cache, the prefetch thread fills it.
namespace {
LightLock level_lock;
struct LevelLockInit { LevelLockInit(){LightLock_Init(&level_lock);} } level_lock_init;
}
extern "C" void sm64ds_level_lock(){LightLock_Lock(&level_lock);}
extern "C" void sm64ds_level_unlock(){LightLock_Unlock(&level_lock);}
