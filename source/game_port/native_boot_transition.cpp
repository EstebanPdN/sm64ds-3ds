#include "sm64ds/game_files.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/native_runtime.hpp"
#include <array>
#include <cstring>

extern "C" {
void LoadOverlay(int);
void UnloadOverlay(int);
void func_ov000_020aa420();
void _ZN5Sound16LoadInitialGroupEi(int);
void LoadFont(unsigned char);
extern std::uint16_t* data_0209d3b8;
extern int data_0209d3b0;
extern std::uint8_t data_020a6134[];
extern std::uint8_t data_020a621c[];
}

// func_0201a2f8's actual overlay entry is OV000's handle-table initializer.
// The recovered caller labels 0x020aa420 as func_ov001_020aa420, but both
// overlays occupy that address and OV001's body updates player cap icons.
// arm9/relocs.txt:2834 records modules(0,1); LoadOverlay(0) selects OV000.
// Keep the original OV000 initializer, not the source's ambiguous label.
extern "C" void func_0201a2f8() {
    auto* service=sm64ds::bound_native_game_files();
    if(!service || !data_0209d3b8)
        sm64ds_native_missing("Boot transition requires the verified native card catalog");
    if(sm64ds::native_platform_boot_indicator()==2)
        sm64ds_native_missing("DS download-play boot is unavailable in the native application");

    // func_0205d94c caches card FAT/FNT bytes temporarily to accelerate this
    // very loop. Native card lookups already use AssetPack's validated in-RAM
    // directory. There is no DS FSArchive cache buffer to allocate, activate,
    // invalidate with func_0205c91c or free. This removes that I/O optimization,
    // while retaining all 2058 original path resolutions and handle writes.
    std::array<std::uint16_t,0x80a> expected{};
    std::memcpy(expected.data(),data_0209d3b8,sizeof(expected));
    LoadOverlay(0);
    func_ov000_020aa420();
    if(!sm64ds::native_platform_ok())
        sm64ds_native_missing("Native platform failed during original OV000 initialization");
    if(!data_0209d3b8 || data_0209d3b0!=static_cast<int>(expected.size()) ||
       std::memcmp(expected.data(),data_0209d3b8,sizeof(expected)))
        sm64ds_native_missing("Original OV000 handle table differs from the verified ROM catalog");
    // func_02018a58 replaces the pointer with an ORIGINAL-heap allocation.
    // It does not deallocate the previous pointer; the preparatory catalog
    // vector remains owned by native_runtime, never passed to Memory::Free.
    UnloadOverlay(0);
    LoadOverlay(1);
    // This original forwarding function sets bank 0x20 through the explicitly
    // deferred native audio service. It does not claim that playback started.
    _ZN5Sound16LoadInitialGroupEi(0);
    LoadFont(0);
}

// The original handle-table initializer temporarily lowers its own thread's
// priority to 31. The native loop has exactly one cooperative context. Keep
// its real SDK record and priority, but do not enter the DS CPU context-switch
// machinery merely to select the same sole runnable context again.
extern "C" int func_02057e84(void* target,int priority) {
    void* current=nullptr;
    void* head=nullptr;
    void* next=nullptr;
    std::memcpy(&current,data_020a6134+8,4);
    std::memcpy(&head,data_020a6134+12,4);
    if(target!=data_020a621c || target!=current || target!=head)
        sm64ds_native_missing("Priority change requires an unsupported additional game context");
    std::memcpy(&next,static_cast<std::uint8_t*>(target)+0x68,4);
    if(next)
        sm64ds_native_missing("Priority change requires an unsupported additional game context");
    if(priority<0 || priority>31)return 0;
    const auto previous=sm64ds::native_platform_irq_disable();
    std::memcpy(static_cast<std::uint8_t*>(target)+0x70,&priority,4);
    sm64ds::native_platform_irq_restore(previous);
    return 1;
}
