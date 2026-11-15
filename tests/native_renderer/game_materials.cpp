#include "game_materials.hpp"
#include "model_components_hooks.hpp"
#include "texture_sequence_hooks.hpp"
#include "native_model_fixture.h"
#include "ModelBase.h"
#include "TextureSequence.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
extern "C" void* __wrap__ZN13SharedFilePtr4LoadEv(void*);
extern "C" void __wrap_func_02046bbc(ModelComponents*,BTP_File*,unsigned);
static BMD_File file{};
extern "C" void* __real__ZN13SharedFilePtr4LoadEv(void*){return &file;}
extern "C" void __real__ZN13SharedFilePtr7ReleaseEv(void*){}
extern "C" void __real_func_020462d0(ModelComponents*,BMD_File*,void*){}
extern "C" void __real_func_02046bbc(ModelComponents*,BTP_File*,unsigned){}
extern "C" void __real__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents*,Matrix4x3*,Vector3*){}
static bool submit(const ModelBindings&,const ModelComponents&,const Matrix4x3*,const Vector3*,void*){return true;}
int main(){
    ModelBank bank;assert(ModelBank::open(native_model_fixture,sizeof(native_model_fixture),bank)==Error::none);
    // The only native run uses original material 1, testing the raw 48-byte stride.
    const ImageBinding image{1,2,0,8,8};const MaterialBinding material{1,8,8,ColourSource::literal,true};
    const PartBinding part{0,0,false};ModelBindings binding{261,{},1,2,&image,1,&material,1,&part,1};
    std::memcpy(binding.source_sha256.data(),bank.source_sha256(),32);assert(validate(bank,binding)==Error::none);
    auto bad_image=image;bad_image.width=16;auto bad_binding=binding;bad_binding.image=&bad_image;
    assert(validate(bank,bad_binding)==Error::invalid_data);
    ModelCatalog catalog;assert(ModelCatalog::open(&binding,1,catalog)==Error::none&&hooks::install(catalog,submit,nullptr));
    unsigned char handle[16]{};const std::uint16_t id=261;std::memcpy(handle,&id,2);__wrap__ZN13SharedFilePtr4LoadEv(handle);
    alignas(BMD_Material) std::array<game::RuntimeMaterial,2> runtime{};
    auto& source=runtime[1];source.texture=0;source.palette=0;
    source.scale_x=4096;source.scale_y=8192;source.angle=0xa123;
    source.polygon=0x1f00c0;source.diffuse_ambient=0xffff;
    source.texture_flags=1u<<30;source.translate_x=-170;source.translate_y=255;
    std::array<BMD_Texture,2> textures{};textures[0].unk_0c=32u|(16u<<16);textures[1].unk_0c=8u|(8u<<16);
    file.numBones=1;file.numMaterials=2;file.numTextures=2;file.numPalettes=3;file.textures=textures.data();
    ModelComponents model{};model.modelFile=&file;model.materials=reinterpret_cast<BMD_Material*>(runtime.data());
    // Logical animation selects prepared image (1,2); initial runtime IDs stay (0,0).
    std::array<std::uint16_t,4> tex{{1,0,0,0}},pal{{2,0,0,0}};
    std::array<std::uint16_t,6> track{{1,0,0,0,1,0}};std::uint16_t zero=0;
    BTP_File animation{};animation.unk_00=1;animation.numTexRecords=animation.numPalRecords=animation.numSeqRecords=1;
    animation.unk_04=reinterpret_cast<char*>(tex.data());animation.unk_0c=reinterpret_cast<char*>(pal.data());
    animation.unk_10=animation.unk_14=animation.unk_18=reinterpret_cast<char*>(&zero);
    animation.unk_20=reinterpret_cast<char*>(track.data());__wrap_func_02046bbc(&model,&animation,0);
    std::array<std::int16_t,8192> table{};table[0x123*2]=2048;table[0x123*2+1]=-1024;
    std::array<LiveMaterial,2> scratch{};
    assert(hooks::capture_materials(binding,model,table.data(),4096,scratch.data(),2)==Error::none);
    assert(!scratch[0].visible&&scratch[1].texture==1&&scratch[1].palette==2&&source.texture==0&&source.palette==0);
    LiveMaterial expected;assert(game::capture_material(source,{1,2,{8,8},true},{8,8},{32,16},2048,-1024,expected)==Error::none);
    assert(scratch[1].state.fixed_uv.coefficient==expected.state.fixed_uv.coefficient);
    assert(scratch[1].state.fixed_uv.width==8&&scratch[1].state.fixed_uv.height==8);
    // Continue through the production instance capture; final snapshot owns all values.
    Matrix pose{};for(unsigned i=0;i<4;++i)pose.row[i][i]=1;
    Texture resident{1,0,&file};PartInstance occurrence;
    InstanceInputs input{&pose,nullptr,1,scratch.data(),2,&resident,1};
    assert(capture_instance(binding,input,&occurrence,1)==Error::none&&occurrence.visible);
    assert(occurrence.material.texture==resident);
    const auto retained=occurrence.material.fixed_uv.coefficient;
    source.translate_x=700;source.polygon|=0x80000000u;
    assert(hooks::capture_materials(binding,model,nullptr,0,scratch.data(),2)==Error::none);
    assert(!scratch[1].visible&&occurrence.material.fixed_uv.coefficient==retained);
    // A hidden material needs neither trig nor a prepared image. Visible failures
    // are returned before any GPU work and may never be published as a frame.
    source.polygon&=~0x80000000u;
    assert(hooks::capture_materials(binding,model,nullptr,0,scratch.data(),2)==Error::invalid_data);
    assert(hooks::capture_materials(binding,model,table.data(),4095,scratch.data(),2)==Error::invalid_data);
    source.texture_flags=0;assert(hooks::capture_materials(binding,model,nullptr,0,scratch.data(),2)==Error::none);
    assert(scratch[1].state.uv_mode==UvMode::affine&&scratch[1].state.uv[0]==1&&scratch[1].state.uv[4]==1);
    source.texture_flags=(2u<<30)|(3u<<20)|(2u<<23);source.translate_x=-257;source.translate_y=0x7fffffff;
    assert(hooks::capture_materials(binding,model,nullptr,0,scratch.data(),2)==Error::none);
    assert(scratch[1].state.uv_mode==UvMode::normal&&scratch[1].normal_half_width==32&&scratch[1].normal_half_height==16);
    assert(scratch[1].state.normal_uv().offset[0]==-2&&scratch[1].state.normal_uv().offset[1]==-1);
    source.texture_flags=3u<<30;assert(hooks::capture_materials(binding,model,nullptr,0,scratch.data(),2)==Error::unsupported);
    source.texture_flags=0;tex[0]=0;__wrap_func_02046bbc(&model,&animation,0);
    assert(hooks::capture_materials(binding,model,nullptr,0,scratch.data(),2)==Error::unsupported);
    std::puts("Live BTP identity -> original material -> native instance, dimensions, angle and refusal passed");
}
