#pragma once
#include "scene_frame.hpp"

namespace sm64ds::render {
// The game's two built-in shadow volumes (ShadowModel::InitCylinder and
// InitCuboid): an octagonal and a square prism of half-width 1/16 from
// y=-1/8 to y=0, as their own display lists draw them. Each volume becomes
// view-space quads whose faces wind counter-clockwise seen from outside (the
// DS front faces), ready for one dynamic quad stream: perimeter order
// v0..v3, drawn as the triangles (0,1,2) and (0,2,3).
constexpr std::size_t shadow_cylinder_quads=16,shadow_cuboid_quads=8;
constexpr std::size_t max_shadow_vertices=max_frame_shadows*shadow_cylinder_quads*4;
// Returns the vertices written (a multiple of four); volumes that do not fit
// in `capacity` are left out whole.
inline std::size_t build_shadow_quads(const ShadowVolume* volume,std::size_t count,Vertex* out,std::size_t capacity) noexcept {
    constexpr float r=256/4096.f,d=181/4096.f,low=-512/4096.f;
    struct Ring {unsigned points;float xz[8][2];};
    // Increasing angle with x=cos, z=sin.
    static constexpr Ring rings[2]={{8,{{r,0},{d,d},{0,r},{-d,d},{-r,0},{-d,-d},{0,-r},{d,-d}}},
                                    {4,{{r,r},{-r,r},{-r,-r},{r,-r},{0,0},{0,0},{0,0},{0,0}}}};
    std::size_t used=0;
    for(std::size_t s=0;s<count;++s){const auto& v=volume[s];
        const auto& ring=rings[v.shape==ShadowVolume::Shape::cuboid];const unsigned n=ring.points;
        if(capacity-used<std::size_t(n+n)*4)break; // n sides, n/2 quads per cap
        const auto alpha=std::uint8_t((unsigned(v.opacity)*255+15)/31);
        const auto place=[&](float x,float y,float z){
            const auto& m=v.pose;
            return Vertex{m[0]*x+m[3]*y+m[6]*z+m[9],m[1]*x+m[4]*y+m[7]*z+m[10],m[2]*x+m[5]*y+m[8]*z+m[11],0,0,0,0,0,alpha};
        };
        Vertex bottom[8],top[8];
        for(unsigned i=0;i<n;++i){bottom[i]=place(ring.xz[i][0],low,ring.xz[i][1]);top[i]=place(ring.xz[i][0],0,ring.xz[i][1]);}
        const Vertex bottom_centre=place(0,low,0),top_centre=place(0,0,0);
        const auto quad=[&](const Vertex& a,const Vertex& b,const Vertex& c,const Vertex& e){out[used++]=a;out[used++]=b;out[used++]=c;out[used++]=e;};
        for(unsigned i=0;i<n;++i){const unsigned j=(i+1)%n;quad(bottom[i],top[i],top[j],bottom[j]);}
        for(unsigned i=0;i<n;i+=2){const unsigned j=i+1,k=(i+2)%n;
            quad(bottom_centre,bottom[i],bottom[j],bottom[k]);
            quad(top_centre,top[k],top[j],top[i]);
        }
    }
    return used;
}
}
