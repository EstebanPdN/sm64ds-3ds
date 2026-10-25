// Native SDK display/upload providers for the original BootScene/title/menu.
// Semantics follow the corresponding upstream/src SDK bodies, pinned at
// 6230b2996636e3fd0e03cb178161c94cb4b73cc9. Original scene/game logic is unchanged.
// DS addresses are identifiers passed to native_video, never host pointers.
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/game_draw.hpp"
#include "ntr/gx.h"
#include <cstdint>
#include <cstring>

extern "C" {
extern std::uint16_t data_020a6088[];
extern std::uint16_t data_02099fcc, data_020a6084;
extern std::uint32_t data_020a60a4, data_020a60a8;
extern std::uint8_t data_0208ee00;
void Vram__Map(std::uint16_t);
std::uint16_t DisableVramBanks(std::uint16_t*);
void func_020190b8();
}

namespace {
using namespace sm64ds;
using u32=std::uint32_t;
using u16=std::uint16_t;
using uptr=std::uintptr_t;
bool bus(uptr address) {return address>=0x04000000 && address<0x08000000;}
bool native_address(uptr address,unsigned bytes) {
    if(!address || address>UINTPTR_MAX-bytes ||
       (address>=0x02000000 && address<0x08000000)) {
        native_platform_fail("DMA/copy contains an untranslated DS address");return false;
    }
    return true;
}
bool read_unit(uptr address,unsigned bytes,u32& value) {
    if(bus(address)) {
        value=u32(native_video_read(u32(address),bytes));return native_video_ok();
    }
    if(!native_address(address,bytes))return false;
    value=0;std::memcpy(&value,reinterpret_cast<const void*>(address),bytes);return true;
}
bool write_unit(uptr address,unsigned bytes,u32 value) {
    if(bus(address))return native_video_write(u32(address),value,bytes);
    if(!native_address(address,bytes))return false;
    std::memcpy(reinterpret_cast<void*>(address),&value,bytes);return true;
}
bool channel_ok(u32 channel) {
    if(channel<4)return true;
    native_platform_fail("invalid original DMA channel");return false;
}
bool copy_units(uptr source,uptr destination,u32 bytes,unsigned unit,bool fixed=false) {
    // The original SDK rounds down to complete DMA words/halfwords.
    const u32 count=bytes/unit;
    if(count>0x200000) {native_platform_fail("native DMA transfer too large");return false;}
    const auto extent=uptr(count)*unit;
    if(destination>UINTPTR_MAX-extent || (!fixed && source>UINTPTR_MAX-extent)) {
        native_platform_fail("native DMA address range overflow");return false;
    }
    for(u32 i=0;i<count;++i) {
        u32 value;
        if(!read_unit(source+(fixed?0:uptr(i)*unit),unit,value) ||
           !write_unit(destination+uptr(i)*unit,unit,value))return false;
    }
    return true;
}
void upload(u32 base,const void* source,u32 offset,u32 bytes,unsigned unit,u32 capacity) {
    if(!base || offset>capacity || bytes>capacity-offset || base>UINT32_MAX-offset) {
        native_platform_fail("original video upload outside destination");return;
    }
    copy_units(reinterpret_cast<uptr>(source),base+offset,bytes,unit);
}
u32 bg_address(bool sub,unsigned bg,bool chars) {
    const u32 registers=sub?0x04001000:0x04000000;
    const u32 base=sub?0x06200000:0x06000000;
    const u32 display=u32(native_video_read(registers,4));
    const u32 control=u32(native_video_read(registers+8+bg*2,2));
    const unsigned mode=display&7;
    if(chars) {
        if(bg==2 && mode>=5 && (control&0x80))return 0;
        if(bg==3 && (mode>=6 || (mode>=3 && (control&0x80))))return 0;
        return base+((control&0x3c)>>2)*0x4000+(!sub?((display>>24)&7)*0x10000:0);
    }
    const u32 offset=(control>>8)&31;
    if(bg>=2 && mode>=6)return !sub && bg==2 && mode==6 ? base : 0;
    const bool bitmap=(control&0x80) && ((bg==2 && mode==5) || (bg==3 && mode>=3));
    return base+offset*(bitmap?0x4000:0x800)+(!bitmap && !sub?((display>>27)&7)*0x10000:0);
}
void* bg_pointer(bool sub,unsigned bg,bool chars) {
    const auto address=bg_address(sub,bg,chars);
    return address?native_video_pointer(address,1):nullptr;
}
void bank_register(unsigned bank,unsigned control) {
    static constexpr unsigned offsets[]={0x240,0x241,0x242,0x243,0x244,0x245,0x246,0x248,0x249};
    native_video_write(0x04000000+offsets[bank],control,1);
}
void bank_shadow(unsigned slot,u16 mask) {
    data_020a6088[0]=u16((data_020a6088[0]|data_020a6088[slot])&~mask);
    data_020a6088[slot]=mask;
}
void main_bank(u16 mask,bool objects) {
    const bool large=mask && !(mask&~(objects?3u:15u));
    const bool small=mask && !(mask&~0x70u);
    if(mask && !large && !small) {
        native_platform_fail("unsupported main BG/OBJ bank combination");return;
    }
    bank_shadow(objects?2:1,mask);
    const unsigned mode=objects?2:1;
    if(large) {
        unsigned slot=0;
        for(unsigned bank=0;bank<4;++bank)if(mask&(1u<<bank))bank_register(bank,0x80|mode|((slot++)<<3));
    } else if(small) {
        unsigned slot=0;
        if(mask&0x10) {bank_register(4,0x80|mode);slot=2;}
        for(unsigned bank=5;bank<=6;++bank)if(mask&(1u<<bank))bank_register(bank,0x80|mode|((slot++)<<3));
    }
    Vram__Map(data_020a6088[0]);
}
void sub_ext_bank(u16 mask,bool objects) {
    const auto expected=objects?0x100:0x80;
    if(mask && mask!=expected) {native_platform_fail("unsupported sub extended palette bank");return;}
    bank_shadow(objects?12:11,mask);
    const u32 flag=objects?0x80000000:0x40000000;
    const u32 display=u32(native_video_read(0x04001000,4));
    native_video_write(0x04001000,mask?display|flag:display&~flag,4);
    if(mask)bank_register(objects?8:7,objects?0x83:0x82);
    Vram__Map(data_020a6088[0]);
}
void main_bg_ext_bank(u16 mask) {
    // func_02054748's supported E/F/G assignments. F owns palette slots
    // 0/1, G owns 2/3; E supplies all four slots from one physical bank.
    if(mask && mask!=0x10 && mask!=0x20 && mask!=0x40 && mask!=0x60) {
        native_platform_fail("unsupported main BG extended palette bank");return;
    }
    bank_shadow(7,mask);
    const u32 display=u32(native_video_read(0x04000000,4));
    native_video_write(0x04000000,mask?display|0x40000000u:display&~0x40000000u,4);
    if(mask==0x10)bank_register(4,0x84);
    if(mask&0x20)bank_register(5,0x84);
    if(mask&0x40)bank_register(6,0x8c);
    Vram__Map(data_020a6088[0]);
}
void clear_extended_enable(bool sub,bool objects) {
    const u32 reg=sub?0x04001000:0x04000000;
    const u32 flag=objects?0x80000000:0x40000000;
    native_video_write(reg,u32(native_video_read(reg,4))&~flag,4);
}

// SDK stack queries occur before the queued geometry is rendered. Start from
// the last consumed decoder state and parse the ordered pending stream without
// applying matrices or emitting geometry a second time. Parameter words are
// never interpreted as command bytes, including FIFO/direct-port mixtures.
bool pending_stack_state(ntr::GxStackState& state) {
    if(!sm64ds_draw_ok() || !ntr::gx_get_stack_state(state))return false;
    if(state.projection<0 || state.projection>1 || state.position<0 || state.position>31)return false;
    auto execute=[&](unsigned command,u32 value) {
        if(command==0x10)state.mode=value&3;
        else if(command==0x11) {
            if(state.mode==3)return false; // texture stack is not implemented by the decoder
            int& depth=state.mode==0?state.projection:state.position;
            if(depth>=(state.mode==0?1:31))return false;
            ++depth;
        } else if(command==0x12) {
            if(state.mode==3)return false;
            if(state.mode==0) {if(state.projection>0)--state.projection;}
            else {
                const int amount=(value&32)?int(value&63)-64:int(value&63);
                state.position-=amount;
                if(state.position<0 || state.position>31)return false;
            }
        }
        return true;
    };
    unsigned queue[4]={}, queued=0, next=0, have=0, port=0, port_have=0;
    u32 first=0;
    auto zero_parameters=[&]() {
        while(next<queued) {
            const int need=ntr::gx_command_parameter_count(std::uint8_t(queue[next]));
            if(need<0)return false;
            if(need)break;
            if(!execute(queue[next++],0))return false;
        }
        if(next==queued)queued=0;
        return true;
    };
    const auto* words=sm64ds_draw_entries();
    for(u32 index=0;index<sm64ds_draw_count();++index) {
        const auto word=words[index];
        if(word.port) {
            const int need=ntr::gx_command_parameter_count(std::uint8_t(word.port));
            if(need<0)return false;
            if(need<=1) {if(!execute(word.port,word.value))return false;}
            else {
                if(port_have && port!=word.port)return false;
                port=word.port;
                if(++port_have==unsigned(need))port_have=0;
            }
        } else if(!queued) {
            for(unsigned i=0;i<4;++i)queue[i]=(word.value>>(8*i))&255;
            queued=4;next=have=0;
            if(!zero_parameters())return false;
        } else {
            const int need=ntr::gx_command_parameter_count(std::uint8_t(queue[next]));
            if(need<1)return false;
            if(!have)first=word.value;
            if(++have==unsigned(need)) {
                if(!execute(queue[next++],first))return false;
                have=0;
                if(!zero_parameters())return false;
            }
        }
    }
    return queued==0 && have==0 && port_have==0;
}
bool query_stack(ntr::GxStackState& state) {
    if(pending_stack_state(state))return true;
    native_platform_fail("GX stack query found an incomplete or unsupported command stream");
    return false;
}
}

