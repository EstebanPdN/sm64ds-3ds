#include "texture_sequence_hooks.hpp"
#include "model_components_hooks.hpp"
#include "ModelBase.h"
#include "TextureSequence.h"
#include <array>
#include <cstring>

namespace sm64ds::render::game_hooks {
namespace {
constexpr unsigned Capacity=512;
constexpr std::uint16_t Poison=65535;
struct Selection {
    const ModelComponents* owner=nullptr;
    const BMD_Material* storage=nullptr;
    const BMD_File* file=nullptr;
    std::uint16_t material=0,texture=0,palette=0;
    bool tombstone=false;
};
struct Track {std::uint16_t material,padding[3],count,offset;};
static_assert(sizeof(Track)==12);
struct Choice {std::uint16_t material,texture,palette;};
std::array<Selection,Capacity> entries{};
std::array<Choice,Capacity> choices{}; // synchronous game-thread scratch, not stack
bool exhausted=false;
// BUILD134 (exact cut): one bit per owner hash, set when an entry is stored
// for that owner and cleared only with the whole table. A clear bit proves
// the table holds nothing for the owner, so the lookup (a probe into a 10 KB
// table for every visible material of every model) is skipped; a set bit
// only means "look". Most models never run a texture sequence.
std::uint64_t owners=0;
std::uint64_t owner_bit(const ModelComponents& owner) noexcept {
    auto value=reinterpret_cast<std::uintptr_t>(&owner)>>4;value^=value>>6;value^=value>>12;
    return std::uint64_t(1)<<(value&63u);
}
std::size_t start(const ModelComponents& owner,unsigned material) noexcept {
    auto value=reinterpret_cast<std::uintptr_t>(&owner)>>4;value^=value>>9;
    return (value^(std::uintptr_t(material)*0x9e37u))&(Capacity-1);
}
Selection* find(const ModelComponents& owner,unsigned material) noexcept {
    if(!(owners&owner_bit(owner)))return nullptr;
    const auto first=start(owner,material);
    for(unsigned i=0;i<Capacity;++i){auto& e=entries[(first+i)&(Capacity-1)];
        if(e.owner==&owner&&e.material==material)return &e;
        if(!e.owner&&!e.tombstone)return nullptr;
    }
    return nullptr;
}
bool put(const ModelComponents& owner,unsigned material,unsigned texture,unsigned palette) noexcept {
    const auto first=start(owner,material);Selection* free=nullptr;
    for(unsigned i=0;i<Capacity;++i){auto& e=entries[(first+i)&(Capacity-1)];
        if(e.owner==&owner&&e.material==material){e.storage=owner.materials;e.file=owner.modelFile;e.texture=texture;e.palette=palette;return true;}
        if(!e.owner){if(!free)free=&e;if(!e.tombstone)break;}
    }
    if(!free){exhausted=true;return false;}
    owners|=owner_bit(owner);
    *free={&owner,owner.materials,owner.modelFile,std::uint16_t(material),std::uint16_t(texture),std::uint16_t(palette),false};return true;
}
void remove(Selection& e) noexcept {e={};e.tombstone=true;}
std::uint16_t half(const char* p) noexcept {std::uint16_t value;std::memcpy(&value,p,2);return value;}
bool initial(const ModelComponents& model,unsigned material,ImageIdentity& output) noexcept {
    if(!model.modelFile||!model.materials||material>=model.modelFile->numMaterials)return false;
    game::RuntimeMaterial source;
    std::memcpy(&source,reinterpret_cast<const char*>(model.materials)+material*sizeof(source),sizeof(source));
    output={source.texture,source.palette};return true;
}
bool current(const ModelComponents& model,unsigned material,ImageIdentity& output) noexcept {
    if(const auto* e=find(model,material)){
        if(e->storage!=model.materials||e->file!=model.modelFile)return false;
        output={e->texture,e->palette==Poison?ModelBank::no_texture:e->palette};return true;
    }
    return initial(model,material,output);
}
bool select(const ModelComponents& model,const BTP_File& file,unsigned frame) noexcept {
    if(!model.modelFile||!model.materials||!file.unk_00||frame>=file.unk_00||file.numSeqRecords>Capacity)return false;
    if(!file.numSeqRecords)return true; // original updater's empty loop
    if(!file.unk_20||!file.unk_10||!file.unk_14||
       !file.numTexRecords||!file.unk_04||(file.numPalRecords&&(!file.unk_0c||!file.unk_18)))return false;
    // BTP is trusted live game animation storage. Count/range checks bound its
    // native observation; file/relocation validation remains the loader's job.
    for(unsigned i=0;i<file.numSeqRecords;++i){
        Track track;std::memcpy(&track,file.unk_20+i*sizeof(Track),sizeof(track));
        if(track.material==Poison||track.material>=model.modelFile->numMaterials||!track.count||
           unsigned(track.offset)+track.count>65536)return false;
        unsigned key=unsigned(track.offset)+track.count;
        do{--key;if(half(file.unk_10+key*2)<=frame)break;}while(key>track.offset);
        if(half(file.unk_10+key*2)>frame)return false;
        const auto texture_record=half(file.unk_14+key*2);
        if(texture_record>=file.numTexRecords)return false;
        const auto texture=half(file.unk_04+texture_record*8);
        if(texture==Poison||texture>=model.modelFile->numTextures)return false;
        ImageIdentity previous;if(!current(model,track.material,previous))return false;
        std::uint32_t palette=previous.palette;
        if(file.numPalRecords){
            const auto palette_record=half(file.unk_18+key*2);
            if(palette_record>=file.numPalRecords)return false;
            palette=half(file.unk_0c+palette_record*8);
            // The recovered original's unsigned comparison to -1 is ambiguous;
            // do not accept a missing-name sentinel as an actual palette index.
            if(palette==Poison||palette>=model.modelFile->numPalettes)return false;
        }
        if(palette!=ModelBank::no_texture&&palette>=Poison)return false;
        choices[i]={track.material,texture,std::uint16_t(palette)};
    }
    return true;
}
void poison(const ModelComponents& model) noexcept {put(model,Poison,0,0);}
void commit(const ModelComponents& model,const BTP_File& file) noexcept {
    for(unsigned i=0;i<file.numSeqRecords;++i)if(!put(model,choices[i].material,choices[i].texture,choices[i].palette)){
        report_image_failure();return;
    }
    if(auto* e=find(model,Poison))remove(*e);
}
}
void initialize_image_tracking() noexcept {entries={};exhausted=false;owners=0;}
void reset_image_tracking(const ModelComponents& model) noexcept {
    if(!tracking_enabled())return;
    for(auto& entry:entries)if(entry.owner==&model)remove(entry);
}
void retire_image_tracking(const BMD_File* file) noexcept {
    for(auto& entry:entries)if(entry.owner&&entry.file==file)remove(entry);
}
bool image_state_valid(const ModelComponents& model) noexcept {
    return !exhausted&&!find(model,Poison);
}
bool selected_image(const ModelComponents& model,unsigned material,ImageIdentity& output) noexcept {
    if(!image_state_valid(model))return false;
    ImageIdentity next;if(!current(model,material,next))return false;
    output=next;return true;
}
bool selected_image_checked(const ModelComponents& model,unsigned material,ImageIdentity& output) noexcept {
    ImageIdentity next;if(!current(model,material,next))return false;
    output=next;return true;
}
}

extern "C" {
void __real_func_020462d0(ModelComponents*,BMD_File*,void*);
void __wrap_func_020462d0(ModelComponents* model,BMD_File* file,void* arena){
    __real_func_020462d0(model,file,arena);
    if(model)sm64ds::render::game_hooks::reset_image_tracking(*model);
}
void __real_func_02046bbc(ModelComponents*,BTP_File*,unsigned);
void __wrap_func_02046bbc(ModelComponents* model,BTP_File* file,unsigned frame){
    using namespace sm64ds::render::game_hooks;
    if(!model||!file||!tracking_enabled()||!tracks_model(*model)){__real_func_02046bbc(model,file,frame);return;}
    const bool valid=select(*model,*file,frame);
    if(valid||!native_active())__real_func_02046bbc(model,file,frame);
    if(!valid){poison(*model);report_image_failure();return;}
    commit(*model,*file);
}
}
