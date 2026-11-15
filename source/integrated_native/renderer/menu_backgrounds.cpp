#include "menu_backgrounds.hpp"
#include "menu_backdrops.hpp"
#include "bank_crc.hpp"
#include <algorithm>
#include <cstring>
#ifdef SM64DS_NATIVE_BACKGROUND_CATALOG_HEADER
#include SM64DS_NATIVE_BACKGROUND_CATALOG_HEADER
#endif
namespace sm64ds::render::game_hooks {
namespace {MenuBackgroundState state;MenuBackgroundCatalog configured;MenuBackdropState backdrops;MenuBackdropCatalog backdrop_catalog;}
bool install_menu_backdrop_views(MenuBackdropCatalog c,CanvasCatalog canvases) noexcept {
    if(validate(c,canvases)!=Error::none)return false;
    backdrop_catalog=c;backdrops.configure(c);return true;
}
MenuBackgroundCatalog prepared_menu_backgrounds() noexcept {
#ifdef SM64DS_NATIVE_BACKGROUND_CATALOG_HEADER
    return {&prepared_backgrounds::bank};
#else
    return {};
#endif
}
Error validate(MenuBackgroundCatalog catalog,CanvasCatalog canvases) noexcept {
    if(!catalog.bank)return Error::none;
    const auto& b=*catalog.bank;const auto* canvas=canvases.find(b.bank);
    if(b.resource!=1||b.width!=32||b.height!=32||b.character_index!=38||b.character_bytes!=16384||b.palette_bytes!=512||
       b.variants!=5||!b.variant||!canvas||canvas->source_sha256!=b.canvas_sha256)return Error::invalid_data;
    for(unsigned i=0;i<b.variants;++i){const auto& v=b.variant[i];
        if(v.part>=canvas->parts||v.bounds.left>=v.bounds.right||v.bounds.top>=v.bounds.bottom||v.bounds.right>b.width*8||v.bounds.bottom>b.height*8)return Error::invalid_data;
        for(unsigned n=0;n<i;++n)if(b.variant[n].map_crc==v.map_crc)return Error::invalid_data;
    }
    return Error::none;
}
void MenuBackgroundState::configure(MenuBackgroundCatalog catalog) noexcept {binding_=catalog.bank;reset();}
void MenuBackgroundState::reset() noexcept {
    owner_=map_=nullptr;command_={};tx_=ty_=0;variant_=batch_variant_=batch_step_=0;
    map_known_=cropped_=loading_=characters_=palette_=assets_=batch_=batch_ok_=false;
}
void MenuBackgroundState::observe(unsigned resource,const void* owner,const void* map,unsigned width,unsigned height,std::uint32_t crc) noexcept {
    if(!binding_||resource!=binding_->resource)return;
    owner_=owner;map_=map;map_known_=cropped_=false;batch_=false;++stats_.registered;
    if(!owner||!map||width!=binding_->width||height!=binding_->height){++stats_.unknown;return;}
    for(unsigned i=0;i<binding_->variants;++i)if(binding_->variant[i].map_crc==crc){variant_=i;map_known_=true;return;}
    ++stats_.unknown;
}
void MenuBackgroundState::retire(const void* owner) noexcept {
    if(owner&&owner==owner_){owner_=map_=nullptr;map_known_=cropped_=batch_=false;command_={};++stats_.retired;}
}
void MenuBackgroundState::assets_begin(int layout) noexcept {
    if(layout==1){loading_=true;characters_=palette_=assets_=false;}
}
void MenuBackgroundState::file(unsigned index,std::uint32_t crc) noexcept {
    if(!binding_||!loading_)return;
    if(index>=38&&index<=42)characters_=index==binding_->character_index&&crc==binding_->character_crc;
    if(index==43)palette_=crc==binding_->palette_crc;
}
void MenuBackgroundState::assets_end(int layout) noexcept {
    if(layout==1){assets_=loading_&&characters_&&palette_;loading_=false;}
}
void MenuBackgroundState::identity(const void* owner,const void* map,unsigned width,unsigned height) noexcept {
    if(owner&&owner==owner_&&(map!=map_||!binding_||width!=binding_->width||height!=binding_->height)){map_known_=cropped_=false;++stats_.unknown;}
}
void MenuBackgroundState::palette_begin(int mode) noexcept {
    if(!binding_||!owner_)return;
    if(batch_){map_known_=false;++stats_.unknown;}
    batch_=true;batch_ok_=map_known_;batch_step_=0;
    batch_variant_=mode==0?1:mode==1?2:mode==2?3:4;
}
void MenuBackgroundState::palette_write(const void* address,int count,int value) noexcept {
    if(!map_||count<=0)return;
    const auto base=std::uintptr_t(map_),at=std::uintptr_t(address);
    const auto bytes=std::uint64_t(unsigned(count))*2;
    // Observe any overlapping mutation, including a write starting before the
    // registered source. Do not silently retain a previously known variant.
    if(at>=base?at-base>=2048:std::uint64_t(base-at)>=bytes)return;
    ++stats_.palette_writes;
    constexpr unsigned offset[]={0x106,0x146,0x286,0x2c6,0x386,0x3c6};
    const unsigned expected=batch_variant_==1?0u:batch_variant_==2?1u:batch_variant_==3?2u:3u;
    if(!batch_||!batch_ok_||batch_step_>=6||at!=base+offset[batch_step_]||count!=32||value!=(batch_step_/2==expected?4:3)){
        map_known_=batch_ok_=false;++stats_.unknown;return;
    }
    ++batch_step_;
}
void MenuBackgroundState::palette_end() noexcept {
    if(batch_&&(!batch_ok_||batch_step_!=6)){map_known_=false;++stats_.unknown;}
    batch_=batch_ok_=false;batch_step_=0;
}
Error MenuBackgroundState::crop(const void* owner,int x,int y,unsigned fill,BackgroundCpuState& output) noexcept {
    ++stats_.crops;
    if(!binding_||!owner||owner!=owner_)return Error::unsupported;
    if(batch_){if(!batch_ok_||batch_step_!=6)map_known_=false;else {variant_=batch_variant_;++stats_.palette_batches;}}
    if(!map_known_||fill){cropped_=false;return Error::unsupported;}
    const auto tile=[](int v){const auto q=std::int64_t(v);return q>=0?q/8:-((-q+7)/8);};
    const auto ix=tile(x),iy=tile(y);tx_=std::int32_t(ix*8);ty_=std::int32_t(iy*8);
    BackgroundCpuState next;next.x=std::int32_t(-(std::int64_t(x)-ix*8)*4096);next.y=std::int32_t(-(std::int64_t(y)-iy*8)*4096);
    next.filled_bytes=std::uint64_t(binding_->width)*binding_->height*2;
    const auto w=std::max(std::int64_t(0),std::int64_t(binding_->width)-(ix<0?-ix:ix));
    const auto h=std::max(std::int64_t(0),std::int64_t(binding_->height)-(iy<0?-iy:iy));
    next.copied_bytes=std::uint64_t(w*h)*2;cropped_=true;output=next;
    ++stats_.replaced;stats_.filled_bytes+=next.filled_bytes;stats_.copied_bytes+=next.copied_bytes;return Error::none;
}
Error MenuBackgroundState::backgrounds_begin(unsigned engines) noexcept {
    if(!engines||(engines&~3u))return Error::invalid_data;
    for(unsigned e=0;e<2;++e)if(engines&(1u<<e))command_[e]={};
    return Error::none;
}
void MenuBackgroundState::publish(unsigned resource,const void* owner,unsigned engines,unsigned layer,unsigned priority,int x,int y) noexcept {
    if(!binding_||resource!=binding_->resource)return;
    for(unsigned e=0;e<2;++e)if(engines&(1u<<e)){auto& c=command_[e];c={};c.seen=true;c.layer=std::uint8_t(layer);c.priority=std::uint8_t(priority);
        c.known=e==0&&owner&&owner==owner_&&map_known_&&cropped_&&assets_&&layer==3&&priority<=3;
        c.translate_x=tx_;c.translate_y=ty_;c.scroll_x=x;c.scroll_y=y;c.variant=std::uint16_t(variant_);
    }
}
Error MenuBackgroundState::emit(Screen main,SubmitBackground sink,void* user) noexcept {
    if(!binding_)return Error::none;
    if(!screen_width(main)||!sink)return Error::invalid_data;
    // Validate complete source coverage before appending any layer.
    for(const auto& c:command_)if(c.seen&&(!c.known||c.variant>=binding_->variants))return Error::unsupported;
    for(unsigned e=0;e<2;++e){const auto& c=command_[e];if(!c.seen)continue;const auto& variant=binding_->variant[c.variant];
        CanvasCrop crop{std::uint16_t(binding_->width*8),std::uint16_t(binding_->height*8),256,192,c.translate_x,c.translate_y,c.scroll_x,c.scroll_y};
        CanvasWindows windows;auto error=canvas_windows(crop,windows);if(error!=Error::none)return error;
        const auto side=e==0?main:(main==Screen::top?Screen::bottom:Screen::top);
        for(unsigned n=0;n<windows.count;++n){const auto& w=windows.window[n];const auto& b=variant.bounds;
            if(w.right<=w.x+b.left||w.left>=w.x+b.right||w.bottom<=w.y+b.top||w.top>=w.y+b.bottom){++stats_.hidden_windows;continue;}
            CanvasPlacement placement;error=canvas_placement(crop,w,side,{side==Screen::top?40.f:0,0,320,240},placement);if(error!=Error::none)return error;
            ImageLayer image;image.bank_id=binding_->bank;image.source=LayerSource::canvas;image.part=variant.part;image.screen=side;
            image.order=std::uint16_t((3-c.priority)*1024+(3-c.layer)*128);image.rectangle=placement.rectangle;image.clip=placement.clip;image.material.blend=Blend::alpha;
            if(!sink(image,user))return Error::capacity;
            ++stats_.emitted;
        }
    }
    return Error::none;
}
bool install_menu_backgrounds(MenuBackgroundCatalog catalog,CanvasCatalog canvases) noexcept {
    if(validate(catalog,canvases)!=Error::none)return false;
    configured=catalog;state.configure(catalog);return true;
}
void menu_background_scene_changed() noexcept {state.reset();backdrops.reset();}
void menu_background_group_begin(unsigned flags) noexcept {if(backdrop_catalog.views)backdrops.backgrounds_begin(flags);else state.backgrounds_begin(flags);}
void menu_background_identity(const void* owner,const void* map,unsigned w,unsigned h) noexcept {if(backdrop_catalog.views)backdrops.identity(owner,map,w,h);else state.identity(owner,map,w,h);}
void menu_background_publish(unsigned resource,const void* owner,unsigned engines,unsigned layer,unsigned priority,int x,int y) noexcept {if(backdrop_catalog.views)backdrops.publish(resource,owner,engines,layer,priority,x,y);else state.publish(resource,owner,engines,layer,priority,x,y);}
Error menu_background_emit(Screen main,SubmitBackground sink,void* user,bool diagnostic_partial) noexcept {return backdrop_catalog.views?backdrops.emit(main,sink,user,diagnostic_partial):state.emit(main,sink,user);}
const MenuBackgroundStatistics& menu_background_statistics() noexcept {return state.statistics();}
bool write_menu_background_capture(FILE* file,std::uint64_t frame) noexcept {
    if(backdrop_catalog.views)return backdrops.write_capture(file,frame);
    if(!file)return false;
    if(std::fprintf(file,"{\"format\":\"SM64_NATIVE_MENU_BACKGROUND_1\",\"original_scene_accepted\":false,\"physical_capture\":false,\"frame\":%llu,\"commands\":[",static_cast<unsigned long long>(frame))<0)return false;
    for(unsigned e=0;e<2;++e){const auto& c=state.commands()[e];
        if(std::fprintf(file,"%s{\"seen\":%s,\"known\":%s,\"variant\":%u,\"layer\":%u,\"priority\":%u,\"translate\":[%ld,%ld],\"scroll\":[%ld,%ld]}",e?",":"",c.seen?"true":"false",c.known?"true":"false",unsigned(c.variant),unsigned(c.layer),unsigned(c.priority),static_cast<long>(c.translate_x),static_cast<long>(c.translate_y),static_cast<long>(c.scroll_x),static_cast<long>(c.scroll_y))<0)return false;
    }
    return std::fputs("]}\n",file)!=EOF&&!std::ferror(file);
}
}
#ifdef SM64DS_FRESH_NATIVE_GAME
#include "model_components_hooks.hpp"
extern "C" {
int __real_func_ov007_020adc08(int,void*);
int __real_func_ov007_020c0a9c(void*);
void __real_func_ov007_020c0638(void*,int,int,std::uint16_t);
void __real_func_ov007_020c0288(void*,int,int);
void __real_func_ov007_020b1390(int);
void __real_func_ov007_020ada70();
extern void* data_ov007_0210342c;
extern std::uint32_t data_ov007_020d7e28[],data_ov007_020d7e38[];
}
namespace sm64ds::render::game_hooks {
namespace {
template<class T>T field(const void* p,unsigned at){T value;std::memcpy(&value,static_cast<const char*>(p)+at,sizeof(value));return value;}
}
void menu_background_assets_begin(int layout) noexcept {
    if(!native_2d_active())return;
    if(!backdrop_catalog.views){state.assets_begin(layout);return;}
    if(layout!=1&&layout!=3)return;
    const auto* settings=field<const void*>(data_ov007_0210342c,0x28);
    if(backdrops.begin(unsigned(layout),field<std::uint8_t>(settings,0x3a))!=Error::none)report_image_failure();
}
void menu_background_assets_end(int layout) noexcept {
    if(!native_2d_active())return;
    if(!backdrop_catalog.views){state.assets_end(layout);return;}
    if(layout!=1&&layout!=3)return;
    backdrops.confirm(0,layout==1&&backdrops.main_files_ready());
    const auto* v=backdrops.selected(2);bool okay=v!=nullptr;
    const auto matches=[](const void* p,std::uint32_t bytes,std::uint32_t crc){return field<std::uint32_t>(p,8)==bytes&&detail::bank_crc32(static_cast<const std::uint8_t*>(p),bytes,bytes)==crc;};
    if(v){okay=matches(reinterpret_cast<const void*>(std::uintptr_t(data_ov007_020d7e28[v->character])),v->character_bytes,v->character_crc)&&
              matches(reinterpret_cast<const void*>(std::uintptr_t(data_ov007_020d7e38[0])),v->palette_bytes,v->palette_crc);
        for(unsigned p=0;p<4&&okay;++p)okay=matches(reinterpret_cast<const void*>(std::uintptr_t(data_ov007_020d7e38[p+2])),v->colour_palette_bytes[p],v->colour_palette_crc[p]);}
    backdrops.confirm(1,okay);if(!okay||(layout==1&&!backdrops.main_files_ready()))report_image_failure();
}
void menu_background_file_loaded(int index,const void* file) noexcept {
    if(native_2d_active()&&backdrop_catalog.views&&file){
        const auto* v=backdrops.selected(1);if(!v||!((index>=37&&index<=42)||index==43))return;
        const auto bytes=index==43?v->palette_bytes:v->character_bytes;
        backdrops.file(unsigned(index),detail::bank_crc32(static_cast<const std::uint8_t*>(file),bytes,bytes));return;
    }
    if(!native_2d_active()||!configured.bank||!file||index<0||!state.expects_file(unsigned(index)))return;
    const auto& b=*configured.bank;
    if(index>=38&&index<=42)state.file(unsigned(index),index==b.character_index?detail::bank_crc32(static_cast<const std::uint8_t*>(file),b.character_bytes,b.character_bytes):0);
    if(index==43)state.file(43,detail::bank_crc32(static_cast<const std::uint8_t*>(file),b.palette_bytes,b.palette_bytes));
}
}
extern "C" int __wrap_func_ov007_020adc08(int resource,void* owner){
    const auto result=__real_func_ov007_020adc08(resource,owner);using namespace sm64ds::render::game_hooks;
    if(native_2d_active()&&backdrop_catalog.views&&resource>=1&&resource<=4&&owner){
        const auto* map=field<const std::uint8_t*>(owner,resource==1?0x14:0x10);
        const auto w=field<std::uint16_t>(owner,0x1c),h=field<std::uint16_t>(owner,0x1e);
        const auto crc=map&&w==32&&h==32?sm64ds::render::detail::bank_crc32(map,2048,2048):0;
        backdrops.bind(unsigned(resource),owner,map,w,h,crc);
    }else if(native_2d_active()&&configured.bank&&resource==configured.bank->resource&&owner){const auto* map=field<const std::uint8_t*>(owner,0x14);
        const auto w=field<std::uint16_t>(owner,0x1c),h=field<std::uint16_t>(owner,0x1e);
        const auto crc=map&&w==32&&h==32?sm64ds::render::detail::bank_crc32(map,2048,2048):0;
        state.observe(unsigned(resource),owner,map,w,h,crc);
    }
    return result;
}
extern "C" int __wrap_func_ov007_020c0a9c(void* owner){sm64ds::render::game_hooks::state.retire(owner);sm64ds::render::game_hooks::backdrops.retire(owner);return __real_func_ov007_020c0a9c(owner);}
extern "C" void __wrap_func_ov007_020c0638(void* owner,int x,int y,std::uint16_t fill){
    using namespace sm64ds::render::game_hooks;BackgroundCpuState next;
    if(native_2d_active()&&owner)menu_background_identity(owner,field<const void*>(owner,0x14),field<std::uint16_t>(owner,0x1c),field<std::uint16_t>(owner,0x1e));
    if(!native_2d_active()||(backdrop_catalog.views?backdrops.crop(owner,x,y,fill,next):state.crop(owner,x,y,fill,next))!=sm64ds::render::Error::none){__real_func_ov007_020c0638(owner,x,y,fill);return;}
    const int dirty=1;std::memcpy(static_cast<char*>(owner)+0xc,&dirty,4);
    std::memcpy(static_cast<char*>(owner)+0x20,&next.x,4);std::memcpy(static_cast<char*>(owner)+0x24,&next.y,4);
}
extern "C" void __wrap_func_ov007_020c0288(void* map,int count,int value){
    using namespace sm64ds::render::game_hooks;if(native_2d_active()){if(backdrop_catalog.views)backdrops.palette_write(map,count,value);else state.palette_write(map,count,value);}
    __real_func_ov007_020c0288(map,count,value);
}
extern "C" void __wrap_func_ov007_020b1390(int mode){
    using namespace sm64ds::render::game_hooks;const bool active=native_2d_active();if(active){if(backdrop_catalog.views)backdrops.palette_begin(mode);else state.palette_begin(mode);}
    __real_func_ov007_020b1390(mode);if(active){if(backdrop_catalog.views)backdrops.palette_end();else state.palette_end();}
}
extern "C" void __wrap_func_ov007_020ada70(){
    using namespace sm64ds::render::game_hooks;
    if(native_2d_active()&&backdrop_catalog.views){const auto* source=field<const void*>(data_ov007_0210342c,0x40);const auto value=field<std::int16_t>(source,0);
        if(value>=4&&value<=7&&backdrops.sub_palette(unsigned(value-3))!=sm64ds::render::Error::none)report_image_failure();}
    __real_func_ov007_020ada70();
}
#else
namespace sm64ds::render::game_hooks {
void menu_background_assets_begin(int layout) noexcept {state.assets_begin(layout);}
void menu_background_assets_end(int layout) noexcept {state.assets_end(layout);}
void menu_background_file_loaded(int,const void*) noexcept {}
}
#endif
