#pragma once
#include "menu_backgrounds.hpp"
namespace sm64ds::render::game_hooks {
struct MenuBackdropPart {
    std::uint32_t map_crc=0;
    std::uint16_t part=0;
    std::uint8_t palette=0;
    BackgroundBounds bounds{};
    std::uint16_t backdrop_rgb5=0;
    bool repeat_edges=false; // offline proof: repeated row gradient, including source dithering
};
struct MenuBackdropView {
    std::uint16_t bank=0;
    std::uint8_t layout=0,engine=0,languages=0,resource=0,parts=0;
    const MenuBackdropPart* part=nullptr;
    std::array<std::uint8_t,32> source_sha256{};
    std::uint16_t character=0;
    std::uint32_t character_bytes=0,character_crc=0,palette_bytes=0,palette_crc=0;
    std::array<std::uint32_t,4> colour_palette_bytes{},colour_palette_crc{};
};
struct MenuBackdropCatalog {const MenuBackdropView* view=nullptr;std::size_t views=0;};
Error validate(MenuBackdropCatalog,CanvasCatalog) noexcept;
MenuBackdropCatalog prepared_menu_backdrops() noexcept;
bool install_menu_backdrop_views(MenuBackdropCatalog,CanvasCatalog) noexcept;
class MenuBackdropState {
public:
    void configure(MenuBackdropCatalog c) noexcept {catalog_=c;reset();}
    void reset() noexcept;
    Error begin(unsigned layout,unsigned language) noexcept;
    const MenuBackdropView* selected(unsigned resource) const noexcept;
    void confirm(unsigned engine,bool okay) noexcept {ready_[engine]=okay;}
    void file(unsigned member,std::uint32_t crc) noexcept;
    bool main_files_ready() const noexcept {return characters_&&palette_;}
    void bind(unsigned resource,const void* owner,const void* map,unsigned width,unsigned height,std::uint32_t crc) noexcept;
    void retire(const void*) noexcept;
    void identity(const void*,const void*,unsigned,unsigned) noexcept;
    void palette_begin(int) noexcept;
    void palette_write(const void*,int count,int value) noexcept;
    void palette_end() noexcept;
    Error sub_palette(unsigned) noexcept;
    Error crop(const void*,int x,int y,unsigned fill,BackgroundCpuState&) noexcept;
    Error backgrounds_begin(unsigned engines) noexcept;
    void publish(unsigned resource,const void*,unsigned engines,unsigned layer,unsigned priority,int x,int y) noexcept;
    Error emit(Screen,SubmitBackground,void*,bool diagnostic_partial=false) noexcept;
    bool write_capture(FILE*,std::uint64_t frame) const noexcept;
private:
    struct Owner {const void* owner=nullptr;const void* map=nullptr;std::uint32_t crc=0;std::int32_t tx=0,ty=0;bool known=false,cropped=false;};
    struct Command {unsigned resource=0,layer=0,priority=0;int x=0,y=0;bool seen=false,known=false;};
    MenuBackdropCatalog catalog_{};
    std::array<Owner,4> owner_{};
    std::array<std::array<Command,4>,2> command_{};
    std::array<bool,2> ready_{};
    unsigned layout_=0,language_=0,main_palette_=0,sub_palette_=0,batch_palette_=0,batch_step_=0;
    bool characters_=false,palette_=false,batch_=false,batch_ok_=false;
};
}
