#include "sm64ds/game_geometry.hpp"
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_platform.hpp"
#include <cstdint>
#include <cstring>

extern "C" [[noreturn]] void Crash();
extern "C" {
extern std::uint16_t data_020a6088[];
extern std::uint16_t data_02086314[],data_02086324[],data_02086326[],data_02086328[];
extern std::uint32_t data_02099fd0;
extern std::uint32_t data_020a60ac,data_020a60b0,data_020a60b4,data_020a60b8,
                     data_020a60bc,data_020a60c0;
void Vram__Map(std::uint16_t);
std::uint16_t func_0205417c();
std::uint16_t func_02054168();
void func_02059fa8(std::uint32_t);
}
namespace {
using u16=std::uint16_t;
using u32=std::uint32_t;
void require(bool valid,const char* reason) {
    if(!valid) {sm64ds::native_platform_fail(reason);Crash();}
}
bool palette_mask(u16 mask) {
    // The original SDK's E/G combination 0x50 has no bank-control case and
    // a zero upload-base table entry. Do not pretend it is a flat 80-KiB bank.
    return mask==0 || mask==0x10 || mask==0x20 || mask==0x30 ||
           mask==0x40 || mask==0x60 || mask==0x70;
}
u32 bank_bytes(u16 mask) {
    u32 size=0;
    for(unsigned bank=0;bank<4;++bank)if(mask&(1u<<bank))size+=0x20000;
    if(mask&0x10)size+=0x10000;
    if(mask&0x20)size+=0x4000;
    if(mask&0x40)size+=0x4000;
    return size;
}
void bank_shadow(unsigned slot,u16 mask) {
    data_020a6088[0]=u16((data_020a6088[0]|data_020a6088[slot])&~mask);
    data_020a6088[slot]=mask;
}
void write(u32 address,u32 value,unsigned bytes) {
    require(sm64ds::native_video_write(address,value,bytes),"native texture bank register write failed");
}
void sync_physical_banks(bool palette,u16 mask) {
    auto& geometry=sm64ds::game_geometry();
    auto* out=palette?geometry.palettes:geometry.textures;
    u32 offset=0;
    const unsigned first=palette?4:0,last=palette?7:4;
    for(unsigned bank=first;bank<last;++bank)if(mask&(1u<<bank)) {
        const u32 address=bank<4?0x06800000+bank*0x20000:
                          bank==4?0x06880000:0x06890000+(bank-5)*0x4000;
        const u32 size=bank<4?0x20000:bank==4?0x10000:0x4000;
        const auto* physical=sm64ds::native_video_pointer(address,size);
        require(physical!=nullptr,"native texture bank physical storage is unavailable");
        std::memcpy(out+offset,physical,size);
        offset+=size;
    }
}
void validate_transfer(bool palette,const void* source,u32 offset,u32 size) {
    const auto& geometry=sm64ds::game_geometry();
    const auto saved=palette?data_020a60b4:data_020a60b8;
    const auto capacity=palette?geometry.palette_capacity:geometry.texture_capacity;
    require((palette?geometry.palette_transfer:geometry.texture_transfer) && saved &&
            (palette?palette_mask(u16(saved)):!(saved&~15u)) &&
            data_020a6088[palette?5:4]==0 && (data_020a6088[0]&saved)==saved,
            "native texture upload has no original LCDC bank transaction");
    require(!(offset&3u) && offset<=capacity && size<=capacity-offset && (!size || source),
            "native texture upload exceeds its original bank capacity");
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    require(!size || ((address<0x02000000 || address>=0x08000000) &&
                      address<=UINTPTR_MAX-size),
            "native texture upload contains an untranslated DS source");
    for(unsigned bank=palette?4:0;bank<(palette?7u:4u);++bank)if(saved&(1u<<bank))
        require(sm64ds::native_video_read(0x04000240+bank,1)==0x80,
                "native texture upload bank was reassigned during its LCDC transaction");
}
void copy(u32 address,const void* source,u32 size) {
    require(sm64ds::native_video_copy(address,source,size),"native texture upload to physical VRAM failed");
}
void wait_transfer() {
    if(data_02099fd0!=~0u)func_02059fa8(data_02099fd0);
    require(sm64ds::native_platform_ok(),"original texture DMA did not complete");
}
}
// SDK contracts follow the original Set/Begin/Load/End bodies pinned at
// 6230b2996636e3fd0e03cb178161c94cb4b73cc9. Owned physical VRAM is authoritative;
// GameGeometry keeps the logical texture-slot view consumed by the renderer.
// Original Model allocators and their offsets remain in the game closure.
namespace GX {
void SetBankForTex(unsigned short banks) {
    auto& geometry=sm64ds::game_geometry();
    require(!(banks&~15u) && !geometry.texture_transfer,
            "unsupported texture bank mask or unfinished texture transaction");
    require(geometry.set_texture_banks(banks),"native texture bank configuration failed");
    bank_shadow(4,banks);
    const auto display=u32(sm64ds::native_video_read(0x04000060,2));
    write(0x04000060,banks?(display&~0x3000u)|1:display&0xcffeu,2);
    unsigned slot=0;
    for(unsigned bank=0;bank<4;++bank)if(banks&(1u<<bank))
        write(0x04000240+bank,0x83|((slot++)<<3),1);
    Vram__Map(data_020a6088[0]);
    sync_physical_banks(false,banks);
}
void SetBankForTexPltt(unsigned short banks) {
    auto& geometry=sm64ds::game_geometry();
    require(palette_mask(banks) && !geometry.palette_transfer,
            "unsupported texture palette bank mask or unfinished palette transaction");
    require(geometry.set_palette_banks(banks),"native texture palette bank configuration failed");
    bank_shadow(5,banks);
    unsigned slot=0;
    if(banks&0x10) {write(0x04000244,0x83,1);slot=2;}
    if(banks&0x20)write(0x04000245,0x83|((slot++)<<3),1);
    if(banks&0x40)write(0x04000246,0x83|(slot<<3),1);
    Vram__Map(data_020a6088[0]);
    sync_physical_banks(true,banks);
}
void BeginLoadTex() {
    auto& geometry=sm64ds::game_geometry();
    const u16 banks=data_020a6088[4];
    require(banks && !(banks&~15u) && geometry.texture_capacity==bank_bytes(banks),
            "original texture BeginLoad has no configured banks");
    const u32 base=u32(data_02086324[banks*3])<<12;
    const u32 second=u32(data_02086326[banks*3])<<12;
    const u32 top=u32(data_02086328[banks*3])<<12;
    u32 expected_base=0,expected_second=0,expected_top=0,slot=0;
    unsigned previous=0;
    for(unsigned bank=0;bank<4;++bank)if(banks&(1u<<bank)) {
        const u32 address=0x06800000+bank*0x20000;
        if(!slot)expected_base=address;
        else if(bank!=previous+1) {expected_second=address;expected_top=slot*0x20000;}
        previous=bank;++slot;
    }
    require(base==expected_base && second==expected_second && top==expected_top,
            "original texture upload-window table is invalid");
    require(geometry.begin(false),"nested native texture BeginLoad");
    require(func_0205417c()==banks,"original texture bank reset changed its owner");
    data_020a60b8=banks;data_020a60ac=base;data_020a60bc=second;data_020a60c0=top;
}
void LoadTex(const void* source,unsigned offset,unsigned size) {
    validate_transfer(false,source,offset,size);
    // Both original CPU and DMA implementations copy complete words only.
    const u32 bytes=size&~3u;
    if(bytes) {
        const auto* input=static_cast<const std::uint8_t*>(source);
        if(!data_020a60bc)copy(data_020a60ac+offset,input,bytes);
        else if(offset>=data_020a60c0)copy(data_020a60bc+offset-data_020a60c0,input,bytes);
        else {
            const u32 first=bytes<data_020a60c0-offset?bytes:data_020a60c0-offset;
            copy(data_020a60ac+offset,input,first);
            if(first<bytes)copy(data_020a60bc,input+first,bytes-first);
        }
    }
    require(sm64ds::game_geometry().upload(false,source,offset,bytes),
            "native logical texture upload failed");
}
void EndLoadTex() {
    auto& geometry=sm64ds::game_geometry();
    require(geometry.texture_transfer && data_020a60b8,"native texture EndLoad has no transaction");
    validate_transfer(false,nullptr,0,0);
    wait_transfer();
    require(geometry.end(false),"native texture transaction could not end");
    SetBankForTex(u16(data_020a60b8));
    data_020a60c0=data_020a60bc=data_020a60ac=data_020a60b8=0;
}
void BeginLoadTexPltt() {
    auto& geometry=sm64ds::game_geometry();
    const u16 banks=data_020a6088[5];
    require(banks && palette_mask(banks) && geometry.palette_capacity==bank_bytes(banks),
            "original palette BeginLoad has no configured banks");
    const u32 base=u32(data_02086314[banks>>4])<<12;
    const u32 expected_base=banks&0x10?0x06880000:banks&0x20?0x06890000:0x06894000;
    require(base==expected_base,
            "original palette upload-window table is invalid");
    require(geometry.begin(true),"nested native texture palette BeginLoad");
    require(func_02054168()==banks,"original palette bank reset changed its owner");
    data_020a60b4=banks;data_020a60b0=base;
}
void LoadTexPltt(const void* source,unsigned offset,unsigned size) {
    validate_transfer(true,source,offset,size);
    const u32 bytes=size&~3u;
    if(bytes)copy(data_020a60b0+offset,source,bytes);
    require(sm64ds::game_geometry().upload(true,source,offset,bytes),
            "native logical texture palette upload failed");
}
void EndLoadTexPltt() {
    auto& geometry=sm64ds::game_geometry();
    require(geometry.palette_transfer && data_020a60b4,"native palette EndLoad has no transaction");
    validate_transfer(true,nullptr,0,0);
    wait_transfer();
    require(geometry.end(true),"native texture palette transaction could not end");
    SetBankForTexPltt(u16(data_020a60b4));
    data_020a60b4=data_020a60b0=0;
}
}
extern "C" unsigned func_02053e34() { return bank_bytes(data_020a6088[4]); }
extern "C" unsigned func_02053e1c() { return bank_bytes(data_020a6088[5]); }
