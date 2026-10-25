// Host contract check for the SDK video providers. Uses the real owned VRAM
// storage and GX decoder; only platform IRQs and original bank-state globals
// are fixtures. This does not establish emulator or physical-console behavior.
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/game_draw.hpp"
#include "ntr/gx.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>

namespace {
bool platform_failed;
constexpr unsigned bank_offsets[]={0x240,0x241,0x242,0x243,0x244,0x245,0x246,0x248,0x249};
}
extern "C" {
std::uint16_t data_020a6088[13]{};
std::uint16_t data_02099fcc{},data_020a6084{};
std::uint32_t data_020a60a4{},data_020a60a8{};
std::uint8_t data_0208ee00{};
void func_020190b8() {}
// Same LCDC and disable bodies as native_boot_services.cpp. The test keeps
// their small bank boundary separate from boot's cartridge/IRQ dependencies.
void Vram__Map(std::uint16_t mask) {
    for(unsigned bank=0;bank<9;++bank)if(mask&(1u<<bank))
        assert(sm64ds::native_video_write(0x04000000+bank_offsets[bank],0x80,1));
}
std::uint16_t DisableVramBanks(std::uint16_t* field) {
    const auto old=*field;*field=0;
    for(unsigned bank=0;bank<9;++bank)if(old&(1u<<bank))
        assert(sm64ds::native_video_write(0x04000000+bank_offsets[bank],0,1));
    return old;
}
std::uint16_t func_020541a4();
std::uint16_t func_02054190();
std::uint16_t func_0205417c();
std::uint16_t func_02054168();
std::uint16_t func_02054154();
std::uint16_t func_02054140();
void func_02054430(std::uint32_t);
std::uint16_t func_02053f58();
void func_02054748(int);
std::uint16_t func_02053fe0();
std::uint16_t func_02053fbc();
void _ZN2GX12SetBankForBGEt(std::uint16_t);
void _ZN2GX22SetBankForSubBGExtPlttEt(std::uint16_t);
void _ZN2GX23SetBankForSubOBJExtPlttEt(std::uint16_t);
void _ZN3GXS18BeginLoadBGExtPlttEv();
void _ZN3GXS19BeginLoadOBJExtPlttEv();
void _ZN3GXS16EndLoadBGExtPlttEv();
void _ZN3GXS17EndLoadOBJExtPlttEv();
void _ZN3GXS13LoadBGExtPlttEPKvjj(const void*,std::uint32_t,std::uint32_t);
void _ZN3GXS14LoadOBJExtPlttEPKvjj(const void*,std::uint32_t,std::uint32_t);
void _ZN3G2x13SetBlendAlphaEPVttttj(volatile std::uint16_t*,std::uint16_t,
    std::uint16_t,std::uint16_t,std::uint32_t);
}
namespace sm64ds {
void native_platform_fail(const char*) {platform_failed=true;}
void native_platform_irq_set_master(bool) {}
void native_platform_irq_raise(std::uint32_t) {}
}
namespace {
void reset() {
    platform_failed=false;
    std::memset(data_020a6088,0,sizeof(data_020a6088));
    data_020a60a4=data_020a60a8=0;
    sm64ds::native_video_reset();
    sm64ds_draw_reset();ntr::gx_reset();
}
std::uint32_t read(std::uint32_t address,unsigned bytes=4) {
    return std::uint32_t(sm64ds::native_video_read(address,bytes));
}
}
int main() {
    reset();
    func_02054430(0x1ff);
    assert(data_020a6088[0]==0x1ff);
    for(auto offset:bank_offsets)assert(read(0x04000000+offset,1)==0x80);
    assert(sm64ds::native_video_write(0x06860000,0x5a3cc3a5,4));
    _ZN2GX12SetBankForBGEt(8);
    assert(data_020a6088[1]==8 && !(data_020a6088[0]&8));
    assert(read(0x06000000)==0x5a3cc3a5);
    assert(func_020541a4()==8);
    assert(data_020a6088[1]==0 && (data_020a6088[0]&8));
    assert(read(0x04000243,1)==0x80 && read(0x06860000)==0x5a3cc3a5);

    // The six ResetBank wrappers used before the original menu clears VRAM
    // must access precisely +2,+4,+8,+10,+18,+20 in the 26-byte SDK state.
    const unsigned slots[]={1,2,4,5,9,10};
    std::uint16_t (*resets[])()={func_020541a4,func_02054190,func_0205417c,
        func_02054168,func_02054154,func_02054140};
    for(unsigned i=0;i<6;++i) {
        reset();
        const auto mask=std::uint16_t(1u<<i);
        data_020a6088[slots[i]]=mask;
        assert(resets[i]()==mask);
        assert(data_020a6088[slots[i]]==0 && data_020a6088[0]==mask);
        assert(read(0x04000000+bank_offsets[i],1)==0x80);
    }
    func_02054430(0x1ff);
    assert(sm64ds::native_video_write(0x06860000,0x3c5a,2));
    assert(func_02053f58()==0x1ff && data_020a6088[0]==0);
    for(auto offset:bank_offsets)assert(read(0x04000000+offset,1)==0);
    assert(read(0x06860000,2)==0x3c5a); // disabled bank contents survive

    reset();
    for(const int mask:{0x10,0x20,0x40,0x60}) {
        func_02054748(mask);
        assert(data_020a6088[7]==mask && (read(0x04000000)&0x40000000));
        if(mask==0x10)assert(read(0x04000244,1)==0x84);
        if(mask&0x20)assert(read(0x04000245,1)==0x84);
        if(mask&0x40)assert(read(0x04000246,1)==0x8c);
    }
    assert(sm64ds::native_video_write(0x06894000,0xdeadbeef,4));
    assert(func_02053fe0()==0x60 && !(read(0x04000000)&0x40000000));
    assert(data_020a6088[7]==0 && read(0x04000245,1)==0 && read(0x04000246,1)==0);
    assert(read(0x06894000)==0xdeadbeef);
    const auto old_state=data_020a6088[0];
    func_02054748(0x30);
    assert(platform_failed && data_020a6088[0]==old_state && data_020a6088[7]==0);

    reset();
    _ZN2GX22SetBankForSubBGExtPlttEt(0x80);
    _ZN3GXS18BeginLoadBGExtPlttEv();
    assert(data_020a60a4==0x80 && data_020a6088[11]==0 && (data_020a6088[0]&0x80));
    assert(!(read(0x04001000)&0x40000000) && read(0x04000248,1)==0x80);
    const std::uint32_t bg[]={0xaaaa5555,0x12345678};
    _ZN3GXS13LoadBGExtPlttEPKvjj(bg,0x6000,sizeof(bg));
    _ZN3GXS16EndLoadBGExtPlttEv();
    assert(data_020a60a4==0 && data_020a6088[11]==0x80 && !(data_020a6088[0]&0x80));
    assert(read(0x04000248,1)==0x82 && (read(0x04001000)&0x40000000));
    assert(read(0x0689e000)==bg[0] && read(0x0689e004)==bg[1]);
    _ZN2GX23SetBankForSubOBJExtPlttEt(0x100);
    _ZN3GXS19BeginLoadOBJExtPlttEv();
    const std::uint32_t obj=0x76543210;
    _ZN3GXS14LoadOBJExtPlttEPKvjj(&obj,0x1ffc,sizeof(obj));
    _ZN3GXS17EndLoadOBJExtPlttEv();
    assert(data_020a60a8==0 && data_020a6088[12]==0x100 && !(data_020a6088[0]&0x100));
    assert(read(0x04000249,1)==0x83 && (read(0x04001000)&0x80000000));
    assert(read(0x068a1ffc)==obj);
    assert(!platform_failed && sm64ds::native_video_ok());

    reset();
    volatile auto* bld=reinterpret_cast<volatile std::uint16_t*>(std::uintptr_t(0x04000050));
    assert(sm64ds::native_video_write(0x04000054,0x1357,2));
    _ZN3G2x13SetBlendAlphaEPVttttj(bld,0,8,0,0x10);
    assert(read(0x04000050,2)==0x0840 && read(0x04000052,2)==0x1000);
    assert(read(0x04000054,2)==0x1357);
    std::uint16_t host_registers[3]={0,0,0xbeef};
    _ZN3G2x13SetBlendAlphaEPVttttj(host_registers,1,2,8,16);
    assert(host_registers[0]==0x0241 && host_registers[1]==0x1008 && host_registers[2]==0xbeef);
    assert(!platform_failed && sm64ds::native_video_ok());
    std::puts("Host video SDK contracts passed: bank ownership/content, extended palettes, blend alpha.");
}
