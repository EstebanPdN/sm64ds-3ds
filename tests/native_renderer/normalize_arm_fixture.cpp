// ARM11 fixture for tools/test_native_normalize_arm.py: the production
// normal-basis builder (exact VFP divide/square root since BUILD131) beside
// the BUILD130 integer loops it replaced, on the same bone matrix.
#include "game_model_pose.hpp"
#include <cstring>
using namespace sm64ds::render;
namespace {
std::int32_t word(std::uint32_t bits) noexcept {std::int32_t r;std::memcpy(&r,&bits,4);return r;}
std::uint64_t product(std::int32_t a,std::int32_t b) noexcept {return std::uint64_t(std::int64_t(a)*b);}
// BUILD130 source, verbatim.
std::uint64_t root130(std::uint64_t n) noexcept {
    std::uint64_t result=0,bit=std::uint64_t(1)<<62;
    while(bit>n)bit>>=2;
    while(bit){if(n>=result+bit){n-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
    return result;
}
bool normalize_columns130(game::Affine& normal) noexcept {
    for(unsigned column=0;column<3;++column){
        auto* vector=normal.value+column*3;std::uint64_t sq=0;
        for(unsigned axis=0;axis<3;++axis){const auto v=vector[axis];if(v<-0x1000000||v>0x1000000)return false;sq+=product(v,v);}
        if(!sq){for(unsigned axis=0;axis<3;++axis)vector[axis]=0;continue;}
        const auto factor=((std::uint64_t(1)<<56)/sq)*root130(sq<<2);
        for(unsigned axis=0;axis<3;++axis){const auto value=factor*std::uint64_t(std::int64_t(vector[axis]))+(std::uint64_t(1)<<44);
            vector[axis]=word(std::uint32_t(value>>45)|((value>>63)?(0xffffffffu<<19):0u));}
    }
    return true;
}
MaterialBinding material{0,8,8,ColourSource::emission,false,true};
PartBinding parts[]={{0,0,false,0}};
const ModelBindings binding{205,{},1,1,nullptr,0,&material,1,parts,1};
unsigned char records[52];
}
// bone: 12 words; out: 9 words. Returns 1 when the basis was produced.
extern "C" int normalize_production(const game::Affine* bone,std::int32_t* out) {
    records[24]=1;NormalBasis basis;
    if(game::build_normal_bases(binding,bone,records,1,nullptr,&basis,1)!=Error::none)return 0;
    std::memcpy(out,basis.value,sizeof basis.value);return 1;
}
extern "C" int normalize_build130(const game::Affine* bone,std::int32_t* out) {
    game::Affine normal=*bone;
    if(!normalize_columns130(normal))return 0;
    // build_normal_bases composes with the unit view: an exact identity here.
    const auto combined=game::compose(normal,game::unit_affine());
    std::memcpy(out,combined.value,9*sizeof(std::int32_t));return 1;
}
