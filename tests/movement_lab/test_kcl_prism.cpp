#include "kcl_prism.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace movement_geometry;
int main(int argc,char** argv){
 Prism p{};p.extent=100*65536;p.anchor[0]=-64;p.anchor[1]=128;p.anchor[2]=192;
 const int16_t normals[4][3]={{0,1024,0},{0,0,1024},{-1024,0,0},{1024,0,1024}};std::memcpy(p.normal,normals,sizeof normals);p.attribute=21;
 Triangle t{};assert(decode(p,t)==Decode::Ok);
 assert(t.vertex[0].x==-1&&t.vertex[0].y==2&&t.vertex[0].z==3&&t.attribute==21);
 assert(t.vertex[1].x==-1&&t.vertex[1].z==103&&t.vertex[2].x==99&&t.vertex[2].z==3&&t.normal.y==1);
 auto before=t;p.extent=0;assert(decode(p,t)==Decode::NonpositiveExtent&&std::memcmp(&t,&before,sizeof t)==0);
 p.extent=65536;std::memset(p.normal[0],0,6);assert(decode(p,t)==Decode::InvalidNormal);
 std::memcpy(p.normal,normals,sizeof normals);p.normal[3][0]=p.normal[3][2]=0;assert(decode(p,t)==Decode::ParallelEdges);
 if(argc==2){
  FILE* f=std::fopen(argv[1],"rb");assert(f);uint32_t header[2];assert(std::fread(header,8,1,f)==1&&header[0]==0x314c434d&&header[1]<=65536);
  unsigned errors=0;for(unsigned i=0;i<header[1];++i){
   unsigned char raw[44];assert(std::fread(raw,44,1,f)==1);Prism item{};
   std::memcpy(&item.extent,raw,4);std::memcpy(item.anchor,raw+4,12);std::memcpy(item.normal,raw+16,24);std::memcpy(&item.attribute,raw+40,2);
   Triangle out{};if(decode(item,out)!=Decode::Ok){++errors;continue;}
   const Vec3 a=out.vertex[0];
   for(const auto& point:out.vertex)assert(std::abs(dot(out.normal,subtract(point,a)))<1e-6);
   assert(dot(cross(subtract(out.vertex[1],a),subtract(out.vertex[2],a)),out.normal)>0);
   assert(out.attribute==item.attribute);
  }
  assert(std::fgetc(f)==EOF);std::fclose(f);std::printf("Original captured KCL prisms=%u invalid=%u; precision/plane/winding/attribute checks\n",header[1],errors);assert(!errors);
 }
}
