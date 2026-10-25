#include "sm64ds/game_data.hpp"
#include <cstdio>
#include <string>

int main() {
    using namespace sm64ds::game_data;
    // A BLZ sequence of three literals and a length-15, distance-3 reference.
    // Decoded in reverse order, it produces six ABC triplets.
    const std::vector<uint8_t> packed={0,0xc0,'A','B','C',0x10,
                                      14,0,0,8,4,0,0,0};
    std::vector<uint8_t> out;
    std::string error;
    auto require=[](bool success,const char* label) {
        if(!success) std::fprintf(stderr,"FAIL %s\n",label);
        return success;
    };
    if(!require(decode_blz(packed,18,out,error),"valid BLZ")) return 1;
    if(!require(std::string(out.begin(),out.end())=="ABCABCABCABCABCABC","decoded bytes")) return 1;
    const auto previous=out;
    auto bad=packed;bad[1]=0xff;
    if(!require(!decode_blz(bad,18,out,error)&&out==previous,"invalid displacement preserves output")) return 1;
    bad=packed;bad[9]=7;
    if(!require(!decode_blz(bad,18,out,error),"short footer")) return 1;
    bad=packed;bad[10]=255;
    if(!require(!decode_blz(bad,18,out,error),"table size mismatch")) return 1;
    bad=packed;bad[5]=0xff;
    if(!require(!decode_blz(bad,18,out,error),"read before any decoded bytes")) return 1;
    bad.resize(7);
    if(!require(!decode_blz(bad,18,out,error),"truncated input")) return 1;
    OverlayImage image;image.base=0x02000000;image.bytes.resize(8);
    size_t offset=0;
    if(!require(image.range(image.base+8,0,offset)&&offset==8,"empty end span")) return 1;
    if(!require(!image.range(image.base-1,1,offset),"address before image")) return 1;
    if(!require(!image.range(image.base+8,1,offset),"address after image")) return 1;
    if(!require(!image.range(image.base+7,size_t(-1),offset),"overflowing span")) return 1;
    std::puts("game_data bounded BLZ and address tests PASS");
    return 0;
}
