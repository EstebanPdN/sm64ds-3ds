#include "menu_obj_catalog.hpp"
#ifdef SM64DS_NATIVE_MENU_OBJ_CATALOG_HEADER
#include SM64DS_NATIVE_MENU_OBJ_CATALOG_HEADER
#endif
namespace sm64ds::render::game_hooks {
Error validate(MenuObjCatalog catalog,ImageCatalog images) noexcept {
    if(!catalog.views)return catalog.view?Error::invalid_data:Error::none;
    if(!catalog.view||catalog.views>20)return Error::invalid_data;
    for(std::size_t i=0;i<catalog.views;++i){const auto& v=catalog.view[i];
        if((v.layout!=1&&v.layout!=3)||v.engine>1||!v.languages||(v.languages&~63u)||v.cells.banks!=6)return Error::invalid_data;
        const auto error=validate(v.cells,images);if(error!=Error::none)return error;
        for(unsigned bank=0;bank<6;++bank)if(v.cells.bank[bank].source_bank!=bank||
            v.cells.bank[bank].image_bank!=v.cells.bank[0].image_bank)return Error::invalid_data;
        const auto& replacement=v.replacement;
        if(v.languages&3u){
            if(v.languages!=3||replacement.member||replacement.bytes||replacement.crc)return Error::invalid_data;
        }else{
            unsigned language=2;while(language<6&&v.languages!=(1u<<language))++language;
            if(language==6||replacement.member!=(v.layout==1?24u:25u)+(language-2)*2||
                replacement.bytes<8192||replacement.bytes>32768)return Error::invalid_data;
        }
        if(v.engine==0){
            if(v.icons.member!=44||v.icons.bytes!=8192||v.icon_palette.member!=45||v.icon_palette.bytes!=224)return Error::invalid_data;
        }else if(v.icons.member||v.icons.bytes||v.icons.crc||v.icon_palette.member||v.icon_palette.bytes||v.icon_palette.crc)return Error::invalid_data;
        for(std::size_t j=0;j<i;++j){const auto& prior=catalog.view[j];
            if(prior.layout==v.layout&&prior.engine==v.engine&&(prior.languages&v.languages))return Error::invalid_data;
            // Every native view describes the same source cell owners. Their
            // character/palette recipes may differ; source cell identity may not.
            for(unsigned bank=0;bank<6;++bank){const auto& a=prior.cells.bank[bank];const auto& b=v.cells.bank[bank];
                if(a.cells!=b.cells||a.cell_bytes!=b.cell_bytes||a.cell_crc!=b.cell_crc)return Error::invalid_data;
            }
            for(const auto* a:{&prior.replacement,&prior.icons,&prior.icon_palette})
                for(const auto* b:{&v.replacement,&v.icons,&v.icon_palette})if(a->member&&a->member==b->member&&
                    (a->bytes!=b->bytes||a->crc!=b->crc))return Error::invalid_data;
        }
    }
    return Error::none;
}
const MenuObjView* menu_obj_view(MenuObjCatalog catalog,unsigned layout,unsigned engine,unsigned language) noexcept {
    if(language>=6)return nullptr;
    for(std::size_t i=0;i<catalog.views;++i){const auto& v=catalog.view[i];
        if(v.layout==layout&&v.engine==engine&&(v.languages&(1u<<language)))return &v;
    }
    return nullptr;
}
MenuObjCatalog prepared_menu_objects() noexcept {
#ifdef SM64DS_NATIVE_MENU_OBJ_CATALOG_HEADER
    return {prepared_menu_obj::views,sizeof(prepared_menu_obj::views)/sizeof(prepared_menu_obj::views[0])};
#else
    return {};
#endif
}
void MenuObjAssets::reset() noexcept {pending_={};ready_={};replacement_={};icons_={};palette_={};failed_=false;}
Error MenuObjAssets::begin(MenuObjCatalog catalog,unsigned layout,unsigned language) noexcept {
    ready_={};replacement_={};failed_=false;
    for(unsigned e=0;e<2;++e)pending_[e]=menu_obj_view(catalog,layout,e,language);
    if(!pending_[0]||!pending_[1]){pending_={};return Error::unsupported;}
    return Error::none;
}
const MenuObjFile* MenuObjAssets::expected_file(unsigned member) const noexcept {
    for(const auto* v:pending_)if(v){
        if(v->replacement.member==member&&member)return &v->replacement;
        if(v->icons.member==member&&member)return &v->icons;
        if(v->icon_palette.member==member&&member)return &v->icon_palette;
    }
    return nullptr;
}
Error MenuObjAssets::file(unsigned member,std::uint32_t bytes,std::uint32_t crc) noexcept {
    const auto* expected=expected_file(member);
    if(!expected)return Error::none;
    if(expected->bytes!=bytes||expected->crc!=crc){
        failed_=true;ready_={};if(member==44)icons_={};if(member==45)palette_={};return Error::invalid_data;
    }
    if(member==44)icons_=*expected;
    else if(member==45)palette_=*expected;
    else for(unsigned e=0;e<2;++e)if(pending_[e]&&pending_[e]->replacement.member==member)replacement_[e]=true;
    return Error::none;
}
Error MenuObjAssets::commit(unsigned engine) noexcept {
    const auto* v=pending_[engine];
    if(failed_||!v||(v->replacement.member&&!replacement_[engine])||
        (v->icons.member&&(icons_.bytes!=v->icons.bytes||icons_.crc!=v->icons.crc))||
        (v->icon_palette.member&&(palette_.bytes!=v->icon_palette.bytes||palette_.crc!=v->icon_palette.crc)))return Error::unsupported;
    ready_[engine]=v;return Error::none;
}
}
