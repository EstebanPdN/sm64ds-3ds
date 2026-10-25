#pragma once
#include "world_triangle.hpp"
#include <algorithm>

namespace movement_geometry {
struct WallCylinder {float x,y,z,offset_y,radius;};
struct WallPush {float x,z;};
enum class WallContact {NeedsDs=-2,Invalid=-1,Missing=0,Hit=1};
// N64 cylinder-versus-wall admission, using fractional DS world vertices.
// A caller accumulates each push against the SAME original cylinder center,
// matching N64's multi-wall semantics. This narrow phase does not enumerate
// candidates, apply DS actor/pass-through filters or own stable contacts.
inline WallContact wall_contact(const WorldTriangle& wall,const WallCylinder& cylinder,WallPush& output) {
    const auto& triangle=wall.geometry;const auto n=triangle.normal;
    for(double value:{double(cylinder.x),double(cylinder.y),double(cylinder.z),double(cylinder.offset_y),double(cylinder.radius),n.x,n.y,n.z,wall.plane_anchor.x,wall.plane_anchor.y,wall.plane_anchor.z})
        if(!std::isfinite(value))return WallContact::Invalid;
    if(cylinder.radius<0)return WallContact::Invalid;
    for(const auto& v:triangle.vertex)for(double value:{v.x,v.y,v.z})
        if(!std::isfinite(value))return WallContact::Invalid;
    const double norm=dot(n,n);
    if(norm<.25 || norm>2.25)return WallContact::Invalid;
    if(n.y<-.01 || n.y>.01)return WallContact::Missing;
    const float y=cylinder.y+cylinder.offset_y;
    if(!std::isfinite(y))return WallContact::Invalid;
    double lower=triangle.vertex[0].y,upper=lower;
    for(const auto& v:triangle.vertex){lower=std::min(lower,v.y);upper=std::max(upper,v.y);}
    if(wall.ds_only){
        // Keep zero-area DS prisms as original geometry, not a vanished wall.
        // A conservative cylinder/segment bound hands nearby movement to DS;
        // its sphere query retains the original edge/vertex response.
        double minx=triangle.vertex[0].x,maxx=minx,minz=triangle.vertex[0].z,maxz=minz;
        for(const auto& v:triangle.vertex){minx=std::min(minx,v.x);maxx=std::max(maxx,v.x);minz=std::min(minz,v.z);maxz=std::max(maxz,v.z);}
        const double radius=std::min(cylinder.radius,200.f);
        return y<lower-radius||y>upper+radius||cylinder.x<minx-radius||cylinder.x>maxx+radius||cylinder.z<minz-radius||cylinder.z>maxz+radius?WallContact::Missing:WallContact::NeedsDs;
    }
    if(y<lower-5 || y>upper+5)return WallContact::Missing;
    const float nx=float(n.x),ny=float(n.y),nz=float(n.z);
    const float origin=-(nx*float(wall.plane_anchor.x)+ny*float(wall.plane_anchor.y)+nz*float(wall.plane_anchor.z));
    const float offset=nx*cylinder.x+ny*y+nz*cylinder.z+origin;
    const float radius=std::min(cylinder.radius,200.f);
    if(!std::isfinite(offset))return WallContact::Invalid;
    if(offset< -radius || offset>radius)return WallContact::Missing;
    const bool project_x=nx<-.707f || nx>.707f;
    const double coordinate=project_x?-double(cylinder.z):double(cylinder.x);
    const bool positive=(project_x?nx:nz)>0;
    double projected_area=0;
    for(unsigned i=0;i<3;++i) {
        const auto a=triangle.vertex[i],b=triangle.vertex[(i+1)%3];
        const double wa=project_x?-a.z:a.x,wb=project_x?-b.z:b.x;
        const double edge=(a.y-y)*(wb-wa)-(wa-coordinate)*(b.y-a.y);
        projected_area+=a.y*wb-wa*b.y;
        if(positive?edge>0:edge<0)return WallContact::Missing;
    }
    if(std::abs(projected_area)<1e-12)return WallContact::Invalid;
    const WallPush push{nx*(radius-offset),nz*(radius-offset)};
    if(!std::isfinite(push.x) || !std::isfinite(push.z))return WallContact::Invalid;
    output=push;return WallContact::Hit;
}
}
