#include "scene_frame.hpp"
#include "native_projection.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>
using namespace sm64ds::render;
int main(){
    SceneClear clear;
    assert(game::capture_clear(32767,0,32767,63,true,clear)==Error::none&&clear.rgba==0xffffff00u&&clear.fog_mark);
    assert(game::capture_clear(31,31,32767,63,false,clear)==Error::none&&clear.rgba==0xff0000ffu&&!clear.fog_mark);
    assert(game::capture_clear(0x421,31,32767,63,false,clear)==Error::none&&clear.rgba==0x080808ffu);
    assert(game::capture_clear(0,0,32766,63,false,clear)==Error::unsupported&&clear.rgba==0x080808ffu);
    assert(game::capture_clear(32768,31,32767,63,false,clear)==Error::invalid_data&&clear.rgba==0x080808ffu);
    // Independent RH90deg perspective: aspect4/3, near1,far9. Check clip
    // orientation/depth against the SDK's documented [-w,0] convention.
    std::int32_t camera[16]{};camera[0]=3072;camera[5]=4096;camera[10]=-5120;
    camera[11]=-4096;camera[14]=-9216;Matrix projection{};
    assert(native_projection(camera,.8f,projection)==Error::none);
    assert(projection.row[0][1]==1&&projection.row[1][0]==-.6f);
    assert(projection.row[2][2]==-.125f&&projection.row[2][3]==-1.125f&&projection.row[3][2]==-1);
    assert(projection.row[2][2]*-1+projection.row[2][3]==-1);
    assert(projection.row[2][2]*-9+projection.row[2][3]==0);
    assert(native_projection(camera,0,projection)==Error::invalid_data);
    const ImageBinding images[]={{2,3,0,8,8},{2,4,1,8,8}};
    const MaterialBinding material{0,8,8,ColourSource::literal,false};const PartBinding part{0,0,false};
    const ModelBindings bindings{261,{},1,1,images,2,&material,1,&part,1};
    auto pose=identity();LiveMaterial live;live.texture=2;live.palette=3;
    const InstanceInputs input{&pose,nullptr,1,&live,1,nullptr,0};
    std::array<Occurrence,2> occurrences{};std::array<PreparedPart,2> parts{};
    SceneFrame frame(occurrences.data(),occurrences.size(),parts.data(),parts.size());
    SceneView view{};assert(frame.acquire(view)==Error::invalid_phase);
    assert(frame.begin(7,10)==Error::none&&frame.begin(7,11)==Error::invalid_phase);
    assert(frame.clear(clear)==Error::none);
    assert(frame.append(bindings,input,{17,1024,0})==Error::none);
    pose.row[0][3]=32;live.palette=4;
    assert(frame.append(bindings,input)==Error::none&&frame.seal(true,0)==Error::none);
    pose.row[0][3]=99;live.palette=999;
    assert(frame.append(bindings,input)==Error::invalid_phase&&frame.acquire(view)==Error::none);
    assert(view.scene==7&&view.serial==10&&view.occurrences==2&&view.parts==2);
    assert(view.occurrence[0].identity.owner==17&&view.occurrence[0].identity.component==1024);
    assert(!view.occurrence[1].identity.tracked());
    assert(view.clear.rgba==0x080808ffu&&frame.clear({})==Error::invalid_phase);
    assert(view.occurrence[0].file_id==261&&view.occurrence[1].first==1);
    assert(view.part[0].image==0&&view.part[1].image==1&&!view.part[0].material.texture&&!view.part[1].material.texture);
    assert(view.part[0].object_to_clip.row[0][3]==0&&view.part[1].object_to_clip.row[0][3]==32);
    assert(frame.begin(8,11)==Error::invalid_phase&&frame.acquire(view)==Error::invalid_phase);
    assert(frame.release()==Error::none&&frame.release()==Error::invalid_phase);
    // A bad later model discards the WHOLE frame rather than publishing an
    // apparently valid prefix. Subsequent frames can recover normally.
    assert(frame.begin(8,11)==Error::none);live.palette=3;assert(frame.append(bindings,input)==Error::none);
    live.palette=999;assert(frame.append(bindings,input)==Error::invalid_data);
    assert(frame.seal(true,0)==Error::invalid_data&&frame.acquire(view)==Error::invalid_phase&&frame.captured_parts()==0);
    assert(frame.begin(8,12)==Error::none);live.palette=3;
    for(unsigned i=0;i<2;++i)assert(frame.append(bindings,input)==Error::none);
    assert(frame.append(bindings,input)==Error::capacity&&frame.seal(true,0)==Error::capacity);
    assert(frame.begin(8,13)==Error::none&&frame.append(bindings,input)==Error::none);
    assert(frame.seal(true,1)==Error::unsupported&&frame.acquire(view)==Error::invalid_phase);
    assert(frame.begin(8,14)==Error::none&&frame.seal(false,0)==Error::invalid_data);
    assert(frame.begin(8,15)==Error::none&&frame.seal(true,0)==Error::none&&frame.acquire(view)==Error::none);
    assert(!view.occurrences&&!view.parts&&frame.release()==Error::none);
    MaterialBinding lit_material{0,8,8,ColourSource::lighting,false,false,true,0x03e0001fu};
    PartBinding lit_part{0,0,false,0xffffu,1};auto lit_bindings=bindings;
    lit_bindings.bones=2;lit_bindings.material=&lit_material;lit_bindings.part=&lit_part;
    std::array<Matrix,2> poses{{identity(),identity()}};std::array<NormalBasis,2> bases{};
    bases[0].value[0]=4096;bases[1].value[0]=8192;
    LightSet source;source.known=1;source.light[0]={{1,2,3},{4,5,6},512};
    live.lighting={7,8,9,10,1,false};
    InstanceInputs lit_input{poses.data(),nullptr,2,&live,1,nullptr,0,nullptr,bases.data(),nullptr,&source};
    std::array<LightSet,2> saved_lights{};std::array<PreparedLighting,2> saved_states{};
    SceneFrame lit_frame(occurrences.data(),2,parts.data(),2,saved_lights.data(),2,saved_states.data(),2);
    assert(lit_frame.begin(9,16)==Error::none&&lit_frame.append(lit_bindings,lit_input)==Error::none);
    source.light[0].colour[0]=17;bases[1].value[0]=12288;
    assert(lit_frame.append(lit_bindings,lit_input)==Error::none&&lit_frame.seal(true,0)==Error::none);
    source.light[0].colour[0]=31;bases[1].value[0]=0;live.lighting.specular=30;
    assert(lit_frame.acquire(view)==Error::none&&view.lighting.light_count==2&&view.lighting.state_count==2);
    LightingInput lit;
    assert(resolve_lighting(view.lighting,view.part[0].lighting,lit)==Error::none);
    assert(lit.normal[0]==8192&&lit.lights->light[0].colour[0]==4&&lit.material.specular==9);
    assert(lit.material.diffuse==31&&lit.material.ambient==992);
    assert(resolve_lighting(view.lighting,view.part[1].lighting,lit)==Error::none);
    assert(lit.normal[0]==12288&&lit.lights->light[0].colour[0]==17&&lit_frame.release()==Error::none);
    // Failed later capture rolls back palette additions and leaves caller
    // output unchanged, including a valid earlier palette entry.
    LightingPalette palette(saved_lights.data(),2,saved_states.data(),2);std::uint16_t entry;
    assert(palette.intern(lit,entry)==Error::none);const auto mark=palette.mark();
    std::array<PreparedPart,2> output{};output[0].image=1234;output[1].image=5678;
    auto failing_bindings=lit_bindings;PartBinding failing_parts[2]={lit_part,lit_part};
    MaterialBinding failing_materials[2]={lit_material,lit_material};failing_materials[1].source_material=1;
    failing_parts[1].material=1;failing_bindings.parts=failing_bindings.materials=failing_bindings.source_materials=2;
    failing_bindings.part=failing_parts;failing_bindings.material=failing_materials;
    LiveMaterial failing_live[2]={live,live};failing_live[1].palette=999;
    lit_input.material=failing_live;lit_input.materials=2;
    assert(capture_prepared(failing_bindings,lit_input,output.data(),2,&palette)==Error::invalid_data);
    assert(output[0].image==1234&&output[1].image==5678&&palette.mark().lights==mark.lights&&palette.mark().states==mark.states);
    // The frame's tail is unpublished, so a late refusal needs no second
    // capture pass. A later accepted occurrence must overwrite that tail and
    // expose only its own parts/palette entries, including in playable mode.
    lit_frame.set_lenient(true);
    assert(lit_frame.begin(9,19)==Error::none);
    assert(lit_frame.append(failing_bindings,lit_input)==Error::invalid_data);
    assert(lit_frame.captured_parts()==0&&lit_frame.captured_occurrences()==0);
    assert(lit_frame.append(lit_bindings,lit_input)==Error::none);
    assert(lit_frame.seal(true,0)==Error::none&&lit_frame.acquire(view)==Error::none);
    assert(view.parts==1&&view.occurrences==1&&view.lighting.light_count==1&&view.lighting.state_count==1);
    assert(resolve_lighting(view.lighting,view.part[0].lighting,lit)==Error::none&&lit.material.specular==30);
    assert(lit_frame.release()==Error::none);lit_frame.set_lenient(false);
    // A procedural mesh owns geometry, material and lights after publication,
    // even if its source grid is simulated again before the worker consumes it.
    std::array<NormalVertex,3> triangle{},stored{};
    for(auto& v:triangle){v.base={1,2,3,0,0,255,255,255,255};v.z=-512;v.uv_source=2;}
    lit_frame.set_skin_storage(stored.data(),stored.size());
    PreparedPart mesh;mesh.object_to_clip=identity();mesh.visible=true;mesh.image=0;
    assert(lit_frame.begin(10,17)==Error::none);
    assert(lit_frame.append_mesh(261,mesh,&lit,triangle.data(),3)==Error::none);
    assert(lit_frame.seal(true,0)==Error::none);
    triangle[0].base.x=1000;mesh.image=99;
    assert(lit_frame.acquire(view)==Error::none&&view.occurrences==1&&view.parts==1);
    assert(view.occurrence[0].kind==Occurrence::Kind::mesh&&view.skin_vertex[0].base.x==1&&view.part[0].image==0);
    assert(lit_frame.release()==Error::none);
    assert(lit_frame.begin(10,18)==Error::none);
    triangle[0].x=512;
    assert(lit_frame.append_mesh(261,mesh,&lit,triangle.data(),3)==Error::invalid_data);
    assert(lit_frame.captured_parts()==0&&lit_frame.captured_skin_vertices()==0&&lit_frame.seal(true,0)==Error::invalid_data);
    // A complete text/sprite batch owns its geometry and clip after capture.
    // Interleaved screen vertices must never join two world-geometry ranges.
    std::array<Vertex,16> arena{};std::array<QuadState,2> quad_states{};
    std::array<ImageLayer,2> layers{};std::array<Vertex,8> glyphs{};
    for(unsigned i=0;i<glyphs.size();++i)glyphs[i]={float(i+10),float(i+20),0,float(i)/8,.5f,255,128,64,255};
    const auto expected_glyphs=glyphs;
    SceneFrame mixed(occurrences.data(),2,parts.data(),2,nullptr,0,nullptr,0,
        {arena.data(),arena.size(),quad_states.data(),quad_states.size()},layers.data(),layers.size());
    ImageLayer text;text.bank_id=6000;text.rectangle={0,0,1,1};text.clip={40,12,360,84};text.order=4096;
    QuadState world;world.object_to_clip=identity();
    assert(mixed.begin(11,20)==Error::none);
    assert(mixed.append_quads(untextured_quads,world,glyphs.data(),4)==Error::none);
    assert(mixed.append_image_quads(text,glyphs.data(),8)==Error::none);
    assert(mixed.append_quads(untextured_quads,world,glyphs.data(),4)==Error::none);
    glyphs[0].x=1000;text.clip={};
    assert(mixed.seal(true,0)==Error::none&&mixed.acquire(view)==Error::none);
    assert(view.occurrences==2&&view.occurrence[0].first==0&&view.occurrence[0].count==4);
    assert(view.occurrence[1].first==12&&view.occurrence[1].count==4&&view.quad_vertices==16);
    assert(view.image_layers==1&&view.image_layer[0].first_vertex==4&&view.image_layer[0].vertices==8);
    assert(view.image_layer[0].clip==ScreenClip({40,12,360,84}));
    assert(std::memcmp(view.quad_vertex+4,expected_glyphs.data(),sizeof(expected_glyphs))==0);
    assert(mixed.release()==Error::none);
    // A rejected later batch publishes neither a layer nor a partial range.
    mixed.set_lenient(true);glyphs=expected_glyphs;
    assert(mixed.begin(11,21)==Error::none);
    assert(mixed.append_image_quads(text,glyphs.data(),8)==Error::none);
    const auto rejected=std::array<Vertex,12>{};
    assert(mixed.append_image_quads(text,rejected.data(),12)==Error::capacity);
    assert(mixed.captured_images()==1&&mixed.captured_quads()==2);
    assert(mixed.seal(true,0)==Error::none&&mixed.acquire(view)==Error::none);
    assert(view.image_layers==1&&view.quad_vertices==8&&mixed.release()==Error::none);
    // Cadence recovery discards an unpublished capture while the other
    // store is still borrowed by the renderer. Repeated discards must leave
    // that lease intact and the capture store reusable for the next update.
    std::array<ImageLayer,1> held_images{},recovery_images{};
    std::array<Occurrence,1> held_occurrences{},recovery_occurrences{};
    std::array<PreparedPart,1> held_parts{},recovery_parts{};
    SceneFrame held(held_occurrences.data(),1,held_parts.data(),1,nullptr,0,nullptr,0,{},held_images.data(),1);
    SceneFrame recovery(recovery_occurrences.data(),1,recovery_parts.data(),1,nullptr,0,nullptr,0,{},recovery_images.data(),1);
    assert(held.begin(12,1)==Error::none&&held.append_image(text)==Error::none);
    assert(held.seal(true,0)==Error::none&&held.acquire(view)==Error::none);
    const auto held_layer=view.image_layer[0];
    for(unsigned i=0;i<3;++i){
        assert(recovery.begin(12,i+2)==Error::none&&recovery.append_image(text)==Error::none);
        assert(recovery.seal(true,0)==Error::none&&recovery.release()==Error::none);
        assert(view.image_layers==1&&view.image_layer[0].bank_id==held_layer.bank_id);
        assert(held.phase()==SceneFrame::Phase::consuming);
    }
    assert(held.release()==Error::none&&recovery.begin(12,5)==Error::none);
    assert(recovery.append_image(text)==Error::none&&recovery.seal(true,0)==Error::none);
    assert(recovery.acquire(view)==Error::none&&view.serial==5&&view.image_layers==1&&recovery.release()==Error::none);
    // Shadow volumes: owned copies in capture order, refused outside capture
    // or with an opacity the DS cannot express; a full table drops the volume
    // and keeps the frame.
    ShadowVolume volume;volume.pose={1,0,0,0,1,0,0,0,1,0,0,0};volume.opacity=15;const auto lens=identity();
    assert(recovery.append_shadow(volume,lens)==Error::invalid_phase);
    assert(recovery.begin(12,6)==Error::none);
    volume.opacity=0;assert(recovery.append_shadow(volume,lens)==Error::invalid_data);
    volume.opacity=32;assert(recovery.append_shadow(volume,lens)==Error::invalid_data);
    volume.opacity=9;volume.pose[11]=std::numeric_limits<float>::infinity();assert(recovery.append_shadow(volume,lens)==Error::invalid_data);volume.pose[11]=0;
    for(unsigned i=0;i<max_frame_shadows+3;++i){
        volume.opacity=std::uint8_t(1+i%31);volume.shape=i%2?ShadowVolume::Shape::cuboid:ShadowVolume::Shape::cylinder;
        assert(recovery.append_shadow(volume,lens)==Error::none);
    }
    assert(recovery.captured_shadows()==max_frame_shadows&&recovery.dropped()>=3);
    volume.opacity=31; // the frame keeps its own copies
    assert(recovery.seal(true,0)==Error::none&&recovery.acquire(view)==Error::none);
    assert(view.shadows==max_frame_shadows&&view.shadow[0].opacity==1&&view.shadow[1].shape==ShadowVolume::Shape::cuboid&&view.shadow[33].opacity==3);
    assert(recovery.release()==Error::none&&recovery.begin(12,7)==Error::none&&recovery.captured_shadows()==0);
    assert(recovery.seal(true,0)==Error::none&&recovery.acquire(view)==Error::none&&view.shadows==0&&recovery.release()==Error::none);
    std::puts("Native scene snapshots own logical images/poses, meshes, shadow volumes and batched screen geometry; publication/ordering/discard passed");
}
