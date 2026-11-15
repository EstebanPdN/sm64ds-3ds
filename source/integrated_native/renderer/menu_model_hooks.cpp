#include "model_components_hooks.hpp"
#include "particle_hooks.hpp"
#include "menu_backgrounds.hpp"
#include "menu_oam_hooks.hpp"
#include <cstdint>

// Overlay7 owns its own resource table and bypasses SharedFilePtr/Model::LoadFile.
// Its loader's explicit BMD switch proves these resource types; the table stores
// overlay0 handles, so resolve them through the original FAT-ID service. Observe
// only new allocations, after relocation. Never infer identities from pixels or
// virtual texture addresses. Source: func_ov007_020b3f20 / 020b3edc.
extern "C" {
extern void* data_ov007_02103370[];
extern std::uint16_t data_ov007_020d7584[];
unsigned func_02018a24(unsigned);
void* __real_func_ov007_020b3f20(int);
void __real_func_ov007_020b3edc(int);
}
namespace {
bool model_index(int index) noexcept {
    return (index>=1&&index<=3)||index==0x16||(index>=0x20&&index<=0x24);
}
}
extern "C" void* __wrap_func_ov007_020b3f20(int index){
    const bool observe=sm64ds::render::game_hooks::tracking_enabled()&&model_index(index)
        &&!data_ov007_02103370[index];
    void* file=__real_func_ov007_020b3f20(index);
    sm64ds::render::game_hooks::menu_background_file_loaded(index,file);
    sm64ds::render::game_hooks::menu_obj_file_loaded(index,file);
    if(index==0x17&&file)sm64ds::render::game_hooks::particle_file_loaded(file,1);
    if(observe&&file){
        const unsigned id=func_02018a24(data_ov007_020d7584[index]);
        if(id<=0xffffu)sm64ds::render::game_hooks::direct_model_loaded(std::uint16_t(id),file);
        else if(sm64ds::render::game_hooks::native_active())sm64ds::render::game_hooks::report_image_failure();
    }
    return file;
}
extern "C" void __wrap_func_ov007_020b3edc(int index){
    if(index==0x17)sm64ds::render::game_hooks::particle_file_retiring(data_ov007_02103370[index]);
    if(sm64ds::render::game_hooks::tracking_enabled()&&model_index(index))
        sm64ds::render::game_hooks::model_retiring(data_ov007_02103370[index]);
    __real_func_ov007_020b3edc(index);
}
