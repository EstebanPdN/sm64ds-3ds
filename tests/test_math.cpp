#include "sm64ds/fixed_math.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <initializer_list>

static int32_t signed_bits(uint32_t n) {int32_t r;std::memcpy(&r,&n,4);return r;}
static int32_t expected_divide(int32_t a,int32_t b) {
    // Independent wide reference: no signed overflow or truncation before
    // the emulated 64-bit result, then mathematical floor for the ASR.
    __int128 q;
    if(!b)q=a<0?1:-1;
    else q=(__int128(a)*4294967296LL)/b;
    const auto bits=uint64_t(q);
    q=bits;
    if(bits>>63)q-=(__int128(1)<<64);
    q+=0x80000;
    if(q>std::numeric_limits<int64_t>::max())q-=(__int128(1)<<64);
    const __int128 divisor=1048576;
    q=q<0 ? -((-q+divisor-1)/divisor) : q/divisor;
    return signed_bits(uint32_t(q));
}
static int32_t expected_remainder(int32_t a,int32_t b) {
    if(!b)return a;
    const __int128 quotient=__int128(a)/b;
    return int32_t(__int128(a)-quotient*b);
}
int main() {
    using namespace sm64ds::math;
    assert(divide_q12(1,6)==683); // truncating PC seam gave 682
    assert(divide_q12(-1,6)==-683);
    assert(divide(1,0)==-1 && divide(-1,0)==1);
    assert(divide(INT32_MIN,-1)==INT32_MIN);
    assert(remainder(INT32_MIN,-1)==0);
    assert(remainder(1234,0)==1234 && remainder(-1234,0)==-1234);
    assert(remainder(-1234,10)==-4 && remainder(1234,-10)==4);
    const int32_t edge[]={0,1,-1,2,-2,4096,-4096,INT32_MIN,INT32_MAX};
    for(auto a:edge)for(auto b:edge) {
        assert(divide_q12(a,b)==expected_divide(a,b));
        assert(remainder(a,b)==expected_remainder(a,b));
    }
    uint32_t rng=0x534d3634;
    for(unsigned i=0;i<100000;++i) {
        rng=rng*1664525u+1013904223u;int32_t a=signed_bits(rng);
        rng=rng*1664525u+1013904223u;int32_t b=signed_bits(rng);
        assert(divide_q12(a,b)==expected_divide(a,b));
        assert(remainder(a,b)==expected_remainder(a,b));
        const uint64_t value=(uint64_t(uint32_t(a))<<32)|uint32_t(b);
        const uint64_t root=integer_sqrt(value);
        assert(root*root<=value);
        assert(root==UINT32_MAX || (root+1)*(root+1)>value);
    }
    assert(integer_sqrt(UINT64_MAX)==UINT32_MAX);
    assert(game_sqrt(2)==1 && game_sqrt(3)==2 && game_sqrt(25)==5);
    const int32_t a[]={0,0,0},b[]={3*4096,4*4096,0};
    assert(distance3(a,b)==5*4096);
    const int32_t audio_pos[]={3*4096,2*4096,0};
    assert(audio_distance3(audio_pos)==5);
    assert(audio_distance3(a)==0);
    int32_t normal[3];
    normalize3(a,normal);
    assert(normal[0]==0 && normal[1]==0 && normal[2]==0);
    normalize3(b,normal);
    assert(normal[0]==2458 && normal[1]==3277 && normal[2]==0);
    normalize3(normal,normal);
    // Integer normalization is not exactly idempotent; retain DS rounding.
    assert(normal[0]==2458 && normal[1]==3276 && normal[2]==0);
    for(unsigned axis=0;axis<3;++axis) for(int sign:{-1,1}) {
        int32_t v[]={0,0,0};v[axis]=sign*4096;
        normalize3(v,normal);
        for(unsigned j=0;j<3;++j) assert(normal[j]==(j==axis?sign*4096:0));
    }
}
