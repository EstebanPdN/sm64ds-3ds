#include "wall_contact.hpp"
#include "libsm64.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <array>
extern "C" void fixture_wall_mesh(const SM64Surface*,unsigned);
extern "C" const SM64SurfaceCollisionData* fixture_wall_surface(unsigned);
extern "C" int fixture_original_walls(SM64WallCollisionData*);
using namespace movement_geometry;
static WorldTriangle world(const SM64SurfaceCollisionData& surface) {
    WorldTriangle result{};
    const int32_t* vertices[]={surface.vertex1,surface.vertex2,surface.vertex3};
    for(unsigned i=0;i<3;++i)result.geometry.vertex[i]={double(vertices[i][0]),double(vertices[i][1]),double(vertices[i][2])};
    result.geometry.normal={surface.normal.x,surface.normal.y,surface.normal.z};
    result.plane_anchor=result.geometry.vertex[0];return result;
}
int main() {
    // Both signs, both projection branches, exact diagonal threshold sides,
    // tilted classification boundaries, fractional centers and radii >200.
    unsigned checks=0,hits=0;
    for(const auto& direction:std::initializer_list<std::array<int,2>>{{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1},{3,4},{4,3}}) {
        const int tx=direction[0]*40,tz=direction[1]*40;
        for(int tilt:{0,1,12}) {
            SM64Surface surface{};
            const int32_t vertices[3][3]={{100,0,-200},{100+tx,0,-200+tz},{100+tilt,500,-200}};
            std::memcpy(surface.vertices,vertices,sizeof vertices);fixture_wall_mesh(&surface,1);
            const auto* loaded=fixture_wall_surface(0);assert(loaded&&loaded->isValid);
            const auto wall=world(*loaded);
            for(float y:{-1.f,0.f,.25f,123.5f,499.75f,500.f,501.f})
            for(float x:{-100.f,99.75f,100.f,100.25f,125.f,200.f})
            for(float z:{-300.f,-200.25f,-200.f,-199.75f,-175.f,0.f})
            for(float radius:{0.f,.25f,30.f,200.f,300.f}) {
                SM64WallCollisionData original{};original.x=x;original.y=y;original.z=z;original.radius=radius;
                const int expected=fixture_original_walls(&original);
                WallPush push{987,654};const auto result=wall_contact(wall,{x,y,z,0,radius},push);
                assert(result!=WallContact::Invalid);
                assert((result==WallContact::Hit)==bool(expected));
                if(expected) {
                    assert(original.numWalls==1 && original.walls[0]==loaded);
                    assert(std::abs((x+push.x)-original.x)<1e-4 && std::abs((z+push.z)-original.z)<1e-4);++hits;
                } else assert(push.x==987 && push.z==654);
                ++checks;
            }
        }
    }
    // Multi-wall correction uses the unchanged input center. More than four
    // hits still contribute pushes, while the engine retains the first four.
    SM64Surface corner[6]{};
    const int32_t x_wall[3][3]={{100,0,-200},{100,1000,-200},{100,0,200}};
    const int32_t z_wall[3][3]={{-200,0,100},{200,0,100},{-200,1000,100}};
    for(unsigned i=0;i<6;++i)std::memcpy(corner[i].vertices,i%2?z_wall:x_wall,sizeof x_wall);
    fixture_wall_mesh(corner,6);
    SM64WallCollisionData original{};original.x=110;original.y=100;original.z=110;original.radius=30;
    assert(fixture_original_walls(&original)==6 && original.numWalls==4);
    float corrected_x=110,corrected_z=110;
    for(unsigned i=0;i<6;++i) {
        const auto* surface=fixture_wall_surface(i);WallPush push{};
        assert(wall_contact(world(*surface),{110,100,110,0,30},push)==WallContact::Hit);
        corrected_x+=push.x;corrected_z+=push.z;
        if(i<4)assert(original.walls[i]==surface);
    }
    assert(corrected_x==original.x && corrected_z==original.z && corrected_x==170 && corrected_z==170);
    // A real DS-sized fractional triangle must not be rounded into a different
    // edge. Check both sides of its center edge, including the exact boundary.
    WorldTriangle wall{};wall.geometry.normal={1,0,0};wall.plane_anchor={10.25,0,0};
    wall.geometry.vertex[0]={10.25,0,.25};wall.geometry.vertex[1]={10.25,100,.25};wall.geometry.vertex[2]={10.25,0,100.25};
    for(float z:{.125f,.25f,.375f}) {
        WallPush push{987,654};auto result=wall_contact(wall,{10,0,z,0,30},push);
        assert((result==WallContact::Hit)==(z>=.25f));
        if(z>=.25f)assert(push.x==30.25f && push.z==0);
    }
    const float nan=std::numeric_limits<float>::quiet_NaN();WallPush unchanged{12,34};
    for(auto invalid:std::initializer_list<WallCylinder>{{nan,0,0,0,30},{0,0,0,0,-1},{0,0,0,nan,30}})
        assert(wall_contact(wall,invalid,unchanged)==WallContact::Invalid && unchanged.x==12 && unchanged.z==34);
    fixture_wall_mesh(nullptr,0);
    std::printf("Wall narrow phase: %u actual N64 comparisons, %u hits; six-wall corner/first-four retention, fractional edges and invalid-output checks passed\n",checks,hits);
}
