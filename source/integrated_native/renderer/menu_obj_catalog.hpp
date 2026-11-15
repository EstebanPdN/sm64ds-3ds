#pragma once
#include "menu_cell_capture.hpp"
namespace sm64ds::render::game_hooks {
struct MenuObjFile {
    std::uint16_t member=0;
    std::uint32_t bytes=0,crc=0;
};
struct MenuObjView {
    std::uint8_t layout=0,engine=0,languages=0;
    MenuCellCatalog cells{};
    MenuObjFile replacement{},icons{},icon_palette{};
};
// Immutable native resources selected by semantic menu state. No OAM, VRAM
// addresses or mutable source pointers enter this catalog.
struct MenuObjCatalog {const MenuObjView* view=nullptr;std::size_t views=0;};
Error validate(MenuObjCatalog,ImageCatalog) noexcept;
const MenuObjView* menu_obj_view(MenuObjCatalog,unsigned layout,unsigned engine,unsigned language) noexcept;
MenuObjCatalog prepared_menu_objects() noexcept;
// Asset-load transaction. Only identities/CRCs survive the source loader;
// frame producers receive a view after its required files are observed.
class MenuObjAssets {
public:
    void reset() noexcept;
    Error begin(MenuObjCatalog,unsigned layout,unsigned language) noexcept;
    const MenuObjFile* expected_file(unsigned member) const noexcept;
    Error file(unsigned member,std::uint32_t bytes,std::uint32_t crc) noexcept;
    Error commit(unsigned engine) noexcept;
    const MenuObjView* pending(unsigned engine) const noexcept {return pending_[engine];}
    const MenuObjView* view(unsigned engine) const noexcept {return ready_[engine];}
private:
    std::array<const MenuObjView*,2> pending_{},ready_{};
    std::array<bool,2> replacement_{};
    MenuObjFile icons_{},palette_{};
    bool failed_=false;
};
}
