#include "menu_cell_capture.hpp"
#ifdef SM64DS_NATIVE_MENU_CATALOG_HEADER
#include SM64DS_NATIVE_MENU_CATALOG_HEADER
#endif
namespace sm64ds::render::game_hooks {
MenuCellCatalog prepared_menu_cells() noexcept {
#ifdef SM64DS_NATIVE_MENU_CATALOG_HEADER
    return {prepared_menu::banks,sizeof(prepared_menu::banks)/sizeof(prepared_menu::banks[0])};
#else
    return {};
#endif
}
bool menu_cell_affine_identity(const std::array<std::int32_t,4>& source) noexcept {
    constexpr std::uint16_t identity[]={256,0,0,256};
    for(unsigned i=0;i<4;++i)if(std::uint16_t(std::uint32_t(source[i])>>4)!=identity[i])return false;
    return true;
}
Error validate(MenuCellCatalog catalog) noexcept {
    if(!catalog.banks||catalog.banks>6||!catalog.bank)return Error::invalid_data;
    for(std::size_t b=0;b<catalog.banks;++b){const auto& bank=catalog.bank[b];
        if(bank.source_bank>=6||(b&&bank.source_bank<=catalog.bank[b-1].source_bank)||!bank.cells||bank.cells>64||!bank.cell||
           bank.cell_bytes<16||bank.cell_bytes>1024*1024||bank.character_bytes<16||bank.character_bytes>1024*1024||
           bank.palette_bytes<16||bank.palette_bytes>4096)return Error::invalid_data;
        for(unsigned i=0;i<bank.cells;++i){const auto& c=bank.cell[i];
            if(!c.part||!c.parts||c.parts>128||c.image>=128||!texture_dimensions(c.width,c.height)||!c.character_end||
               c.character_end>bank.character_bytes)return Error::invalid_data;
            for(unsigned p=0;p<c.parts;++p){const auto& r=c.part[p];
                if(!r.width||!r.height||r.width>64||r.height>64||r.x<c.left||r.y<c.top||
                   int(r.x)-c.left+r.width>c.width||int(r.y)-c.top+r.height>c.height)return Error::invalid_data;
            }
        }
    }
    return Error::none;
}
Error validate(MenuCellCatalog menu,ImageCatalog images) noexcept {
    const auto error=validate(menu);if(error!=Error::none)return error;
    for(std::size_t b=0;b<menu.banks;++b){const auto& bank=menu.bank[b];const auto* native=images.find(bank.image_bank);
        if(!native||native->source_sha256!=bank.image_source_sha256)return Error::invalid_data;
        for(unsigned c=0;c<bank.cells;++c){const auto& cell=bank.cell[c];
            if(cell.image>=native->images||native->image[cell.image].width!=cell.width||native->image[cell.image].height!=cell.height)return Error::invalid_data;
        }
    }
    return Error::none;
}
Error MenuCellCapture::begin(SubmitMenuCell sink,void* user) noexcept {
    if(active_)return Error::invalid_phase;
    if(!sink)return Error::invalid_data;
    sink_=sink;user_=user;next_=0;failure_=group_failure_=Error::none;active_=true;return Error::none;
}
Error MenuCellCapture::part(const MenuCellEvent& e) noexcept {
    if(!active_)return Error::invalid_phase;
    if(!lenient_&&failure_!=Error::none)return failure_;
    if(lenient_&&e.part==0){next_=0;group_failure_=Error::none;}
    if(group_failure_!=Error::none)return group_failure_;
    if(!e.bank||e.cell>=e.bank->cells||!e.bank->cell||!screen_width(e.screen)||e.slot>=128||e.priority<0||e.priority>3||
       e.x<-65536||e.x>65536||e.y<-65536||e.y>65536)return fail(Error::invalid_data);
    const auto& c=e.bank->cell[e.cell];
    if(!c.parts||c.parts>128||!c.part||!texture_dimensions(c.width,c.height)||c.image>=128||
       e.part>=c.parts||e.slot<e.part||unsigned(e.slot-e.part)+c.parts>128)return fail(Error::invalid_data);
    if(e.affine!=-1||e.mosaic||(e.flip&~0x30000000u))return fail(Error::unsupported);
    if(!next_){if(e.part)return fail(Error::invalid_data);first_=e;}
    else if(e.part!=next_||e.bank!=first_.bank||e.cell!=first_.cell||e.screen!=first_.screen||e.slot!=first_.slot+next_||
            e.x!=first_.x||e.y!=first_.y||e.priority!=first_.priority||e.flip!=first_.flip)return fail(Error::invalid_data);
    if(++next_!=c.parts)return Error::none;
    ImageLayer image;image.bank_id=e.bank->image_bank;image.image=c.image;image.screen=e.screen;
    image.order=std::uint16_t((3-e.priority)*1024+512+127-first_.slot);
    const bool fx=e.flip&0x10000000u,fy=e.flip&0x20000000u;
    const int raw_left=e.x+(fx?-int(c.left)-c.width:c.left),raw_top=e.y+(fy?-int(c.top)-c.height:c.top);
    const float left=float((unsigned(raw_left)&511u)),top=float((unsigned(raw_top)&255u));
    image.rectangle={(e.screen==Screen::top?40.f:0)+left*1.25f,top*1.25f,c.width*1.25f,c.height*1.25f};
    image.material.blend=Blend::alpha;
    image.tone_target=16;
    image.material.uv={fx?-1.f:1.f,0,(fx?1.f:0)+(fx?-1.f:1.f)/(64*c.width),
                      0,fy?-1.f:1.f,(fy?1.f:0)+(fy?-1.f:1.f)/(64*c.height)};
    next_=0;
    // Keep off-screen entrance/exit cells valid. Wrap their native image
    // periodically, then clip to the original viewport; never read OAM back.
    const auto base=image;const float origin=e.screen==Screen::top?40.f:0;
    for(const int dy:{-256,0})for(const int dx:{-512,0}){
        image=base;image.rectangle.x+=1.25f*dx;image.rectangle.y+=1.25f*dy;
        if(image.rectangle.x>=origin+320||image.rectangle.x+image.rectangle.width<=origin||
           image.rectangle.y>=240||image.rectangle.y+image.rectangle.height<=0)continue;
        image.clip={std::uint16_t(origin),0,std::uint16_t(origin+320),240};
        if(!valid(image))return fail(Error::invalid_data);
        if(!sink_(image,user_))return fail(Error::invalid_data);
    }
    return Error::none;
}
Error MenuCellCapture::finish() noexcept {
    if(!active_)return Error::invalid_phase;
    if(next_)fail(Error::invalid_data);
    active_=false;sink_=nullptr;user_=nullptr;next_=0;return failure_;
}
}
