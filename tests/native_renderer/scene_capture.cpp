#include "scene_capture.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <string>
using namespace sm64ds::render;
int main(int argc,char** argv){
    assert(argc==2);
    Occurrence occurrence[]={{17,0,1},{1,0,4,Occurrence::Kind::quads,0}};
    PreparedPart part;part.object_to_clip=identity();part.visible=true;part.image=2;part.lighting=0;
    part.material.uv_mode=UvMode::fixed;part.material.fixed_uv.coefficient[1]=-17;part.material.fixed_uv.offset[0]=-32768;
    QuadState quad;quad.object_to_clip=identity();quad.image=4;quad.bank=QuadState::Bank::images;quad.polygon_id=35;quad.fog=true;
    quad.material.blend=Blend::alpha;quad.material.tint=0xfe123457;
    Vertex vertex[]={{-1,-2,0,0,0,1,2,3,4},{1,-2,0,1,0,5,6,7,8},{1,2,0,1,1,9,10,11,12},{-1,2,0,0,1,13,14,15,16}};
    LightSet lights;lights.known=1;lights.table_known=true;lights.generation=std::numeric_limits<std::uint64_t>::max();
    lights.light[0].direction={-1024,511,0};lights.light[0].colour={31,12,0};lights.light[0].specular_reciprocal=262144;
    for(unsigned i=0;i<128;++i)lights.shininess[i]=127-i;
    lights.toon_known=true;for(unsigned i=0;i<32;++i)lights.toon[i]=std::uint16_t(i|(31-i)<<5|(i*7%32)<<10);
    PreparedLighting lighting;lighting.lights=0;lighting.normal={4096,0,-3,0,4096,0,0,0,4096};
    lighting.material.mask=1;lighting.material.diffuse=32767;lighting.material.specular=1;lighting.material.use_shininess=true;
    SceneView view{occurrence,&part,2,1,7,121,{&lights,&lighting,1,1},{0x0a123400,63,true},vertex,&quad,4,1};
    ImageLayer image;image.bank_id=4096;image.image=3;image.screen=Screen::bottom;image.order=3707;
    image.rectangle={0,160,320,20};image.material.blend=Blend::alpha;
    image.tone_target=16;
    view.image_layer=&image;view.image_layers=1;view.scene_screen=Screen::bottom;view.scene_order=1408;
    const ComponentCaptureInfo info{121,9,true,true};
    auto invalid=[&]{FILE* file=std::tmpfile();assert(file);assert(!write_component_capture(file,view,info));assert(std::ftell(file)==0);assert(std::fclose(file)==0);};
    assert(!write_component_capture(nullptr,view,info));
    image.image=128;invalid();image.image=3;
    image.source=LayerSource(2);invalid();image.source=LayerSource::images;
    image.part=1;invalid();image.part=0;
    image.rectangle.width=0;invalid();image.rectangle.width=320;
    view.image_layers=128;invalid();view.image_layers=1;
    view.image_layer=nullptr;invalid();view.image_layer=&image;
    view.scene_screen=Screen(2);invalid();view.scene_screen=Screen::bottom;
    vertex[3].v=std::numeric_limits<float>::quiet_NaN();invalid();vertex[3].v=1;
    occurrence[1].state=1;invalid();occurrence[1].state=0;
    occurrence[1].first=4;invalid();occurrence[1].first=0;
    occurrence[1].count=std::numeric_limits<std::uint32_t>::max();invalid();occurrence[1].count=4;
    occurrence[0].first=1;invalid();occurrence[0].first=0;
    part.lighting=1;invalid();part.lighting=0;
    part.material.uv[5]=std::numeric_limits<float>::quiet_NaN();invalid();part.material.uv[5]=0;
    lighting.lights=1;invalid();lighting.lights=0;
    lighting.material.diffuse=65535;invalid();lighting.material.diffuse=32767;
    view.quad_vertices=8196;invalid();view.quad_vertices=4;
    view.quad_states=257;invalid();view.quad_states=1;
    // Any GPU handle, even with an otherwise valid material, is excluded.
    part.material.texture.generation=1;invalid();part.material.texture={};
    FILE* file=std::fopen(argv[1],"wb");assert(file);assert(write_component_capture(file,view,info));assert(std::fclose(file)==0);
    file=std::fopen(argv[1],"rb");assert(file);assert(!write_component_capture(file,view,info));assert(std::fclose(file)==0);
    // The same owned vertex arena may contain screen batches. Every quad has
    // exactly one owner; a duplicated world/screen span must refuse export.
    image.first_vertex=0;image.vertices=4;invalid();
    view.occurrences=1;view.quad_states=0;
    file=std::tmpfile();assert(file);assert(write_component_capture(file,view,info));assert(std::fclose(file)==0);
    image.first_vertex=4;invalid();image.first_vertex=0;
    image.vertices=0;invalid();
    view={};file=std::tmpfile();assert(file);assert(write_component_capture(file,view,{}));assert(std::fclose(file)==0);
    Occurrence mixed[]={{17,0,1},{17,0,3,Occurrence::Kind::skin,0},{untextured_quads,3,3,Occurrence::Kind::mesh,1}};
    PreparedPart mixed_parts[2]={part,part};mixed_parts[1].image=ModelBank::no_texture;
    mixed_parts[1].highlight=true;mixed_parts[1].material.blend=Blend::highlight;
    mixed_parts[1].material.uv_mode=UvMode::affine;
    lighting.material.use_shininess=false;lighting.material.specular=0;lighting.material.toon=true;
    NormalVertex skin[6]{};
    for(unsigned i=0;i<6;++i){skin[i].base={float(i),-2,3,.25f,.75f,17,23,31,255};
        skin[i].x=-512;skin[i].y=511;skin[i].z=-1;skin[i].uv_source=std::int16_t(i%3);}
    view={mixed,mixed_parts,3,2,7,121,{&lights,&lighting,1,1}};
    ImageLayer mixed_images[2]={image,image};
    mixed_images[1].source=LayerSource::solid;mixed_images[1].bank_id=mixed_images[1].image=mixed_images[1].part=0;
    mixed_images[1].tone_target=32;
    view.image_layer=mixed_images;view.image_layers=2;
    view.skin_vertex=skin;view.skin_vertices=6;
    view.skin_vertex=nullptr;invalid();view.skin_vertex=skin;
    view.skin_vertices=4095*3+3;invalid();view.skin_vertices=6;
    view.skin_vertices=5;invalid();view.skin_vertices=6;
    skin[5].base.v=std::numeric_limits<float>::infinity();invalid();skin[5].base.v=.75f;
    skin[5].x=512;invalid();skin[5].x=-512;
    skin[5].uv_source=3;invalid();skin[5].uv_source=2;
    mixed[1].state=1;invalid();mixed[1].state=0;
    mixed[1].first=3;invalid();mixed[1].first=0;
    mixed[1].count=6;invalid();mixed[1].count=3;
    mixed[2].state=0;invalid();mixed[2].state=1;
    mixed[2].file_id=17;invalid();mixed[2].file_id=untextured_quads;
    const auto skin_path=std::string(argv[1])+".skin.json";
    file=std::fopen(skin_path.c_str(),"wb");assert(file);
    assert(write_component_capture(file,view,info));assert(std::fclose(file)==0);
}
