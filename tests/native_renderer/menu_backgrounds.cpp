#include "menu_backgrounds.hpp"
#include <cassert>
#include <limits>
using namespace sm64ds::render;
using namespace sm64ds::render::game_hooks;
namespace {
ImageLayer output[8];unsigned used=0;
bool submit(const ImageLayer& image,void*){if(used>=8)return false;output[used++]=image;return true;}
bool reject(const ImageLayer&,void*){return false;}
}
int main(){
    MenuBackgroundVariant variants[]={{1,0,{24,32,104,96}},{2,1,{24,32,104,96}},{3,2,{24,32,104,96}},{4,3,{24,32,104,96}},{5,3,{24,32,104,96}}};
    MenuBackgroundBinding binding{1,5001,32,32,38,5,16384,42,512,43,variants,{}};binding.canvas_sha256.fill(1);
    CanvasBinding native{5001,binding.canvas_sha256,4,1};CanvasCatalog canvases;assert(CanvasCatalog::open(&native,1,canvases)==Error::none);
    assert(validate(MenuBackgroundCatalog{&binding},canvases)==Error::none);
    auto bad=binding;bad.variant=nullptr;assert(validate(MenuBackgroundCatalog{&bad},canvases)==Error::invalid_data);
    bad=binding;bad.canvas_sha256.fill(0);assert(validate(MenuBackgroundCatalog{&bad},canvases)==Error::invalid_data);
    assert(validate(MenuBackgroundCatalog{},canvases)==Error::none);
    int owner=0,other=0;std::uint16_t map[1024]{};MenuBackgroundState state;state.configure({&binding});
    state.observe(1,&owner,map,32,32,1);BackgroundCpuState cpu;cpu.x=999;
    assert(state.crop(&other,0,0,0,cpu)==Error::unsupported&&cpu.x==999);
    assert(state.crop(&owner,0,-192,0,cpu)==Error::none&&cpu.x==0&&cpu.y==0&&cpu.filled_bytes==2048&&cpu.copied_bytes==512);
    state.publish(1,&owner,1,3,1,0,0);assert(state.emit(Screen::bottom,submit,nullptr)==Error::unsupported&&used==0);
    state.assets_begin(1);assert(state.expects_file(38));state.file(38,42);state.file(43,43);state.assets_end(1);assert(!state.expects_file(38));
    state.publish(1,&owner,1,3,1,0,0);assert(state.emit(Screen::bottom,submit,nullptr)==Error::none&&used==0&&state.statistics().hidden_windows==1);
    assert(state.crop(&owner,0,0,0,cpu)==Error::none);assert(state.emit(Screen::bottom,submit,nullptr)==Error::none&&used==0); // published transform remains owned
    assert(state.backgrounds_begin(1)==Error::none);state.publish(1,&owner,1,3,1,0,0);
    assert(state.emit(Screen::bottom,submit,nullptr)==Error::none&&used==1&&output[0].bank_id==5001&&output[0].part==0&&output[0].source==LayerSource::canvas);
    assert(output[0].order==2048&&output[0].rectangle.x==-1.f/16&&output[0].clip.right==320);
    assert(state.emit(Screen::bottom,reject,nullptr)==Error::capacity);
    constexpr unsigned offsets[]={0x106,0x146,0x286,0x2c6,0x386,0x3c6};
    for(int mode:{0,1,2,3,-1,999}){
        state.palette_begin(mode);
        for(unsigned i=0;i<6;++i)state.palette_write(reinterpret_cast<char*>(map)+offsets[i],32,(mode==int(i/2))?4:3);
        assert(state.crop(&owner,0,0,0,cpu)==Error::none);state.palette_end();state.publish(1,&owner,1,3,0,0,0);used=0;
        assert(state.emit(Screen::top,submit,nullptr)==Error::none&&used==1&&output[0].screen==Screen::top);
        assert(output[0].part==(mode==0?1:mode==1?2:3)&&output[0].order==3072&&output[0].clip.left==40);
    }
    state.palette_write(map,1,3);assert(state.crop(&owner,0,0,0,cpu)==Error::unsupported); // unknown mutation refuses
    state.observe(1,&owner,map,32,32,2);assert(state.crop(&owner,-1,-9,0,cpu)==Error::none&&cpu.x==-7*4096&&cpu.y==-7*4096);
    state.publish(1,&owner,1,3,0,-7,-7);state.publish(1,&owner,2,3,0,-7,-7);used=0;
    assert(state.emit(Screen::bottom,submit,nullptr)==Error::unsupported&&used==0); // both engines validated before append
    assert(state.backgrounds_begin(0)==Error::invalid_data);assert(state.backgrounds_begin(3)==Error::none);
    assert(state.crop(&owner,std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),0,cpu)==Error::none&&cpu.copied_bytes==0);
    state.publish(1,&owner,1,3,1,0,0);assert(state.emit(Screen::bottom,submit,nullptr)==Error::none&&used==0);
    state.identity(&owner,&other,32,32);assert(state.crop(&owner,0,0,0,cpu)==Error::unsupported);
    state.observe(1,&owner,map,32,32,1);assert(state.crop(&owner,0,0,1,cpu)==Error::unsupported);
    state.retire(&other);state.retire(&owner);assert(state.crop(&owner,0,0,0,cpu)==Error::unsupported);
    state.observe(1,&other,map,32,32,999);assert(state.crop(&other,0,0,0,cpu)==Error::unsupported);
    state.observe(1,&other,map,32,32,1);state.palette_begin(0);state.palette_write(reinterpret_cast<char*>(map)+0x106,32,4);state.palette_end();
    assert(state.crop(&other,0,0,0,cpu)==Error::unsupported);state.reset();assert(!state.commands()[0].seen);
}
