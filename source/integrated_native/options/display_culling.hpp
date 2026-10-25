#pragma once
#include <cstdint>
#include <climits>
namespace display_culling {
// Only horizontal frustum extent changes. Near/farZ, vertical FOV and actor
// distance/activation thresholds remain the original values.
constexpr int32_t aspect(int32_t original,uint8_t screen_mode){
 if(screen_mode!=1 || original<=0)return original;
 const int64_t wide=(int64_t(original)*5+2)/4;
 return wide>INT32_MAX?INT32_MAX:int32_t(wide);
}
}
