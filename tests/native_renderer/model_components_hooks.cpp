#include "model_components_hooks.hpp"
#include "ModelBase.h"
#include <cassert>
#include <cstring>
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
extern "C" void* __wrap__ZN13SharedFilePtr4LoadEv(void*);
extern "C" void __wrap__ZN13SharedFilePtr7ReleaseEv(void*);
extern "C" void* __wrap__ZN5Model8LoadFileER13SharedFilePtr(void*);
extern "C" void* __wrap_func_ov007_020b3f20(int);
extern "C" void __wrap_func_ov007_020b3edc(int);
extern "C" void __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents*,Matrix4x3*,Vector3*);
static void* loaded=nullptr;
static unsigned old_renders=0,native_renders=0;
static bool accept=true;
static unsigned clock_calls=0;
extern "C" {
void* data_ov007_02103370[0x2e]{};
std::uint16_t data_ov007_020d7584[0x2e]{};
unsigned func_02018a24(unsigned handle){assert(handle==0x2b3);return 261;}
void* __real_func_ov007_020b3f20(int index){
    assert(index>=0&&index<0x2e);
    if(index&&!data_ov007_02103370[index])data_ov007_02103370[index]=loaded;
    return data_ov007_02103370[index];
}
void __real_func_ov007_020b3edc(int index){
    assert(index>=0&&index<0x2e);
    if(index==1&&data_ov007_02103370[index]){
        // Identity must retire BEFORE the original deallocator can recycle it.
        ModelComponents m{};m.modelFile=static_cast<BMD_File*>(loaded);
        assert(!hooks::tracks_model(m));
    }
    data_ov007_02103370[index]=nullptr;
}
}
static std::uint64_t tick(){return ++clock_calls*100u;}
extern "C" void __real_func_020462d0(void*,void*,void*){}
extern "C" void __real_func_02046bbc(void*,void*,unsigned){}
extern "C" void* __real__ZN13SharedFilePtr4LoadEv(void* h) {std::memcpy(static_cast<char*>(h)+4,&loaded,sizeof(loaded));return loaded;}
extern "C" void* __real__ZN5Model8LoadFileER13SharedFilePtr(void* h){return __real__ZN13SharedFilePtr4LoadEv(h);}
extern "C" void __real__ZN13SharedFilePtr7ReleaseEv(void* h) {--static_cast<unsigned char*>(h)[2];}
extern "C" void __real__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents*,Matrix4x3*,Vector3*) {++old_renders;}
static unsigned expected_file=261;
static bool submit(const ModelBindings& b,const ModelComponents& m,const Matrix4x3*,const Vector3*,void* user){
    assert(user==&loaded && b.file_id==expected_file && m.modelFile==loaded);++native_renders;return accept;
}
int main() {
    const MaterialBinding material{0,1,1,ColourSource::literal,false};const PartBinding part{0,0,false};
    const ModelBindings bindings[]={{261,{},1,1,nullptr,0,&material,1,&part,1},
                                   {262,{},1,1,nullptr,0,&material,1,&part,1}};ModelCatalog catalog;
    const std::uint16_t source_models[]={261,262,263};
    assert(ModelCatalog::open(bindings,2,catalog,source_models,3)==Error::none);
    BMD_File file{};file.numBones=file.numMaterials=1;ModelComponents model{};model.modelFile=&file;
    __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);assert(old_renders==1);
    assert(hooks::install(catalog,submit,&loaded));assert(!hooks::install(catalog,submit,&loaded));
    unsigned char handle[16]{};const unsigned short id=261;std::memcpy(handle,&id,2);handle[2]=1;
    loaded=&file;assert(__wrap__ZN13SharedFilePtr4LoadEv(handle)==loaded);
    assert(hooks::activate(true)&&hooks::begin_frame());
    __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    assert(hooks::end_frame()&&old_renders==1&&native_renders==1);
    accept=false;assert(hooks::begin_frame());
    __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    assert(!hooks::end_frame()&&old_renders==1);accept=true;
    __wrap__ZN13SharedFilePtr7ReleaseEv(handle);
    assert(hooks::begin_frame());__wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    assert(!hooks::end_frame()&&hooks::statistics().missing_models==1&&old_renders==1);
    // Reuse the same original allocation address over many scene lifetimes.
    for(unsigned i=0;i<5000;++i){
        handle[2]=1;__wrap__ZN13SharedFilePtr4LoadEv(handle);assert(hooks::begin_frame());
        __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);assert(hooks::end_frame());
        __wrap__ZN13SharedFilePtr7ReleaseEv(handle);
    }
    assert(old_renders==1&&hooks::statistics().lifetime_failures==0);
    handle[2]=1;__wrap__ZN13SharedFilePtr4LoadEv(handle);
    const unsigned short other=262;std::memcpy(handle,&other,2);
    __wrap__ZN13SharedFilePtr4LoadEv(handle);auto before=native_renders;expected_file=262;
    assert(hooks::begin_frame());__wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    // A confirmed new load retires old image tracking and replaces the stale
    // association. The draw must use the NEW file, never the former identity.
    assert(hooks::end_frame() && native_renders==before+1 && hooks::statistics().lifetime_failures==1);
    ++before;expected_file=261;
    __wrap__ZN13SharedFilePtr7ReleaseEv(handle);
    std::memcpy(handle,&id,2);handle[2]=1;__wrap__ZN13SharedFilePtr4LoadEv(handle);
    assert(hooks::begin_frame());__wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    assert(hooks::end_frame() && native_renders==before+1);
    assert(hooks::statistics().producer_ticks==0 && hooks::set_clock(tick));
    assert(hooks::begin_frame() && !hooks::set_clock(nullptr));
    __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    assert(hooks::end_frame() && hooks::statistics().producer_ticks==100 && clock_calls==2);
    assert(hooks::set_clock(nullptr) && hooks::begin_frame());
    __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    assert(hooks::end_frame() && clock_calls==2);
    assert(hooks::activate(false));
    __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);assert(old_renders==2);
    // Unsupported source models retain their logical FAT identity for useful
    // migration diagnostics; they never masquerade as a native binding.
    __wrap__ZN13SharedFilePtr7ReleaseEv(handle);
    const std::uint16_t unsupported=263;std::memcpy(handle,&unsupported,2);handle[2]=1;
    __wrap__ZN13SharedFilePtr4LoadEv(handle);assert(hooks::activate(true)&&hooks::begin_frame());
    __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    assert(!hooks::end_frame()&&hooks::statistics().last_missing_file==263&&old_renders==2);
    __wrap__ZN13SharedFilePtr7ReleaseEv(handle);
    const std::uint16_t archive=0x8020;std::memcpy(handle,&archive,2);handle[2]=1;
    assert(__wrap__ZN5Model8LoadFileER13SharedFilePtr(handle)==loaded&&hooks::begin_frame());
    __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
    assert(!hooks::end_frame()&&hooks::statistics().last_missing_file==archive&&old_renders==2);
    __wrap__ZN13SharedFilePtr7ReleaseEv(handle);
    data_ov007_020d7584[1]=0x2b3;
    const auto direct_before=hooks::statistics().direct_model_loads;
    for(unsigned i=0;i<5000;++i){
        assert(__wrap_func_ov007_020b3f20(1)==loaded&&hooks::tracks_model(model));
        const auto count=hooks::statistics().direct_model_loads;
        assert(count==direct_before+i+1);
        assert(__wrap_func_ov007_020b3f20(1)==loaded&&hooks::statistics().direct_model_loads==count);
        assert(hooks::begin_frame());
        __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(&model,nullptr,nullptr);
        assert(hooks::end_frame());
        __wrap_func_ov007_020b3edc(1);assert(!hooks::tracks_model(model));
    }
    // Animations, textures and slot zero are not BMD identities.
    assert(__wrap_func_ov007_020b3f20(0)==nullptr);
    assert(__wrap_func_ov007_020b3f20(4)==loaded&&!hooks::tracks_model(model));
    __wrap_func_ov007_020b3edc(4);
    assert(hooks::statistics().direct_model_loads==direct_before+5000&&old_renders==2);
}
