// Exact host comparison of the real compositor against its immutable previous
// implementation. Fixtures contain generated data; boot inputs are local dumps.
// This test does not establish 3DS hardware correctness or performance.
#include "sm64ds/native_2d.hpp"
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/game_draw.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace sm64ds::reference { void native_2d_scanout(Native2DFrame&); }
namespace {
using Scan = void (*)(sm64ds::Native2DFrame&);
unsigned platform_failures, side_effects, comparisons, capture_checks;
void require(bool condition, const std::string& message) {
    if(!condition) {std::fprintf(stderr,"native 2D check: %s\n",message.c_str());std::exit(1);}
}
struct Snapshot {
    std::vector<std::uint8_t> regs=std::vector<std::uint8_t>(0x2000);
    std::vector<std::uint8_t> palette=std::vector<std::uint8_t>(0x800);
    std::vector<std::uint8_t> vram=std::vector<std::uint8_t>(0xa4000);
    std::vector<std::uint8_t> oam=std::vector<std::uint8_t>(0x800);
};
void put(std::vector<std::uint8_t>& bytes,unsigned at,std::uint32_t value,unsigned count=2) {
    require(at+count<=bytes.size(),"fixture write bounds");
    for(unsigned i=0;i<count;++i)bytes[at+i]=std::uint8_t(value>>(i*8));
}
void load(const Snapshot& s) {
    sm64ds::native_video_reset();platform_failures=side_effects=0;
    std::memcpy(sm64ds::native_video_pointer(0x04000000,s.regs.size()),s.regs.data(),s.regs.size());
    std::memcpy(sm64ds::native_video_pointer(0x05000000,s.palette.size()),s.palette.data(),s.palette.size());
    std::memcpy(sm64ds::native_video_pointer(0x06800000,s.vram.size()),s.vram.data(),s.vram.size());
    std::memcpy(sm64ds::native_video_pointer(0x07000000,s.oam.size()),s.oam.data(),s.oam.size());
}
void unchanged(const Snapshot& s,const std::string& name) {
    require(!std::memcmp(sm64ds::native_video_pointer(0x04000000,s.regs.size()),s.regs.data(),s.regs.size()),name+": register mutation");
    require(!std::memcmp(sm64ds::native_video_pointer(0x05000000,s.palette.size()),s.palette.data(),s.palette.size()),name+": palette mutation");
    require(!std::memcmp(sm64ds::native_video_pointer(0x06800000,s.vram.size()),s.vram.data(),s.vram.size()),name+": VRAM mutation");
    require(!std::memcmp(sm64ds::native_video_pointer(0x07000000,s.oam.size()),s.oam.data(),s.oam.size()),name+": OAM mutation");
    require(side_effects==0,name+": unexpected IRQ/GX side effect");
}
std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream stream(path,std::ios::binary);
    require(bool(stream),std::string("cannot open ")+path);
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
Snapshot boot_dump(const char* path) {
    const auto dump=read_file(path);Snapshot s;
    require(dump.size()==s.regs.size()+s.palette.size()+s.vram.size(),"boot video dump size");
    std::size_t at=0;
    for(auto* span:{&s.regs,&s.palette,&s.vram}) {
        std::copy_n(dump.data()+at,span->size(),span->data());at+=span->size();
    }
    require(!(s.regs[1]&0x10)&&!(s.regs[0x1001]&0x10),"boot dump must have OBJ off: OAM was not captured");
    return s;
}
std::unique_ptr<sm64ds::Native2DFrame> run(Scan scan,const Snapshot& s,
                                         const std::string& name,std::uint32_t initial) {
    auto frame=std::make_unique<sm64ds::Native2DFrame>();
    for(auto* layer:{&frame->underlay,&frame->overlay,&frame->bottom})
        std::fill_n(layer->pixels,256*256,initial);
    load(s);scan(*frame);unchanged(s,name);return frame;
}
void equal_pixels(const sm64ds::Native2DLayer& a,const sm64ds::Native2DLayer& b,
                  const std::string& name) {
    for(unsigned i=0;i<256*256;++i)if(a.pixels[i]!=b.pixels[i]) {
        char detail[160];std::snprintf(detail,sizeof(detail)," at (%u,%u): %08x != %08x",i%256,i/256,a.pixels[i],b.pixels[i]);
        require(false,name+detail);
    }
}
std::unique_ptr<sm64ds::Native2DFrame> compare(const Snapshot& s,const std::string& name,
                                            bool okay=true,std::uint32_t initial=0x12345678) {
    const auto before=run(sm64ds::reference::native_2d_scanout,s,name+" reference",initial);
    const auto old_ok=sm64ds::native_video_ok();const auto old_bad=sm64ds::native_video_bad_address();
    const auto old_fail=platform_failures;
    auto after=run(sm64ds::native_2d_scanout,s,name+" optimized",initial);
    require(old_ok==sm64ds::native_video_ok()&&old_bad==sm64ds::native_video_bad_address()&&old_fail==platform_failures,name+": changed failure diagnostic");
    require(old_ok==okay,name+": unexpected video validity");
    equal_pixels(before->underlay,after->underlay,name+" main");
    equal_pixels(before->overlay,after->overlay,name+" overlay");
    equal_pixels(before->bottom,after->bottom,name+" sub");
    require(before->main_on_top==after->main_on_top&&before->has_3d==after->has_3d,name+": changed route/geometry flags");
    ++comparisons;return after;
}
void map_banks(Snapshot& s) {
    s.regs[0x240]=0x81;s.regs[0x241]=0x82;s.regs[0x242]=0x84;s.regs[0x243]=0x84;
    s.regs[0x244]=0x84;s.regs[0x245]=0x85;s.regs[0x246]=0x80;
    s.regs[0x248]=0x82;s.regs[0x249]=0x83;
    for(unsigned i=0;i<256;++i)put(s.oam,i*8,0x0200); // disabled nonaffine OBJ
}
Snapshot solid() {
    Snapshot s;map_banks(s);put(s.regs,0x304,0x8000);
    for(unsigned engine=0;engine<2;++engine) {
        const unsigned reg=engine*0x1000,physical=engine?0x40000:0;
        put(s.regs,reg,0x00010f00,4);
        for(unsigned bg=0;bg<4;++bg) {
            put(s.regs,reg+8+bg*2,(16+2*bg)<<8);
            for(unsigned i=0;i<32;++i)s.vram[physical+(bg+1)*32+i]=std::uint8_t((bg+1)*0x11);
            for(unsigned cell=0;cell<1024;++cell)put(s.vram,physical+0x8000+bg*0x1000+cell*2,bg+1);
        }
        for(unsigned pal=0;pal<2;++pal) {
            const unsigned base=engine*0x400+pal*0x200;
            put(s.palette,base+2,0x001f);put(s.palette,base+4,0x03e0);
            put(s.palette,base+6,0x7c00);put(s.palette,base+8,0x7fff);
        }
        std::fill_n(s.vram.data()+(engine?0x60000:0x20000)+32,32,0x22);
    }
    return s;
}
void object(Snapshot& s,unsigned engine,unsigned slot,unsigned x,unsigned y,
            unsigned mode,unsigned prio,unsigned tile=1,unsigned palette=0,
            unsigned additional0=0,unsigned additional1=0) {
    const unsigned oam=engine*0x400+slot*8;
    put(s.oam,oam,(y&255)|(mode<<10)|additional0);
    put(s.oam,oam+2,(x&511)|additional1);
    put(s.oam,oam+4,(tile&1023)|(prio<<10)|(palette<<12));
}
Snapshot varied(unsigned seed) {
    Snapshot s;map_banks(s);std::mt19937 rng(seed);
    for(auto& byte:s.vram)byte=std::uint8_t(rng());
    for(auto& byte:s.palette)byte=std::uint8_t(rng());
    put(s.regs,0x304,(seed&1)?0x8000:0);
    for(unsigned engine=0;engine<2;++engine) {
        const unsigned reg=engine*0x1000,mode=(seed+engine)%6;
        const unsigned windows=(seed%8)<<13;
        const unsigned bg_mask=(seed%9)?15:((seed/9)%16);
        const unsigned objects=(seed%5)?0x1000:0;
        std::uint32_t control=0x10000|mode|(bg_mask<<8)|objects|windows|
            ((seed&1)?0x10:0)|((seed&2)?0x20:0)|((seed&4)?0x40:0)|
            ((seed%4)<<20)|((seed%3)?0x40000000:0)|((seed%4)?0x80000000:0);
        if(engine==0&&(seed%7==0))control|=0x108; // real geometry layers, no synthetic 3D pixels
        put(s.regs,reg,control,4);
        for(unsigned bg=0;bg<4;++bg) {
            const unsigned priority=(seed%4==0)?0:(seed+bg*3+engine)%4;
            const bool extended=(mode==3&&bg==3)||(mode==4&&bg==3)||(mode==5&&bg>=2);
            const bool bitmap=extended&&seed%3==1;
            unsigned cnt=priority|((bg&3)<<2)|((16+bg*3)<<8)|((seed&1)?0x80:0)|
                ((seed&2)?0x2000:0)|(((seed+bg)%4)<<14);
            if(bitmap)cnt=priority|0x80|((seed&1)?4:0)|((seed&2)?0x2000:0)|((seed%2)<<14);
            else if(extended)cnt&=~0x80u; // extended tile entries; bitmap is explicit
            put(s.regs,reg+8+bg*2,cnt);
            put(s.regs,reg+0x10+bg*4,(seed*19+bg*7)&511);
            put(s.regs,reg+0x12+bg*4,(seed*11+bg*23)&511);
        }
        for(unsigned bg=2;bg<4;++bg) {
            const unsigned ab=reg+0x20+(bg-2)*0x10;
            put(s.regs,ab,(seed%3)?256:192);put(s.regs,ab+2,(seed%3)?0:64);
            put(s.regs,ab+4,(seed%3)?0:std::uint16_t(-64));put(s.regs,ab+6,256);
            put(s.regs,ab+8,std::uint32_t((int(seed%13)-6)*256)&0x0fffffff,4);
            put(s.regs,ab+12,std::uint32_t((int(seed%17)-8)*256)&0x0fffffff,4);
        }
        put(s.regs,reg+0x40,(16<<8)|192);put(s.regs,reg+0x42,(96<<8)|32);
        put(s.regs,reg+0x44,(24<<8)|160);put(s.regs,reg+0x46,(80<<8)|0);
        put(s.regs,reg+0x48,((seed*23)&63)|(((seed*37)&63)<<8));
        put(s.regs,reg+0x4a,((seed*41)&63)|(((seed*59)&63)<<8));
        put(s.regs,reg+0x50,(seed%4<<6)|((seed*13)&63)|(((seed*29)&63)<<8));
        put(s.regs,reg+0x52,(seed%32)|(((seed*7)%32)<<8));put(s.regs,reg+0x54,(seed*11)%32);
        put(s.regs,reg+0x6c,(seed%4<<14)|((seed*5)%32));
        for(unsigned i=0;i<12;++i) {
            const bool affine=i==5||i==6;
            object(s,engine,i,(i*19+seed*3)%300,(i*17+seed)%216,i%4,(seed+i)%4,
                (i*8+seed)%96,i%16,(i&1?0x2000:0)|(affine?0x100:0)|(i==6?0x200:0),
                (i%3<<14)|(affine?0x200:(i&2?0x1000:0))|(i&4&&!affine?0x2000:0));
        }
        // Affine OBJ group1 identity with a quarter shear.
        const unsigned base=engine*0x400+4*8;
        put(s.oam,base+6,256);put(s.oam,base+14,64);
        put(s.oam,base+22,std::uint16_t(-64));put(s.oam,base+30,256);
    }
    return s;
}
double benchmark(Scan scan,const Snapshot& s,unsigned iterations) {
    load(s);auto frame=std::make_unique<sm64ds::Native2DFrame>();
    scan(*frame);std::vector<double> samples;
    for(unsigned i=0;i<iterations;++i) {
        const auto start=std::chrono::steady_clock::now();scan(*frame);
        samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
    require(sm64ds::native_video_ok()&&platform_failures==0&&side_effects==0,"benchmark video state");
    std::sort(samples.begin(),samples.end());return samples[samples.size()/2];
}
void capture_equal(const Snapshot& s,const std::string& name) {
    // Build the reference using the same composition with its downstream
    // presentation selector and master brightness neutralized in a fixture.
    // The tested tap must leave the original live registers unchanged.
    auto composited=s;
    const unsigned control=std::uint32_t(s.regs[0])|(std::uint32_t(s.regs[1])<<8)|
        (std::uint32_t(s.regs[2])<<16)|(std::uint32_t(s.regs[3])<<24);
    put(composited.regs,0,(control&~0x30000u)|0x10000,4);put(composited.regs,0x6c,0);
    const auto expected=run(sm64ds::reference::native_2d_scanout,composited,name+" capture reference",0x12345678);
    auto actual=std::make_unique<sm64ds::Native2DFrame>();
    for(auto* layer:{&actual->underlay,&actual->overlay,&actual->bottom})
        std::fill_n(layer->pixels,256*256,0x12345678);
    load(s);std::string error="must clear on success";
    require(sm64ds::native_2d_capture_layers(*actual,error),name+": capture failed: "+error);
    require(error.empty()&&sm64ds::native_video_ok()&&platform_failures==0,name+": capture status");
    unchanged(s,name+" capture");
    equal_pixels(expected->underlay,actual->underlay,name+" capture main");
    equal_pixels(expected->overlay,actual->overlay,name+" capture overlay");
    require(expected->has_3d==actual->has_3d&&expected->main_on_top==actual->main_on_top,name+": capture geometry/route flags");
    for(auto pixel:actual->bottom.pixels)require(pixel==0x12345678,name+": tap sampled sub engine");
    // Following presentation observes precisely the same original registers,
    // not the neutralized reference fixture or capture's routing/cache state.
    auto presentation=std::make_unique<sm64ds::Native2DFrame>();
    sm64ds::native_2d_scanout(*presentation);
    const auto original=run(sm64ds::reference::native_2d_scanout,s,name+" after capture reference",0);
    equal_pixels(original->underlay,presentation->underlay,name+" presentation after tap");
    equal_pixels(original->overlay,presentation->overlay,name+" presentation overlay after tap");
    equal_pixels(original->bottom,presentation->bottom,name+" presentation sub after tap");
    ++capture_checks;
}
void capture_reject(const Snapshot& s,const std::string& name,bool invalid_state=false) {
    auto frame=std::make_unique<sm64ds::Native2DFrame>();
    for(auto* layer:{&frame->underlay,&frame->overlay,&frame->bottom})
        std::fill_n(layer->pixels,256*256,0x23456789);
    frame->main_on_top=false;frame->has_3d=true;load(s);
    if(invalid_state)sm64ds::native_video_read(0x06080000,1);
    std::string error;
    require(!sm64ds::native_2d_capture_layers(*frame,error)&&!error.empty(),name+": rejection/error missing");
    unchanged(s,name);
    for(auto* layer:{&frame->underlay,&frame->overlay,&frame->bottom})
        for(auto pixel:layer->pixels)require(pixel==0x23456789,name+": rejected capture mutated output");
    require(!frame->main_on_top&&frame->has_3d,name+": rejected capture mutated flags");
    ++capture_checks;
}
} // namespace
namespace sm64ds {
void native_platform_fail(const char*) {++platform_failures;}
void native_platform_irq_set_master(bool) {++side_effects;}
// No compositor fixture queries GXSTAT. A query fails and is visible to the test.
bool native_video_gx_status(std::uint32_t&) {++side_effects;return false;}
}
extern "C" {
void sm64ds_draw_port(std::uint32_t,std::uint32_t) {++side_effects;}
void sm64ds_draw_fifo(std::uint32_t) {++side_effects;}
bool sm64ds_draw_ok() {return false;}
}
int main(int argc,char** argv) {
    require(argc==3||argc==4,"arguments: boot-video.bin boot-scanout.bin [benchmark iterations]");
    const bool blend_off=std::getenv("SM64DS_BLEND_OFF")&&std::getenv("SM64DS_BLEND_OFF")[0]!='0';
    const auto boot=boot_dump(argv[1]);const auto expected=read_file(argv[2]);
    require(expected.size()==3*sizeof(sm64ds::Native2DLayer),"boot scanout dump size");
    auto output=compare(boot,"real boot dump",true,0);
    std::size_t at=0;
    for(const auto* layer:{&output->underlay,&output->overlay,&output->bottom}) {
        if(!blend_off)require(!std::memcmp(expected.data()+at,layer->pixels,sizeof(*layer)),"real boot pixels differ from captured Azahar dump");
        at+=sizeof(*layer);
    }
    auto s=solid();output=compare(s,"BG ties and lower BG ID");
    require(output->underlay.pixels[0]==0xffff0000&&output->bottom.pixels[0]==0xffff0000,"BG tie chooses BG0");
    for(unsigned engine=0;engine<2;++engine) {
        put(s.regs,engine*0x1000,0x00011f00,4);object(s,engine,0,16,16,0,0);
    }
    output=compare(s,"OBJ over equal BG priority");
    require(output->underlay.pixels[16*256+16]==0xff00ff00,"OBJ wins equal priority");
    for(unsigned engine=0;engine<2;++engine) {
        object(s,engine,0,16,16,1,0);put(s.regs,engine*0x1000+0x50,0x0100);put(s.regs,engine*0x1000+0x52,0x0808);
    }
    output=compare(s,"semi OBJ uses second target and 5-bit alpha");
    require(output->underlay.pixels[16*256+16]==(blend_off?0xff00ff00:0xff7b7b00),"semi OBJ color");
    for(unsigned engine=0;engine<2;++engine) {
        object(s,engine,0,16,16,2,0);put(s.regs,engine*0x1000,0x00019f00,4);
        put(s.regs,engine*0x1000+0x4a,0x0402);
    }
    output=compare(s,"OBJ window with different inside/outside BGs");
    require(output->underlay.pixels[0]==0xff00ff00&&output->underlay.pixels[16*256+16]==0xff0000ff,"OBJ window mask");
    // Main may leave OBJ on while sub turns it off. Neither can see stale OBJ
    // coverage from the previous engine or from the preceding scanout.
    put(s.regs,0x1000,0x00018f00,4);
    output=compare(s,"sub OBJ off retains WINOUT and clears stale masks");
    require(output->bottom.pixels[16*256+16]==0xff00ff00,"stale sub OBJ window");
    put(s.regs,0,0x00018f00,4);put(s.palette,4,0x7fff);
    output=compare(s,"both OBJ off and palette changed between frames");
    require(output->underlay.pixels[16*256+16]==0xffffffff,"palette cache refresh/stale main OBJ window");
    s=solid();put(s.regs,0x6c,0x8010);put(s.regs,0x106c,0x4010);
    output=compare(s,"master brightness extreme modes");
    require(output->underlay.pixels[0]==0xff000000&&output->bottom.pixels[0]==0xffffffff,"master brightness endpoints");
    for(unsigned seed=1;seed<=72;++seed)compare(varied(seed),"varied fixture "+std::to_string(seed));
    for(unsigned mode=0;mode<4;++mode) {
        s=solid();put(s.regs,0,(mode<<16)|0xf00|(2<<18),4);put(s.regs,0x1000,(mode<<16)|0xf00,4);
        compare(s,"display mode "+std::to_string(mode));
        put(s.regs,0,(mode<<16)|0xf80,4);put(s.regs,0x1000,(mode<<16)|0xf80,4);
        output=compare(s,"forced blank mode "+std::to_string(mode));
        require(output->underlay.pixels[0]==0xffffffff&&output->bottom.pixels[0]==0xffffffff,"forced blank white presentation");
    }
    s=solid();s.regs[0x240]=0;s.regs[0x242]=0;
    compare(s,"unmapped BG preserves read failure",false);
    capture_equal(boot,"real boot capture");
    for(unsigned seed=1;seed<=72;++seed)capture_equal(varied(seed),"varied capture "+std::to_string(seed));
    for(unsigned mode=0;mode<4;++mode) {
        s=solid();put(s.regs,0,(mode<<16)|0xf00|(2<<18),4);put(s.regs,0x6c,0x8010);
        capture_equal(s,"pre-brightness/pre-selector mode "+std::to_string(mode));
        put(s.regs,0,(mode<<16)|0xf08,4);
        capture_equal(s,"geometry independent of display mode "+std::to_string(mode));
        put(s.regs,0,(mode<<16)|0xe08,4);
        capture_equal(s,"disabled BG0 never enables geometry mode "+std::to_string(mode));
        put(s.regs,0,(mode<<16)|0xf80,4);
        capture_reject(s,"forced blank capture mode "+std::to_string(mode));
    }
    s=solid();put(s.regs,0,0x00010f06,4);capture_reject(s,"large bitmap capture not implemented");
    put(s.regs,0,0x00010f07,4);capture_reject(s,"prohibited BG mode capture");
    s=solid();capture_reject(s,"invalid video capture state",true);
    // Engine B video reads are unnecessary to Source A. Its missing BG bank
    // would fail a dual-screen presentation but does not invalidate this tap.
    s=solid();s.regs[0x242]=0;load(s);std::string capture_error;
    auto main_only=std::make_unique<sm64ds::Native2DFrame>();
    require(sm64ds::native_2d_capture_layers(*main_only,capture_error)&&capture_error.empty()&&sm64ds::native_video_ok(),"capture must not read sub engine");
    unchanged(s,"main-only capture");++capture_checks;
    double old_ms=0,new_ms=0;
    if(argc==4) {
        const unsigned iterations=unsigned(std::strtoul(argv[3],nullptr,10));
        require(iterations>0&&iterations<=2000,"benchmark iteration bounds");
        old_ms=benchmark(sm64ds::reference::native_2d_scanout,boot,iterations);
        new_ms=benchmark(sm64ds::native_2d_scanout,boot,iterations);
    }
    std::printf("{\"status\":\"passed\",\"comparisons\":%u,\"capture_checks\":%u,\"blend_off\":%s,\"boot_reference_median_ms\":%.6f,\"boot_optimized_median_ms\":%.6f}\n",comparisons,capture_checks,blend_off?"true":"false",old_ms,new_ms);
}
