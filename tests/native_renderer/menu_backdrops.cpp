#include "menu_backdrops.hpp"
#include <cassert>
#include <vector>
using namespace sm64ds::render;
using namespace sm64ds::render::game_hooks;
struct Output {
    std::vector<ImageLayer> layers;
    static bool submit(const ImageLayer& layer,void* p){static_cast<Output*>(p)->layers.push_back(layer);return true;}
};
int main(){
    std::array<MenuBackdropPart,5> main,sub2,sub3;
    for(unsigned i=0;i<5;++i){main[i]={100+i,std::uint16_t(i),std::uint8_t(i),{0,0,256,256},32767};
        sub2[i]={200,std::uint16_t(i),std::uint8_t(i),{0,0,256,256},992};sub3[i]={300,std::uint16_t(i),std::uint8_t(i),{0,0,256,256},992};}
    std::array<CanvasBinding,3> banks;
    std::array<MenuBackdropView,3> views;
    for(unsigned i=0;i<3;++i){banks[i]={5200+i,{},5,1};auto& v=views[i];v.bank=std::uint16_t(5200+i);v.layout=1;
        v.engine=i?1:0;v.languages=i?63:2;v.resource=std::uint8_t(i+1);v.parts=5;v.part=i==0?main.data():i==1?sub2.data():sub3.data();
        v.character=i?0:38;v.character_bytes=16384;v.character_crc=1;v.palette_bytes=512;v.palette_crc=2;
        if(i){v.colour_palette_bytes={512,512,512,512};v.colour_palette_crc={3,4,5,6};}}
    CanvasCatalog canvases;assert(CanvasCatalog::open(banks.data(),banks.size(),canvases)==Error::none);
    MenuBackdropCatalog catalog{views.data(),views.size()};assert(validate(catalog,canvases)==Error::none);
    views[2].character_crc=9;assert(validate(catalog,canvases)==Error::invalid_data);views[2].character_crc=1;
    main[2].palette=1;assert(validate(catalog,canvases)==Error::invalid_data);main[2].palette=2;
    std::array<std::uint16_t,1024> map1{},map2{},map3{};int owners[3]{};
    MenuBackdropState state;state.configure(catalog);
    state.bind(1,&owners[0],map1.data(),32,32,100);state.bind(2,&owners[1],map2.data(),32,32,200);state.bind(3,&owners[2],map3.data(),32,32,300);
    assert(state.begin(1,1)==Error::none);state.file(38,1);state.file(43,2);assert(state.main_files_ready());state.confirm(0,true);state.confirm(1,true);
    BackgroundCpuState cpu;assert(state.crop(&owners[0],-10,14,0,cpu)==Error::none);
    assert(cpu.x==-6*4096&&cpu.y==-6*4096&&cpu.copied_bytes==30*31*2&&cpu.filled_bytes==2048);
    Output out;assert(state.backgrounds_begin(3)==Error::none);
    state.publish(1,&owners[0],1,3,1,-6,-6);state.publish(2,&owners[1],2,3,0,-257,511);state.publish(3,&owners[2],2,2,3,0,0);
    assert(state.emit(Screen::bottom,Output::submit,&out)==Error::none&&!out.layers.empty());
    bool main_seen=false,sub_seen=false;for(const auto& layer:out.layers){assert(valid(layer));
        if(layer.source==LayerSource::solid){assert(layer.material.tint==(layer.screen==Screen::bottom?0xfbfbfbffu:0x00fb00ffu));continue;}
        if(layer.bank_id==5200){main_seen=true;assert(layer.screen==Screen::bottom&&layer.part==0);}
        else{sub_seen=true;assert(layer.screen==Screen::top&&layer.part==0);}}
    assert(main_seen&&sub_seen);
    sub3[0].repeat_edges=true;out.layers.clear();
    assert(state.emit(Screen::bottom,Output::submit,&out)==Error::none);
    unsigned edges=0;for(const auto& layer:out.layers){
        if(layer.source==LayerSource::solid){assert(layer.tone_target==32);continue;}
        if(layer.bank_id==5202){assert(layer.tone_target==4);
            if(layer.clip.left==0||layer.clip.right==400)++edges;}
    }
    assert(edges==2);sub3[0].repeat_edges=false;
    // A diagnostic migration route may display proven layers while retaining
    // an explicit failure for an unmigrated producer. Acceptance stays atomic.
    out.layers.clear();state.publish(6,&owners[0],1,1,1,0,0);
    assert(state.emit(Screen::bottom,Output::submit,&out)==Error::unsupported&&out.layers.empty());
    assert(state.emit(Screen::bottom,Output::submit,&out,true)==Error::unsupported&&!out.layers.empty());
    out.layers.clear();assert(state.backgrounds_begin(3)==Error::none);
    // Exact six source writes choose a prepared part; no source pixels move.
    state.palette_begin(1);constexpr unsigned offsets[]={0x106,0x146,0x286,0x2c6,0x386,0x3c6};
    for(unsigned i=0;i<6;++i)state.palette_write(reinterpret_cast<const char*>(map1.data())+offsets[i],32,i/2==1?4:3);
    assert(state.crop(&owners[0],0,0,0,cpu)==Error::none);state.palette_end();assert(state.sub_palette(4)==Error::none);
    out.layers.clear();assert(state.backgrounds_begin(3)==Error::none);state.publish(1,&owners[0],1,3,1,0,0);state.publish(2,&owners[1],2,3,0,0,0);
    assert(state.emit(Screen::bottom,Output::submit,&out)==Error::none);
    for(const auto& layer:out.layers)if(layer.source==LayerSource::canvas)assert(layer.part==(layer.bank_id==5200?2:4));
    // Lifecycle/invalidations must refuse the entire publication before output.
    out.layers.clear();state.retire(&owners[1]);assert(state.emit(Screen::bottom,Output::submit,&out)==Error::unsupported&&out.layers.empty());
    state.bind(2,&owners[1],map2.data(),32,32,200);state.publish(2,&owners[1],2,3,0,0,0);
    state.palette_begin(0);state.palette_write(map1.data(),31,3);state.palette_end();
    assert(state.emit(Screen::bottom,Output::submit,&out)==Error::unsupported&&out.layers.empty());
    assert(state.begin(1,5)==Error::unsupported);assert(state.begin(3,1)==Error::unsupported);
    assert(state.sub_palette(0)==Error::unsupported);assert(state.begin(9,1)==Error::unsupported);
}
