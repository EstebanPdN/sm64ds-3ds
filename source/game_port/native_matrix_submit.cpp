// Native matrix submission boundaries from the pinned SDK bodies, including
// G3i::LookAt_ at
// upstream/src/_ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3.cpp, commit
// 6230b2996636e3fd0e03cb178161c94cb4b73cc9. Mathematics and 48-byte matrix
// order are retained; DS geometry ports become the real owned GX stream.
#include "sm64ds/game_draw.hpp"

#include <cstdint>
#include <cstring>

extern "C" {
[[noreturn]] void sm64ds_native_missing(const char* operation);
void NormalizeVec3(const std::int32_t* vector,std::int32_t* output);
void CrossVec3(const std::int32_t* first,const std::int32_t* second,std::int32_t* output);
std::int32_t DotVec3(const std::int32_t* first,const std::int32_t* second);
}

namespace {
std::int32_t signed32(std::uint32_t bits) {
    std::int32_t value;std::memcpy(&value,&bits,sizeof value);return value;
}
std::int32_t negate(std::int32_t value) {
    return signed32(0u-std::uint32_t(value));
}
}

extern "C" void _ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(
    const std::int32_t* at,const std::int32_t* up,const std::int32_t* eye,
    bool draw,std::int32_t* matrix) {
    if(!at || !up || !eye)
        sm64ds_native_missing("G3i::LookAt_ requires its three original vectors");
    std::int32_t forward[3],right[3],up2[3];
    for(unsigned axis=0;axis<3;++axis)
        forward[axis]=signed32(std::uint32_t(at[axis])-std::uint32_t(eye[axis]));
    NormalizeVec3(forward,forward);
    CrossVec3(up,forward,right);
    NormalizeVec3(right,right);
    CrossVec3(forward,right,up2);

    // Preserve the original column ordering inside the row-vector 4x3 matrix,
    // and the original sequence: nine rotation words precede the dot products.
    // MTX_MODE=2 loads both the position and vector matrices in the GX decoder.
    std::int32_t rotation[9];
    for(unsigned axis=0;axis<3;++axis) {
        rotation[axis*3]=right[axis];rotation[axis*3+1]=up2[axis];
        rotation[axis*3+2]=forward[axis];
    }
    if(draw) {
        sm64ds_draw_port(0x10,2);
        for(auto value:rotation)sm64ds_draw_port(0x17,std::uint32_t(value));
    }
    // The SDK uses the FIRST input in these dot products. Substituting the
    // third input from a conventional look-at implementation changes its ABI.
    const std::int32_t translation[3]={
        negate(DotVec3(at,right)),negate(DotVec3(at,up2)),negate(DotVec3(at,forward))};
    if(draw)for(auto value:translation)sm64ds_draw_port(0x17,std::uint32_t(value));
    if(matrix) {
        std::memcpy(matrix,rotation,sizeof rotation);
        std::memcpy(matrix+9,translation,sizeof translation);
    }
}

extern "C" void func_0205532c(const std::int32_t* rotation,
                             const std::int32_t* translation) {
    if(!rotation || !translation)
        sm64ds_native_missing("MTX_MULT_4x3 requires its original rotation and translation");
    // The SDK sends command 0x19 to GXFIFO, then Copy36Bytes writes the nine
    // rotation words through FIFO aliases 0x04000400..0x04000420. Each alias
    // is the same command stream; treating those destinations as RAM loses
    // the matrix. The three translation words complete the original command.
    sm64ds_draw_fifo(0x19);
    for(unsigned i=0;i<9;++i)sm64ds_draw_fifo(std::uint32_t(rotation[i]));
    for(unsigned i=0;i<3;++i)sm64ds_draw_fifo(std::uint32_t(translation[i]));
}
