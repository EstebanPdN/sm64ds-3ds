#include "game_sketch_geometry.hpp"
#include <cassert>
#include <cmath>
using namespace sm64ds::render;
using namespace sm64ds::render::game;
static int angle(int y,int x){return int(std::atan2(double(y),double(x))*65536/(2*std::acos(-1.)));}
int main(){
    std::array<std::int16_t,8192> table{};
    for(unsigned i=0;i<4096;++i){const auto a=i*2*std::acos(-1.)/4096;table[i*2]=std::int16_t(std::lround(std::sin(a)*4096));table[i*2+1]=std::int16_t(std::lround(std::cos(a)*4096));}
    SketchPoint points[]={{{0,0,4096}},{{8192,0,4096}},{{8192,8192,4096}}};
    SketchStroke s{points,2,1024,4096,31,31,angle,table.data()};Vertex v[8]{};std::size_t n=999;
    assert(sketch_geometry(s,v,8,n)==Error::none&&n==4);
    assert(v[0].x==0&&v[0].y==.25f&&v[1].y==-.25f);
    assert(v[2].x==2&&v[2].y==-.25f&&v[3].y==.25f&&v[3].z==1);
    assert(v[0].r==255&&v[0].g==0&&v[0].b==0&&v[0].a==255);
    s.progress=2048;assert(sketch_geometry(s,v,8,n)==Error::none&&v[2].x==1);
    s.progress=-1;assert(sketch_geometry(s,v,8,n)==Error::none&&v[2].x==0);
    s.progress=8192;assert(sketch_geometry(s,v,8,n)==Error::none&&v[2].x==2);
    s.count=1;assert(sketch_geometry(s,v,8,n)==Error::none&&n==4);
    assert(v[0].x==-.25f&&v[0].y==.25f&&v[2].x==.25f&&v[2].y==-.25f);
    s.count=3;s.progress=4096;assert(sketch_geometry(s,v,8,n)==Error::none&&n==8);
    assert(v[3].x<2&&v[3].y>0); // bisector at the corner
    assert(v[4].x==v[3].x&&v[4].y==v[3].y); // strip continuity
    const auto saved=v[0];n=777;
    assert(sketch_geometry(s,v,7,n)==Error::capacity&&n==777&&v[0].x==saved.x);
    s.points=nullptr;assert(sketch_geometry(s,v,8,n)==Error::invalid_data);
    s.count=0;assert(sketch_geometry(s,nullptr,0,n)==Error::none&&n==0);
    // Wrapped tangents near -pi/pi must point along their common normal.
    SketchPoint wrap[]={{{8192,-16,0}},{{0,0,0}},{{-8192,-16,0}}};s.points=wrap;s.count=3;
    assert(sketch_geometry(s,v,8,n)==Error::none&&v[3].y<-.24f);
}
