#include "sm64ds/native_camera.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
// Host clock-only contract. Any original body/math/lifecycle invocation is
// a hard failure, never a fabricated successful service/collision result.
extern "C" {
unsigned char data_0208715c[40],data_0209b008[192],data_02082214[16384];
unsigned char data_0209f294,data_0209f2c4,data_0209f20c,data_020a0e40;
unsigned char data_0209f49c[96],data_0209f49e[96];
std::int32_t data_0209fc48,data_0209b454;
void* data_0209f318;
std::int32_t LenVec3(const void*) {std::abort();}
std::int16_t Vec3_HorzAngle(const void*,const void*) {std::abort();}
std::int16_t Vec3_VertAngle(const void*,const void*) {std::abort();}
std::int32_t sm64ds_original_camera_init(void*) {std::abort();}
std::int32_t sm64ds_original_camera_cleanup(void*) {std::abort();}
void sm64ds_original_camera_pending_destroy(void*) {std::abort();}
}
int main() {
    using sm64ds::native_camera_stats;
    sm64ds::native_camera_reset();
    sm64ds::native_camera_publish({1.f,1.f},1,0);
    assert(native_camera_stats().elapsed_us==0);
    std::uint64_t total=0;
    for(std::uint64_t i=1;i<=120;++i) {
        sm64ds::native_camera_publish({1.f,1.f},i+1,i);
        const auto dt=native_camera_stats().elapsed_us;
        assert(dt==16666 || dt==16667);total+=dt;
    }
    assert(total==2000000);
    const auto consumed=native_camera_stats().consumed_ticks;
    sm64ds::native_camera_publish({-1.f,-1.f},121,999999);
    assert(native_camera_stats().consumed_ticks==consumed && native_camera_stats().last_vblank==120);
    sm64ds::native_camera_publish({0.f,0.f},120,119);
    assert(native_camera_stats().consumed_ticks==consumed && native_camera_stats().last_tick==121);
    total=0;
    for(std::uint64_t i=1;i<=30;++i) {
        sm64ds::native_camera_publish({0.f,0.f},121+i,120+i*2);
        const auto dt=native_camera_stats().elapsed_us;
        assert(dt==33333 || dt==33334);total+=dt;
    }
    assert(total==1000000);
    // Pause publication still stamps actual consumption while body is absent.
    data_0209fc48=1;sm64ds::native_camera_publish({1.f,0.f},152,181);
    assert(native_camera_stats().last_tick==152 && native_camera_stats().last_vblank==181);
    data_0209fc48=0;sm64ds::native_camera_publish({1.f,0.f},153,182);
    assert(native_camera_stats().elapsed_us==16667);
    sm64ds::native_camera_publish({1.f,0.f},154,282);
    assert(native_camera_stats().elapsed_us==0);
    sm64ds::native_camera_publish({1.f,0.f},155,283);
    assert(native_camera_stats().elapsed_us==16666);
    sm64ds::native_camera_publish({1.f,0.f},156,280);
    assert(native_camera_stats().elapsed_us==0);
    sm64ds::native_camera_publish({1.f,0.f},157,286);
    assert(native_camera_stats().elapsed_us==100000);
    sm64ds::native_camera_reset();
    assert(native_camera_stats().consumed_ticks==0 && native_camera_stats().publications==0);
    std::puts("PASS: logical60/1 clock; residue; duplicate; stale; pause; gaps; backwards; startup reset");
}
