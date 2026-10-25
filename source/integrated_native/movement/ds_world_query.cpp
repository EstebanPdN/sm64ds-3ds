#include "ds_world_query.h"
#include "dBgCh_Gnd.h"
#include "dBgCh_Lin.h"
#include "dBgW.h"
#include "dBgW_KcMbgSclY.h"
#include "SurfaceInfo.h"
#include <cstring>
#include <stdint.h>
#include <limits.h>

static_assert(sizeof(void*) == 4, "This bridge runs inside the ARM game");
static_assert(sizeof(dBgCh_Gnd) == 0x50, "Original ground-query ABI");
static_assert(sizeof(DsWorldGroundHit) == 40, "Fixed-width contact ABI");
static_assert(sizeof(DsWorldLineHit) == 48, "Fixed-width line contact ABI");

template<bool Water>
static int world_ground(const int32_t position[3], void* actor,
                         int32_t probe_height, DsWorldGroundHit* hit) {
    if (!position || !hit || probe_height < 0) return 0;
    const Vector3 point{int(position[0]),int(position[1]),int(position[2])};
    dBgCh_Gnd query;
    query.SetObjAndPos(point, static_cast<dActor_c*>(actor));
    if (Water) {query.StartDetectingWater();query.StopDetectingOrdinary();}
    query.mProbeHeight = probe_height;
    // DetectClsn initializes the lower bound itself; seeding clsnY before this
    // call would have no effect. The original registry owns query ordering.
    if (!query.DetectClsn() || !query.hasClsn || query.flags >= 24 ||
        !query.unk_024 || query.triangleID >= 32768) return 0;
    DsWorldGroundHit result{};
    result.height = query.clsnY;
    result.normal[0] = query.surface.normal.x;
    result.normal[1] = query.surface.normal.y;
    result.normal[2] = query.surface.normal.z;
    result.clps[0] = query.surface.clps.w0;
    result.clps[1] = query.surface.clps.w1;
    result.collider = uint32_t(query.unk_024);
    result.actor = uint32_t(query.unk_020);
    result.owner_id = query.clsnID;
    result.triangle = query.triangleID;
    result.slot = query.flags;
    *hit = result;
    return 1;
}

extern "C" int sm64ds_world_ground(const int32_t position[3], void* actor,
                                    int32_t probe_height, DsWorldGroundHit* hit) {
    return world_ground<false>(position,actor,probe_height,hit);
}
extern "C" int sm64ds_world_water_ground(const int32_t position[3], void* actor,
                                          int32_t probe_height, DsWorldGroundHit* hit) {
    return world_ground<true>(position,actor,probe_height,hit);
}

extern "C" int sm64ds_motion_ground(const int32_t world_position[3], void* actor,
                                     DsWorldGroundHit* hit) {
    if (!world_position || !hit) return 0;
    constexpr int32_t lower = -11000 * 4096;
    int32_t point[3];
    for (unsigned i=0; i<3; ++i) {
        int64_t value = int64_t(world_position[i]) * 4096;
        // Static DS floor queries require their quantized input Y to be
        // strictly above the plane. One KCL coordinate step includes an exact
        // Y+78 boundary without widening the admitted set of floor heights.
        if (i==1) value += 78 * 4096 + 64;
        if (value < INT_MIN || value > INT_MAX) return 0;
        point[i] = int32_t(value);
    }
    const int64_t depth = int64_t(point[1]) - lower;
    if (depth <= 0 || depth > INT_MAX) return 0;
    DsWorldGroundHit result;
    if (!sm64ds_world_ground(point,actor,int32_t(depth),&result) ||
        result.height <= lower || int64_t(result.height) > int64_t(point[1])-64) return 0;
    *hit = result;
    return 1;
}

