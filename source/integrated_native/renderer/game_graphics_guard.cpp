#ifdef SM64DS_FRESH_NATIVE_GAME
#include "game_runtime.h"
#include <cstring>
// These original platform entrypoints are graphics writers. Native mode counts
// and refuses their frames without interpreting the command words. CPU game
// arithmetic/animation stays in its original functions; missing graphics
// producers remain explicit migration failures.
extern "C" void __real__ZN3ntr13gx_write_fifoEm(uint32_t);
extern "C" void __real__ZN3ntr13gx_write_portEmm(uint32_t,uint32_t);
extern "C" void __real__ZN3ntr19gx_write_fifo_blockEPKmi(const uint32_t*,int);
extern "C" void __wrap__ZN3ntr13gx_write_fifoEm(uint32_t word){
    if(sm64ds_fresh_native_enabled()){sm64ds_fresh_native_legacy_write(1,1,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),0);return;}__real__ZN3ntr13gx_write_fifoEm(word);
}
extern "C" void __wrap__ZN3ntr13gx_write_portEmm(uint32_t address,uint32_t word){
    if(sm64ds_fresh_native_enabled()){sm64ds_fresh_native_legacy_write(1,2,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),address);return;}__real__ZN3ntr13gx_write_portEmm(address,word);
}
extern "C" void __wrap__ZN3ntr19gx_write_fifo_blockEPKmi(const uint32_t* words,int count){
    if(sm64ds_fresh_native_enabled()){sm64ds_fresh_native_legacy_write(count>0?uint32_t(count):1,3,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),0);return;}__real__ZN3ntr19gx_write_fifo_blockEPKmi(words,count);
}
extern "C" void __real__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(int,int,int,int,int,int,bool,void*);
extern "C" void __wrap__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(int sine,int cosine,int aspect,int near_z,int far_z,int scale,bool install,void* destination){
    if(!sm64ds_fresh_native_enabled()){
        __real__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(sine,cosine,aspect,near_z,far_z,scale,install,destination);return;
    }
    int32_t matrix[16];
    // Original CPU fixed math, flag=false: no GX writes. Its legacy type name
    // says Matrix4x3, but authoritative body writes16 words; preserve all64 bytes.
    __real__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(sine,cosine,aspect,near_z,far_z,scale,false,matrix);
    if(destination)std::memcpy(destination,matrix,sizeof(matrix));
    if(install)sm64ds_fresh_native_projection(matrix);
}
extern "C" void __real__ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(const void*,const void*,const void*,bool,void*);
extern "C" void __real_func_02055624();
extern "C" void __real_func_020557b4();
extern "C" void __real__ZN3G3X13SetClearColorEtiiib(uint16_t,int,int,int,bool);
extern "C" void __wrap_func_02055624(){
    if(sm64ds_fresh_native_enabled()){sm64ds_fresh_native_reset_graphics();return;}__real_func_02055624();
}
extern "C" void __wrap_func_020557b4(){
    // Original routine waits/resets DS stacks and installs default material
    // bindings. Native passes own GPU synchronization and each native draw has
    // complete explicit material state; no inherited translator bindings exist.
    if(sm64ds_fresh_native_enabled()){sm64ds_fresh_native_reset_graphics();return;}__real_func_020557b4();
}
extern "C" void __wrap__ZN3G3X13SetClearColorEtiiib(uint16_t rgb,int alpha,int depth,int id,bool fog){
    if(sm64ds_fresh_native_enabled()){sm64ds_fresh_native_clear_colour(rgb,alpha,depth,id,int(fog));return;}
    __real__ZN3G3X13SetClearColorEtiiib(rgb,alpha,depth,id,fog);
}
extern "C" void sm64ds_fresh_native_look_at(const void* at,const void* up,const void* eye,int install,void* destination){
    if(!sm64ds_fresh_native_enabled()){
        __real__ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(at,up,eye,install!=0,destination);return;
    }
    int32_t matrix[12];
    __real__ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(at,up,eye,false,matrix);
    if(destination)std::memcpy(destination,matrix,sizeof(matrix));
    if(install)sm64ds_fresh_native_view(matrix);
}
#endif
