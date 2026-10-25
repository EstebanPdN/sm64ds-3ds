#include "sm64ds/narc.hpp"
#include <cstring>
#include <limits>

namespace sm64ds {
namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | uint32_t(u16(p+2))<<16; }
}
bool parse_narc(const std::vector<uint8_t>& image, std::vector<NarcMember>& members,
                std::string& error) {
    const auto fail=[&](const char* message) { error=message; return false; };
    if(image.size()<16 || image.size()>std::numeric_limits<uint32_t>::max())
        return fail("Invalid NARC image size");
    const auto* p=image.data();
    if(std::memcmp(p,"NARC",4) || u16(p+4)!=0xfffe || u16(p+6)!=0x0100 ||
       u32(p+8)!=image.size() || u16(p+12)!=16 || u16(p+14)!=3)
        return fail("Unsupported or inconsistent NARC header");
    size_t at=16, fat=0, names=0, payload=0, payload_size=0;
    uint16_t count=0;
    for(unsigned i=0;i<3;++i) {
        if(at>image.size() || image.size()-at<8) return fail("Truncated NARC chunk");
        const uint32_t size=u32(p+at+4);
        if(size<8 || size>image.size()-at) return fail("NARC chunk outside image");
        if(!std::memcmp(p+at,"BTAF",4)) {
            if(fat || size<12) return fail("Invalid or duplicate NARC FAT");
            count=u16(p+at+8);
            if(!count || u16(p+at+10)!=0 || size!=12+uint32_t(count)*8)
                return fail("Inconsistent NARC member count");
            fat=at+12;
        } else if(!std::memcmp(p+at,"BTNF",4)) {
            if(names || size<16) return fail("Invalid or duplicate NARC names chunk");
            names=at;
        } else if(!std::memcmp(p+at,"GMIF",4)) {
            if(payload) return fail("Duplicate NARC image chunk");
            payload=at+8; payload_size=size-8;
        } else return fail("Unsupported NARC chunk");
        at+=size;
    }
    if(at!=image.size() || !fat || !names || !payload)
        return fail("Incomplete NARC chunk directory");
    std::vector<NarcMember> result;
    result.reserve(count);
    uint32_t previous_end=0;
    for(unsigned i=0;i<count;++i) {
        const uint32_t begin=u32(p+fat+8*i), end=u32(p+fat+8*i+4);
        if(begin<previous_end || end<begin || end>payload_size)
            return fail("NARC member overlaps or exceeds its payload");
        result.push_back({uint32_t(payload+begin),end-begin});
        previous_end=end;
    }
    members=std::move(result);error.clear();return true;
}
}
