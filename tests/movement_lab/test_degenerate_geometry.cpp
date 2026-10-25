#include "wall_contact.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace movement_geometry;
int main(){
 // Original ARM sm64ds_world_prism_at output for Whomp triangle375:
 // capture308210c0...3f, candidatec9b3818befd4c551. Exact source normals
 // have zero determinant; host FMA used to produce a tiny nonzero area.
 DsWorldPrism p{};p.extent=170424832;p.anchor[0]=201600;p.anchor[1]=144000;p.anchor[2]=-32000;
 const int16_t normals[4][3]={{1024,0,0},{0,923,-443},{0,-923,443},{0,-95,-1020}};
 std::memcpy(p.normal,normals,sizeof normals);p.attribute=21;p.kcl=0x2000;p.scale_y=4096;
 p.transform[0]=p.transform[4]=p.transform[8]=4096;for(unsigned i=0;i<3;++i)p.world_anchor[i]=p.anchor[i]*64;
 const int32_t normal[]={4096,0,0};WorldTriangle decoded;assert(decode_world(p,normal,decoded)==WorldDecode::Degenerate&&decoded.ds_only);
 WallCylinder c{};c.radius=50;for(const auto& v:decoded.geometry.vertex){c.x+=float(v.x/3);c.y+=float(v.y/3);c.z+=float(v.z/3);}
 WallPush output{12345,67890};assert(wall_contact(decoded,c,output)==WallContact::NeedsDs&&output.x==12345&&output.z==67890);
 c.x+=10000;c.z+=10000;assert(wall_contact(decoded,c,output)==WallContact::Missing&&output.x==12345&&output.z==67890);
 p.extent=0;assert(decode_world(p,normal,decoded)==WorldDecode::InvalidPrism);
 std::puts("Whomp prism375: exact degeneracy independent of FMA; near DS ownership, far native traversal, staged output preserved");
}
