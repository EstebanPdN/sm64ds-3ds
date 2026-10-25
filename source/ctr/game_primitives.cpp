#include "sm64ds/game_primitives.hpp"
#include <cstring>

namespace {
std::int32_t negate_register(std::int32_t value) {
    const auto bits=0u-static_cast<std::uint32_t>(value);
    std::int32_t result;
    std::memcpy(&result,&bits,sizeof(result));
    return result;
}
}

extern "C" void func_02052800(std::int32_t* matrix,std::int32_t sine,std::int32_t cosine) {
    const std::int32_t values[]={4096,0,0,0,cosine,sine,0,negate_register(sine),cosine,0,0,0};
    std::memcpy(matrix,values,sizeof(values));
}
extern "C" void func_02052820(std::int32_t* matrix,std::int32_t sine,std::int32_t cosine) {
    const std::int32_t values[]={cosine,0,negate_register(sine),0,4096,0,sine,0,cosine,0,0,0};
    std::memcpy(matrix,values,sizeof(values));
}
extern "C" void func_0205283c(std::int32_t* matrix,std::int32_t sine,std::int32_t cosine) {
    const std::int32_t values[]={cosine,sine,0,negate_register(sine),cosine,0,0,0,4096,0,0,0};
    std::memcpy(matrix,values,sizeof(values));
}

extern "C" void Matrix3x3_SetRotationZ(std::int32_t* matrix,
                                       std::int32_t sine, std::int32_t cosine) {
    // Original Thumb NEG wraps modulo 2^32, including sine == INT32_MIN.
    // Express the bit operation without C++ signed-overflow undefined behavior.
    const std::uint32_t negated = 0u - static_cast<std::uint32_t>(sine);
    matrix[0] = cosine;
    matrix[1] = sine;
    matrix[2] = 0;
    std::memcpy(&matrix[3], &negated, sizeof(negated));
    matrix[4] = cosine;
    matrix[5] = 0;
    matrix[6] = 0;
    matrix[7] = 0;
    matrix[8] = 4096;
}
