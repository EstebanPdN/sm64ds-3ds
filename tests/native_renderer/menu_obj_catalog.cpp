#include "menu_obj_catalog.hpp"
#include <array>
#include <cassert>
using namespace sm64ds::render;
using namespace sm64ds::render::game_hooks;
int main(){
    MenuCellPart part{0,0,8,8};MenuCellImage cell{&part,1,0,0,0,8,8,32};
    std::array<MenuCellBank,6> banks;
    for(unsigned i=0;i<6;++i)banks[i]={std::uint16_t(i),4100,&cell,1,32,1,64,2,64,3};
    ImageAsset asset{8,8,ImageBank::Alpha::independent};ImageBankBinding binding{4100,{},&asset,1};ImageCatalog images;
    assert(ImageCatalog::open(&binding,1,images)==Error::none);
    std::array<MenuObjView,4> views;
    views[0]={1,0,3,{banks.data(),6},{},{44,8192,4},{45,224,5}};
    views[1]={1,1,3,{banks.data(),6},{},{},{}};
    views[2]={3,1,4,{banks.data(),6},{25,12788,6},{},{}};
    views[3]={3,0,4,{banks.data(),6},{25,12788,6},{44,8192,4},{45,224,5}};
    MenuObjCatalog catalog{views.data(),views.size()};
    assert(validate(catalog,images)==Error::none);
    assert(menu_obj_view(catalog,1,0,0)==&views[0]&&menu_obj_view(catalog,1,0,1)==&views[0]);
    assert(menu_obj_view(catalog,1,1,1)==&views[1]&&menu_obj_view(catalog,3,1,2)==&views[2]);
    // Missing translations/layouts are explicit, never another language's art.
    assert(!menu_obj_view(catalog,1,0,2)&&!menu_obj_view(catalog,3,0,3)&&!menu_obj_view(catalog,1,1,6));
    views[2].layout=1;views[2].languages=3;views[2].replacement={};
    assert(validate(catalog,images)==Error::invalid_data); // overlapping route
    views[2]={3,1,4,{banks.data(),6},{25,12788,6},{},{}};
    views[2].replacement.member=24;assert(validate(catalog,images)==Error::invalid_data);views[2].replacement.member=25;
    views[2].languages=12;assert(validate(catalog,images)==Error::invalid_data);views[2].languages=4;
    views[0].icons.bytes=8191;assert(validate(catalog,images)==Error::invalid_data);views[0].icons.bytes=8192;
    views[1].icons={44,8192,4};assert(validate(catalog,images)==Error::invalid_data);views[1].icons={};
    views[2].cells.banks=5;assert(validate(catalog,images)==Error::invalid_data);views[2].cells.banks=6;
    assert(validate(catalog,ImageCatalog{})==Error::invalid_data);
    assert(validate(MenuObjCatalog{},images)==Error::none);
    views[2]={3,1,4,{banks.data(),6},{25,12788,6},{},{}};
    views[3].replacement.crc=7;assert(validate(catalog,images)==Error::invalid_data);views[3].replacement.crc=6;
    MenuObjAssets assets;
    assert(assets.begin(catalog,1,1)==Error::none);
    assert(assets.commit(0)==Error::unsupported&&assets.commit(1)==Error::none);
    assert(assets.file(44,8192,4)==Error::none&&assets.file(45,224,5)==Error::none);
    assert(assets.commit(0)==Error::none&&assets.view(0)==&views[0]);
    // Returning from drawing preserves previously loaded MAIN icon identity.
    assert(assets.begin(catalog,1,0)==Error::none&&assets.commit(0)==Error::none);
    assert(assets.file(44,8192,9)==Error::invalid_data&&!assets.view(0));
    assert(assets.file(44,8192,4)==Error::none&&assets.commit(0)==Error::unsupported);
    assert(assets.begin(catalog,1,0)==Error::none&&assets.commit(0)==Error::none);
    assert(assets.begin(catalog,3,2)==Error::none);
    assert(assets.commit(1)==Error::unsupported);
    assert(assets.file(25,12788,6)==Error::none&&assets.commit(1)==Error::none&&assets.view(1)==&views[2]);
    assert(assets.commit(0)==Error::none&&assets.view(0)==&views[3]);
    assert(assets.begin(catalog,3,3)==Error::unsupported&&assets.commit(0)==Error::unsupported&&assets.commit(1)==Error::unsupported);
    assets.reset();assert(!assets.view(0)&&!assets.view(1));
}
