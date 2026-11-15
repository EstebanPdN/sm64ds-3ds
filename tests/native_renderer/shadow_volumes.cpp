#include "shadow_volumes.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>
using namespace sm64ds::render;
namespace {
struct P {float x,y,z;};
P point(const Vertex& v){return {v.x,v.y,v.z};}
P sub(P a,P b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
P cross(P a,P b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float dot(P a,P b){return a.x*b.x+a.y*b.y+a.z*b.z;}
using Key=std::tuple<long,long,long>;
Key key(const Vertex& v){return {std::lround(v.x*65536),std::lround(v.y*65536),std::lround(v.z*65536)};}
// Closed and wound outwards: every triangle faces away from the centre and
// every directed edge is matched by exactly one opposite edge.
void closed_outwards(const Vertex* quads,std::size_t vertices,P centre){
    assert(vertices&&vertices%4==0);
    std::map<std::pair<Key,Key>,int> edges;
    for(std::size_t q=0;q<vertices;q+=4){
        constexpr unsigned pattern[2][3]={{0,1,2},{0,2,3}};
        for(const auto& t:pattern){
            const P a=point(quads[q+t[0]]),b=point(quads[q+t[1]]),c=point(quads[q+t[2]]);
            const P normal=cross(sub(b,a),sub(c,a));
            const P middle{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};
            assert(dot(normal,sub(middle,centre))>0);
            for(unsigned e=0;e<3;++e)++edges[{key(quads[q+t[e]]),key(quads[q+t[(e+1)%3]])}];
        }
    }
    for(const auto& [edge,count]:edges){
        assert(count==1);
        const auto opposite=edges.find({edge.second,edge.first});
        assert(opposite!=edges.end()&&opposite->second==1);
    }
}
}
int main(){
    std::vector<Vertex> out(max_shadow_vertices);
    ShadowVolume cylinder;cylinder.pose={1,0,0,0,1,0,0,0,1,0,0,0};cylinder.opacity=31;
    auto count=build_shadow_quads(&cylinder,1,out.data(),out.size());
    assert(count==shadow_cylinder_quads*4);
    closed_outwards(out.data(),count,{0,-1/16.f,0});
    float low=0,high=-1,radius=0;
    for(std::size_t i=0;i<count;++i){const auto& v=out[i];
        assert(v.r==0&&v.g==0&&v.b==0&&v.a==255);
        low=std::fmin(low,v.y);high=std::fmax(high,v.y);radius=std::fmax(radius,std::sqrt(v.x*v.x+v.z*v.z));}
    assert(low==-0.125f&&high==0&&radius==0.0625f);
    // A cuboid under a scaled, turned and moved pose (x -> z, z -> -x).
    ShadowVolume cuboid;cuboid.shape=ShadowVolume::Shape::cuboid;cuboid.opacity=1;
    cuboid.pose={0,0,16, 0,8,0, -16,0,0, 100,-50,-300};
    count=build_shadow_quads(&cuboid,1,out.data(),out.size());
    assert(count==shadow_cuboid_quads*4);
    closed_outwards(out.data(),count,{100,-50.5f,-300});
    for(std::size_t i=0;i<count;++i){const auto& v=out[i];
        assert(v.a==8&&std::fabs(v.x-100)<=1.0001f&&std::fabs(v.z+300)<=1.0001f&&v.y<=-50&&v.y>=-51);}
    // Several volumes share the stream; one that does not fit is left out whole.
    const std::array<ShadowVolume,3> three{cylinder,cuboid,cylinder};
    count=build_shadow_quads(three.data(),three.size(),out.data(),out.size());
    assert(count==(2*shadow_cylinder_quads+shadow_cuboid_quads)*4);
    assert(build_shadow_quads(three.data(),three.size(),out.data(),(shadow_cylinder_quads+shadow_cuboid_quads)*4+60)==(shadow_cylinder_quads+shadow_cuboid_quads)*4);
    assert(build_shadow_quads(three.data(),three.size(),out.data(),0)==0);
    std::vector<ShadowVolume> full(max_frame_shadows,cylinder);
    assert(build_shadow_quads(full.data(),full.size(),out.data(),out.size())==max_shadow_vertices);
    std::puts("Shadow volumes: closed prisms wound outwards, posed in view space, one quad stream for all");
}
