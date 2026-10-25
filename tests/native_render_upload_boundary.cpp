// Isolated CPU-test boundaries. A reached IRQ, bank service or GX stack query
// fails explicitly; none is implemented as a successful rendering operation.
#include "sm64ds/native_platform.hpp"
#include "ntr/gx.h"
#include <cstdint>
extern "C" [[noreturn]] void sm64ds_native_missing(const char*);
extern "C" {
std::uint16_t data_020a6088[13]{},data_02099fcc{},data_020a6084{};
std::uint32_t data_020a60a4{},data_020a60a8{};
std::uint8_t data_0208ee00{};
void Vram__Map(std::uint16_t) {sm64ds_native_missing("Render upload unexpectedly remapped bank");}
std::uint16_t DisableVramBanks(std::uint16_t*) {sm64ds_native_missing("Render upload unexpectedly disabled bank");}
void func_020190b8() {sm64ds_native_missing("Render upload unexpectedly reset video services");}
}
namespace sm64ds {
void native_platform_fail(const char* message) {sm64ds_native_missing(message);}
void native_platform_irq_set_master(bool) {sm64ds_native_missing("Render upload unexpectedly changed IRQ state");}
void native_platform_irq_raise(std::uint32_t) {sm64ds_native_missing("Render upload unexpectedly raised IRQ");}
}
#ifdef SM64DS_RENDER_CPU_ORACLE
namespace ntr {
bool gx_get_stack_state(GxStackState&) {sm64ds_native_missing("Render upload unexpectedly queried GX stack");}
int gx_command_parameter_count(std::uint8_t) {sm64ds_native_missing("Render upload unexpectedly parsed pending stack");}
}
#endif

