#include "model_components_hooks.hpp"
#include "texture_sequence_hooks.hpp"
#include "ModelBase.h"
#include <array>
#include <cstring>

#ifdef __3DS__
extern "C" {extern BMD_File data_020ad560,data_020ad524;} // ShadowModel::InitCylinder / InitCuboid
#endif

namespace sm64ds::render::game_hooks {
namespace {
struct Association { const void* file=nullptr;const ModelBindings* binding=nullptr;bool tombstone=false;
    std::uint32_t file_id=0xffffffffu;bool conflict=false; };
std::array<Association,512> files{};
ModelCatalog catalog;
Submit submit=nullptr;
SubmitShadow submit_shadow=nullptr;
Clock clock=nullptr;
void* context=nullptr;
bool installed=false,active=false,frame_open=false,fault=false;
Statistics counters;
std::size_t slot(const void* file) noexcept {
    std::uintptr_t value=reinterpret_cast<std::uintptr_t>(file)>>4;
    value^=value>>9;return std::size_t(value)&(files.size()-1);
}
Association* find(const void* file) noexcept {
    const auto start=slot(file);
    for(unsigned step=0;step<files.size();++step){
        auto& entry=files[(start+step)&(files.size()-1)];
        if(entry.file==file)return &entry;
        if(!entry.file&&!entry.tombstone)return nullptr;
    }
    return nullptr;
}
bool associate(const void* file,const ModelBindings* binding,std::uint32_t id) noexcept {
    const auto start=slot(file);Association* free=nullptr;
    for(unsigned step=0;step<files.size();++step){
        auto& entry=files[(start+step)&(files.size()-1)];
        if(entry.file==file){
            if(entry.binding==binding&&entry.file_id==id&&!entry.conflict)return true;
            // A missed lifetime boundary: the address now holds a NEW load.
            // Replace the stale identity (never draw through the old one).
            retire_image_tracking(static_cast<const BMD_File*>(file));
            entry={file,binding,false,id,false};return false;
        }
        if(!entry.file){if(!free)free=&entry;if(!entry.tombstone)break;}
    }
    if(!free)return false;
    *free={file,binding,false,id,false};return true;
}
void observe_load(const void* handle,const void* file,bool model=false) noexcept {
    if(!installed||!handle||!file)return;
    ++counters.load_notifications;if(model)++counters.typed_model_loads;
    std::uint16_t id;std::memcpy(&id,handle,2);
    const auto* binding=catalog.find(id);if(!model&&!binding&&!catalog.source_model(id))return;
    ++counters.observed_loads;
    if(!associate(file,binding,id)){++counters.lifetime_failures;if(active)fault=true;}
}
void observe_release(const void* handle) noexcept {
    if(!installed||!handle)return;
    const auto* bytes=static_cast<const unsigned char*>(handle);
    if(bytes[2]!=1)return; // original last-reference branch
    const void* file=nullptr;std::memcpy(&file,bytes+4,sizeof(file));
    if(!file)return;
    model_retiring(file);
}
bool dry_update=false;
std::array<MissingModel,max_missing_models> missing{};
std::size_t missing_count=0;std::uint32_t missing_overflow=0;
void copy_name(char* out,std::size_t size,const char* name) noexcept {
    std::size_t n=0;
    if(name)for(;n+1<size&&name[n];++n){const unsigned char c=static_cast<unsigned char>(name[n]);out[n]=c>=0x20&&c<0x7f&&c!=','?char(c):'?';}
    out[n]=0;
}
void note_missing(const BMD_File* file,const Association* entry) noexcept {
    for(std::size_t i=0;i<missing_count;++i)if(missing[i].file==file){++missing[i].calls;return;}
    if(missing_count==missing.size()){++missing_overflow;return;}
    auto& m=missing[missing_count++];m={};m.file=file;m.calls=1;
    m.file_id=entry?entry->file_id:0xffffffffu;m.reason=!entry?1:entry->conflict?3:2;
    m.bones=std::uint16_t(file->numBones);m.materials=std::uint16_t(file->numMaterials);m.textures=std::uint16_t(file->numTextures);
    m.palettes=std::uint16_t(file->numPalettes);m.groups=std::uint16_t(file->numDisplayListGroups);
    // The game itself reads these fixed-up names when it binds materials.
    if(file->numMaterials&&file->numMaterials<256&&file->materials)copy_name(m.material,sizeof m.material,file->materials[0].name);
    if(file->numTextures&&file->numTextures<256&&file->textures)copy_name(m.texture,sizeof m.texture,file->textures[0].name);
}
void produce(const ModelComponents* model,const Matrix4x3* view,const Vector3* scale) noexcept {
    ++counters.model_calls;
    if(dry_update){++counters.dry_calls;return;}
    if(!frame_open||!model||!model->modelFile||!image_state_valid(*model)){++counters.producer_failures;fault=true;return;}
#ifdef __3DS__
    if(submit_shadow&&(model->modelFile==&data_020ad560||model->modelFile==&data_020ad524)){
        // ShadowModel::RenderAll draws each volume twice (mask, then render).
        if(!submit_shadow(*model,view,model->modelFile==&data_020ad524,context)){++counters.producer_failures;fault=true;}
        return;
    }
#endif
    auto* entry=find(model->modelFile);
    if(!entry||!entry->binding||entry->conflict){++counters.missing_models;counters.last_missing_file=entry?entry->file_id:0xffffffffu;
        counters.last_missing_address=reinterpret_cast<std::uintptr_t>(model->modelFile);note_missing(model->modelFile,entry);fault=true;return;}
    const auto& binding=*entry->binding;
    if(model->modelFile->numBones!=binding.bones || model->modelFile->numMaterials!=binding.source_materials){
        ++counters.producer_failures;fault=true;return;
    }
    const auto start=clock?clock():0;
    const bool result=submit(binding,*model,view,scale,context);
    if(clock)counters.producer_ticks+=clock()-start;
    if(!result){++counters.producer_failures;fault=true;return;}
    ++counters.submitted;
}
}
bool install(ModelCatalog source,Submit producer,void* user) noexcept {
    if(installed||active||frame_open||!source.size()||!producer)return false;
    catalog=source;submit=producer;context=user;initialize_image_tracking();installed=true;return true;
}
void install_shadows(SubmitShadow producer) noexcept {submit_shadow=producer;}
bool activate(bool value) noexcept {
    if(!installed||frame_open)return false;
    active=value;fault=false;return true;
}
bool set_clock(Clock source) noexcept {
    if(frame_open)return false;
    clock=source;return true;
}
bool begin_frame() noexcept {
    if(!active||frame_open)return false;
    fault=false;frame_open=true;return true;
}
bool end_frame() noexcept {
    if(!active||!frame_open)return false;
    frame_open=false;return !fault;
}
const Statistics& statistics() noexcept {return counters;}
std::size_t missing_census(const MissingModel** table,std::uint32_t* overflow) noexcept {
    if(table)*table=missing.data();if(overflow)*overflow=missing_overflow;return missing_count;
}
void clear_missing_census() noexcept {missing_count=0;missing_overflow=0;}
bool tracking_enabled() noexcept {return installed;}
bool native_active() noexcept {return active;}
namespace {bool bridge_2d=false;}
bool native_2d_active() noexcept {return active&&!bridge_2d;}
void set_bridge_2d(bool value) noexcept {bridge_2d=value;}
void set_dry(bool value) noexcept {dry_update=value;}
bool dry() noexcept {return dry_update;}
bool tracks_model(const ModelComponents& model) noexcept {const auto* e=find(model.modelFile);return e&&e->binding;}
const ModelBindings* binding_for_file(const void* file) noexcept {
    if(!installed||!file)return nullptr;
    const auto* entry=find(file);return entry&&!entry->conflict?entry->binding:nullptr;
}
void report_image_failure() noexcept {++counters.producer_failures;if(active)fault=true;}
void model_loaded(const void* handle,const void* file) noexcept {observe_load(handle,file,true);}
void direct_model_loaded(std::uint16_t id,const void* file) noexcept {
    if(!installed||!file)return;
    ++counters.direct_model_loads;
    observe_load(&id,file,true);
}
void raw_file_loaded(std::uint16_t id,const void* file) noexcept {
    if(!installed||!file)return;
    ++counters.direct_model_loads;observe_load(&id,file,false);
}
void model_retiring(const void* file) noexcept {
    if(!installed||!file)return;
    if(auto* entry=find(file)){
        retire_image_tracking(static_cast<const BMD_File*>(file));
        *entry={nullptr,nullptr,true};++counters.observed_releases;
    }
}
}

