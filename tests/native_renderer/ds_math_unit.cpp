// Exactness of ds_math_unit.h against 128-bit references and the original
// restoring square root, over edge cases and millions of random operands.
#include "../../source/native_renderer/ds_math_unit.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {
using i128=__int128;
std::uint32_t reference_sqrt(std::uint64_t v){
    // The emulated DS unit's former loop (port/ntr/io.cpp isqrt64).
    std::uint64_t rem=0,root=0;
    for(int i=0;i<32;++i){
        root<<=1;rem=(rem<<2)|(v>>62);v<<=2;
        if(rem>root){rem-=root|1;root+=2;}
    }
    return std::uint32_t(root>>1);
}
unsigned long long handled=0,declined=0;
void check_divide(std::int64_t n,std::int64_t d){
    std::int64_t q=0x5a5a,r=0x5a5a;
    const int ok=sm64ds_ds_math_divide(n,d,&q,&r);
    const i128 limit=i128(1)<<61;
    if(!ok){
        // Declining is only allowed outside the documented exact range.
        const bool outside=d==0||i128(n)<=-limit||i128(n)>=limit||i128(d)<=-limit||i128(d)>=limit||
            (i128(n)/i128(d)>=(i128(1)<<52))||(i128(n)/i128(d)<=-(i128(1)<<52));
        if(!outside){std::fprintf(stderr,"declined %lld / %lld\n",(long long)n,(long long)d);assert(false);}
        assert(q==0x5a5a&&r==0x5a5a);++declined;return;
    }
    const i128 eq=i128(n)/i128(d),er=i128(n)%i128(d);
    if(i128(q)!=eq||i128(r)!=er){
        std::fprintf(stderr,"divide %lld / %lld = %lld r %lld, expected %lld r %lld\n",(long long)n,(long long)d,
            (long long)q,(long long)r,(long long)eq,(long long)er);
        assert(false);
    }
    ++handled;
}
void check_sqrt(std::uint64_t v){
    const auto got=sm64ds_ds_math_isqrt64(v),want=reference_sqrt(v);
    if(got!=want){std::fprintf(stderr,"sqrt %llu = %u, expected %u\n",(unsigned long long)v,got,want);assert(false);}
}
void check_fdiv(std::int32_t a,std::int32_t b){
    if(!b)return;
    const std::int32_t want=std::int32_t(std::int64_t((i128(a)*4096)/i128(b)));
    const std::int32_t got=sm64ds_ds_math_fdiv(a,b);
    if(got!=want){std::fprintf(stderr,"fdiv %d / %d = %d, expected %d\n",a,b,got,want);assert(false);}
}
std::uint64_t bits(std::mt19937_64& g,unsigned width){
    if(!width)return 0;
    const std::uint64_t value=g()>>(64-width);
    return value|(std::uint64_t(1)<<(width-1)); // exactly `width` significant bits
}
std::int64_t signed_value(std::mt19937_64& g,unsigned width){
    const auto magnitude=bits(g,width>63?63:width);
    return (g()&1)?-std::int64_t(magnitude):std::int64_t(magnitude);
}
}

int main(){
    std::vector<std::int64_t> edges={0,1,2,3,4,5,7,8,9,10,4095,4096,4097,INT32_MAX,INT32_MIN,
        std::int64_t(INT32_MAX)+1,INT64_MAX,INT64_MIN,INT64_MAX-1,INT64_MIN+1};
    for(unsigned k=0;k<63;++k){const std::int64_t p=std::int64_t(1)<<k;edges.insert(edges.end(),{p,p-1,p+1});if(k<61)edges.push_back(3*p);}
    const auto base=edges.size();
    for(std::size_t i=0;i<base;++i)if(edges[i]!=INT64_MIN)edges.push_back(-edges[i]);
    for(auto n:edges)for(auto d:edges){
        if(n==INT64_MIN&&d==-1)continue; // C overflow; the callers keep that edge
        check_divide(n,d);
    }
    std::mt19937_64 g(0x5eed64d5ull);
    // Every magnitude class, both signs.
    for(unsigned nw=0;nw<=63;++nw)for(unsigned dw=1;dw<=63;++dw)for(unsigned i=0;i<400;++i){
        const auto n=signed_value(g,nw),d=signed_value(g,dw);
        if(d)check_divide(n,d);
    }
    // NormalizeVec3: 2^56 / (x^2+y^2+z^2) for game-sized vectors.
    for(unsigned i=0;i<2000000;++i){
        const unsigned w=1+unsigned(g()%27);
        const std::int64_t x=signed_value(g,w),y=signed_value(g,1+unsigned(g()%27)),z=signed_value(g,1+unsigned(g()%27));
        const std::int64_t sq=x*x+y*y+z*z;
        if(sq)check_divide(std::int64_t(1)<<56,sq);
        check_sqrt(std::uint64_t(sq)<<2);
    }
    // The game's fixed-point 64/32 divides: (a << 12) / b.
    for(unsigned i=0;i<3000000;++i){
        const auto a=std::int32_t(signed_value(g,1+unsigned(g()%31)));
        const auto b=std::int32_t(signed_value(g,1+unsigned(g()%31)));
        check_fdiv(a,b);
        if(b)check_divide(std::int64_t(a)*4096,b);
    }
    for(std::int32_t a:{0,1,-1,4095,-4096,524287,524288,-524288,-524289,INT32_MAX,INT32_MIN})
        for(std::int32_t b:{1,-1,2,-2,3,4096,-4096,65536,INT32_MAX,INT32_MIN})check_fdiv(a,b);
    // Square roots: perfect squares and their neighbours, edges, random widths.
    for(std::uint64_t k:{0ull,1ull,2ull,3ull,65535ull,65536ull,4294967295ull,3037000499ull,3037000500ull}){
        const std::uint64_t s=k*k;check_sqrt(s);if(s)check_sqrt(s-1);check_sqrt(s+1);
    }
    check_sqrt(UINT64_MAX);check_sqrt(UINT64_MAX-1);check_sqrt(std::uint64_t(1)<<63);
    for(unsigned w=1;w<=64;++w)for(unsigned i=0;i<20000;++i){
        const std::uint64_t v=w==64?g():bits(g,w);check_sqrt(v);
        const std::uint64_t root=sm64ds_ds_math_isqrt64(v),square=root*root;
        check_sqrt(square);if(square)check_sqrt(square-1);
    }
    for(unsigned i=0;i<4000000;++i)check_sqrt(g());
    std::printf("ds_math_unit: %llu exact divides, %llu declined to the library, square roots exact\n",handled,declined);
    return 0;
}
