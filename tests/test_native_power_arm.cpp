// The production platform body is compiled unchanged in this TU. The only
// white-box helpers copy its state for the IRQ mutation contract; no substitute
// IRQ implementation or DSP/ARM7 reply is provided.
#include "../source/game_port/native_platform.cpp"
#include "sm64ds/native_video.hpp"
#include <cstddef>
#include <cstring>

extern "C" {
std::uint32_t data_020a8110{},data_020a8114[4]{},data_020a7fc8[32]{};
[[noreturn]] void sm64ds_native_missing(const char*) {for(;;){}}
[[noreturn]] void test_power_unexpected(const char*) {for(;;){}}
u64 svcGetSystemTick(void) {return 0;} // Explicit CPU initialization-clock input.
void gspSetEventCallback(GSPGPU_Event,ThreadFunc,void*,bool) {
    test_power_unexpected("GSP callback registration must be observed by CPU fixture");
}
unsigned test_power_state_size() {return sizeof(sm64ds::g);}
unsigned test_power_mask_offset() {
    return offsetof(sm64ds::Platform,stats)+offsetof(sm64ds::NativePlatformStats,enabled_irqs);
}
void test_power_state_copy(void* out) {std::memcpy(out,&sm64ds::g,sizeof(sm64ds::g));}
void test_power_setup(unsigned mask,unsigned pending,unsigned cpsr,unsigned master) {
    sm64ds::native_platform_init();
    sm64ds::native_platform_irq_enable_mask(mask);
    sm64ds::native_platform_irq_raise(pending);
    sm64ds::native_platform_irq_restore_all(cpsr);
    sm64ds::native_platform_irq_set_master(master!=0);
}
void test_power_state_info(unsigned* out) {
    out[0]=sm64ds::g.stats.enabled_irqs;out[1]=sm64ds::g.stats.pending_irqs;
    out[2]=sm64ds::g.cpsr_mask;out[3]=sm64ds::g.master;
    out[4]=sm64ds::g.stats.okay;out[5]=unsigned(sm64ds::g.stats.vblanks);
}
}
namespace sm64ds {
bool native_video_gx_status(std::uint32_t&) {test_power_unexpected("GX status");}
}
extern "C" void sm64ds_draw_fifo(std::uint32_t) {test_power_unexpected("GX FIFO");}
extern "C" void sm64ds_draw_port(std::uint32_t,std::uint32_t) {test_power_unexpected("GX port");}
extern "C" bool sm64ds_draw_ok() {test_power_unexpected("GX status");}
