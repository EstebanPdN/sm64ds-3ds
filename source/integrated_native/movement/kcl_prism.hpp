#pragma once
// Converts one already-bounds-checked original DS KCL prism. No allocation,
// integer-coordinate rounding, CLPS remapping or collision ownership changes.
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <utility>
namespace movement_geometry {
struct Prism {int32_t extent;int32_t anchor[3];int16_t normal[4][3];uint16_t attribute;};
struct Vec3 {double x,y,z;};
struct Triangle {Vec3 vertex[3],normal;uint16_t attribute;};
enum class Decode {Ok,NonpositiveExtent,InvalidNormal,ParallelEdges,InvalidCoordinates,Degenerate};
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 subtract(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 offset(Vec3 a,Vec3 delta,double scale){return {a.x+delta.x*scale,a.y+delta.y*scale,a.z+delta.z*scale};}
inline Decode decode(const Prism& input,Triangle& output){
 if(input.extent<=0)return Decode::NonpositiveExtent;
 Vec3 n[4];for(unsigned i=0;i<4;++i)n[i]={input.normal[i][0]/1024.,input.normal[i][1]/1024.,input.normal[i][2]/1024.};
 const double norm=std::sqrt(dot(n[0],n[0]));if(norm<1e-9)return Decode::InvalidNormal;
 const auto a=Vec3{input.anchor[0]/64.,input.anchor[1]/64.,input.anchor[2]/64.};
 const auto u=cross(n[0],n[1]),v=cross(n[0],n[2]);const double du=dot(u,n[3]),dv=dot(v,n[3]);
 if(std::abs(du)<1e-12||std::abs(dv)<1e-12)return Decode::ParallelEdges;
 Triangle candidate{{a,offset(a,u,(input.extent/65536.)/du),offset(a,v,(input.extent/65536.)/dv)},
                    {n[0].x/norm,n[0].y/norm,n[0].z/norm},input.attribute};
 for(const auto& point:candidate.vertex)for(double value:{point.x,point.y,point.z})
  if(!std::isfinite(value)||std::abs(value)>2147483647.)return Decode::InvalidCoordinates;
 const auto face=cross(subtract(candidate.vertex[1],a),subtract(candidate.vertex[2],a));
 // Exact integer determinant: mathematically parallel prism edges can
 // acquire a tiny nonzero area under host FMA but zero area on ARM/VFP.
 // Their source identity, not floating rounding, decides DS delegation.
 const auto& a0=input.normal[0];const auto& a1=input.normal[1];const auto& a2=input.normal[2];
 const int64_t determinant=int64_t(a0[0])*(int64_t(a1[1])*a2[2]-int64_t(a1[2])*a2[1])+
  int64_t(a0[1])*(int64_t(a1[2])*a2[0]-int64_t(a1[0])*a2[2])+
  int64_t(a0[2])*(int64_t(a1[0])*a2[1]-int64_t(a1[1])*a2[0]);
 const double winding=dot(face,n[0]);if(!determinant||std::abs(winding)<1e-12){output=candidate;return Decode::Degenerate;}
 if(winding<0)std::swap(candidate.vertex[1],candidate.vertex[2]);
 output=candidate;return Decode::Ok;
}
}
