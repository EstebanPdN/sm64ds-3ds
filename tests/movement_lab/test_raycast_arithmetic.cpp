#include "raycast_arithmetic.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
int main() {
    struct V {int32_t x,y,z;};
    assert(!safe_raycast::long_segment(V{0,0,0},V{0,2048*64,0}));
    assert(safe_raycast::long_segment(V{0,0,0},V{0,2048*64+1,0}));
    assert(safe_raycast::long_segment(V{INT32_MIN,0,0},V{INT32_MAX,0,0}));
    unsigned fast=0,wide=0;
    for(int32_t delta:{-33554432,-6400000,-640,-1,0,1,640,6400000,33554431})
        for(int32_t t:{0,1,16,1024,32768,65520,65536}) {
            assert(safe_raycast::interpolate(delta,t,false,1,-1)==int32_t((int64_t(delta)*t)>>16));++fast;
        }
    for(int32_t delta:{-33554432,-6400000,-640,-1,0,1,640,6400000,33554431})
        for(int64_t start:{int64_t(1),int64_t(655360),int64_t(1)<<40})
            for(int64_t end:{int64_t(-1),int64_t(-655360),-(int64_t(1)<<40)}) {
                const long double exact=std::floor((long double)delta*start/(start-end));
                assert(safe_raycast::interpolate(delta,0,true,start,end)==exact);++wide;
            }
    assert(safe_raycast::interpolate(6400000,0,true,655360,-6552944640LL)==640);
    std::printf("ray arithmetic: %u ordinary cases unchanged; %u wide-ratio cases matched long-double floor\n",fast,wide);
}
