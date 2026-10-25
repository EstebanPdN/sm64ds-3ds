// Native leaves for the original game's symbol names. The game runs on one
// simulation thread: its asynchronous divide/result pair shares one result,
// matching the single arithmetic unit used by these source functions.
#include "sm64ds/fixed_math.hpp"
#include <cstring>

namespace { int64_t quotient=0; }
extern "C" [[noreturn]] void sm64ds_native_missing(const char* operation);

extern "C" void ARMMathSaveState(void* state) {
    if (!state) sm64ds_native_missing("Native arithmetic save has no state buffer");
    const auto bits=uint64_t(quotient);
    // The original renderer owns an opaque 28-byte stack buffer. Its native
    // contents describe the ONE pending software quotient; no DS register
    // modes or nonexistent sqrt service are manufactured here.
    const uint32_t words[7]={0x4e4d5331u,1u,uint32_t(bits),uint32_t(bits>>32),
                            ~uint32_t(bits),~uint32_t(bits>>32),0u};
    static_assert(sizeof(words)==28,"Native math snapshot must fit the ROM caller");
    std::memcpy(state,words,sizeof(words));
}

extern "C" void ARMMathLoadState(const void* state) {
    if (!state) sm64ds_native_missing("Native arithmetic load has no state buffer");
    uint32_t words[7];
    std::memcpy(words,state,sizeof(words));
    if(words[0]!=0x4e4d5331u || words[1]!=1u || words[4]!=~words[2] ||
       words[5]!=~words[3] || words[6]!=0u)
        sm64ds_native_missing("Native arithmetic snapshot format or contents are invalid");
    const auto bits=uint64_t(words[2])|(uint64_t(words[3])<<32);
    std::memcpy(&quotient,&bits,sizeof(quotient));
}

extern "C" void _ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE(int a,int b) {
    quotient=sm64ds::math::divide_q32(a,b);
}
namespace cstd {
int div(int a,int b) {return sm64ds::math::divide(a,b);}
int mod(int a,int b) {return sm64ds::math::remainder(a,b);}
int fdiv(int a,int b) {
    _ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE(a,b);
    return sm64ds::math::round_q32_to_q12(quotient);
}
int fdiv_result() {return sm64ds::math::round_q32_to_q12(quotient);}
long long ldiv(int a,int b) {
    _ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE(a,b);
    return quotient;
}
long long ldiv_result() {return quotient;}
int sqrt(unsigned long long value) {return sm64ds::math::game_sqrt(value);}
}
extern "C" int func_02053274(const int32_t* a,const int32_t* b) {
    return sm64ds::math::distance3(a,b);
}
extern "C" int LenVec3(const int32_t* vector) {
    // Original LenVec3 squares three signed words into an unsigned 64-bit
    // sum. game_sqrt retains its wrapping <<2, 32-bit add-one and signed ASR.
    // The vector is read only; no DS IME or square-root registers are needed.
    uint64_t squared=0;
    for(unsigned i=0;i<3;++i) {
        const int64_t component=vector[i];
        squared+=uint64_t(component*component);
    }
    return sm64ds::math::game_sqrt(squared);
}
extern "C" void NormalizeVec3(const int32_t* source,int32_t* destination) {
    sm64ds::math::normalize3(source,destination);
}
extern "C" int func_02053644(const int16_t* vector) {
    uint64_t squared=0;
    for(unsigned i=0;i<3;++i) {
        const int64_t component=vector[i];
        squared+=uint64_t(component*component);
    }
    return sm64ds::math::game_sqrt(squared);
}
extern "C" void func_02053380(const int16_t* source,int16_t* destination) {
    uint64_t squared=0;
    for(unsigned i=0;i<3;++i) {
        const int64_t component=source[i];
        squared+=uint64_t(component*component);
    }
    // This short-vector leaf uses the same divider/sqrt normalization as
    // NormalizeVec3. Its squared denominator is always positive or zero;
    // signed DS divide-by-zero yields -1 for the positive 2^56 numerator.
    const uint64_t reciprocal=squared ? (uint64_t(1)<<56)/squared : uint64_t(-1);
    const uint64_t scale=reciprocal*sm64ds::math::integer_sqrt(squared<<2);
    for(unsigned i=0;i<3;++i) {
        // ROM rereads source[i] immediately before storing destination[i].
        // Keep that order for in-place and partially overlapping vectors.
        const uint64_t rounded=scale*uint64_t(int64_t(source[i]))+(uint64_t(1)<<44);
        // Only bits 45..60 reach the stored halfword. Their value is identical
        // for logical/arithmetic shift, with no signed-overflow or narrowing UB.
        const uint16_t bits=uint16_t(rounded>>45);
        std::memcpy(destination+i,&bits,sizeof(bits));
    }
}
// The adopted walk core calls the scalar quotient of the ITCM divide leaf.
// Its software divide-by-zero path retains the numerator, unlike the DS
// arithmetic unit exposed by cstd::div. No two-register remainder ABI here.
extern "C" int func_01ffabe4(int numerator,int denominator) {
    return denominator ? sm64ds::math::divide(numerator,denominator) : numerator;
}
extern "C" int func_02049018(const int32_t* position) {
    return sm64ds::math::audio_distance3(position);
}
