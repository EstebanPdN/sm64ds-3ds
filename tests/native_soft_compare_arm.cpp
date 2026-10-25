#include <cstdint>

// Native fixture storage models the actual original BSS status word with
// surrounding canaries. The compiled original getter returns this alias.
extern "C" {
alignas(16) std::uint32_t soft_fp_storage[9] = {
    0xa5a5a5a5u,0xa5a5a5a5u,0xa5a5a5a5u,0xa5a5a5a5u,
    0,
    0xa5a5a5a5u,0xa5a5a5a5u,0xa5a5a5a5u,0xa5a5a5a5u
};
}
asm(".global data_020aa3f4\n.set data_020aa3f4, soft_fp_storage+16\n");

// These are non-returning observation boundaries, never successful substitutes
// for the original software FP dispatcher. Unicorn stops at their entry and
// compares the actual arguments/status against the ROM's same dispatch edge.
extern "C" [[noreturn]] std::uint32_t func_0206dd30(std::uint32_t,
    std::uint32_t,std::uint32_t) {
    asm volatile("udf #0");
    __builtin_unreachable();
}
extern "C" [[noreturn]] std::uint32_t func_0206ddcc(std::uint32_t,std::uint32_t) {
    asm volatile("udf #0");
    __builtin_unreachable();
}
