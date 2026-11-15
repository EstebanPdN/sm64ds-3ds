#include "lighting.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
using namespace sm64ds::render;
static int wrap(std::int64_t n){const auto v=(n%2048+2048)%2048;return int(v>=1024?v-2048:v);}
int main(){
    LightingState state;LightSet retained;retained.generation=123;
    assert(state.snapshot(1,false,retained)==Error::unsupported&&retained.generation==123);
    assert(state.snapshot(0,true,retained)==Error::none&&!retained.known);
    LightingState::Basis view{};view[0]=view[4]=view[8]=4096;state.view(view);
    assert(state.light(0,{{-512,0,511}},0xffff)==Error::none);
    assert(state.snapshot(1,false,retained)==Error::none&&retained.light[0].direction[0]==512&&retained.light[0].specular_reciprocal==262144);
    const auto generation=state.generation();assert(state.light(0,{{-512,0,511}},0x7fff)==Error::none&&state.generation()==generation);
    view[8]=8192;state.view(view);LightSet before;
    assert(state.snapshot(1,false,before)==Error::none&&before.light[0]==retained.light[0]); // view alone never retransforms a latched light
    assert(state.light(0,{{0,0,256}},0)==Error::none&&state.snapshot(1,false,before)==Error::none&&before.light[0].specular_reciprocal==0);
    assert(retained.light[0].direction[0]==512); // snapshot is owned, not borrowed
    assert(state.light(4,{{0,0,0}},0)==Error::invalid_data&&state.light(0,{{512,0,0}},0)==Error::invalid_data);
    std::uint32_t seed=1;
    for(unsigned i=0;i<10000;++i){
        for(auto& x:view){seed=seed*1664525u+1013904223u;std::memcpy(&x,&seed,4);}
        std::array<std::int16_t,3> n;
        for(auto& x:n){seed=seed*1664525u+1013904223u;x=int(seed%1024)-512;}
        seed=seed*1664525u+1013904223u;const auto colour=std::uint16_t(seed);const unsigned slot=i%4;state.view(view);
        assert(state.light(slot,n,colour)==Error::none&&state.snapshot(1u<<slot,false,before)==Error::none);
        int z=0;
        for(unsigned j=0;j<3;++j){std::int64_t sum=0;for(unsigned k=0;k<3;++k)sum+=std::int64_t(n[k])*view[k*3+j];
            const auto shifted=std::int64_t(std::floor(double(sum)/4096.));
            assert(before.light[slot].direction[j]==wrap(-shifted)&&before.light[slot].colour[j]==((colour>>(j*5))&31));
            if(j==2)z=wrap(shifted);
        }
        const int denominator=512-z;assert(before.light[slot].specular_reciprocal==(denominator?262144/denominator:0));
    }
    assert(state.snapshot(15,true,before)==Error::unsupported);
    std::array<std::uint8_t,128> table;for(unsigned i=0;i<128;++i)table[i]=i*2;
    assert(state.table(table.data(),127)==Error::invalid_data&&state.table(table.data(),128)==Error::none);
    assert(state.snapshot(15,true,before)==Error::none&&before.shininess==table);
    const auto table_generation=state.generation();assert(state.table(table.data(),128)==Error::none&&state.generation()==table_generation);
    const auto copy=before;table[0]=255;assert(state.table(table.data(),128)==Error::none&&copy.shininess[0]==0);
    retained.generation=123;assert(state.snapshot(16,false,retained)==Error::invalid_data&&retained.generation==123);
    state.reset();assert(!state.has_view()&&!state.known()&&state.snapshot(1,false,retained)==Error::unsupported);
}
