#include "game_runtime.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
static bool enabled=false,last_install=false;
static unsigned original_words=0,guarded_words=0,projections=0,views=0;
static unsigned probes=0,io_calls=0;
static unsigned original_resets=0,native_resets=0,original_clears=0,native_clears=0;
static bool look_install=false;
static std::array<int32_t,16> projected{};
extern "C" int sm64ds_fresh_native_enabled(){return enabled;}
extern "C" void sm64ds_fresh_native_legacy_words(uint32_t n){guarded_words+=n;}
extern "C" void sm64ds_fresh_native_legacy_write(uint32_t n,uint32_t kind,uintptr_t caller,uint32_t address){
    assert(kind>=1&&kind<=3&&caller);if(kind==2)assert(address==0x04000500u);guarded_words+=n;
}
extern "C" void sm64ds_fresh_native_writer_probe(uint32_t kind,uintptr_t caller,uint32_t,unsigned width){
    if(enabled){assert(kind==4&&caller&&width==4);++probes;}
}
extern "C" void __real__ZN3ntr8io_writeEmyj(uint32_t,uint64_t value,unsigned width){assert(value==0x123456789abcdef0ull&&width==4);++io_calls;}
extern "C" void __wrap__ZN3ntr8io_writeEmyj(uint32_t,uint64_t,unsigned);
extern "C" void sm64ds_fresh_native_reset_graphics(){++native_resets;}
extern "C" void sm64ds_fresh_native_clear_colour(uint16_t rgb,int alpha,int depth,int id,int fog){
    assert(rgb==32767&&alpha==0&&depth==32767&&id==63&&fog==1);++native_clears;
}
extern "C" void __real_func_02055624(){++original_resets;}
extern "C" void __real_func_020557b4(){++original_resets;}
extern "C" void __real__ZN3G3X13SetClearColorEtiiib(uint16_t,int,int,int,bool){++original_clears;}
extern "C" void __wrap_func_02055624();
extern "C" void __wrap_func_020557b4();
extern "C" void __wrap__ZN3G3X13SetClearColorEtiiib(uint16_t,int,int,int,bool);
extern "C" void sm64ds_fresh_native_view(const int32_t* p){++views;for(unsigned i=0;i<12;++i)assert(p[i]==int32_t(i+10));}
extern "C" void __real__ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(const void*,const void*,const void*,bool install,void* destination){
    look_install=install;int32_t values[12];for(unsigned i=0;i<12;++i)values[i]=i+10;
    if(destination)std::memcpy(destination,values,sizeof(values));
}
extern "C" void sm64ds_fresh_native_projection(const int32_t* p){++projections;std::memcpy(projected.data(),p,64);}
extern "C" void __real__ZN3ntr13gx_write_fifoEm(uint32_t){++original_words;}
extern "C" void __real__ZN3ntr13gx_write_portEmm(uint32_t,uint32_t){++original_words;}
extern "C" void __real__ZN3ntr19gx_write_fifo_blockEPKmi(const uint32_t*,int n){original_words+=n;}
extern "C" void __real__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(int a,int b,int c,int d,int e,int f,bool install,void* out){
    last_install=install;int32_t values[16];for(unsigned i=0;i<16;++i)values[i]=int32_t(i)+a+b+c+d+e+f;
    if(out)std::memcpy(out,values,64);
}
extern "C" void __wrap__ZN3ntr13gx_write_fifoEm(uint32_t);
extern "C" void __wrap__ZN3ntr13gx_write_portEmm(uint32_t,uint32_t);
extern "C" void __wrap__ZN3ntr19gx_write_fifo_blockEPKmi(const uint32_t*,int);
extern "C" void __wrap__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(int,int,int,int,int,int,bool,void*);
int main(){
    auto words=[](){__wrap__ZN3ntr13gx_write_fifoEm(0xffffffffu);__wrap__ZN3ntr13gx_write_portEmm(0x04000500,0);
        __wrap__ZN3ntr19gx_write_fifo_blockEPKmi(nullptr,5);};
    words();assert(original_words==7&&!guarded_words);
    enabled=true;words();assert(original_words==7&&guarded_words==7); // command bytes never dereferenced/interpreted
    __wrap__ZN3ntr8io_writeEmyj(0x04000440,0x123456789abcdef0ull,4);
    __wrap__ZN3ntr8io_writeEmyj(0x040002b0,0x123456789abcdef0ull,4);
    assert(io_calls==2&&probes==1&&guarded_words==7); // arithmetic I/O is not graphics provenance
    __wrap_func_02055624();__wrap_func_020557b4();__wrap__ZN3G3X13SetClearColorEtiiib(32767,0,32767,63,true);
    assert(native_resets==2&&native_clears==1&&!original_resets&&!original_clears);
    std::array<int32_t,17> matrix{};matrix[16]=0x12345678;
    __wrap__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(1,2,3,4,5,6,true,matrix.data());
    assert(!last_install&&projections==1&&matrix[0]==21&&matrix[15]==36&&matrix[16]==0x12345678);
    for(unsigned i=0;i<16;++i)assert(matrix[i]==projected[i]);
    __wrap__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(1,2,3,4,5,6,false,matrix.data());
    assert(!last_install&&projections==1);
    enabled=false;
    __wrap__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(1,2,3,4,5,6,true,matrix.data());
    assert(last_install&&projections==1);
    enabled=true;std::array<int32_t,13> look{};look[12]=0x12345678;
    sm64ds_fresh_native_look_at(nullptr,nullptr,nullptr,1,look.data());
    assert(!look_install&&views==1&&look[0]==10&&look[11]==21&&look[12]==0x12345678);
    sm64ds_fresh_native_look_at(nullptr,nullptr,nullptr,0,nullptr);assert(views==1&&!look_install);
    enabled=false;sm64ds_fresh_native_look_at(nullptr,nullptr,nullptr,1,look.data());assert(look_install&&views==1);
    __wrap_func_02055624();__wrap_func_020557b4();__wrap__ZN3G3X13SetClearColorEtiiib(0,0,0,0,false);
    assert(original_resets==2&&original_clears==1&&native_resets==2&&native_clears==1);
    std::puts("Native graphics guard blocks unknown words without decoding; camera math disables GX writes and preserves full matrix output");
}
