// Compile/link only in an explicitly selected diagnostic build. Normal builds
// never wrap the hot generic I/O path. The original operation always executes;
// the existing graphics guard remains responsible for refusal and word counts.
#ifdef SM64DS_FRESH_NATIVE_WRITER_PROBE
#include "game_runtime.h"
extern "C" void __real__ZN3ntr8io_writeEmyj(uint32_t,uint64_t,unsigned);
extern "C" void __wrap__ZN3ntr8io_writeEmyj(uint32_t address,uint64_t value,unsigned width){
    const bool graphics=(address<0x04000070u&&address>=0x04000000u)||
        (address>=0x04000320u&&address<0x04000700u)||(address>=0x04001000u&&address<0x04001070u);
    if(graphics)sm64ds_fresh_native_writer_probe(4,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),address,width);
    __real__ZN3ntr8io_writeEmyj(address,value,width);
}
#endif
