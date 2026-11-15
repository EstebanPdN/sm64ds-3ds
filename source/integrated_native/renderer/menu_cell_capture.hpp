#pragma once
#include "image_layer.hpp"
#include "image_catalog.hpp"
namespace sm64ds::render::game_hooks {
struct MenuCellPart {std::int16_t x,y;std::uint16_t width,height;};
struct MenuCellImage {
    const MenuCellPart* part;
    std::uint16_t parts,image;
    std::int16_t left,top;
    std::uint16_t width,height;
    std::uint32_t character_end;
};
struct MenuCellBank {
    std::uint16_t source_bank,image_bank;
    const MenuCellImage* cell;
    std::uint16_t cells;
    std::uint32_t cell_bytes,cell_crc,character_bytes,character_crc,palette_bytes,palette_crc;
    std::array<std::uint8_t,32> image_source_sha256{};
};
struct MenuCellCatalog {const MenuCellBank* bank=nullptr;std::size_t banks=0;};
MenuCellCatalog prepared_menu_cells() noexcept;
// Original affine coefficients are Fix12; hardware consumes signed8.8 after
// shift4 and signed16 reduction. Identity is safe to submit as an ordinary cell.
bool menu_cell_affine_identity(const std::array<std::int32_t,4>&) noexcept;
Error validate(MenuCellCatalog) noexcept;
Error validate(MenuCellCatalog,ImageCatalog) noexcept;
// These are typed original producer arguments, not an OAM packet. Identity
// and native screen routing have already been resolved by the source seam.
struct MenuCellEvent {
    const MenuCellBank* bank=nullptr;
    std::uint16_t cell=0,part=0,slot=0;
    Screen screen=Screen::top;
    int x=0,y=0,priority=0,affine=-1;
    std::uint32_t flip=0;
    bool mosaic=false;
};
using SubmitMenuCell=bool (*)(const ImageLayer&,void*);
class MenuCellCapture {
public:
    Error begin(SubmitMenuCell,void*) noexcept;
    Error part(const MenuCellEvent&) noexcept;
    Error finish() noexcept;
    Error failure() const noexcept {return failure_;}
    void set_lenient(bool value) noexcept {lenient_=value;}
private:
    SubmitMenuCell sink_=nullptr;void* user_=nullptr;
    MenuCellEvent first_{};
    std::uint16_t next_=0;
    bool active_=false;
    bool lenient_=false;
    Error failure_=Error::none;
    Error group_failure_=Error::none;
    Error fail(Error e) noexcept {if(failure_==Error::none)failure_=e;group_failure_=e;return e;}
};
}
