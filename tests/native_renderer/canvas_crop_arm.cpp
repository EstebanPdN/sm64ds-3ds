// Original map cropping versus native finite-domain windows, ARM32 only.
#include "canvas_windows.hpp"
#include <cstring>
using namespace sm64ds::render;
namespace {
struct Source {
    int type,priority,unused,dirty;
    std::uint16_t* destination;
    const std::uint16_t* source;
    int unused2;
    std::uint16_t width,height;
    int x,y;
};
static_assert(sizeof(Source)==0x28&&offsetof(Source,width)==0x1c);
std::uint16_t pixels[1024]{},destination[1026]{};
unsigned random_state=0x43524f50;
unsigned next(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
int modulo(int value,int size){const auto r=value%size;return r<0?r+size:r;}
int floor8(int value){return value>=0?value/8:-((-value+7)/8);}
}
extern "C" {
volatile unsigned canvas_native_interval=0;
void reference_crop(Source*,int,int,std::uint16_t);
void MultiStore16(std::uint16_t value,char* out,int bytes){for(int i=0;i<bytes;i+=2)std::memcpy(out+i,&value,2);}
void MultiCopyHalf(char* source,char* out,int bytes){std::memcpy(out,source,unsigned(bytes));}
}
#define REQUIRE(v) do {if(!(v))return __LINE__;} while(0)
extern "C" int native_canvas_crop_regression(){
    for(unsigned case_id=0;case_id<4096;++case_id){
        const auto w=8u<<next()%3,h=8u<<next()%3;
        Source source{};source.width=std::uint16_t(w);source.height=std::uint16_t(h);
        source.destination=destination+1;source.source=pixels;destination[0]=destination[w*h+1]=0xabcd;
        for(unsigned i=0;i<w*h;++i)pixels[i]=std::uint16_t(i+1);
        const int x=int(next()%(w*24+17))-int(w*12+8),y=int(next()%(h*24+17))-int(h*12+8);
        reference_crop(&source,x,y,0);
        REQUIRE(destination[0]==0xabcd&&destination[w*h+1]==0xabcd&&source.dirty==1);
        const int sx=source.x/4096+int(next()%1025)-512,sy=source.y/4096+int(next()%1025)-512;
        CanvasCrop crop{std::uint16_t(w*8),std::uint16_t(h*8),std::uint16_t(w*8),std::uint16_t(h*8>192?192:h*8),floor8(x)*8,floor8(y)*8,sx,sy};
        CanvasWindows windows;canvas_native_interval=1;const auto error=canvas_windows(crop,windows);canvas_native_interval=0;
        REQUIRE(error==Error::none);
        // Scan destination seams, sub-tile remainders and random viewport
        // pixels against the ACTUAL map produced by the original ARM helper.
        for(unsigned probe=0;probe<48;++probe){
            const int px=probe<4?int(probe&1)*(crop.viewport_width-1):int(next()%crop.viewport_width);
            const int py=probe<4?int(probe>>1)*(crop.viewport_height-1):int(next()%crop.viewport_height);
            const int qx=modulo(px+sx,crop.width),qy=modulo(py+sy,crop.height);
            const auto expected=source.destination[(qy/8)*w+qx/8];std::uint16_t actual=0;unsigned covered=0;
            for(unsigned n=0;n<windows.count;++n){const auto& win=windows.window[n];
                if(px<win.left||px>=win.right||py<win.top||py>=win.bottom)continue;
                const int ux=px-win.x,uy=py-win.y;
                REQUIRE(ux>=0&&ux<crop.width&&uy>=0&&uy<crop.height);
                REQUIRE(ux%8==qx%8&&uy%8==qy%8);
                actual=pixels[(uy/8)*w+ux/8];++covered;
            }
            REQUIRE(covered<=1&&actual==expected);
        }
    }
    return 0;
}
