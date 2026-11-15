// Original ARM crop/palette/select behavior against native source adapters.
#include "menu_backgrounds.hpp"
#include "bank_crc.hpp"
#include <cstring>
using namespace sm64ds::render;
using namespace sm64ds::render::game_hooks;
namespace {
struct Source {int type,priority,enabled,dirty;std::uint16_t* destination;std::uint16_t* source;int unused;std::uint16_t width,height;int x,y,extra_x,extra_y,next,previous;};
static_assert(sizeof(Source)==0x38&&offsetof(Source,width)==0x1c);
bool enabled=false;unsigned forwards=0,retirements=0,rng=0x42475354;std::uint64_t filled=0,copied=0;
std::uint16_t original[1024],native_map[1024],reference_map[1024],native_pixels[1024],reference_pixels[1024],scratch[1024];
std::uint8_t characters[16384],palette[512];
alignas(4) std::uint8_t global[0x190],container[8],cursor_container[4],cursor[0x30],q[8],select0[16],select1[32];void* selects[2];
Source native_owner{},reference_owner{};
MenuBackgroundVariant variants[5];MenuBackgroundBinding binding{1,5001,32,32,38,5,16384,0,512,0,variants,{}};
unsigned next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
template<class T>void set(void* p,unsigned at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
std::uint32_t crc(const void* p,unsigned bytes){return detail::bank_crc32(static_cast<const std::uint8_t*>(p),bytes,bytes);}
bool submit(const ImageLayer& layer,void*){return valid(layer);}
}
namespace sm64ds::render::game_hooks {bool native_active() noexcept{return enabled;}}
extern "C" {
void* data_ov007_0210342c=global;
std::uint8_t data_ov007_020ccc44[]={3,4};
volatile unsigned background_native_interval=0;
void reference_crop(Source*,int,int,std::uint16_t);
void reference_palette(std::uint16_t*,int,int);
void original_select(int);
int __wrap_func_ov007_020adc08(int,void*);
int __wrap_func_ov007_020c0a9c(void*);
void __wrap_func_ov007_020c0638(void*,int,int,std::uint16_t);
void __wrap_func_ov007_020c0288(void*,int,int);
void __wrap_func_ov007_020b1390(int);
int __real_func_ov007_020adc08(int,void* p){auto& s=*static_cast<Source*>(p);s.source=s.destination;s.destination=native_pixels;return 0x1234;}
int __real_func_ov007_020c0a9c(void*){++retirements;return 0x5678;}
void __real_func_ov007_020c0638(void* p,int x,int y,std::uint16_t fill){++forwards;reference_crop(static_cast<Source*>(p),x,y,fill);}
void __real_func_ov007_020c0288(void* p,int n,int value){reference_palette(static_cast<std::uint16_t*>(p),n,value);}
void __real_func_ov007_020b1390(int mode){original_select(mode);}
void func_ov007_020c0288(void* p,int n,int value){if(enabled)__wrap_func_ov007_020c0288(p,n,value);else reference_palette(static_cast<std::uint16_t*>(p),n,value);}
void func_ov007_020c0638(void* p,int x,int y,std::uint16_t fill){if(enabled)__wrap_func_ov007_020c0638(p,x,y,fill);else reference_crop(static_cast<Source*>(p),x,y,fill);}
void MultiStore16(std::uint16_t value,char* out,int bytes){filled+=unsigned(bytes);for(int i=0;i<bytes;i+=2)std::memcpy(out+i,&value,2);}
void MultiCopyHalf(char* source,char* out,int bytes){copied+=unsigned(bytes);std::memcpy(out,source,unsigned(bytes));}
}
#define REQUIRE(v) do {if(!(v))return __LINE__;} while(0)
extern "C" int native_background_bridge_regression(){
    for(unsigned i=0;i<1024;++i)original[i]=std::uint16_t(next());
    for(unsigned i=0;i<sizeof(characters);++i)characters[i]=std::uint8_t(next());
    for(unsigned i=0;i<sizeof(palette);++i)palette[i]=std::uint8_t(next());
    for(unsigned variant=0;variant<5;++variant){std::memcpy(scratch,original,sizeof(original));
        if(variant)for(unsigned n=0;n<6;++n){const unsigned offsets[]={0x106,0x146,0x286,0x2c6,0x386,0x3c6};reference_palette(scratch+offsets[n]/2,32,n/2==variant-1?4:3);}
        variants[variant]={crc(scratch,sizeof(scratch)),std::uint16_t(variant),{24,32,104,96}};
    }
    binding.character_crc=crc(characters,sizeof(characters));binding.palette_crc=crc(palette,sizeof(palette));binding.canvas_sha256.fill(1);
    CanvasBinding canvas{5001,binding.canvas_sha256,5,1};CanvasCatalog catalog;REQUIRE(CanvasCatalog::open(&canvas,1,catalog)==Error::none);
    REQUIRE(install_menu_backgrounds({&binding},catalog));enabled=true;
    std::memcpy(native_map,original,sizeof(original));std::memcpy(reference_map,original,sizeof(original));
    native_owner={3,1,1,0,native_map,nullptr,99,32,32,0,0,7,-9,11,13};
    REQUIRE(__wrap_func_ov007_020adc08(1,&native_owner)==0x1234);
    reference_owner=native_owner;reference_owner.source=reference_map;reference_owner.destination=reference_pixels;
    menu_background_assets_begin(1);menu_background_file_loaded(38,characters);menu_background_file_loaded(43,palette);menu_background_assets_end(1);
    filled=copied=0;std::uint64_t expected_fill=0,expected_copy=0;
    for(unsigned test=0;test<2048;++test){const int x=int(next()%777)-388,y=int(next()%777)-388;
        std::memset(native_pixels,0xce,sizeof(native_pixels));reference_crop(&reference_owner,x,y,0);expected_fill=filled;expected_copy=copied;
        background_native_interval=1;__wrap_func_ov007_020c0638(&native_owner,x,y,0);background_native_interval=0;
        REQUIRE(!forwards&&filled==expected_fill&&copied==expected_copy);
        for(unsigned i=0;i<sizeof(Source);++i)if(i<0x10||i>=0x18)REQUIRE(reinterpret_cast<unsigned char*>(&native_owner)[i]==reinterpret_cast<unsigned char*>(&reference_owner)[i]);
        for(auto pixel:native_pixels)REQUIRE(pixel==0xcece);
        menu_background_group_begin(1);menu_background_publish(1,&native_owner,1,3,1,native_owner.x/4096,native_owner.y/4096);
        REQUIRE(menu_background_emit(Screen::bottom,submit,nullptr)==Error::none);
    }
    REQUIRE(menu_background_statistics().replaced==2048&&menu_background_statistics().filled_bytes==expected_fill&&menu_background_statistics().copied_bytes==expected_copy);
    set(global,0xcc,static_cast<void*>(container));set(global,0xc0,static_cast<void*>(cursor_container));set(cursor_container,0,static_cast<void*>(cursor));set(cursor,0x24,static_cast<void*>(q));set(q,2,std::int16_t(-17));
    selects[0]=select0;selects[1]=select1;set(global,0x134,static_cast<void*>(selects));set(select0,8,123456);set(select1,0xc,std::uint16_t(27));
    for(int mode:{0,1,2,3,-9,27}){
        std::uint8_t expected_cursor[sizeof(cursor)];set(container,4,static_cast<void*>(&reference_owner));enabled=false;original_select(mode);std::memcpy(expected_cursor,cursor,sizeof(cursor));
        set(container,4,static_cast<void*>(&native_owner));enabled=true;const auto before_fill=filled,before_copy=copied;
        background_native_interval=1;__wrap_func_ov007_020b1390(mode);background_native_interval=0;
        REQUIRE(filled==before_fill&&copied==before_copy&&!forwards);
        REQUIRE(std::memcmp(native_map,reference_map,sizeof(native_map))==0&&std::memcmp(cursor,expected_cursor,sizeof(cursor))==0);
        REQUIRE(native_owner.x==reference_owner.x&&native_owner.y==reference_owner.y&&native_owner.dirty==reference_owner.dirty);
        menu_background_group_begin(1);menu_background_publish(1,&native_owner,1,3,1,0,0);REQUIRE(menu_background_emit(Screen::bottom,submit,nullptr)==Error::none);
    }
    REQUIRE(menu_background_statistics().palette_batches==6&&menu_background_statistics().palette_writes==36);
    __wrap_func_ov007_020c0288(native_map,1,7);__wrap_func_ov007_020c0638(&native_owner,0,0,0);REQUIRE(forwards==1);
    menu_background_group_begin(1);menu_background_publish(1,&native_owner,1,3,1,0,0);REQUIRE(menu_background_emit(Screen::bottom,submit,nullptr)==Error::unsupported);
    enabled=false;__wrap_func_ov007_020c0638(&reference_owner,8,8,0);REQUIRE(forwards==2);
    REQUIRE(__wrap_func_ov007_020c0a9c(&native_owner)==0x5678&&retirements==1);enabled=true;
    REQUIRE(menu_background_statistics().retired==1);return 0;
}
