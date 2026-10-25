#include "sm64ds/native_video.hpp"
#include "sm64ds/game_draw.hpp"
#include "sm64ds/native_platform.hpp"
#include <cstring>

namespace sm64ds {
namespace {
alignas(32) std::uint8_t registers[0x2000];
alignas(32) std::uint8_t vram[0xa4000];
alignas(32) std::uint8_t palette[0x800];
alignas(32) std::uint8_t oam[0x800];
std::uint32_t bad_address;
bool okay = true;
struct Bank { unsigned reg, offset, size; };
constexpr Bank banks[] = {
    {0x240,0x00000,0x20000}, {0x241,0x20000,0x20000},
    {0x242,0x40000,0x20000}, {0x243,0x60000,0x20000},
    {0x244,0x80000,0x10000}, {0x245,0x90000,0x04000},
    {0x246,0x94000,0x04000}, {0x248,0x98000,0x08000},
    {0x249,0xa0000,0x04000},
};
void bad(std::uint32_t address) {
    if (okay) bad_address = address;
    okay = false;
    native_platform_fail("unsupported native video address or transfer");
}
bool fits(std::uint32_t a, std::size_t n, std::uint32_t base, std::size_t size) {
    return a >= base && n <= size && a - base <= size - n;
}
std::uint32_t bank_home(unsigned i) {
    const unsigned control = registers[banks[i].reg];
    if (!(control & 0x80)) return 0;
    const unsigned mode = control & 7;
    const unsigned slot = (control >> 3) & 3;
    if (!mode) return 0x06800000 + banks[i].offset;
    if (i < 4) {
        if (mode == 1) return 0x06000000 + slot * 0x20000;
        if (i < 2 && mode == 2) return 0x06400000 + (slot & 1) * 0x20000;
        if (i == 2 && mode == 4) return 0x06200000;
        if (i == 3 && mode == 4) return 0x06600000;
    } else if (i == 4) {
        if (mode == 1) return 0x06000000;
        if (mode == 2) return 0x06400000;
    } else if (i == 5 || i == 6) {
        const auto offset = (slot & 1) * 0x4000 + (slot >> 1) * 0x10000;
        if (mode == 1) return 0x06000000 + offset;
        if (mode == 2) return 0x06400000 + offset;
    } else if (i == 7) {
        if (mode == 1) return 0x06200000;
    } else if (i == 8) {
        if (mode == 1) return 0x06208000;
        if (mode == 2) return 0x06600000;
    }
    // Texture and extended-palette allocations are not ordinary 2D windows.
    return 0;
}
std::uint8_t* resolve(std::uint32_t address, std::size_t bytes) {
    if (fits(address,bytes,0x04000000,sizeof(registers))) return registers+(address-0x04000000);
    if (fits(address,bytes,0x05000000,sizeof(palette))) return palette+(address-0x05000000);
    if (fits(address,bytes,0x07000000,sizeof(oam))) return oam+(address-0x07000000);
    if (fits(address,bytes,0x06800000,sizeof(vram))) return vram+(address-0x06800000);
    std::uint8_t* result = nullptr;
    for (unsigned i=0; i<9; ++i) {
        const auto base = bank_home(i);
        if (!base || !fits(address,bytes,base,banks[i].size)) continue;
        // Overlapping bank reads are wired-OR on DS, and cannot be exposed as
        // one native writable pointer. Fail instead of choosing a bank.
        if (result) return nullptr;
        result = vram+banks[i].offset+(address-base);
    }
    return result;
}
} // namespace

void native_video_reset() {
    std::memset(registers,0,sizeof(registers));
    std::memset(vram,0,sizeof(vram));
    std::memset(palette,0,sizeof(palette));
    std::memset(oam,0,sizeof(oam));
    okay = true;
    bad_address = 0;
}
void* native_video_pointer(std::uint32_t address, std::size_t bytes) {
    auto* p=resolve(address,bytes);
    if (!p) bad(address);
    return p;
}
std::uint64_t native_video_read(std::uint32_t address, unsigned bytes) {
    if (bytes!=1 && bytes!=2 && bytes!=4 && bytes!=8) { bad(address); return 0; }
    if (address>=0x04000600 && address<0x04000604 && bytes<=4-(address-0x04000600)) {
        std::uint32_t status=0;
        if (!native_video_gx_status(status)) {bad(address);return 0;}
        // IRQ mode is writable; FIFO/matrix state is derived, never mirrored.
        status|=std::uint32_t(registers[0x603]&0xc0)<<24;
        const unsigned shift=8*(address-0x04000600);
        return (std::uint64_t(status)>>shift)&((1ull<<(bytes*8))-1);
    }
    auto* p=resolve(address,bytes);
    if (!p) { bad(address); return 0; }
    std::uint64_t result=0;
    for (unsigned i=0; i<bytes; ++i) result |= std::uint64_t(p[i]) << (i*8);
    return result;
}
bool native_video_write(std::uint32_t address, std::uint64_t value, unsigned bytes) {
    if (bytes!=1 && bytes!=2 && bytes!=4 && bytes!=8) { bad(address); return false; }
    if (address>=0x04000400 && address<=0x0400043c && !(address&3) && bytes==4) {
        sm64ds_draw_fifo(static_cast<std::uint32_t>(value));
        return sm64ds_draw_ok();
    }
    if (address>=0x04000440 && address<=0x040005c8 && !(address&3) && bytes==4) {
        sm64ds_draw_port((address-0x04000400)/4, static_cast<std::uint32_t>(value));
        return sm64ds_draw_ok();
    }
    if (address==0x04000208 && (bytes==2 || bytes==4)) {
        native_platform_irq_set_master(value&1);
    }
    if (address>=0x04000600 && address<0x04000604 && bytes<=4-(address-0x04000600)) {
        // GXSTAT writes acknowledge stack errors and select FIFO IRQ mode.
        // No hardware busy flag or writable stack depth exists in this backend.
        if (address+bytes==0x04000604)
            registers[0x603]=static_cast<std::uint8_t>((value>>(8*(bytes-1)))&0xc0);
        return true;
    }
    if (address==0x04000060 && bytes==2) value&=~0x3000ull; // DISP3DCNT errors are W1C
    auto* p=resolve(address,bytes);
    if (!p) { bad(address); return false; }
    for (unsigned i=0; i<bytes; ++i) p[i]=static_cast<std::uint8_t>(value>>(i*8));
    return true;
}
bool native_video_write_callback(void*, std::uint32_t a, std::uint32_t v, unsigned n) {
    return native_video_write(a,v,n);
}
bool native_video_copy(std::uint32_t destination, const void* source, std::size_t bytes) {
    if (!bytes) return true;
    if (!source) { bad(destination); return false; }
    if (auto* contiguous=resolve(destination,bytes)) {
        std::memmove(contiguous,source,bytes);
        return true;
    }
    if (bytes>0x100000000ull-destination) { bad(destination); return false; }
    // Transfers may cross adjacent VRAM banks. Resolve the exact bytes rather
    // than assuming that adjacent display aliases are contiguous host memory.
    const auto* p=static_cast<const std::uint8_t*>(source);
    for (std::size_t i=0; i<bytes; ++i) {
        auto* dst=resolve(destination+static_cast<std::uint32_t>(i),1);
        if (!dst) { bad(destination+static_cast<std::uint32_t>(i)); return false; }
        *dst=p[i];
    }
    return true;
}
bool native_video_ok() { return okay; }
std::uint32_t native_video_bad_address() { return bad_address; }
} // namespace sm64ds