extern "C" int sm64ds_world_line(const int32_t start[3],const int32_t end[3],
                                  void* actor,DsWorldLineHit* hit) {
    if (!start || !end || !hit) return 0;
    // Original SetObjAndLine forms start+end and end-start in signed32-bit
    // arithmetic, then computes their distance. Reject overflow first.
    int64_t squared=0;
    for (unsigned i=0;i<3;++i) {
        const int64_t sum=int64_t(start[i])+end[i];
        const int64_t delta=int64_t(end[i])-start[i];
        if (sum<INT_MIN || sum>INT_MAX || delta<INT_MIN || delta>INT_MAX) return 0;
        const int64_t term=delta*delta;
        if (term>int64_t(INT_MAX)*INT_MAX-squared) return 0;
        squared+=term;
    }
    if (!squared) return 0;
    dBgCh_Lin query;
    const Vector3 a{int(start[0]),int(start[1]),int(start[2])};
    const Vector3 b{int(end[0]),int(end[1]),int(end[2])};
    query.SetObjAndLine(a,b,static_cast<dActor_c*>(actor));
    if (!query.DetectClsn() || !query.hasClsn || query.flags>=24 ||
        !query.unk_024 || query.triangleID>=32768) return 0;
    DsWorldLineHit result{};
    result.position[0]=query.lineEnd.x;result.position[1]=query.lineEnd.y;result.position[2]=query.lineEnd.z;
    result.normal[0]=query.surface.normal.x;result.normal[1]=query.surface.normal.y;result.normal[2]=query.surface.normal.z;
    result.clps[0]=query.surface.clps.w0;result.clps[1]=query.surface.clps.w1;
    result.collider=uint32_t(query.unk_024);result.actor=uint32_t(query.unk_020);
    result.owner_id=query.clsnID;result.triangle=query.triangleID;result.slot=query.flags;
    *hit=result;return 1;
}

extern "C" dBgW* data_020a0c80[];
extern "C" const unsigned char scale_y_vtable[] asm("_ZTV14dBgW_KcMbgSclY") __attribute__((weak));
extern "C" const unsigned char static_kcl_vtable[] asm("_ZTV7dBgW_Kc") __attribute__((weak));
extern "C" const unsigned char moving_kcl_vtable[] asm("_ZTV10dBgW_KcMbg") __attribute__((weak));
extern "C" void func_0203aa10(dBgW_KcMbgSclY*,const Vector3*,Vector3*);
static_assert(sizeof(DsWorldPrism)==112,"Fixed-width prism ABI");
// Some actors register the same enabled KCL owner twice. Its stored slot is
// the canonical entry; aliases must point to that exact live owner, never to
// a disabled/stale object. Enumerate its geometry once while accepting the
// original DS query's alias identity for contact validation.
static bool registry_entry(unsigned slot,const dBgW* collider) {
 return collider && slot<24 && data_020a0c80[slot]==collider &&
  collider->slotIdx<24 && data_020a0c80[collider->slotIdx]==collider;
}

