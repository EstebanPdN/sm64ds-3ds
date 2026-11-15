#pragma once
#include "canvas_catalog.hpp"
#include "canvas_windows.hpp"
#include "image_layer.hpp"
#include <cstdio>
namespace sm64ds::render::game_hooks {
struct BackgroundBounds {std::uint16_t left,top,right,bottom;};
struct MenuBackgroundVariant {std::uint32_t map_crc;std::uint16_t part;BackgroundBounds bounds;};
struct MenuBackgroundBinding {
    std::uint16_t resource,bank,width,height,character_index,variants;
    std::uint32_t character_bytes,character_crc,palette_bytes,palette_crc;
    const MenuBackgroundVariant* variant;
    std::array<std::uint8_t,32> canvas_sha256;
};
struct MenuBackgroundCatalog {const MenuBackgroundBinding* bank=nullptr;};
MenuBackgroundCatalog prepared_menu_backgrounds() noexcept;
Error validate(MenuBackgroundCatalog,CanvasCatalog) noexcept;
struct BackgroundCpuState {std::int32_t x=0,y=0;std::uint64_t filled_bytes=0,copied_bytes=0;};
struct MenuBackgroundCommand {
    std::int32_t translate_x=0,translate_y=0,scroll_x=0,scroll_y=0;
    std::uint16_t variant=0;
    std::uint8_t layer=0,priority=0;
    bool seen=false,known=false;
};
struct MenuBackgroundStatistics {
    std::uint64_t registered=0,retired=0,crops=0,replaced=0,filled_bytes=0,copied_bytes=0;
    std::uint64_t palette_writes=0,palette_batches=0,unknown=0,emitted=0,hidden_windows=0;
};
using SubmitBackground=bool (*)(const ImageLayer&,void*);
// The adapter retains one registered source identity until explicit retirement.
// Published commands and emitted frame layers never retain those pointers.
class MenuBackgroundState {
public:
    void configure(MenuBackgroundCatalog) noexcept;
    void reset() noexcept;
    void observe(unsigned resource,const void* owner,const void* map,unsigned width,unsigned height,std::uint32_t crc) noexcept;
    void retire(const void*) noexcept;
    void assets_begin(int layout) noexcept;
    void file(unsigned index,std::uint32_t crc) noexcept;
    void assets_end(int layout) noexcept;
    bool expects_file(unsigned index) const noexcept {return binding_&&loading_&&index>=38&&index<=43;}
    void identity(const void*,const void*,unsigned,unsigned) noexcept;
    void palette_begin(int mode) noexcept;
    void palette_write(const void*,int count,int value) noexcept;
    void palette_end() noexcept;
    Error crop(const void*,int x,int y,unsigned fill,BackgroundCpuState&) noexcept;
    Error backgrounds_begin(unsigned engines) noexcept;
    void publish(unsigned resource,const void*,unsigned engines,unsigned layer,unsigned priority,int x,int y) noexcept;
    Error emit(Screen main,SubmitBackground,void*) noexcept;
    const std::array<MenuBackgroundCommand,2>& commands() const noexcept {return command_;}
    const MenuBackgroundStatistics& statistics() const noexcept {return stats_;}
private:
    const MenuBackgroundBinding* binding_=nullptr;
    const void* owner_=nullptr;const void* map_=nullptr;
    std::array<MenuBackgroundCommand,2> command_{};
    MenuBackgroundStatistics stats_{};
    std::int32_t tx_=0,ty_=0;
    unsigned variant_=0,batch_variant_=0,batch_step_=0;
    bool map_known_=false,cropped_=false,loading_=false,characters_=false,palette_=false,assets_=false,batch_=false,batch_ok_=false;
};
bool install_menu_backgrounds(MenuBackgroundCatalog,CanvasCatalog) noexcept;
void menu_background_scene_changed() noexcept;
void menu_background_assets_begin(int) noexcept;
void menu_background_assets_end(int) noexcept;
void menu_background_file_loaded(int,const void*) noexcept;
void menu_background_group_begin(unsigned) noexcept;
void menu_background_identity(const void*,const void*,unsigned,unsigned) noexcept;
void menu_background_publish(unsigned,const void*,unsigned,unsigned,unsigned,int,int) noexcept;
Error menu_background_emit(Screen,SubmitBackground,void*,bool diagnostic_partial=false) noexcept;
const MenuBackgroundStatistics& menu_background_statistics() noexcept;
bool write_menu_background_capture(FILE*,std::uint64_t) noexcept;
}
