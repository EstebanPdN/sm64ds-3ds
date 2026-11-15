#include "scene_frame.hpp"
#include "game_sprite_geometry.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace sm64ds::render;
int main(){
    std::array<Vertex,128> geometry{};game::SpriteGeometry source;game::SpriteGeometryResult result;
    source.sample_width=source.sample_height=24;
    assert(game::sprite_geometry(source,geometry.data(),geometry.size(),result)==Error::none);
    assert(result.vertices==4&&result.source_quads==9&&result.collapsed);
    // c49bc divides4096 by3 before emitting each coordinate; retaining that
    // cropped endpoint is essential when collapsing its nine cells.
    assert(geometry[2].x==4095.f/4096&&geometry[0].y==4095.f/4096&&geometry[2].u==3);
    source.offset_s=2047;source.sample_width=16;source.sample_height=8;
    assert(game::sprite_geometry(source,geometry.data(),geometry.size(),result)==Error::none);
    assert(!result.collapsed&&result.vertices==8&&result.source_quads==2);
    assert(geometry[0].u==32752.f/128&&geometry[2].u==-32656.f/128);
    assert(geometry[4].u==geometry[2].u&&geometry[6].u==-32528.f/128);
    const std::uint16_t tiles[]={2,65535,0,1};source={};source.image_width=16;source.image_height=16;
    source.sample_width=source.sample_height=16;source.tiles=tiles;source.tile_count=4;
    assert(game::sprite_geometry(source,geometry.data(),geometry.size(),result)==Error::none);
    assert(result.source_quads==3&&result.vertices==12&&!result.collapsed);
    assert(geometry[0].x==0&&geometry[0].y==1&&geometry[0].u==0&&geometry[0].v==.5f);
    assert(geometry[4].x==0&&geometry[4].y==.5f&&geometry[4].u==0&&geometry[4].v==0);
    const auto saved=geometry[0];const auto old=result;source.tile_count=3;
    assert(game::sprite_geometry(source,geometry.data(),geometry.size(),result)==Error::invalid_data);
    assert(geometry[0].y==saved.y&&result.vertices==old.vertices);
    source.tile_count=4;
    assert(game::sprite_geometry(source,geometry.data(),4,result)==Error::capacity&&geometry[0].y==saved.y);
    source={};source.direct_quad=true;source.corner_colours=true;source.colour={31,992,31744,32767};source.alpha=15;
    assert(game::sprite_geometry(source,geometry.data(),geometry.size(),result)==Error::none);
    assert(result.vertices==4&&geometry[0].r==255&&geometry[1].g==255&&geometry[2].b==255&&geometry[3].r==255);
    assert(geometry[0].a==123); // alpha is five-bit, independent of RGB.
    std::array<Occurrence,4> occurrences{};std::array<PreparedPart,1> parts{};
    std::array<Vertex,32> vertices{};std::array<QuadState,3> states{};
    SceneFrame frame(occurrences.data(),4,parts.data(),1,nullptr,0,nullptr,0,{vertices.data(),vertices.size(),states.data(),states.size()});
    QuadState state;state.object_to_clip=identity();state.material.cull=Cull::none;
    assert(frame.begin(1,1)==Error::none);
    assert(frame.append_quads(untextured_quads,state,geometry.data(),4)==Error::none);
    geometry[0].x=3;
    assert(frame.append_quads(untextured_quads,state,geometry.data(),4)==Error::none&&frame.captured_occurrences()==1);
    const MaterialBinding material{0,8,8,ColourSource::literal,false};const PartBinding part{0,0,false};
    const ModelBindings model{1,{},1,1,nullptr,0,&material,1,&part,1};LiveMaterial live;auto pose=identity();
    assert(frame.append(model,{&pose,nullptr,1,&live,1,nullptr,0})==Error::none);
    assert(frame.append_quads(untextured_quads,state,geometry.data(),4)==Error::none);
    state.object_to_clip.row[0][3]=5;
    assert(frame.append_quads(untextured_quads,state,geometry.data(),4)==Error::none);
    assert(frame.seal(true,0)==Error::none);SceneView view;
    geometry[0].x=999;state.object_to_clip.row[0][3]=999;
    assert(frame.acquire(view)==Error::none&&view.occurrences==4&&view.quad_vertices==16&&view.quad_states==3);
    assert(view.occurrence[0].kind==Occurrence::Kind::quads&&view.occurrence[0].count==8);
    assert(view.occurrence[1].kind==Occurrence::Kind::model&&view.occurrence[2].kind==Occurrence::Kind::quads);
    assert(view.quad_vertex[0].x==0&&view.quad_vertex[4].x==3&&view.quad_state[2].object_to_clip.row[0][3]==5);
    assert(frame.release()==Error::none);
    assert(frame.begin(1,2)==Error::none);
    assert(frame.append_quads(7,state,geometry.data(),4)==Error::invalid_data); // no image with a textured identity
    assert(frame.seal(true,0)==Error::invalid_data&&frame.captured_quads()==0);
    assert(frame.begin(1,3)==Error::none);state.image=0;
    assert(frame.append_quads(7,state,geometry.data(),4)==Error::none);
    geometry[0].u=std::numeric_limits<float>::infinity();
    assert(frame.append_quads(7,state,geometry.data(),4)==Error::invalid_data&&frame.seal(true,0)==Error::invalid_data);
    assert(frame.begin(1,4)==Error::none);geometry[0].u=0;state.image=QuadState::no_image;
    for(unsigned i=0;i<8;++i)assert(frame.append_quads(untextured_quads,state,geometry.data(),4)==Error::none);
    assert(frame.captured_quads()==8&&frame.captured_occurrences()==1);
    assert(frame.append_quads(untextured_quads,state,geometry.data(),4)==Error::capacity&&frame.seal(true,0)==Error::capacity);
    assert(frame.begin(1,5)==Error::none);auto work=frame.quad_workspace();assert(work.data==vertices.data()&&work.capacity==32);
    source={};source.direct_quad=true;
    assert(game::sprite_geometry(source,work.data,work.capacity,result)==Error::none&&frame.captured_quads()==0);
    assert(frame.append_quads(untextured_quads,state,work.data,result.vertices)==Error::none&&frame.captured_quads()==1);
    assert(frame.seal(true,0)==Error::none&&!frame.quad_workspace().data&&frame.acquire(view)==Error::none);
    assert(view.quad_vertex==work.data&&view.quad_vertex[0].a==255&&frame.release()==Error::none);
    // Equal numeric resource IDs in different namespaces are a mandatory
    // ordering/batching barrier; no model image may satisfy an image-only ID.
    assert(frame.begin(1,6)==Error::none);state.image=0;
    assert(frame.append_quads(7,state,geometry.data(),4)==Error::none);state.bank=QuadState::Bank::images;
    assert(frame.append_quads(7,state,geometry.data(),4)==Error::none&&frame.captured_occurrences()==2);
    assert(frame.seal(true,0)==Error::none&&frame.acquire(view)==Error::none);
    assert(view.quad_state[0].bank==QuadState::Bank::model&&view.quad_state[1].bank==QuadState::Bank::images);
    assert(frame.release()==Error::none);state.image=QuadState::no_image;
    assert(!valid(state));state.bank=static_cast<QuadState::Bank>(2);assert(!valid(state));
    source.alpha=0;assert(game::sprite_geometry(source,nullptr,0,result)==Error::none&&!result.vertices);
    std::puts("Native quad capture owns ordered geometry/state; adjacent batching, UV seams, tiles and bounded failure passed");
}
