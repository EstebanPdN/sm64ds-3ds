#pragma once
#include "menu_cell_capture.hpp"
#include "menu_obj_catalog.hpp"
namespace sm64ds::render::game_hooks {
struct MenuOamStatistics {
    std::uint64_t resources=0,calls=0,associated=0,unassociated=0;
    std::uint64_t captured=0,failures=0,cache_hits=0,capture_ticks=0;
    unsigned live_cells=0;
};
// Original animation remains authoritative. In native mode only typed image
// placements are produced; software OAM encoding/transfers are skipped.
using MenuScreenSource=Screen (*)();
bool install_menu_cells(MenuCellCatalog,SubmitMenuCell,void*,MenuObjCatalog={},MenuScreenSource=nullptr) noexcept;
void menu_obj_file_loaded(int,const void*) noexcept;
bool menu_oam_begin() noexcept;
bool menu_oam_end() noexcept;
void menu_oam_lenient(bool) noexcept;
bool menu_presentation_mode(unsigned,Screen&,std::uint16_t&,Screen main_screen=Screen::bottom) noexcept;
void menu_oam_scene_changed() noexcept;
bool menu_oam_presentation(Screen&,std::uint16_t&) noexcept;
void menu_oam_clock(std::uint64_t (*)()) noexcept;
void menu_oam_trace(bool) noexcept;
const MenuOamStatistics& menu_oam_statistics() noexcept;
}
