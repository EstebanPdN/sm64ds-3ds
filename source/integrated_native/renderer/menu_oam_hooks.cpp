#include "menu_oam_hooks.hpp"
#include "menu_backgrounds.hpp"
namespace sm64ds::render::game_hooks {
namespace {
MenuOamStatistics counters;bool trace=false,route_known=false;int mode=0;
MenuCellCatalog catalog;SubmitMenuCell sink=nullptr;void* context=nullptr;
MenuScreenSource screen_source=nullptr;
MenuObjCatalog objects;MenuObjAssets assets;
MenuCellCapture capture;std::uint64_t (*clock_source)()=nullptr;
bool submit_image(const ImageLayer& image,void*){
    if(!sink||!sink(image,context))return false;
    ++counters.captured;return true;
}
}
bool install_menu_cells(MenuCellCatalog next,SubmitMenuCell submit,void* user,MenuObjCatalog views,MenuScreenSource screen) noexcept {
    if(sink||!submit||validate(next)!=Error::none)return false;
    catalog=next;objects=views;sink=submit;context=user;screen_source=screen;return true;
}
bool menu_oam_begin() noexcept {return sink&&capture.begin(submit_image,nullptr)==Error::none;}
bool menu_oam_end() noexcept {return sink&&capture.finish()==Error::none;}
void menu_oam_lenient(bool value) noexcept {capture.set_lenient(value);}
void menu_oam_scene_changed() noexcept {route_known=false;mode=0;assets.reset();}
bool menu_presentation_mode(unsigned layout,Screen& screen,std::uint16_t& order,Screen main) noexcept {
    if(layout<1||layout>6)return false;
    // Layout owns priority, not physical LCD selection. The opening star
    // swaps MAIN to the upper LCD without changing this layout number.
    screen=main;order=std::uint16_t((layout==4||layout==5?2048:1024)+384);return true;
}
bool menu_oam_presentation(Screen& screen,std::uint16_t& order) noexcept {
    return route_known&&menu_presentation_mode(unsigned(mode),screen,order,screen_source?screen_source():Screen::bottom);
}
void menu_oam_clock(std::uint64_t (*clock)()) noexcept {clock_source=clock;}
void menu_oam_trace(bool value) noexcept {trace=value;}
const MenuOamStatistics& menu_oam_statistics() noexcept {return counters;}
}
#ifdef SM64DS_FRESH_NATIVE_GAME
#include "model_components_hooks.hpp"
#include "bank_crc.hpp"
#include <array>
#include <cstdio>
#include <cstring>
extern "C" {
void* __real_func_ov007_020c9688(int);
void __real_func_ov007_020c121c(void*);
void __real_func_ov007_020c0eb0(void*,const void*,int,int,unsigned char,int,int,int);
int __real_func_ov007_020b7138(void*,int*);
void sm64ds_fresh_native_menu_2d_start();
void sm64ds_fresh_native_menu_preload_selector(unsigned);
void __real_func_ov007_020bcba8(int);
void __real_func_ov007_020bd648(int);
void __real_func_ov007_020b7384(void*);
extern std::uint32_t data_ov007_020d7e50[],data_ov007_020d7e28[],data_ov007_020d7e38[];
extern void* data_ov007_0210342c;
extern void* data_ov007_02104bc0;
}
namespace sm64ds::render::game_hooks {
namespace {
static_assert(sizeof(void*)==4,"Original menu cell ABI requires32-bit pointers");
template<class T>T field(const void* base,unsigned offset) noexcept {
    T value;std::memcpy(&value,static_cast<const char*>(base)+offset,sizeof(value));return value;
}
struct Cell {
    const void* owner=nullptr;
    std::uintptr_t source=0;
    std::uint16_t parts=0,cell=0;
    unsigned bank=0;
    const MenuCellBank* binding=nullptr;
};
std::array<Cell,64> cells{};const Cell* cached=nullptr;
std::array<unsigned,2> character_bound{};
void failure() noexcept {++counters.failures;report_image_failure();}
const MenuCellBank* binding_for(unsigned bank) noexcept {
    const auto source=objects.views?objects.view[0].cells:catalog;
    for(std::size_t i=0;i<source.banks;++i)if(source.bank[i].source_bank==bank)return source.bank+i;
    return nullptr;
}
bool resource_crc(const void* source,std::uint32_t bytes,std::uint32_t crc) noexcept {
    return source&&field<std::uint32_t>(source,8)==bytes&&
        detail::bank_crc32(static_cast<const std::uint8_t*>(source),bytes,bytes)==crc;
}
void retire(const void* owner) noexcept {
    cached=nullptr;
    for(auto& c:cells)if(c.owner==owner&&owner){c={};--counters.live_cells;}
}
void observe(int source,void* resource) noexcept {
    if(!resource||!sink||!native_2d_active())return;
    unsigned bank=6;
    for(unsigned i=0;i<6;++i)if(data_ov007_020d7e50[i]==std::uint32_t(source)){bank=i;break;}
    if(bank==6)return;
    retire(resource);
    const auto count=field<std::uint32_t>(resource,0);const auto* entries=field<const char*>(resource,4);
    if(count>cells.size()||!entries||count>cells.size()-counters.live_cells){failure();return;}
    const auto* binding=binding_for(bank);
    if(binding&&(!resource_crc(reinterpret_cast<const void*>(std::uintptr_t(std::uint32_t(source))),binding->cell_bytes,binding->cell_crc)||binding->cells!=count)){
        failure();binding=nullptr;
    }
    for(unsigned i=0;i<count;++i){const auto* e=entries+i*8;const auto parts=field<std::uint32_t>(e,4);
        if(!field<std::uintptr_t>(e,0)||!parts||parts>256||(binding&&binding->cell[i].parts!=parts)){failure();return;}
    }
    for(unsigned i=0;i<count;++i)for(auto& c:cells)if(!c.owner){const auto* e=entries+i*8;
        c={resource,field<std::uintptr_t>(e,0),std::uint16_t(field<std::uint32_t>(e,4)),std::uint16_t(i),bank,binding};
        ++counters.live_cells;break;
    }
    ++counters.resources;
}
void assets_loaded(int layout) noexcept {
    if(!sink||!native_2d_active())return;
    if(layout!=1&&layout!=3)return;
    if(objects.views){
        character_bound={};
        for(unsigned e=0;e<2;++e){const auto* v=assets.pending(e);if(!v){failure();continue;}
            const auto& b=v->cells.bank[0];const unsigned index=layout==3&&e==1?2:1;
            const auto* chars=reinterpret_cast<const void*>(std::uintptr_t(data_ov007_020d7e28[index]));
            const auto* pal=reinterpret_cast<const void*>(std::uintptr_t(data_ov007_020d7e38[1]));
            if(!resource_crc(chars,b.character_bytes,b.character_crc)||!resource_crc(pal,b.palette_bytes,b.palette_crc)||assets.commit(e)!=Error::none){failure();continue;}
            character_bound[e]=16384;
        }
        return;
    }
    if(layout==3){character_bound[1]=0;return;}
    if(layout!=1)return;
    character_bound={};const auto* b=binding_for(0);if(!b||!data_ov007_0210342c)return;
    const auto* chars=reinterpret_cast<const void*>(std::uintptr_t(data_ov007_020d7e28[1]));
    const auto* pal=reinterpret_cast<const void*>(std::uintptr_t(data_ov007_020d7e38[1]));
    if(!resource_crc(chars,b->character_bytes,b->character_crc)||!resource_crc(pal,b->palette_bytes,b->palette_crc)){failure();return;}
    // Exact typed layout1 contract: main copies only the first8KiB of bank1;
    // its next8KiB are a separate icon asset. Sub copies the complete bank1,
    // except language variants which require their own prepared association.
    character_bound[0]=8192;
    const auto* settings=field<const void*>(data_ov007_0210342c,0x28);
    if(settings&&field<unsigned char>(settings,0x3a)<=1)character_bound[1]=16384;
}
bool contains(const Cell& c,std::uintptr_t address,unsigned& part) noexcept {
    if(!c.owner||address<c.source)return false;
    const auto delta=address-c.source;
    if(delta%6||delta/6>=c.parts)return false;
    part=unsigned(delta/6);return true;
}
void produce(void* destination,const void* source,int x,int y,unsigned priority,int flip,int mosaic,int affine) noexcept {
    ++counters.calls;const auto address=reinterpret_cast<std::uintptr_t>(source);const Cell* found=nullptr;unsigned part=0;
    if(cached&&contains(*cached,address,part)){found=cached;++counters.cache_hits;}
    else for(const auto& c:cells)if(contains(c,address,part)){found=&c;cached=found;break;}
    if(found)++counters.associated;else ++counters.unassociated;
    int engine=-1,slot=-1;
    if(data_ov007_0210342c){const auto* owner=field<const void*>(data_ov007_0210342c,0x74);
        if(owner)for(unsigned i=0;i<2;++i){const auto base=field<std::uintptr_t>(owner,0x10+i*4);const auto to=reinterpret_cast<std::uintptr_t>(destination);
            if(base&&to>=base&&to-base<1024&&(to-base)%8==0){engine=int(i);slot=int((to-base)/8);break;}
        }
    }
    Screen main;std::uint16_t order;
    bool accepted=found&&found->binding&&engine>=0&&menu_oam_presentation(main,order);
    const MenuCellBank* binding=found?found->binding:nullptr;
    if(accepted&&objects.views){const auto* v=assets.view(unsigned(engine));binding=v?&v->cells.bank[found->bank]:nullptr;accepted=binding!=nullptr;}
    if(accepted){const auto& cell=binding->cell[found->cell];
        accepted=cell.character_end<=character_bound[unsigned(engine)];
        if(accepted){MenuCellEvent event;event.bank=binding;event.cell=found->cell;event.part=std::uint16_t(part);event.slot=std::uint16_t(slot);
            event.screen=engine==0?main:(main==Screen::top?Screen::bottom:Screen::top);
            event.x=x;event.y=y;event.priority=int(priority);event.flip=std::uint32_t(flip);event.mosaic=mosaic!=0;event.affine=affine;
            // be8b8 owns sixteen semantic cursor bases; be3bc reduces these
            // exact coefficients before publishing SUB OBJ state. Read that
            // producer's values, never decode emulated OAM or assume slot15.
            if(engine==1&&affine>=0&&affine<16&&!flip&&data_ov007_02104bc0){
                const auto basis=field<std::array<std::int32_t,4>>(data_ov007_02104bc0,8+unsigned(affine)*16);
                if(menu_cell_affine_identity(basis))event.affine=-1;
            }
            accepted=capture.part(event)==Error::none;
        }
    }
    if(!accepted)failure();
    if(trace||(!accepted&&counters.failures<=8))std::printf("Fresh native menu2D source: bank=%u cell=%u part=%u engine=%d slot=%d x=%d y=%d priority=%u flip=%08lx mosaic=%d affine=%d typedCaptured=%u originalOAMEncoded=0\n",
        found?found->bank:0xffffu,found?unsigned(found->cell):0xffffu,part,engine,slot,x,y,priority,static_cast<unsigned long>(std::uint32_t(flip)),mosaic,affine,unsigned(accepted));
}
}
void menu_obj_file_loaded(int index,const void* file) noexcept {
    if(!native_2d_active()||!objects.views||!file||index<0)return;
    const auto* expected=assets.expected_file(unsigned(index));if(!expected)return;
    if(index<32&&field<std::uint32_t>(file,8)!=expected->bytes){
        assets.file(unsigned(index),field<std::uint32_t>(file,8),0);failure();return;
    }
    const auto crc=detail::bank_crc32(static_cast<const std::uint8_t*>(file),expected->bytes,expected->bytes);
    if(assets.file(unsigned(index),expected->bytes,crc)!=Error::none)failure();
}
}
extern "C" void* __wrap_func_ov007_020c9688(int source){
    void* resource=__real_func_ov007_020c9688(source);sm64ds::render::game_hooks::observe(source,resource);return resource;
}
extern "C" void __wrap_func_ov007_020c121c(void* resource){
    sm64ds::render::game_hooks::retire(resource);__real_func_ov007_020c121c(resource);
}
extern "C" void __wrap_func_ov007_020c0eb0(void* destination,const void* source,int x,int y,unsigned char priority,int flip,int mosaic,int affine){
    using namespace sm64ds::render::game_hooks;
    if(!native_2d_active()||!sink){__real_func_ov007_020c0eb0(destination,source,x,y,priority,flip,mosaic,affine);return;}
    // BUILD137: a dry update's image is discarded whole (Old 3DS presents one
    // of the menu's two 60 Hz updates); native mode never writes the original
    // OAM, so skipping the capture leaves no state behind.
    if(dry())return;
    const auto start=clock_source?clock_source():0;produce(destination,source,x,y,priority,flip,mosaic,affine);
    if(clock_source)counters.capture_ticks+=clock_source()-start;
}
extern "C" int __wrap_func_ov007_020b7138(void* args,int* flags){
    sm64ds_fresh_native_menu_2d_start();
    sm64ds::render::game_hooks::menu_oam_scene_changed();
    const auto result=__real_func_ov007_020b7138(args,flags);
    sm64ds::render::game_hooks::route_known=true;return result;
}
extern "C" void __wrap_func_ov007_020bcba8(int layout){
    using namespace sm64ds::render::game_hooks;
    if(native_2d_active()&&objects.views&&(layout==1||layout==3)){
        const auto* settings=field<const void*>(data_ov007_0210342c,0x28);
        const auto language=field<unsigned char>(settings,0x3a);
        if(assets.begin(objects,unsigned(layout),language)!=sm64ds::render::Error::none)failure();
        else if(layout==1)sm64ds_fresh_native_menu_preload_selector(language);
    }
    sm64ds::render::game_hooks::menu_background_assets_begin(layout);
    __real_func_ov007_020bcba8(layout);sm64ds::render::game_hooks::assets_loaded(layout);
    sm64ds::render::game_hooks::menu_background_assets_end(layout);
}
extern "C" void sm64ds_fresh_native_head_canvas_mode(int);
extern "C" void __wrap_func_ov007_020bd648(int layout){
    __real_func_ov007_020bd648(layout);sm64ds_fresh_native_head_canvas_mode(layout);
    using namespace sm64ds::render::game_hooks;
    if(native_2d_active()&&mode!=layout)std::printf("Native menu display mode: previous=%d current=%d lowerEngine=MAIN\n",mode,layout);
    mode=layout;
}
extern "C" void __wrap_func_ov007_020b7384(void* owner){
    using namespace sm64ds::render::game_hooks;
    if(!native_2d_active()||!sink)__real_func_ov007_020b7384(owner);
}
#endif
#ifndef SM64DS_FRESH_NATIVE_GAME
namespace sm64ds::render::game_hooks {void menu_obj_file_loaded(int,const void*) noexcept {}}
#endif