static int read_prism(unsigned slot,unsigned triangle,const DsWorldGroundHit* expected,
                      DsWorldGroundHit* hit,DsWorldPrism* prism) {
    if(!prism || !triangle || triangle>=32768 || slot>=24)return 0;
    auto* collider=data_020a0c80[slot];
    if(!registry_entry(slot,collider))return 0;
    if(expected && (uintptr_t(collider)!=expected->collider ||
       uintptr_t(collider->owner)!=expected->actor || collider->ownerUniqueID!=expected->owner_id))return 0;
    uintptr_t vt;std::memcpy(&vt,collider,sizeof vt);
    DsWorldPrism result{};
    if(static_kcl_vtable && vt==uintptr_t(static_kcl_vtable)+8)result.kind=0;
    else if(moving_kcl_vtable && vt==uintptr_t(moving_kcl_vtable)+8)result.kind=1;
    else if(scale_y_vtable && vt==uintptr_t(scale_y_vtable)+8)result.kind=2;
    else return 0;
    auto* mesh=static_cast<dBgW_Kc*>(collider);auto* file=mesh->kclFile;
    if(!file || !file->positions || !file->normals || !file->tris || !file->unk_0c || !mesh->clps.ptr)return 0;
    const uintptr_t p=uintptr_t(file->positions),n=uintptr_t(file->normals),
        t=uintptr_t(file->tris),o=uintptr_t(file->unk_0c);
    // Triangle zero is the biased leading record, overlapping the normals.
    if(p>=n || t>UINTPTR_MAX-16 || n>t+16 || t>=o || (o-t)%16 ||
       (o-t)/16>32768 || triangle>=(o-t)/16)return 0;
    const auto& tri=file->tris[triangle];
    if(uintptr_t(tri.posIdx)*12+12>n-p)return 0;
    const uint16_t indices[]={tri.normalIdx,tri.edgeNormal1Idx,tri.edgeNormal2Idx,tri.edgeNormal3Idx};
    for(unsigned i=0;i<4;++i)if(uintptr_t(indices[i])*6+6>t+16-n)return 0;
    uint16_t header[2];std::memcpy(header,reinterpret_cast<const char*>(mesh->clps.ptr)+4,4);
    if(header[0]!=8 || tri.attribute>=header[1])return 0;
    SurfaceInfo surface;collider->GetSurfaceInfo(int16_t(triangle),surface);
    if(expected && (surface.clps.w0!=expected->clps[0] || surface.clps.w1!=expected->clps[1] ||
       int(surface.normal.x)!=expected->normal[0] || int(surface.normal.y)!=expected->normal[1] ||
       int(surface.normal.z)!=expected->normal[2]))return 0;
    result.extent=tri.length;result.attribute=tri.attribute;result.kcl=uint32_t(uintptr_t(file));
    std::memcpy(result.anchor,file->positions[tri.posIdx],sizeof result.anchor);
    for(unsigned i=0;i<4;++i)std::memcpy(result.normal[i],file->normals[indices[i]],6);
    result.scale_y=4096;
    if(!result.kind)result.transform[0]=result.transform[4]=result.transform[8]=4096;
    else {
        auto* moving=static_cast<dBgW_KcMbg*>(collider);
        std::memcpy(result.transform,&moving->newScaledMat,sizeof result.transform);
        if(result.kind==2)result.scale_y=int(static_cast<dBgW_KcMbgSclY*>(collider)->scaleY);
    }
    Vector3 origin;
    if(result.kind==2) {
        Vector3 local;int* xyz[]={&local.x,&local.y,&local.z};
        for(unsigned i=0;i<3;++i) {
            const int64_t value=int64_t(result.anchor[i])*64;
            if(value<INT_MIN || value>INT_MAX)return 0;
            *xyz[i]=int(value);
        }
        func_0203aa10(static_cast<dBgW_KcMbgSclY*>(collider),&local,&origin);
    } else collider->GetTriangleOrigin(int16_t(triangle),origin);
    result.world_anchor[0]=int(origin.x);result.world_anchor[1]=int(origin.y);result.world_anchor[2]=int(origin.z);
    if(hit) {
        DsWorldGroundHit current{};
        current.normal[0]=int(surface.normal.x);current.normal[1]=int(surface.normal.y);current.normal[2]=int(surface.normal.z);
        current.clps[0]=surface.clps.w0;current.clps[1]=surface.clps.w1;
        current.collider=uint32_t(uintptr_t(collider));current.actor=uint32_t(uintptr_t(collider->owner));
        current.owner_id=collider->ownerUniqueID;current.triangle=uint16_t(triangle);current.slot=uint16_t(slot);
        *hit=current;
    }
    *prism=result;return 1;
}
extern "C" int sm64ds_contact_prism(const DsWorldGroundHit* contact,DsWorldPrism* prism) {
    return contact && contact->collider?read_prism(contact->slot,contact->triangle,contact,nullptr,prism):0;
}
extern "C" int sm64ds_world_prism_at(unsigned slot,unsigned triangle,DsWorldGroundHit* hit,DsWorldPrism* prism) {
    return hit?read_prism(slot,triangle,nullptr,hit,prism):0;
}
extern "C" int sm64ds_world_prism_count(unsigned slot) {
    if(slot>=24)return -1;
    auto* collider=data_020a0c80[slot];if(!collider)return 0;
    if(!registry_entry(slot,collider))return -1;
    if(collider->slotIdx!=slot)return 0; // exact duplicate, canonical slot owns the mesh
    uintptr_t vt;std::memcpy(&vt,collider,sizeof vt);
    if(!((static_kcl_vtable && vt==uintptr_t(static_kcl_vtable)+8) ||
         (moving_kcl_vtable && vt==uintptr_t(moving_kcl_vtable)+8) ||
         (scale_y_vtable && vt==uintptr_t(scale_y_vtable)+8)))return -1;
    auto* mesh=static_cast<dBgW_Kc*>(collider);auto* file=mesh->kclFile;
    if(!file || !file->tris || !file->unk_0c)return -1;
    const uintptr_t start=uintptr_t(file->tris),end=uintptr_t(file->unk_0c);
    if(start>=end || (end-start)%16 || (end-start)/16>32768)return -1;
    return int((end-start)/16)-1;
}
extern "C" int func_02035354(void*,void*);
extern "C" int sm64ds_platform_displace(const DsWorldPlatformRef* platform,
                                        const int32_t position[3],int32_t result[4]) {
    if(!platform || !position || !result || platform->slot>=24 ||
       platform->kind<1 || platform->kind>2 || !platform->collider || !platform->kcl)return 0;
    auto* collider=data_020a0c80[platform->slot];
    if(!registry_entry(platform->slot,collider) || uintptr_t(collider)!=platform->collider ||
       uintptr_t(collider->owner)!=platform->actor || collider->ownerUniqueID!=platform->owner_id)return 0;
    uintptr_t vt;std::memcpy(&vt,collider,sizeof vt);
    if(platform->kind==1?(!moving_kcl_vtable || vt!=uintptr_t(moving_kcl_vtable)+8):
                         (!scale_y_vtable || vt!=uintptr_t(scale_y_vtable)+8))return 0;
    auto* moving=static_cast<dBgW_KcMbg*>(collider);
    if(uintptr_t(moving->kclFile)!=platform->kcl)return 0;
    Vector3 input{int(position[0]),int(position[1]),int(position[2])},output;
    if(!collider->TransformPos(input,output))return 0;
    const int32_t staged[]={int(output.x),int(output.y),int(output.z),int(collider->GetAngularVelY())};
    std::memcpy(result,staged,sizeof staged);return 1;
}
extern "C" int sm64ds_contact_pass_through(const DsWorldGroundHit* contact,void* actor,uint8_t detect_flags,uint8_t wall) {
    DsWorldPrism prism;
    if(!sm64ds_contact_prism(contact,&prism))return -1;
    dBgCh_Gnd query;const Vector3 position{0,0,0};
    query.SetObjAndPos(position,static_cast<dActor_c*>(actor));query.mDetectFlags=detect_flags;
    auto* collider=data_020a0c80[contact->slot];
    if(func_02035354(&query,collider->owner))return 1;
    const CLPS clps{contact->clps[0],contact->clps[1]};
    return dBgCh::ShouldPassThroughImpl(collider,clps,query,wall!=0)?1:0;
}
extern "C" int sm64ds_contact_plane_height(const DsWorldLineHit* contact,
                                            const int32_t position[3],double* height) {
    if (!contact || !position || !height || !contact->normal[1] ||
        !contact->collider || !contact->triangle || contact->triangle>=32768 || contact->slot>=24) return 0;
    auto* collider=data_020a0c80[contact->slot];
    if (uintptr_t(collider)!=contact->collider || !collider ||
        uintptr_t(collider->owner)!=contact->actor || collider->ownerUniqueID!=contact->owner_id) return 0;
    Vector3 origin;
    uintptr_t vtable;std::memcpy(&vtable,collider,sizeof vtable);
    if(scale_y_vtable && vtable==uintptr_t(scale_y_vtable)+8) {
        // SclY inherits GetTriangleOrigin without its extra Y scale. Its
        // actual collision path uses func_0203aa10; follow that path here.
        auto* scaled=static_cast<dBgW_KcMbgSclY*>(collider);auto* file=scaled->kclFile;
        if(!file || !file->positions || !file->normals || !file->tris || !file->unk_0c)return 0;
        const uintptr_t positions=uintptr_t(file->positions),normals=uintptr_t(file->normals),
            triangles=uintptr_t(file->tris),octree=uintptr_t(file->unk_0c);
        if(positions>=normals || triangles>=octree || (octree-triangles)%16 ||
            contact->triangle>=(octree-triangles)/16)return 0;
        const auto& tri=file->tris[contact->triangle];
        if(uintptr_t(tri.posIdx)*12+12>normals-positions)return 0;
        Vector3 local;
        int* components[]={&local.x,&local.y,&local.z};
        for(unsigned i=0;i<3;++i) {
            const int64_t value=int64_t(file->positions[tri.posIdx][i])*64;
            if(value<INT_MIN || value>INT_MAX)return 0;
            *components[i]=int(value);
        }
        func_0203aa10(scaled,&local,&origin);
    } else collider->GetTriangleOrigin(int16_t(contact->triangle),origin);
    const double numerator=double(int64_t(position[0])-int(origin.x))*contact->normal[0]+
                           double(int64_t(position[2])-int(origin.z))*contact->normal[2];
    *height=double(int(origin.y))-numerator/contact->normal[1];
    return 1;
}