extern "C" {
void* __real__ZN13SharedFilePtr4LoadEv(void*);
#ifdef __3DS__
void* __real_func_0201818c(int,int);
unsigned func_02018a24(unsigned);
// Raw ov0-handle loader behind LoadFile(u16) (stage models, many actors).
// It bypasses SharedFilePtr, so record the FAT identity here.
void* __wrap_func_0201818c(int handle,int mode) {
    void* file=__real_func_0201818c(handle,mode);
    if(file&&sm64ds::render::game_hooks::tracking_enabled()){
        const unsigned id=func_02018a24(unsigned(handle));
        if(id<=0xffffu)sm64ds::render::game_hooks::raw_file_loaded(std::uint16_t(id),file);
    }
    return file;
}
#endif
void __real__ZN13SharedFilePtr7ReleaseEv(void*);
void __real__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents*,Matrix4x3*,Vector3*);
void* __wrap__ZN13SharedFilePtr4LoadEv(void* handle) {
    void* file=__real__ZN13SharedFilePtr4LoadEv(handle);
    sm64ds::render::game_hooks::observe_load(handle,file);return file;
}
void __wrap__ZN13SharedFilePtr7ReleaseEv(void* handle) {
    // Retire the identity BEFORE deallocation can recycle its address.
    sm64ds::render::game_hooks::observe_release(handle);
    __real__ZN13SharedFilePtr7ReleaseEv(handle);
}
void __wrap__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(ModelComponents* model,Matrix4x3* view,Vector3* scale) {
    if(!sm64ds::render::game_hooks::active){
        __real__ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(model,view,scale);return;
    }
    // Native mode never falls through into graphics instruction submission.
    // Missing models/errors invalidate this native frame; game logic continues.
    sm64ds::render::game_hooks::produce(model,view,scale);
}
}
