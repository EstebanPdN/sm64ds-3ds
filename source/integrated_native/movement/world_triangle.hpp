#pragma once
#include "ds_world_query.h"
#include "kcl_prism.hpp"
#include <cstring>

namespace movement_geometry {
// Fractional vertices serve wall edge tests. Keep the original query normal
// and rounded world anchor separately: replacing either with a reconstructed
// normalized plane would change DS contact admission at fixed-point borders.
struct WorldTriangle {Triangle geometry;Vec3 plane_anchor;bool ds_only=false;};
enum class WorldDecode {Ok,UnsupportedKind,InvalidScale,InvalidPrism,InvalidNormal,InvalidTransform,Degenerate};
inline WorldDecode decode_world(const DsWorldPrism& input,const int32_t world_normal[3],WorldTriangle& output) {
    if(input.kind>2)return WorldDecode::UnsupportedKind;
    if(input.scale_y<=0 || (input.kind!=2 && input.scale_y!=4096))return WorldDecode::InvalidScale;
    Prism prism{};prism.extent=input.extent;prism.attribute=input.attribute;
    std::memcpy(prism.anchor,input.anchor,sizeof prism.anchor);
    std::memcpy(prism.normal,input.normal,sizeof prism.normal);
    Triangle local;const auto decoded=decode(prism,local);
    if(decoded!=Decode::Ok&&decoded!=Decode::Degenerate)return WorldDecode::InvalidPrism;
    const Vec3 normal{world_normal[0]/4096.,world_normal[1]/4096.,world_normal[2]/4096.};
    const double length2=dot(normal,normal);
    if(!std::isfinite(length2) || length2<.25 || length2>2.25)return WorldDecode::InvalidNormal;
    WorldTriangle result{};result.geometry.attribute=input.attribute;result.geometry.normal=normal;
    result.plane_anchor={input.world_anchor[0]/4096.,input.world_anchor[1]/4096.,input.world_anchor[2]/4096.};
    for(unsigned i=0;i<3;++i) {
        const double point[]={local.vertex[i].x,local.vertex[i].y*input.scale_y/4096.,local.vertex[i].z};
        double world[3];
        for(unsigned row=0;row<3;++row) {
            world[row]=input.transform[9+row]/4096.;
            for(unsigned col=0;col<3;++col)world[row]+=input.transform[row+3*col]*point[col]/4096.;
            if(!std::isfinite(world[row]) || world[row]<INT32_MIN/4096. || world[row]>INT32_MAX/4096.)return WorldDecode::InvalidTransform;
        }
        result.geometry.vertex[i]={world[0],world[1],world[2]};
    }
    const auto a=result.geometry.vertex[0];
    const auto face=cross(subtract(result.geometry.vertex[1],a),subtract(result.geometry.vertex[2],a));
    const double winding=dot(face,normal);
    if(!std::isfinite(winding))return WorldDecode::InvalidTransform;
    if(decoded==Decode::Degenerate||std::abs(winding)<1e-12){result.ds_only=true;output=result;return WorldDecode::Degenerate;}
    if(winding<0)std::swap(result.geometry.vertex[1],result.geometry.vertex[2]);
    output=result;return WorldDecode::Ok;
}
}
