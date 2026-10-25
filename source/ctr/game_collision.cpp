#include "sm64ds/game_collision.hpp"
#include "sm64ds/fixed_math.hpp"
#include "dBgW_Kc.h"
#include "dBgCh_Gnd.h"
#include "dBgCh_Lin.h"
#include "dBgCh_SphCrr.h"
#include "SurfaceInfo.h"
#include <cstring>
#include <new>

// Original BSS scratch records, not cartridge pointers. The selected collider
// subset owns these once. All queries run on the one game simulation thread.
extern "C" {
void _ZN12dBgCh_SphCrr15SetObjAndSphereERK7Vector35Fix12IiEP8dActor_c(
    dBgCh_SphCrr*,const Vector3*,int,dActor_c*);
void func_02037a38(dBgCh_SphCrr*);
alignas(4) unsigned char data_020a0c78[8];
alignas(4) unsigned char data_020a0cec[sizeof(SurfaceInfo)];
unsigned sm64ds_collision_sqrt(unsigned long long x) {
    return sm64ds::math::integer_sqrt(x);
}
}

namespace sm64ds {
static_assert(sizeof(dBgW_Kc) == 0x50);
static_assert(sizeof(dBgCh_Gnd) == 0x50);
static_assert(sizeof(void*) == 4);

bool collision_initialize(void* storage, void* validated_kcl, void* validated_clps) {
    if (!storage || !validated_kcl || !validated_clps ||
        ((reinterpret_cast<uintptr_t>(storage) | reinterpret_cast<uintptr_t>(validated_kcl) |
          reinterpret_cast<uintptr_t>(validated_clps)) & 3u)) return false;
    // Input must have passed validate_collision_data, and remain alive until
    // collision_destroy. Rebase exactly once, into this owned native buffer.
    auto* file = static_cast<KCL_File*>(validated_kcl);
    dBgW_Kc::UpdateFileOffsets(*file);
    auto* collider = new (storage) dBgW_Kc;
    collider->SetFile(file, *static_cast<CLPS_Block*>(validated_clps));
    return true;
}

bool collision_ground(void* storage, const int32_t position[3], int32_t lower_bound,
                      GroundHit& hit) {
    auto* collider = static_cast<dBgW_Kc*>(storage);
    dBgCh_Gnd query;
    query.pos.x = position[0]; query.pos.y = position[1]; query.pos.z = position[2];
    query.clsnY = lower_bound;
    query.hasClsn = 0;
    if (!collider->dBgW_Kc::DetectClsn(query) || !query.hasClsn) return false;
    GroundHit result{};
    result.y = query.clsnY;
    result.triangle = query.triangleID;
    Vector3 normal;
    collider->dBgW_Kc::GetNormal(query.triangleID, normal);
    result.normal[0] = normal.x; result.normal[1] = normal.y; result.normal[2] = normal.z;
    hit = result;
    return true;
}

void collision_destroy(void* storage) {
    static_cast<dBgW_Kc*>(storage)->~dBgW_Kc();
}

bool collision_sphere(void* storage,const int32_t center[3],int32_t radius,SphereHit& hit) {
    if(radius<=0) return false;
    alignas(dBgCh_SphCrr) unsigned char bytes[sizeof(dBgCh_SphCrr)]{};
    auto* query=new (bytes) dBgCh_SphCrr;
    Vector3 position{int(center[0]),int(center[1]),int(center[2])};
    _ZN12dBgCh_SphCrr15SetObjAndSphereERK7Vector35Fix12IiEP8dActor_c(query,&position,int(radius),nullptr);
    const int found=static_cast<dBgW_Kc*>(storage)->dBgW_Kc::DetectClsn(*query);
    func_02037a38(query);
    SphereHit result{};
    result.displacement[0]=query->disp.x; result.displacement[1]=query->disp.y;
    result.displacement[2]=query->disp.z; result.flags=query->flags;
    query->~dBgCh_SphCrr();
    if(found) hit=result;
    return found!=0;
}

bool collision_line(void* storage,const int32_t start[3],const int32_t end[3],int32_t hit[3]) {
    dBgCh_Lin query;
    Vector3 a{int(start[0]),int(start[1]),int(start[2])};
    Vector3 b{int(end[0]),int(end[1]),int(end[2])};
    query.SetObjAndLine(a,b,nullptr);
    if(!static_cast<dBgW_Kc*>(storage)->dBgW_Kc::DetectClsn(query) || !query.hasClsn) return false;
    hit[0]=query.lineEnd.x; hit[1]=query.lineEnd.y; hit[2]=query.lineEnd.z;
    return true;
}
} // namespace sm64ds
