#include "menu_backdrops.hpp"
#include <algorithm>
#ifdef SM64DS_NATIVE_MENU_BACKDROP_CATALOG_HEADER
#include SM64DS_NATIVE_MENU_BACKDROP_CATALOG_HEADER
#endif
namespace sm64ds::render::game_hooks {
Error validate(MenuBackdropCatalog c,CanvasCatalog canvases) noexcept {
    if(!c.views)return c.view?Error::invalid_data:Error::none;
    if(!c.view||c.views>11)return Error::invalid_data;
    for(std::size_t i=0;i<c.views;++i){const auto& v=c.view[i];const auto* bank=canvases.find(v.bank);
        if((v.layout!=1&&v.layout!=3)||v.engine>1||!v.languages||(v.languages&~63u)||v.resource<1||v.resource>4||
           !v.parts||v.parts>5||!v.part||!bank||bank->source_sha256!=v.source_sha256||bank->parts!=v.parts||
           !v.character_bytes||!v.palette_bytes)return Error::invalid_data;
        if(v.resource==1){if(v.layout!=1||v.engine||v.character<37||v.character>42||v.languages!=(1u<<(v.character-37)))return Error::invalid_data;}
        else if(v.engine!=1||v.languages!=63||v.character!=(v.layout==1?0:3)||(v.layout==1&&v.resource==4))return Error::invalid_data;
        if(v.parts!=(v.layout==3?4:5))return Error::invalid_data;
        if(v.engine)for(unsigned p=0;p<4;++p)if(v.colour_palette_bytes[p]<16||v.colour_palette_bytes[p]>4096)return Error::invalid_data;
        for(unsigned p=0;p<v.parts;++p){const auto& part=v.part[p];const auto& b=part.bounds;
            if(part.part!=p||part.palette!=p+(v.layout==3?1:0)||part.backdrop_rgb5>32767||b.left>=b.right||b.top>=b.bottom||b.right>256||b.bottom>256)return Error::invalid_data;
            if(v.engine&&part.map_crc!=v.part[0].map_crc)return Error::invalid_data;
            for(unsigned j=0;j<p;++j)if(v.part[j].palette==part.palette)return Error::invalid_data;
        }
        for(std::size_t j=0;j<i;++j){const auto& prior=c.view[j];
            if(prior.layout==v.layout&&prior.engine==v.engine&&prior.resource==v.resource&&(prior.languages&v.languages))return Error::invalid_data;
            if(prior.layout==v.layout&&prior.engine==v.engine&&prior.character==v.character&&
               (prior.character_bytes!=v.character_bytes||prior.character_crc!=v.character_crc||prior.palette_bytes!=v.palette_bytes||prior.palette_crc!=v.palette_crc||
                prior.colour_palette_bytes!=v.colour_palette_bytes||prior.colour_palette_crc!=v.colour_palette_crc))return Error::invalid_data;
        }
    }
    return Error::none;
}
MenuBackdropCatalog prepared_menu_backdrops() noexcept {
#ifdef SM64DS_NATIVE_MENU_BACKDROP_CATALOG_HEADER
    return {prepared_backdrop_views::views,sizeof(prepared_backdrop_views::views)/sizeof(prepared_backdrop_views::views[0])};
#else
    return {};
#endif
}
void MenuBackdropState::reset() noexcept {
    owner_={};command_={};ready_={};layout_=language_=main_palette_=sub_palette_=batch_palette_=batch_step_=0;
    characters_=palette_=batch_=batch_ok_=false;
}
const MenuBackdropView* MenuBackdropState::selected(unsigned resource) const noexcept {
    for(std::size_t i=0;i<catalog_.views;++i){const auto& v=catalog_.view[i];
        if(v.layout==layout_&&v.resource==resource&&(v.languages&(1u<<language_)))return &v;
    }
    return nullptr;
}
Error MenuBackdropState::begin(unsigned layout,unsigned language) noexcept {
    ready_={};command_={};characters_=palette_=false;
    if((layout!=1&&layout!=3)||language>=6){layout_=0;language_=0;return Error::unsupported;}
    layout_=layout;language_=language;sub_palette_=layout==3?1:0;
    return selected(2)&&selected(3)&&(layout==1?selected(1)!=nullptr:selected(4)!=nullptr)?Error::none:Error::unsupported;
}
void MenuBackdropState::file(unsigned member,std::uint32_t crc) noexcept {
    const auto* v=selected(1);if(!v)return;
    if(member>=37&&member<=42){characters_=member==v->character&&crc==v->character_crc;if(!characters_)ready_[0]=false;}
    if(member==43){palette_=crc==v->palette_crc;if(!palette_)ready_[0]=false;}
}
void MenuBackdropState::bind(unsigned resource,const void* owner,const void* map,unsigned w,unsigned h,std::uint32_t crc) noexcept {
    if(resource<1||resource>4)return;
    auto& o=owner_[resource-1];o={};o.owner=owner;o.map=map;o.crc=crc;
    if(!owner||!map||w!=32||h!=32)return;
    for(std::size_t i=0;i<catalog_.views;++i){const auto& v=catalog_.view[i];if(v.resource!=resource)continue;
        for(unsigned p=0;p<v.parts;++p)if(v.part[p].map_crc==crc){o.known=true;if(resource==1)main_palette_=v.part[p].palette;return;}
    }
}
void MenuBackdropState::retire(const void* owner) noexcept {for(auto& o:owner_)if(owner&&o.owner==owner)o={};}
void MenuBackdropState::identity(const void* owner,const void* map,unsigned w,unsigned h) noexcept {
    for(auto& o:owner_)if(owner&&o.owner==owner&&(map!=o.map||w!=32||h!=32))o.known=o.cropped=false;
}
void MenuBackdropState::palette_begin(int mode) noexcept {
    if(batch_)owner_[0].known=false;
    batch_=true;batch_ok_=owner_[0].known;batch_step_=0;batch_palette_=mode==0?1:mode==1?2:mode==2?3:4;
}
void MenuBackdropState::palette_write(const void* map,int count,int value) noexcept {
    auto& o=owner_[0];if(!o.map||count<=0)return;
    const auto base=std::uintptr_t(o.map),at=std::uintptr_t(map);const auto bytes=std::uint64_t(unsigned(count))*2;
    if(at>=base?at-base>=2048:std::uint64_t(base-at)>=bytes)return;
    constexpr unsigned offsets[]={0x106,0x146,0x286,0x2c6,0x386,0x3c6};
    if(!batch_||!batch_ok_||batch_step_>=6||at!=base+offsets[batch_step_]||count!=32||value!=(batch_step_/2==batch_palette_-1?4:3)){
        o.known=batch_ok_=false;return;
    }
    ++batch_step_;
}
void MenuBackdropState::palette_end() noexcept {
    if(batch_&&(!batch_ok_||batch_step_!=6))owner_[0].known=false;
    batch_=batch_ok_=false;batch_step_=0;
}
Error MenuBackdropState::sub_palette(unsigned palette) noexcept {
    if(palette>4||(layout_==3&&!palette))return Error::unsupported;
    sub_palette_=palette;return Error::none;
}
Error MenuBackdropState::crop(const void* owner,int x,int y,unsigned fill,BackgroundCpuState& out) noexcept {
    auto& o=owner_[0];if(!owner||owner!=o.owner)return Error::unsupported;
    if(batch_){if(!batch_ok_||batch_step_!=6)o.known=false;else main_palette_=batch_palette_;}
    if(!o.known||fill){o.cropped=false;return Error::unsupported;}
    const auto tile=[](int n){const auto v=std::int64_t(n);return v>=0?v/8:-((-v+7)/8);};
    const auto tx=tile(x),ty=tile(y);o.tx=std::int32_t(tx*8);o.ty=std::int32_t(ty*8);o.cropped=true;
    out.x=std::int32_t(-(std::int64_t(x)-tx*8)*4096);out.y=std::int32_t(-(std::int64_t(y)-ty*8)*4096);
    out.filled_bytes=2048;out.copied_bytes=std::uint64_t(std::max(std::int64_t(0),32-(tx<0?-tx:tx))*std::max(std::int64_t(0),32-(ty<0?-ty:ty)))*2;
    return Error::none;
}
Error MenuBackdropState::backgrounds_begin(unsigned engines) noexcept {
    if(!engines||(engines&~3u))return Error::invalid_data;
    for(unsigned e=0;e<2;++e)if(engines&(1u<<e))command_[e]={};
    return Error::none;
}
void MenuBackdropState::publish(unsigned resource,const void* owner,unsigned engines,unsigned layer,unsigned priority,int x,int y) noexcept {
    if(!engines||(engines&~3u)||layer>3||priority>3)return;
    for(unsigned e=0;e<2;++e)if(engines&(1u<<e)){auto& c=command_[e][layer];c={resource,layer,priority,x,y,true,false};
        const auto* v=selected(resource);if(resource<1||resource>4||!v)continue;
        const auto& o=owner_[resource-1];c.known=v->engine==e&&layer==(resource<=2?3u:2u)&&owner&&owner==o.owner&&o.known&&ready_[e]&&(resource!=1||o.cropped);
    }
}
Error MenuBackdropState::emit(Screen main,SubmitBackground sink,void* user,bool diagnostic_partial) noexcept {
    if(!screen_width(main)||!sink)return Error::invalid_data;
    std::array<ImageLayer,98> layers;unsigned count=0;
    for(unsigned e=0;e<2;++e)if(ready_[e]){
        const auto* v=selected(e?2:1);if(!v)continue;
        const auto palette=e?sub_palette_:main_palette_;
        const MenuBackdropPart* part=nullptr;for(unsigned p=0;p<v->parts;++p)if(v->part[p].palette==palette)part=v->part+p;
        if(!part)return Error::unsupported;
        const auto screen=e==0?main:(main==Screen::top?Screen::bottom:Screen::top);
        const auto expand=[](unsigned channel){return (channel<<3)|(channel>>3);};const auto colour=part->backdrop_rgb5;
        auto& layer=layers[count++];layer.source=LayerSource::solid;layer.screen=screen;
        layer.tone_target=32;
        layer.rectangle={0,0,float(screen_width(screen)),240};
        layer.material.tint=(expand(colour&31)<<24)|(expand((colour>>5)&31)<<16)|(expand((colour>>10)&31)<<8)|255;
    }
    bool incomplete=false;
    for(unsigned e=0;e<2;++e)for(const auto& c:command_[e])if(c.seen){
        if(!c.known||!ready_[e]){if(!diagnostic_partial)return Error::unsupported;incomplete=true;continue;}
        const auto* v=selected(c.resource);const auto& o=owner_[c.resource-1];if(!o.known)return Error::unsupported;
        const auto palette=c.resource==1?main_palette_:sub_palette_;
        const MenuBackdropPart* part=nullptr;for(unsigned p=0;p<v->parts;++p)if(v->part[p].palette==palette)part=v->part+p;
        if(!part||(c.resource!=1&&part->map_crc!=o.crc))return Error::unsupported;
        CanvasCrop crop{256,256,256,192,c.resource==1?o.tx:0,c.resource==1?o.ty:0,c.x,c.y};CanvasWindows windows;
        auto error=canvas_windows(crop,windows);if(error!=Error::none)return error;
        const auto screen=e==0?main:(main==Screen::top?Screen::bottom:Screen::top);
        for(unsigned n=0;n<windows.count;++n){const auto& w=windows.window[n];const auto& b=part->bounds;
            if(w.right<=w.x+b.left||w.left>=w.x+b.right||w.bottom<=w.y+b.top||w.top>=w.y+b.bottom)continue;
            CanvasPlacement placement;error=canvas_placement(crop,w,screen,{screen==Screen::top?40.f:0,0,320,240},placement);if(error!=Error::none)return error;
            auto& layer=layers[count++];layer.source=LayerSource::canvas;layer.bank_id=v->bank;layer.part=part->part;layer.screen=screen;
            layer.order=std::uint16_t((3-c.priority)*1024+(3-c.layer)*128);layer.rectangle=placement.rectangle;layer.clip=placement.clip;layer.material.blend=Blend::alpha;
            layer.tone_target=std::uint8_t(1u<<c.layer);
            if(screen==Screen::top&&part->repeat_edges){
                const auto base=layer;
                for(const int shift:{-320,320}){const int left=std::max(0,int(base.clip.left)+shift),right=std::min(400,int(base.clip.right)+shift);
                    if(left>=right)continue;
                    auto& edge=layers[count++];edge=base;edge.rectangle.x+=shift;
                    edge.clip.left=std::uint16_t(left);edge.clip.right=std::uint16_t(right);
                }
            }
        }
    }
    for(unsigned i=0;i<count;++i)if(!sink(layers[i],user))return Error::capacity;
    return incomplete?Error::unsupported:Error::none;
}
bool MenuBackdropState::write_capture(FILE* file,std::uint64_t frame) const noexcept {
    if(!file)return false;
    if(std::fprintf(file,"{\"format\":\"SM64_NATIVE_MENU_BACKDROPS_1\",\"frame\":%llu,\"layout\":%u,\"language\":%u,\"ready\":[%s,%s],\"palettes\":[%u,%u],\"commands\":[",
        static_cast<unsigned long long>(frame),layout_,language_,ready_[0]?"true":"false",ready_[1]?"true":"false",main_palette_,sub_palette_)<0)return false;
    for(unsigned e=0;e<2;++e)for(unsigned slot=0;slot<4;++slot){const auto& c=command_[e][slot];const auto* v=selected(c.resource);
        if(std::fprintf(file,"%s{\"engine\":%u,\"slot\":%u,\"seen\":%s,\"known\":%s,\"resource\":%u,\"bank\":%u,\"priority\":%u,\"scroll\":[%d,%d]}",
            e||slot?",":"",e,slot,c.seen?"true":"false",c.known?"true":"false",c.resource,v?unsigned(v->bank):0,c.priority,c.x,c.y)<0)return false;
    }
    return std::fputs("]}\n",file)!=EOF&&!std::ferror(file);
}
}
