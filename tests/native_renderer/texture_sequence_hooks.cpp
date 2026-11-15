#include "texture_sequence_hooks.hpp"
#include "model_components_hooks.hpp"
#include "ModelBase.h"
#include "TextureSequence.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
extern "C" void* __wrap__ZN13SharedFilePtr4LoadEv(void*);
extern "C" void __wrap__ZN13SharedFilePtr7ReleaseEv(void*);
extern "C" void __wrap_func_020462d0(ModelComponents*,BMD_File*,void*);
extern "C" void __wrap_func_02046bbc(ModelComponents*,BTP_File*,unsigned);
extern "C" void __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents*,Matrix4x3*,Vector3*);
static BMD_File file{};
static unsigned updates=0,initializations=0,renders=0,submissions=0;
extern "C" void* __real__ZN13SharedFilePtr4LoadEv(void* handle){
    void* value=&file;std::memcpy(static_cast<char*>(handle)+4,&value,sizeof(value));return value;
}
extern "C" void __real__ZN13SharedFilePtr7ReleaseEv(void* handle){--static_cast<unsigned char*>(handle)[2];}
extern "C" void __real_func_020462d0(ModelComponents* model,BMD_File* source,void* storage){
    ++initializations;model->modelFile=source;model->materials=static_cast<BMD_Material*>(storage);
}
extern "C" void __real_func_02046bbc(ModelComponents*,BTP_File*,unsigned){++updates;}
extern "C" void __real__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents*,Matrix4x3*,Vector3*){++renders;}
static bool submit(const ModelBindings&,const ModelComponents&,const Matrix4x3*,const Vector3*,void*){++submissions;return true;}
static void expect(const ModelComponents& model,unsigned material,unsigned texture,unsigned palette){
    hooks::ImageIdentity image;assert(hooks::selected_image(model,material,image));
    assert(image.texture==texture&&image.palette==palette);
}
static void refused(const ModelComponents& model){
    hooks::ImageIdentity image{123,456};assert(!hooks::selected_image(model,0,image));
    assert(image.texture==123&&image.palette==456);
}
int main(){
    // The original runtime layout is always a packed 48-byte material, even
    // on a host where BMD_Material contains a wider pointer and is 56 bytes.
    alignas(BMD_Material) std::array<game::RuntimeMaterial,2> storage{};
    storage[0].texture=0;storage[0].palette=1;
    storage[1].texture=3;storage[1].palette=ModelBank::no_texture;
    file.numBones=1;file.numMaterials=2;file.numTextures=4;file.numPalettes=3;
    ModelComponents model{};
    __wrap_func_020462d0(&model,&file,storage.data());
    expect(model,0,0,1);expect(model,1,3,ModelBank::no_texture);
    std::array<std::array<std::uint16_t,4>,3> textures{{{{2,0,0,0}},{{1,0,0,0}},{{3,0,0,0}}}};
    std::array<std::array<std::uint16_t,4>,2> palettes{{{{0,0,0,0}},{{2,0,0,0}}}};
    std::array<std::uint16_t,3> starts{{0,5,9}},tex_keys{{0,1,2}},pal_keys{{1,0,1}};
    std::array<std::uint16_t,6> track{{0,0,0,0,3,0}};
    BTP_File animation{};animation.unk_00=12;animation.numTexRecords=3;
    animation.unk_04=reinterpret_cast<char*>(textures.data());animation.numPalRecords=2;
    animation.unk_0c=reinterpret_cast<char*>(palettes.data());animation.unk_10=reinterpret_cast<char*>(starts.data());
    animation.unk_14=reinterpret_cast<char*>(tex_keys.data());animation.unk_18=reinterpret_cast<char*>(pal_keys.data());
    animation.numSeqRecords=1;animation.unk_20=reinterpret_cast<char*>(track.data());
    // Before installation, only the original updater runs.
    __wrap_func_02046bbc(&model,&animation,5);assert(updates==1);expect(model,0,0,1);
    const MaterialBinding material{0,8,8,ColourSource::literal,false};const PartBinding part{0,0,false};
    const ModelBindings binding{261,{},1,2,nullptr,0,&material,1,&part,1};ModelCatalog catalog;
    assert(ModelCatalog::open(&binding,1,catalog)==Error::none&&hooks::install(catalog,submit,nullptr));
    unsigned char handle[16]{};const std::uint16_t id=261;std::memcpy(handle,&id,2);handle[2]=1;
    assert(__wrap__ZN13SharedFilePtr4LoadEv(handle)==&file&&hooks::tracks_model(model));
    for(unsigned frame:{0u,4u,5u,8u,9u,11u,0u}){
        __wrap_func_02046bbc(&model,&animation,frame);
        const unsigned key=frame>=9?2:frame>=5?1:0;
        expect(model,0,textures[key][0],palettes[pal_keys[key]][0]);
        assert(storage[0].texture==0&&storage[0].palette==1); // stale initial IDs stay unchanged
    }
    animation.numPalRecords=0;animation.unk_0c=nullptr;animation.unk_18=nullptr;
    __wrap_func_02046bbc(&model,&animation,5);expect(model,0,1,2); // preserve prior animated palette
    assert(hooks::activate(true));
    auto invalid=[&]{
        const auto before=updates,draws=submissions;
        assert(hooks::begin_frame());__wrap_func_02046bbc(&model,&animation,5);
        assert(updates==before);refused(model);
        __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
        assert(!hooks::end_frame()&&submissions==draws&&renders==0);
    };
    tex_keys[1]=3;invalid();tex_keys[1]=1;
    __wrap_func_02046bbc(&model,&animation,5);expect(model,0,1,2); // valid update recovers poisoned owner
    textures[1][0]=4;invalid();textures[1][0]=1;
    __wrap_func_02046bbc(&model,&animation,5);expect(model,0,1,2);
    track[4]=0;invalid();track[4]=3;
    track[0]=2;invalid();track[0]=0;
    // Key before the first start and overflowed key ranges are rejected.
    starts[0]=6;starts[1]=7;starts[2]=9;invalid();starts={{0,5,9}};
    track[5]=65535;invalid();track[5]=0;
    __wrap_func_02046bbc(&model,&animation,5);expect(model,0,1,2);
    animation.numPalRecords=2;animation.unk_0c=reinterpret_cast<char*>(palettes.data());
    animation.unk_18=reinterpret_cast<char*>(pal_keys.data());palettes[0][0]=65535;
    invalid();palettes[0][0]=0;
    __wrap_func_02046bbc(&model,&animation,5);expect(model,0,1,0);
    // Invalid inactive comparisons retain legacy forwarding, but poison native state.
    assert(hooks::activate(false));const auto forwarded=updates;
    __wrap_func_02046bbc(&model,&animation,12);assert(updates==forwarded+1);refused(model);
    __wrap_func_020462d0(&model,&file,storage.data());expect(model,0,0,1);assert(initializations==2);
    // Empty original sequences are legal no-ops and need no array pointers.
    BTP_File empty{};empty.unk_00=12;__wrap_func_02046bbc(&model,&empty,5);expect(model,0,0,1);
    __wrap_func_02046bbc(&model,&animation,9);expect(model,0,3,2);
    // Reuse with different file or material storage cannot return cached IDs.
    BMD_File other=file;model.modelFile=&other;refused(model);model.modelFile=&file;
    auto replacement=storage;model.materials=reinterpret_cast<BMD_Material*>(replacement.data());refused(model);
    __wrap_func_020462d0(&model,&file,replacement.data());expect(model,0,0,1);
    __wrap_func_02046bbc(&model,&animation,9);expect(model,0,3,2);
    __wrap__ZN13SharedFilePtr7ReleaseEv(handle); // retirement clears semantic entries before source free
    replacement[0].texture=1;expect(model,0,1,1);
    // Thousands of model lifetimes reuse tombstones without exhausting the table.
    for(unsigned i=0;i<2000;++i){
        handle[2]=1;__wrap__ZN13SharedFilePtr4LoadEv(handle);
        __wrap_func_020462d0(&model,&file,storage.data());
        __wrap_func_02046bbc(&model,&animation,i%12);
        __wrap__ZN13SharedFilePtr7ReleaseEv(handle);
    }
    // A full bounded table fails closed rather than borrowing another model's image.
    handle[2]=1;__wrap__ZN13SharedFilePtr4LoadEv(handle);
    std::array<ModelComponents,513> owners{};
    for(auto& owner:owners){owner.modelFile=&file;owner.materials=reinterpret_cast<BMD_Material*>(storage.data());
        __wrap_func_02046bbc(&owner,&animation,0);}
    for(const auto& owner:owners)refused(owner);
    std::puts("BTP keys, semantic IDs, palette retention, refusal, reuse and bounded lifetime tracking passed");
}
