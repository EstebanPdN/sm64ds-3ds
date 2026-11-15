#include "scene_producer.hpp"
#include "game_materials.hpp"
#include "game_lighting.hpp"
#include "ModelBase.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
extern "C" void* __wrap__ZN13SharedFilePtr4LoadEv(void*);
extern "C" void __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents*,Matrix4x3*,Vector3*);
static BMD_File file{};static unsigned legacy=0;
extern "C" void* __real__ZN13SharedFilePtr4LoadEv(void*){return &file;}
extern "C" void __real__ZN13SharedFilePtr7ReleaseEv(void*){}
extern "C" void __real_func_020462d0(ModelComponents*,BMD_File*,void*){}
extern "C" void __real_func_02046bbc(ModelComponents*,void*,unsigned){}
extern "C" void __real_func_ov007_020c232c(void*){}
extern "C" void __wrap_func_ov007_020c232c(void*);
extern "C" void __real__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents*,Matrix4x3*,Vector3*){++legacy;}
int main(){
    const ImageBinding image{0,0,0,8,8};const MaterialBinding material{0,8,8,ColourSource::literal,false};
    const PartBinding part{0,0,false};const ModelBindings binding{261,{},1,1,&image,1,&material,1,&part,1};
    ModelCatalog catalog;assert(ModelCatalog::open(&binding,1,catalog)==Error::none);
    std::array<Occurrence,4> occurrences{};std::array<PreparedPart,4> parts{};
    std::array<LightSet,4> lights{};std::array<PreparedLighting,4> lighting{};
    SceneFrame frame(occurrences.data(),4,parts.data(),4,lights.data(),4,lighting.data(),4);static hooks::ProducerWorkspace workspace;
    hooks::SceneProducer producer(frame,workspace,nullptr,0);
    assert(hooks::install(catalog,hooks::SceneProducer::submit,&producer)&&hooks::activate(true));
    unsigned char handle[16]{};const std::uint16_t id=261;std::memcpy(handle,&id,2);__wrap__ZN13SharedFilePtr4LoadEv(handle);
    alignas(BMD_Material) game::RuntimeMaterial runtime{};runtime.texture=runtime.palette=0;runtime.polygon=0x1f00c0;
    file.numBones=file.numMaterials=file.numTextures=file.numPalettes=1;
    ModelComponents model{};model.modelFile=&file;model.materials=reinterpret_cast<BMD_Material*>(&runtime);
    // ABI bytes read without reinterpreting the game's Matrix4x3 as Affine.
    auto bone=game::unit_affine();bone.value[9]=3*4096;
    model.transforms=reinterpret_cast<Matrix4x3*>(&bone);
    auto begin=[&](unsigned serial){assert(frame.begin(3,serial)==Error::none&&hooks::begin_frame());producer.begin_frame();};
    auto submit=[&]{__wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);};
    begin(1);submit();assert(!hooks::end_frame()&&frame.seal(false,0)==Error::invalid_data); // no assumed camera
    begin(2);producer.owner(123);assert(producer.projection(identity())==Error::none);submit();
    bone.value[9]=7*4096;runtime.polygon|=0x80000000u;submit(); // hidden models append nothing
    assert(hooks::end_frame()&&frame.seal(true,0)==Error::none);
    SceneView view;assert(frame.acquire(view)==Error::none&&view.occurrences==1&&view.parts==1);
    assert(view.occurrence[0].identity.owner==123&&view.occurrence[0].identity.component==reinterpret_cast<std::uintptr_t>(&model));
    assert(view.part[0].object_to_clip.row[0][3]==3&&view.part[0].image==0&&!view.part[0].material.texture);
    assert(frame.release()==Error::none&&legacy==0);
    begin(3);model.transforms=nullptr;submit();assert(hooks::end_frame()&&frame.seal(true,0)==Error::none); // hidden needs no pose/camera
    assert(frame.acquire(view)==Error::none&&!view.parts&&frame.release()==Error::none);
    begin(4);runtime.polygon&=~0x80000000u;assert(producer.projection(identity())==Error::none);submit();
    assert(!hooks::end_frame()&&frame.seal(false,0)!=Error::none&&legacy==0);
    // Actual 48-byte material + 52-byte runtime bone records. Normal UV uses
    // the palette basis/view, independently of positional scale/translation.
    MaterialBinding normal_material{0,8,8,ColourSource::emission,false,true};
    PartBinding normal_part{0,0,false,0};auto normal_binding=binding;
    normal_binding.material=&normal_material;normal_binding.part=&normal_part;
    std::array<unsigned char,52> runtime_bones{};runtime_bones[24]=1;
    model.bones=reinterpret_cast<BMD_Bone*>(runtime_bones.data());model.transforms=reinterpret_cast<Matrix4x3*>(&bone);
    bone=game::unit_affine();bone.value[0]=bone.value[4]=8192;bone.value[9]=17*4096;
    auto camera=game::unit_affine();camera.value[0]=camera.value[4]=0;camera.value[1]=4096;camera.value[3]=-4096;
    Vector3 scale;scale.x=12288;scale.y=8192;scale.z=4096;
    runtime.texture_flags=2u<<30;runtime.translate_x=-257;runtime.translate_y=511;
    begin(5);assert(producer.projection(identity())==Error::none);
    assert(hooks::SceneProducer::submit(normal_binding,model,reinterpret_cast<Matrix4x3*>(&camera),&scale,&producer));
    assert(hooks::end_frame()&&frame.seal(true,0)==Error::none&&frame.acquire(view)==Error::none);
    const auto& uv=view.part[0].material.normal_uv();
    const std::array<std::int32_t,6> expected{{0,-262144,0,-262144,0,0}};
    assert(view.part[0].material.uv_mode==UvMode::normal&&uv.coefficient==expected&&uv.offset[0]==-2&&uv.offset[1]==1);
    bone.value[0]=0;runtime.translate_x=0;assert(uv.coefficient==expected&&uv.offset[0]==-2);
    assert(frame.release()==Error::none&&legacy==0);
    bone.value[0]=bone.value[4]=8192;
    normal_material.colour=ColourSource::lighting;normal_part.lighting_bone=0;
    runtime.polygon|=1;runtime.diffuse_ambient=0x03e0001f;runtime.specular_emission=0x7c000001;
    hooks::lighting_reset();hooks::lighting_view(camera.value);
    std::array<unsigned char,40> source{};int enabled=1;std::int16_t z=-4096,colour=32767;
    std::memcpy(source.data()+36,&enabled,4);std::memcpy(source.data()+32,&z,2);std::memcpy(source.data()+34,&colour,2);
    __wrap_func_ov007_020c232c(source.data());
    begin(6);assert(producer.projection(identity())==Error::none);
    assert(hooks::SceneProducer::submit(normal_binding,model,reinterpret_cast<Matrix4x3*>(&camera),&scale,&producer));
    assert(hooks::end_frame()&&frame.seal(true,0)==Error::none&&frame.acquire(view)==Error::none);
    LightingInput captured;
    assert(resolve_lighting(view.lighting,view.part[0].lighting,captured)==Error::none);
    assert(captured.material.diffuse==31&&captured.material.ambient==992&&captured.material.specular==1&&captured.material.emission==31744);
    assert(captured.material.mask==1&&captured.lights->light[0].direction[2]==512);
    assert(captured.normal[0]==0&&captured.normal[1]==4096&&captured.normal[3]==-4096);
    runtime.diffuse_ambient=0;hooks::lighting_reset();bone.value[4]=0;
    assert(captured.material.diffuse==31&&captured.lights->light[0].direction[2]==512&&captured.normal[3]==-4096);
    assert(frame.release()==Error::none);
    begin(7);assert(producer.projection(identity())==Error::none);
    assert(!hooks::SceneProducer::submit(normal_binding,model,reinterpret_cast<Matrix4x3*>(&camera),&scale,&producer));
    assert(hooks::end_frame()&&frame.seal(true,0)==Error::unsupported&&legacy==0);
    // Mixed-bone triangles: position and normal use different target spaces.
    // Two actors share a catalog but own distinct poses; publication retains
    // the earlier vertices even after the next actor/capture changes them.
    std::array<game::Affine,ModelBank::max_matrices> skin_bones{};
    skin_bones[0]=game::unit_affine();
    skin_bones[0].value[9]=10*4096;skin_bones[0].value[10]=20*4096;skin_bones[0].value[11]=30*4096;
    skin_bones[1]=game::unit_affine();
    skin_bones[1].value[0]=skin_bones[1].value[4]=0;
    skin_bones[1].value[1]=4096;skin_bones[1].value[3]=-4096;
    auto& last=skin_bones.back();last=game::unit_affine();
    last.value[0]=2*4096;last.value[4]=3*4096;last.value[8]=4*4096;
    last.value[9]=13*4096;last.value[10]=26*4096;last.value[11]=39*4096;
    auto other_bones=skin_bones;other_bones.back().value[9]+=5*4096;
    std::array<SkinVertex,3> source_skin{};
    for(auto& v:source_skin){v.x=1;v.y=2;v.z=3;v.u=.25f;v.v=.75f;
        v.r=17;v.g=41;v.b=89;v.ny=256;v.uv_source=2;}
    source_skin[0].bone=ModelBank::max_matrices-1;source_skin[1].bone=0;source_skin[2].bone=1;
    const std::array<SkinGroup,2> groups{{{0,0,3},{0,0,3}}};
    PartBinding skin_part{0,0,false,0,1};auto skin_binding=binding;
    skin_binding.bones=ModelBank::max_matrices;skin_binding.part=&skin_part;
    skin_binding.skin=groups.data();skin_binding.skins=groups.size();skin_binding.skin_vertex=source_skin.data();
    BMD_File skin_file{};skin_file.numBones=ModelBank::max_matrices;
    skin_file.numMaterials=skin_file.numTextures=skin_file.numPalettes=1;
    game::RuntimeMaterial skin_material{};skin_material.polygon=0x1f00c0;
    ModelComponents actor{};actor.modelFile=&skin_file;
    actor.materials=reinterpret_cast<BMD_Material*>(&skin_material);
    actor.transforms=reinterpret_cast<Matrix4x3*>(skin_bones.data());
    auto other_actor=actor;other_actor.transforms=reinterpret_cast<Matrix4x3*>(other_bones.data());
    std::array<Occurrence,8> skin_occurrences{};std::array<PreparedPart,4> skin_parts{};
    std::array<NormalVertex,12> skin_storage{};
    SceneFrame skin_frame(skin_occurrences.data(),skin_occurrences.size(),skin_parts.data(),skin_parts.size());
    skin_frame.set_skin_storage(skin_storage.data(),skin_storage.size());
    hooks::SceneProducer skin_producer(skin_frame,workspace,nullptr,0);
    const auto start_skin=[&](unsigned serial){
        assert(skin_frame.begin(4,serial)==Error::none);skin_producer.begin_frame();
        assert(skin_producer.projection(identity())==Error::none);
    };
    start_skin(1);
    assert(hooks::SceneProducer::submit(skin_binding,actor,nullptr,nullptr,&skin_producer));
    assert(hooks::SceneProducer::submit(skin_binding,other_actor,nullptr,nullptr,&skin_producer));
    assert(skin_frame.seal(true,0)==Error::none&&skin_frame.acquire(view)==Error::none);
    assert(view.occurrences==6&&view.skin_vertices==12);
    for(unsigned i=0;i<12;++i){const auto& v=view.skin_vertex[i];
        assert(v.base.u==.25f&&v.base.v==.75f&&v.base.r==17&&v.base.g==41&&v.base.b==89&&v.base.a==255&&v.uv_source==2);
        if(i%3==0){assert(v.base.x==(i<6?5:10)&&v.base.y==12&&v.base.z==21);assert(v.x==256&&v.y==0&&v.z==0);}
        if(i%3==1){assert(v.base.x==1&&v.base.y==2&&v.base.z==3);assert(v.x==256&&v.y==0&&v.z==0);}
        if(i%3==2){assert(v.base.x==-12&&v.base.y==-19&&v.base.z==-27);assert(v.x==0&&v.y==256&&v.z==0);}
    }
    last.value[9]+=4096;assert(view.skin_vertex[0].base.x==5);
    assert(skin_frame.release()==Error::none);
    start_skin(2);
    assert(hooks::SceneProducer::submit(skin_binding,actor,nullptr,nullptr,&skin_producer));
    assert(skin_frame.seal(true,0)==Error::none&&skin_frame.acquire(view)==Error::none);
    assert(view.skin_vertices==6&&view.skin_vertex[0].base.x==6&&skin_frame.release()==Error::none);
    // Singular targets skip skin groups as before. A later valid capture must
    // retry that same bone rather than retaining a failed inverse.
    skin_bones[0].value[0]=0;start_skin(3);
    assert(hooks::SceneProducer::submit(skin_binding,actor,nullptr,nullptr,&skin_producer));
    assert(skin_frame.seal(true,0)==Error::none&&skin_frame.acquire(view)==Error::none);
    assert(view.occurrences==1&&view.skin_vertices==0&&skin_frame.release()==Error::none);
    skin_bones[0].value[0]=4096;start_skin(4);
    assert(hooks::SceneProducer::submit(skin_binding,actor,nullptr,nullptr,&skin_producer));
    assert(skin_frame.seal(true,0)==Error::none&&skin_frame.acquire(view)==Error::none);
    assert(view.skin_vertices==6&&view.skin_vertex[0].base.x==6&&skin_frame.release()==Error::none);
    std::puts("Real ModelComponents hook -> material/pose -> owned scene, missing camera refusal and hidden elision passed");
}
