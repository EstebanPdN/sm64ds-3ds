// Synchronous scalar/vector queries own their complete calculation. They need
// neither the DS square-root register file nor its IRQ masking protocol.
// Keep the asynchronous divider family together with its existing consumers:
// PerspectiveW and several adopted functions still observe that unit's state.
#ifdef SM64DS_FRESH_NATIVE_GAME
#include "game_runtime.h"
#include "sm64ds/fixed_math.hpp"
#include "game_model_pose.hpp"
#include <cstdlib>
#include <cstdio>

namespace {
bool enabled=true;
bool native_math() {
    return enabled&&sm64ds_fresh_native_enabled();
}
}
extern "C" void sm64ds_fresh_native_math_configure() {
    const auto* option=std::getenv("SM64DS_NATIVE_MATH");
    enabled=!option||option[0]!='0';
    const auto* assembly=std::getenv("SM64DS_NATIVE_ASM_MATRICES");
    const bool matrices=!assembly||assembly[0]!='0';
    sm64ds::render::game::use_affine_assembly(matrices);
    std::printf("Native math configured: scalar=%u affineArm11=%u\n",unsigned(enabled),unsigned(matrices));
}
extern "C" int __real_LenVec3(const std::int32_t*);
extern "C" int __wrap_LenVec3(const std::int32_t* vector) {
    if(!native_math())return __real_LenVec3(vector);
    std::uint64_t squared=0;
    for(unsigned axis=0;axis<3;++axis){
        const auto value=std::int64_t(vector[axis]);squared+=std::uint64_t(value*value);
    }
    return sm64ds::math::game_sqrt(squared);
}
extern "C" int __real_func_02053274(const std::int32_t*,const std::int32_t*);
extern "C" int __wrap_func_02053274(const std::int32_t* a,const std::int32_t* b) {
    if(!native_math())return __real_func_02053274(a,b);
    return sm64ds::math::distance3(a,b);
}
extern "C" int __real__ZN4cstd4sqrtEy(unsigned long long);
extern "C" int __wrap__ZN4cstd4sqrtEy(unsigned long long value) {
    if(!native_math())return __real__ZN4cstd4sqrtEy(value);
    return sm64ds::math::game_sqrt(value);
}
extern "C" int __real_func_02053644(const std::int16_t*);
extern "C" int __wrap_func_02053644(const std::int16_t* vector) {
    if(!native_math())return __real_func_02053644(vector);
    std::uint64_t squared=0;
    for(unsigned axis=0;axis<3;++axis){
        const auto value=std::int64_t(vector[axis]);squared+=std::uint64_t(value*value);
    }
    return sm64ds::math::game_sqrt(squared);
}
extern "C" int __real_func_02049018(const std::int32_t*);
extern "C" int __wrap_func_02049018(const std::int32_t* vector) {
    if(!native_math())return __real_func_02049018(vector);
    return sm64ds::math::audio_distance3(vector);
}
#endif
