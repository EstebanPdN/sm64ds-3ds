#pragma once
#include <cstdint>
namespace render60 {
// The original BG overlay and the OBJ-combined overlay have independent
// counters. Texture uploads require source identity as well as generation.
struct AffineOverlayKey {
    const uint32_t* source=nullptr;
    uint64_t generation=0;
    bool valid=false;
    bool matches(const uint32_t* pixels,uint64_t version)const{
        return valid && source==pixels && generation==version;
    }
    void commit(const uint32_t* pixels,uint64_t version){source=pixels;generation=version;valid=true;}
    void reset(){valid=false;}
};
}