extern "C" int sm64ds_motion_ceiling(const int32_t world_position[3],void* actor,
                                      DsWorldGroundHit* hit) {
    if (!world_position || !hit) return -1;
    constexpr int32_t upper=100000*4096;
    int32_t start[3],end[3];
    for (unsigned i=0;i<3;++i) {
        const int64_t value=int64_t(world_position[i])*4096;
        if (value<INT_MIN || value>INT_MAX) return -1;
        start[i]=end[i]=int32_t(value);
    }
    const int64_t lower=int64_t(start[1])-78*4096;
    if (lower>=upper) return 0;
    if (lower<INT_MIN+64) return -1;
    start[1]=int32_t(lower)-64;end[1]=upper;
    for (unsigned i=0;i<3;++i) {
        const int64_t sum=int64_t(start[i])+end[i],delta=int64_t(end[i])-start[i];
        if (sum<INT_MIN || sum>INT_MAX || delta<INT_MIN || delta>INT_MAX) return -1;
    }
    uint32_t last_collider=0;uint16_t last_triangle=0;
    while (start[1]<upper) {
        DsWorldLineHit contact;
        if (!sm64ds_world_line(start,end,actor,&contact)) return 0;
        double plane;
        if (!sm64ds_contact_plane_height(&contact,start,&plane) || plane<INT_MIN || plane>INT_MAX) return -1;
        if (contact.normal[1]<=-41 && plane>=double(lower) && plane<upper) {
            DsWorldGroundHit result{};
            result.height=int32_t(plane+(plane>=0?0.5:-0.5));
            for (unsigned i=0;i<3;++i) result.normal[i]=contact.normal[i];
            for (unsigned i=0;i<2;++i) result.clps[i]=contact.clps[i];
            result.collider=contact.collider;result.actor=contact.actor;result.owner_id=contact.owner_id;
            result.triangle=contact.triangle;result.slot=contact.slot;
            *hit=result;return 1;
        }
        // Skip a plane just below the admission boundary, or one too close
        // to vertical for N64 ceiling classification. Require forward progress;
        // stale/quantized repeats are an explicit error, not an infinite loop.
        if (contact.collider==last_collider && contact.triangle==last_triangle) return -1;
        last_collider=contact.collider;last_triangle=contact.triangle;
        int64_t next=int64_t(plane)+128;
        if (next<=start[1]) next=int64_t(start[1])+64;
        if (next>=upper) return 0;
        start[1]=int32_t(next);
    }
    return 0;
}
