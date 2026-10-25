#include "sm64ds/native_runtime.hpp"
#include "sm64ds/native_data.hpp"
#include "sm64ds/game_data.hpp"
#include "sm64ds/game_files.hpp"
#include "sm64ds/game_symbols.hpp"
#include <array>
#include <cstring>
#include <limits>

struct Section { uint8_t* destination; uint32_t module, address, size, zero; };
struct Choice { uint32_t section, offset, expected, first, count; };
struct Candidate { uint32_t module, address; };
struct Constructor { uint32_t module; void (*call)(); };
extern "C" {
extern const Section sm64ds_runtime_sections[],sm64ds_runtime_sections_end[];
extern const sm64ds::NativeDataFixup sm64ds_runtime_fixups[],sm64ds_runtime_fixups_end[];
extern const Choice sm64ds_runtime_choices[],sm64ds_runtime_choices_end[];
extern const Candidate sm64ds_runtime_candidates[],sm64ds_runtime_candidates_end[];
extern const Constructor sm64ds_runtime_ctors[],sm64ds_runtime_ctors_end[];
extern uint16_t* data_0209d3b8;
extern void* data_0209d3b4;
extern uint8_t data_020a8074[];
int sm64ds_native_register_rom_archive();
struct ArchiveRecord {void* image;void* heap;uint16_t base,end;const char* name;const char* path;};
extern ArchiveRecord data_0208ecf4[13];
}
static_assert(sizeof(ArchiveRecord)==20);
namespace {
std::array<bool,104> initialized{};
bool loaded=false, main_initialized=false;
std::vector<uint16_t> handles;
}