namespace sm64ds {
bool native_video_gx_status(std::uint32_t& status) {
    ntr::GxStackState state{};
    if(!query_stack(state))return false;
    // Submissions are CPU-owned and synchronous: there is no hardware FIFO
    // occupancy/busy wait. Matrix levels reflect the validated pending stream.
    status=(3u<<25)|(u32(state.projection)<<13)|(u32(state.position)<<8);
    return true;
}
}

extern "C" {
int func_02053c10(int enable) {
    const u32 old=u32(sm64ds::native_video_read(0x04000004,2));
    sm64ds::native_video_write(0x04000004,enable?old|0x10:old&~0x10u,2);
    return int(old&0x10);
}
void func_02053a90(u16* registers,int amount) {
    const u32 value=amount>0?(u32(amount)|0x4000):
                    amount<0?((0u-u32(amount))|0x8000):0;
    write_unit(reinterpret_cast<uptr>(registers),2,u16(value));
}
void _ZN3G2x12SetBGyAffineEPVtP9Matrix2x2iiii(volatile u16* registers,
        const std::int32_t* matrix,int a,int b,int c,int d) {
    const uptr p=reinterpret_cast<uptr>(registers);
    const u32 pa=u16(matrix[0]>>4),pb=u16(matrix[1]>>4);
    const u32 pc=u16(matrix[2]>>4),pd=u16(matrix[3]>>4);
    write_unit(p,4,pa|(pb<<16));write_unit(p+4,4,pc|(pd<<16));
    // Preserve the original ARM32 wrap before its arithmetic right shift.
    const u32 dx=u32(c)-u32(a),dy=u32(d)-u32(b);
    const u32 x=u32(matrix[0])*dx+u32(matrix[1])*dy+(u32(a)<<12);
    const u32 y=u32(matrix[2])*dx+u32(matrix[3])*dy+(u32(b)<<12);
    write_unit(p+8,4,u32(std::int32_t(x)>>4));
    write_unit(p+12,4,u32(std::int32_t(y)>>4));
}
int func_02055464(int* out) {
    ntr::GxStackState state{};
    const bool valid=query_stack(state);
    // On failure stop the native runtime; never trap the original busy loop.
    *out=valid?state.projection:0;return 0;
}
int func_02055490(int* out) {
    ntr::GxStackState state{};
    const bool valid=query_stack(state);
    *out=valid?state.position:0;return 0;
}
void func_020555a4(const void* source) {
    // The recovered MultiCopyHalf declaration calls r0 "dst", but the ROM
    // primitive loads halfwords from r0 and stores to r1. This is the original
    // 32-entry TOON_TABLE upload, with halfword stores and no geometry command.
    upload(0x04000380,source,0,0x40,2,0x40);
}
void func_02055624() {
    ntr::GxStackState state{};
    if(!query_stack(state))return;
    // The original GXSTAT stack-error acknowledgement has no persistent busy
    // bit in this synchronous submission backend. Preserve its command order.
    sm64ds_draw_port(0x10,3);sm64ds_draw_port(0x15,0);
    sm64ds_draw_port(0x10,0);
    if(state.projection)sm64ds_draw_port(0x12,u32(state.projection));
    sm64ds_draw_port(0x10,2);sm64ds_draw_port(0x12,u32(state.position));
    sm64ds_draw_port(0x15,0);
}
void func_020557b4() {
    // Hardware waits are satisfied by validated synchronous submissions.
    // DISP3DCNT 12/13 acknowledge errors (write-one-to-clear), not enable bits.
    const auto display=sm64ds::native_video_read(0x04000060,2);
    sm64ds::native_video_write(0x04000060,display&~0x3000ull,2);
    func_02055624();
    sm64ds_draw_port(0x29,0x1f0080);
    sm64ds_draw_port(0x2a,0);sm64ds_draw_port(0x2b,0);
}
void func_0200f4b4() {
    func_020557b4();
    sm64ds_draw_port(0x10,3);sm64ds_draw_port(0x15,0);sm64ds_draw_port(0x10,2);
}
void func_020553c0(u32 address) {
    // Original: 32 STMIA bursts of four zero words without writeback. All
    // 0x04000400..0x0400043c aliases feed the same packed GX FIFO.
    if(address<0x04000400 || address>0x04000430 || (address&3)) {
        sm64ds::native_platform_fail("GX FIFO flush destination is not a FIFO alias");return;
    }
    for(unsigned word=0;word<128;++word)sm64ds_draw_fifo(0);
}
void func_02055780() {
    func_020553c0(0x04000400);
    ntr::GxStackState state{};query_stack(state);
}
void func_020554bc() {
    // Original DMA/CPU branches both clear these edge/fog/toon tables; the
    // native transfer provider is synchronous and the original callback is null.
    for(u32 offset=0;offset<0x10;offset+=4)sm64ds::native_video_write(0x04000330+offset,0,4);
    for(u32 offset=0;offset<0x50;offset+=4)sm64ds::native_video_write(0x04000360+offset,0,4);
    for(unsigned word=0;word<32;++word)sm64ds_draw_port(0x34,0);
}

void DMAStartTransfer(u32 channel,u32 source,u32 destination,u32 control) {
    using namespace sm64ds;
    if(!channel_ok(channel))return;
    const u32 reg=0x040000b0+channel*12;
    native_video_write(reg,source,4);native_video_write(reg+4,destination,4);
    if(!(control&0x80000000)) {native_video_write(reg+8,control,4);return;}
    const unsigned timing=(control>>27)&7;
    const unsigned src_mode=(control>>23)&3,dst_mode=(control>>21)&3;
    const unsigned unit=(control&0x04000000)?4:2;
    // Immediate CPU work and GX FIFO uploads are synchronous here. A repeat,
    // HBlank/VBlank/cart transfer requires a real scheduler, not fake completion.
    if((control&0x02000000) || src_mode==3 ||
       (timing && !(timing==7 && destination==0x04000400 && unit==4))) {
        native_platform_fail("original DMA timing or repeat mode not implemented");return;
    }
    u32 count=control&0x1fffff;if(!count)count=0x200000;
    const int src_step=src_mode==2?0:src_mode==1?-int(unit):int(unit);
    const int dst_step=dst_mode==2?0:dst_mode==1?-int(unit):int(unit);
    uptr src=source,dst=destination;
    for(u32 i=0;i<count;++i) {
        u32 value;
        if(!read_unit(src,unit,value) || !write_unit(dst,unit,value))return;
        src+=src_step;dst+=dst_step;
    }
    native_video_write(reg+8,control&~0x80000000u,4);
    if(control&0x40000000)native_platform_irq_raise(1u<<(8+channel));
}
void DMAStartTransferFB(u32 channel,u32 source,u32 destination,u32 control) {
    DMAStartTransfer(channel,source,destination,control);
}
void DMASyncWordTransfer(u32 channel,const void* source,void* destination,u32 bytes) {
    if(channel_ok(channel))copy_units(reinterpret_cast<uptr>(source),reinterpret_cast<uptr>(destination),bytes,4);
}
void DMASyncHalfTransfer(u32 channel,const void* source,void* destination,u32 bytes) {
    if(channel_ok(channel))copy_units(reinterpret_cast<uptr>(source),reinterpret_cast<uptr>(destination),bytes,2);
}
void DMASyncFillTransfer(u32 channel,void* destination,u32 value,u32 bytes) {
    if(channel_ok(channel))copy_units(reinterpret_cast<uptr>(&value),reinterpret_cast<uptr>(destination),bytes,4,true);
}
void func_02059fa8(u32 channel) {
    // Every transfer implemented here has completed before its call returned.
    if(!channel_ok(channel))return;
    if(sm64ds::native_video_read(0x040000b8+channel*12,4)&0x80000000)
        sm64ds::native_platform_fail("DMA wait reached an unimplemented scheduled transfer");
}
void func_02059fd0(u32 channel,const void* source,void* destination,u32 bytes,void (*done)(u32),u32 arg) {
    if(channel_ok(channel) && copy_units(reinterpret_cast<uptr>(source),reinterpret_cast<uptr>(destination),bytes,4) && done)done(arg);
}
void func_0205a064(u32 channel,void* destination,u32 value,u32 bytes,void (*done)(u32),u32 arg) {
    if(channel_ok(channel) && copy_units(reinterpret_cast<uptr>(&value),reinterpret_cast<uptr>(destination),bytes,4,true) && done)done(arg);
}

void _ZN2GX12SetBankForBGEt(u16 mask) {main_bank(mask,false);}
void _ZN2GX13SetBankForOBJEt(u16 mask) {main_bank(mask,true);}
u16 func_020541b8(u16* bank) {
    // SDK ResetBank moves the previous owner to LCDC without erasing VRAM.
    // In particular, this differs from DisableVramBanks, which turns it off.
    if(!bank) {sm64ds::native_platform_fail("null original VRAM bank state");return 0;}
    const u16 old=*bank;
    *bank=0;
    data_020a6088[0]|=old;
    Vram__Map(old);
    return old;
}
u16 func_020541a4() {return func_020541b8(data_020a6088+1);}
u16 func_02054190() {return func_020541b8(data_020a6088+2);}
u16 func_0205417c() {return func_020541b8(data_020a6088+4);}
u16 func_02054168() {return func_020541b8(data_020a6088+5);}
u16 func_02054154() {return func_020541b8(data_020a6088+9);}
u16 func_02054140() {return func_020541b8(data_020a6088+10);}
void func_02054430(u32 mask) {
    data_020a6088[0]|=u16(mask);
    Vram__Map(u16(mask));
}
u16 func_02053f58() {return DisableVramBanks(data_020a6088);}
void func_02054748(int mask) {
    if(mask<0 || mask>0xffff) {sm64ds::native_platform_fail("invalid original extended palette bank mask");return;}
    main_bg_ext_bank(u16(mask));
}
u16 func_02053fe0() {clear_extended_enable(false,false);return DisableVramBanks(data_020a6088+7);}
u16 func_02053fbc() {clear_extended_enable(false,true);return DisableVramBanks(data_020a6088+8);}
u16 func_02053f08() {clear_extended_enable(true,false);return DisableVramBanks(data_020a6088+11);}
u16 func_02053ee0() {clear_extended_enable(true,true);return DisableVramBanks(data_020a6088+12);}
void _ZN2GX15DisableAllBanksEv() {
    for(unsigned slot=0;slot<13;++slot) {
        if(slot==7 || slot==8 || slot==11 || slot==12) {
            const u32 reg=slot<9?0x04000000:0x04001000;
            const u32 flag=(slot==7 || slot==11)?0x40000000:0x80000000;
            sm64ds::native_video_write(reg,sm64ds::native_video_read(reg,4)&~std::uint64_t(flag),4);
        }
        DisableVramBanks(data_020a6088+slot);
    }
}
void _ZN2GX15SetBankForSubBGEt(u16 mask) {
    if(mask && mask!=4 && mask!=0x80 && mask!=0x180) {sm64ds::native_platform_fail("unsupported sub BG bank");return;}
    bank_shadow(9,mask);
    if(mask==4)bank_register(2,0x84);
    if(mask&0x80)bank_register(7,0x81);
    if(mask&0x100)bank_register(8,0x81);
    Vram__Map(data_020a6088[0]);
}
void _ZN2GX16SetBankForSubOBJEt(u16 mask) {
    if(mask && mask!=8 && mask!=0x100) {sm64ds::native_platform_fail("unsupported sub OBJ bank");return;}
    bank_shadow(10,mask);
    if(mask==8)bank_register(3,0x84);
    if(mask==0x100)bank_register(8,0x82);
    Vram__Map(data_020a6088[0]);
}
void _ZN2GX22SetBankForSubBGExtPlttEt(u16 mask) {sub_ext_bank(mask,false);}
void _ZN2GX23SetBankForSubOBJExtPlttEt(u16 mask) {sub_ext_bank(mask,true);}
u16 func_02054118() {clear_extended_enable(true,false);return func_020541b8(data_020a6088+11);}
u16 func_020540f0() {clear_extended_enable(true,true);return func_020541b8(data_020a6088+12);}
void _ZN3GXS18BeginLoadBGExtPlttEv() {data_020a60a4=func_02054118();}
void _ZN3GXS19BeginLoadOBJExtPlttEv() {data_020a60a8=func_020540f0();}
void _ZN3GXS16EndLoadBGExtPlttEv() {sub_ext_bank(u16(data_020a60a4),false);data_020a60a4=0;}
void _ZN3GXS17EndLoadOBJExtPlttEv() {sub_ext_bank(u16(data_020a60a8),true);data_020a60a8=0;}

void _ZN2GX15SetGraphicsModeEiii(int display,int mode,int geometry) {
    const auto was_enabled=data_02099fcc;
    const u32 old=u32(sm64ds::native_video_read(0x04000000,4));
    data_020a6084=u16(display);if(!was_enabled)display=0;
    sm64ds::native_video_write(0x04000000,(old&0xfff0fff0u)|u32(mode)|(u32(display)<<16)|(u32(geometry)<<3),4);
    if(!data_020a6084)data_02099fcc=0;
}
void _ZN3GXS15SetGraphicsModeEi(int mode) {
    const auto old=sm64ds::native_video_read(0x04001000,4);
    sm64ds::native_video_write(0x04001000,(old&~7ull)|u32(mode),4);
}
void _ZN2GX6DispOnEv() {
    data_02099fcc=1;
    const auto old=sm64ds::native_video_read(0x04000000,4);
    sm64ds::native_video_write(0x04000000,data_020a6084?
        (old&~0x30000ull)|(u32(data_020a6084)<<16):old|0x10000,4);
}
void Enable3dEngines() {
    func_020190b8();
    sm64ds::native_video_write(0x04000304,sm64ds::native_video_read(0x04000304,2)|0xc,2);
    data_0208ee00=1;
}
void _ZN3G2x18SetBlendBrightnessEPVtts(volatile u16* registers,u16 planes,std::int16_t amount) {
    const uptr p=reinterpret_cast<uptr>(registers);
    write_unit(p,2,planes|(amount<0?0xc0:0x80));
    write_unit(p+4,2,u16(amount<0?-int(amount):int(amount)));
}
void _ZN3G2x13SetBlendAlphaEPVttttj(volatile u16* registers,u16 first_target,
        u16 second_target,u16 first_alpha,u32 second_alpha) {
    // The ROM loads the fifth argument as a full stack word. Keep its width,
    // ARM32 shift/OR behavior, and single store spanning BLDCNT/BLDALPHA.
    const u32 value=u32(first_target|0x40)|(u32(second_target)<<8)|
                    ((u32(first_alpha)|(second_alpha<<8))<<16);
    write_unit(reinterpret_cast<uptr>(registers),4,value);
}

#define BG_GETTERS(PREFIX,SUB,BG) \
void* PREFIX##12GetBG##BG##ScrPtrEv() {return bg_pointer(SUB,BG,false);} \
void* PREFIX##13GetBG##BG##CharPtrEv() {return bg_pointer(SUB,BG,true);}
BG_GETTERS(_ZN2G2,false,0)
BG_GETTERS(_ZN2G2,false,1)
BG_GETTERS(_ZN2G2,false,2)
BG_GETTERS(_ZN2G2,false,3)
BG_GETTERS(_ZN3G2S,true,0)
BG_GETTERS(_ZN3G2S,true,1)
BG_GETTERS(_ZN3G2S,true,2)
BG_GETTERS(_ZN3G2S,true,3)
#undef BG_GETTERS
void* func_02054efc() {return bg_pointer(false,0,true);}
void* func_02054ea8() {return bg_pointer(false,1,true);}
void* func_02054d88() {
    const u32 address=bg_address(false,3,true);
    // LoadFont copies 0x4000 bytes and decompresses at +0x8000. A one-byte
    // pointer validation cannot safely expose those later pointer offsets.
    return address?sm64ds::native_video_pointer(address,0x10000):nullptr;
}

#define BG_LOAD(NAME,SUB,BG) \
void NAME(const void* source,u32 offset,u32 bytes) {upload(bg_address(SUB,BG,true),source,offset,bytes,4,0x20000);}
BG_LOAD(_ZN2GX11LoadBG0CharEPKvjj,false,0)
BG_LOAD(_ZN2GX11LoadBG1CharEPKvjj,false,1)
BG_LOAD(_ZN3GXS11LoadBG0CharEPKvjj,true,0)
BG_LOAD(_ZN3GXS11LoadBG1CharEPKvjj,true,1)
#undef BG_LOAD
#define VIDEO_LOAD(NAME,BASE,UNIT,CAPACITY) \
void NAME(const void* source,u32 offset,u32 bytes) {upload(BASE,source,offset,bytes,UNIT,CAPACITY);}
VIDEO_LOAD(_ZN2GX10LoadBGPlttEPKvjj,0x05000000,2,0x200)
VIDEO_LOAD(_ZN2GX11LoadOBJPlttEPKvjj,0x05000200,2,0x200)
VIDEO_LOAD(_ZN3GXS10LoadBGPlttEPKvjj,0x05000400,2,0x200)
VIDEO_LOAD(_ZN3GXS11LoadOBJPlttEPKvjj,0x05000600,2,0x200)
VIDEO_LOAD(_ZN2GX7LoadOBJEPKvjj,0x06400000,4,0x40000)
VIDEO_LOAD(_ZN3GXS7LoadOBJEPKvjj,0x06600000,4,0x20000)
VIDEO_LOAD(_ZN3GXS13LoadBGExtPlttEPKvjj,0x06898000,4,0x8000)
VIDEO_LOAD(_ZN3GXS14LoadOBJExtPlttEPKvjj,0x068a0000,4,0x2000)
#undef VIDEO_LOAD
}
