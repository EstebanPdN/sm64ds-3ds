// Failure boundary for the isolated CPU and host test only. Production uses
// its existing native platform. A matrix submission must not query GXSTAT or
// perform IRQ operations, and these paths explicitly fail if reached.
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_platform.hpp"
extern "C" [[noreturn]] void sm64ds_native_missing(const char*);
namespace sm64ds {
void native_platform_fail(const char* message) {sm64ds_native_missing(message);}
void native_platform_irq_set_master(bool) {
    sm64ds_native_missing("Matrix submission unexpectedly changed IRQ state");
}
bool native_video_gx_status(std::uint32_t&) {
    sm64ds_native_missing("Matrix submission unexpectedly queried GXSTAT");
}
}