namespace sm64ds {
bool native_runtime_load(AssetPack& pack,std::string& error) {
    if(loaded) { error="Original runtime already loaded";return false; }
    // The recovered source/table addresses are ASMP0. A USA mapping must not be
    // guessed from a constant offset. Both supplied ROMs remain untouched.
    const auto& info=pack.info();
    if(info.code!="ASMP" || info.revision!=0 || info.crc!=0x29715dec) {
        error="General runtime currently requires the supplied ASMP0 ROM";return false;
    }
    const size_t count=sm64ds_runtime_sections_end-sm64ds_runtime_sections;
    std::vector<std::vector<uint8_t>> staging(count);
    std::vector<NativeDataSection> sections(count);
    game_data::OverlayImage image;uint32_t current=0xfffffffe;
    for(size_t i=0;i<count;++i) {
        const auto& row=sm64ds_runtime_sections[i];
        sections[i]={row.destination,nullptr,row.size,row.zero};
        if(row.zero)continue;
        if(current!=row.module) {
            bool okay=row.module==UINT32_MAX ? game_data::load_arm9(pack,image,error)
                : game_data::load_overlay(pack,row.module,image,error);
            if(!okay)return false;
            current=row.module;
        }
        size_t offset=0;
        if(!image.range(row.address,row.size,offset)) {
            error="Original data section outside its ROM module";return false;
        }
        staging[i].assign(image.bytes.begin()+offset,image.bytes.begin()+offset+row.size);
        sections[i].source=staging[i].data();
    }
    // Ambiguous overlay slots deliberately stay null until their module is
    // activated. Validate their ROM words before clearing the staged copy;
    // the strict relocation primitive continues to accept only real pointers.
    std::vector<NativeDataFixup> resolved;
    for(auto fix=sm64ds_runtime_fixups;fix!=sm64ds_runtime_fixups_end;++fix) {
        if(fix->replacement) {resolved.push_back(*fix);continue;}
        if(fix->section>=count || (fix->offset&3) || sections[fix->section].zero_fill ||
           staging[fix->section].size()<4 || fix->offset>staging[fix->section].size()-4) {
            error="Invalid deferred original data slot";return false;
        }
        uint32_t original=0;
        auto* word=staging[fix->section].data()+fix->offset;
        std::memcpy(&original,word,4);
        if(original!=fix->expected) {error="Deferred original pointer differs from ROM";return false;}
        std::memset(word,0,4);
    }
    const auto result=sm64ds_restore_native_data(sections.data(),uint32_t(count),
        resolved.data(),uint32_t(resolved.size()));
    if(result!=NativeDataResult::success) {
        error="General native data relocation rejected: "+std::to_string(uint32_t(result));return false;
    }
    game_data::HandleTable catalog;
    if(!game_data::load_handles(pack,catalog,error))return false;
    handles.reserve(catalog.asset_indices.size());
    for(auto index:catalog.asset_indices) {
        const auto id=pack.info().files[index].id;
        if(id>=0x8000) { error="Original handle references an invalid FAT ID";return false; }
        handles.push_back(uint16_t(id));
    }
    if(handles.size()!=2058) { error="Unexpected ASMP0 handle count";return false; }
    // The shared preview library owns separate compiled storage for the
    // verified trigonometry/scale constants and eight DSMT callbacks. Restoring
    // the generic ROM sections cannot publish those globals. Use the existing
    // checked ASMP0 initializer before any original scene can read them; an
    // unresolved callback must reject boot instead of becoming a null call.
    if(!sm64ds_prepare_game_data(pack,error))return false;
    data_0209d3b8=handles.data();
    loaded=true;error.clear();return true;
}

bool native_runtime_activate(uint32_t module) {
    if(!loaded || module>=initialized.size())return false;
    // Module groups in the original metadata denote mutually overlaid storage.
    // Its pointers follow the selected module, even though native storage is
    // separate. No arbitrary 'first overlay' is used for an ambiguous pointer.
    for(auto choice=sm64ds_runtime_choices;choice!=sm64ds_runtime_choices_end;++choice) {
        for(uint32_t n=0;n<choice->count;++n) {
            const auto& candidate=sm64ds_runtime_candidates[choice->first+n];
            if(candidate.module!=module)continue;
            auto* destination=sm64ds_runtime_sections[choice->section].destination+choice->offset;
            std::memcpy(destination,&candidate.address,sizeof(candidate.address));
        }
    }
    if(!initialized[module]) {
        initialized[module]=true;
        for(auto ctor=sm64ds_runtime_ctors;ctor!=sm64ds_runtime_ctors_end;++ctor)
            if(ctor->module==module)ctor->call();
    }
    return true;
}

bool native_runtime_bind_archives(GameFiles& files,std::string& error) {
    if(!loaded) {error="Original archive table not initialized";return false;}
    for(const auto& row:data_0208ecf4) {
        if(!row.path) {error="Original NARC path was not relocated";return false;}
        const char* path=row.path;while(*path=='/')++path;
        if(!files.bind_archive_range(row.base,row.end,path,error))return false;
    }
    return true;
}

bool native_runtime_initialize_main() {
    if(!loaded || main_initialized)return false;
    main_initialized=true;
    for(auto ctor=sm64ds_runtime_ctors;ctor!=sm64ds_runtime_ctors_end;++ctor)
        if(ctor->module==UINT32_MAX)ctor->call();
    return true;
}

uint32_t native_runtime_unresolved_data() {
    uint32_t count=0;
    for(auto choice=sm64ds_runtime_choices;choice!=sm64ds_runtime_choices_end;++choice) {
        uint32_t value=0;
        std::memcpy(&value,sm64ds_runtime_sections[choice->section].destination+choice->offset,4);
        count+=value==0;
    }
    return count;
}
}

// Native executable sections already own every recompiled overlay. Activating
// one selects its original pointer aliases and runs its original constructors;
// it must never copy cartridge instructions over the native executable.
extern "C" int FS_LoadOverlay(int processor,int id) {
    if(processor!=0 || id<0 || !sm64ds::native_runtime_activate(uint32_t(id)))return 0;
    return 1;
}
extern "C" void LoadOverlay(int id) {
    if(!FS_LoadOverlay(0,id))sm64ds_native_missing("LoadOverlay: unavailable native module");
}
extern "C" void UnloadOverlay(int id) {
    // Native code/storage stays resident. Original actor destruction remains
    // the scene's responsibility, as before its call to UnloadOverlay.
    if(id<0 || id>=104)sm64ds_native_missing("UnloadOverlay: invalid module");
}
extern "C" void func_02018aa4() {
    // Cartridge filesystem installation: bytes now come from GameFiles and the
    // verified ROM catalog, already bound before the original game boot begins.
    if(!loaded || handles.empty())sm64ds_native_missing("Native cartridge filesystem not ready");
    if(!sm64ds_native_register_rom_archive())
        sm64ds_native_missing("Native cartridge filesystem ROM registry initialization failed");
    data_0209d3b8=handles.data();
    data_0209d3b4=data_020a8074;
}
