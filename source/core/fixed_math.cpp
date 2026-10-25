#include "sm64ds/fixed_math.hpp"
#include <cstring>
#include <limits>

namespace sm64ds::math {
namespace {
int32_t signed_bits(uint32_t bits) {
    int32_t result;
    std::memcpy(&result,&bits,sizeof(result));
    return result;
}
}

int32_t divide(int32_t numerator,int32_t denominator) {
    if(!denominator)return numerator<0 ? 1 : -1;
    if(numerator==std::numeric_limits<int32_t>::min() && denominator==-1)
        return numerator;
    return numerator/denominator;
}

int32_t remainder(int32_t numerator,int32_t denominator) {
    // cstd::mod uses the DS signed 32/32 divider. Its divide-by-zero
    // remainder retains the numerator. Widening also makes INT32_MIN/-1
    // defined in C++ and preserves the hardware's zero remainder.
    if(!denominator)return numerator;
    return int32_t(int64_t(numerator)%int64_t(denominator));
}

int64_t divide_q32(int32_t numerator,int32_t denominator) {
    const int64_t scaled=int64_t(numerator)*4294967296LL;
    if(!denominator)return numerator<0 ? 1 : -1;
    if(scaled==std::numeric_limits<int64_t>::min() && denominator==-1)
        return scaled;
    return scaled/denominator;
}

int32_t round_q32_to_q12(int64_t quotient) {
    // Original fdiv_result: add 0x80000, arithmetic shift by 20, return r0.
    // Unsigned addition gives the ARM wraparound behavior even at INT64_MAX.
    // Only bits 20..51 survive in r0, so no signed shift is necessary.
    return signed_bits(uint32_t((uint64_t(quotient)+0x80000u)>>20));
}

int32_t divide_q12(int32_t numerator,int32_t denominator) {
    return round_q32_to_q12(divide_q32(numerator,denominator));
}

uint32_t integer_sqrt(uint64_t value) {
    // Restoring square root: at most 32 iterations, integer-only and exact.
    uint64_t root=0,bit=uint64_t(1)<<62;
    while(bit>value)bit>>=2;
    while(bit) {
        const uint64_t candidate=root+bit;
        root>>=1;
        if(value>=candidate) {value-=candidate;root+=bit;}
        bit>>=2;
    }
    return uint32_t(root);
}

int32_t game_sqrt(uint64_t value) {
    // cstd::sqrt and func_02053274 use floor(sqrt(value<<2)), add one,
    // then ASR by one. The shift, add and sign are the game's register widths.
    const uint32_t rounded=integer_sqrt(value<<2)+1u;
    return signed_bits((rounded>>1)|(rounded&0x80000000u));
}

int32_t distance3(const int32_t* a,const int32_t* b) {
    uint64_t squared=0;
    for(unsigned i=0;i<3;++i) {
        const int64_t difference=signed_bits(uint32_t(a[i])-uint32_t(b[i]));
        squared+=uint64_t(difference*difference);
    }
    return game_sqrt(squared);
}

void normalize3(const int32_t* source,int32_t* destination) {
    const int32_t input[]={source[0],source[1],source[2]};
    uint64_t square=0;
    for(auto value:input) square+=uint64_t(int64_t(value)*value);
    int64_t denominator;
    std::memcpy(&denominator,&square,sizeof(denominator));
    const int64_t reciprocal=denominator ? (int64_t(1)<<56)/denominator : -1;
    const int64_t length=signed_bits(integer_sqrt(square<<2));
    const uint64_t scale=uint64_t(reciprocal)*uint64_t(length);
    for(unsigned i=0;i<3;++i) {
        const uint64_t rounded=scale*uint64_t(int64_t(input[i]))+(uint64_t(1)<<44);
        const uint32_t shifted=uint32_t(rounded>>45) | ((rounded>>63) ? 0xfff80000u : 0);
        destination[i]=signed_bits(shifted);
    }
}

int32_t audio_distance3(const int32_t* position) {
    // The sound culler doubles Y after dropping Fix12 fractions and asks the
    // DS unit for a 32-bit radicand (different from cstd's scaled sqrt).
    const uint32_t x=uint32_t(position[0]>>12),y=uint32_t(position[1]>>12)*2u,
                   z=uint32_t(position[2]>>12);
    return int32_t(integer_sqrt(x*x+y*y+z*z));
}
}
