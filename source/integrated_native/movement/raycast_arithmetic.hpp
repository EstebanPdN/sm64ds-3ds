#pragma once
#include <cstdint>
#include <initializer_list>
namespace safe_raycast {
// Bound legacy interpolation's per-axis quantum to half a world unit. KCL
// positions use64coordinate units per world unit, and the ratio has12bits.
template<class Vector> inline bool long_segment(const Vector& start,const Vector& end) {
    constexpr int64_t limit=2048*64;
    for (int64_t delta:{int64_t(end.x)-start.x,int64_t(end.y)-start.y,int64_t(end.z)-start.z})
        if (delta>limit || delta< -limit) return true;
    return false;
}
// Short DS segments retain their original Q12->Q16 interpolation exactly.
// Wide plane products require a wider ratio; double has ample precision for
// the bounded32-bit KCL-coordinate delta. Round down as the integer shift does.
inline int32_t interpolate(int32_t delta,int32_t q16,bool wide,int64_t start,int64_t end) {
    if (!wide) return int32_t((int64_t(delta)*q16)>>16);
    const double value=double(delta)*double(start)/double(start-end);
    const int32_t truncated=int32_t(value);
    return double(truncated)>value?truncated-1:truncated;
}
}
