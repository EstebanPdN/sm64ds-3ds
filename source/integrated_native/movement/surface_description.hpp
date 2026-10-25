#pragma once
#include "world_triangle.hpp"
#include "material_profile.hpp"
#include "stable_surfaces.hpp"
#include <algorithm>

namespace movement_geometry {
enum class SurfaceDescribe {Ready,InvalidContact,InvalidGeometry,Water,NeedsDsEffect,UnknownFriction,NeedsPlatform};
struct SurfaceDescription {
    SurfaceKey key{};
    SM64SurfaceCollisionData surface{};
    MaterialProfile material{};
    WorldTriangle triangle{};
};
// Input is a fresh validated DS prism/contact pair. The returned record is a
// value; WorldSession still copies it into stable, retained-contact storage.
// A moving surface requires a separately owned stable platform handle. This
// function neither allocates that owner nor grants it Euler-transform parity.
inline SurfaceDescribe describe_surface(const DsWorldGroundHit& hit,const DsWorldPrism& prism,
                                        SM64SurfaceObjectTransform* platform,SurfaceDescription& output) {
    if(!hit.collider || !prism.kcl || !hit.triangle || hit.triangle>=32768 || hit.slot>=24)
        return SurfaceDescribe::InvalidContact;
    const auto profile=material_profile(hit.clps[0],hit.clps[1]);
    switch(profile.use) {
        case MaterialUse::Water:return SurfaceDescribe::Water;
        case MaterialUse::NeedsDsEffect:return SurfaceDescribe::NeedsDsEffect;
        case MaterialUse::UnknownFriction:return SurfaceDescribe::UnknownFriction;
        case MaterialUse::Solid:break;
    }
    WorldTriangle triangle;
    const auto decoded=decode_world(prism,hit.normal,triangle);
    if(decoded==WorldDecode::Degenerate)return SurfaceDescribe::NeedsDsEffect;
    if(decoded!=WorldDecode::Ok)return SurfaceDescribe::InvalidGeometry;
    if(prism.kind && !platform)return SurfaceDescribe::NeedsPlatform;
    if(!prism.kind && platform)return SurfaceDescribe::InvalidContact;
    SurfaceDescription candidate{};
    candidate.key={hit.collider,hit.actor,uint32_t(hit.owner_id),prism.kcl,hit.triangle,hit.slot};
    candidate.material=profile;candidate.triangle=triangle;
    auto& surface=candidate.surface;
    if(!apply_locomotion_material(profile,surface))return SurfaceDescribe::InvalidContact;
    surface.normal={hit.normal[0]/4096.f,hit.normal[1]/4096.f,hit.normal[2]/4096.f};
    const auto anchor=triangle.plane_anchor;
    surface.originOffset=-(surface.normal.x*float(anchor.x)+surface.normal.y*float(anchor.y)+surface.normal.z*float(anchor.z));
    if(!std::isfinite(surface.originOffset))return SurfaceDescribe::InvalidGeometry;
    surface.transform=platform;
    surface.flags=prism.kind?SURFACE_FLAG_DYNAMIC:0;
    if(surface.normal.x<-.707f || surface.normal.x>.707f)surface.flags|=SURFACE_FLAG_X_PROJECTION;
    // Compatibility fields only. Floor/ceiling admission uses the original DS
    // queries; wall tests use fractional WorldTriangle vertices, not these
    // rounded integer copies. Never feed these back to the legacy mesh path.
    int32_t* vertices[]={surface.vertex1,surface.vertex2,surface.vertex3};
    double lower=triangle.geometry.vertex[0].y,upper=lower;
    for(unsigned i=0;i<3;++i) {
        const auto v=triangle.geometry.vertex[i];
        vertices[i][0]=int32_t(std::round(v.x));vertices[i][1]=int32_t(std::round(v.y));vertices[i][2]=int32_t(std::round(v.z));
        lower=std::min(lower,v.y);upper=std::max(upper,v.y);
    }
    surface.lowerY=int32_t(std::floor(lower))-5;surface.upperY=int32_t(std::ceil(upper))+5;
    // DS parameter is not an N64 room ID. Keep it in material metadata; the
    // DS scene/gameplay owner continues to own rooms and progression.
    surface.room=0;surface.isValid=1;
    output=candidate;return SurfaceDescribe::Ready;
}
}
