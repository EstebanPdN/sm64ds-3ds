#pragma once
#include "native_lower_painter.hpp"
#ifdef __3DS__
#include <3ds.h>
namespace native_diagnostics {
// Caller has drained the game's presentation worker. This uses only the lower
// CPU framebuffer; it never changes graphics ownership or allocates textures.
struct DumpSurface {
    uint8_t* pixels;unsigned width,height;GSPGPU_FramebufferFormat format;
    unsigned stride()const{return format==GSP_RGBA8_OES?4:format==GSP_BGR8_OES?3:2;}
    void pixel(int x,int y,uint32_t c){
        if(x<0||y<0||unsigned(x)>=width||unsigned(y)>=height)return;
        auto* p=pixels+(size_t(x)*height+height-1-unsigned(y))*stride();
        const unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;unsigned v=0;
        switch(format){
        case GSP_RGBA8_OES:p[0]=255;p[1]=b;p[2]=g;p[3]=r;return;
        case GSP_BGR8_OES:p[0]=b;p[1]=g;p[2]=r;return;
        case GSP_RGB565_OES:v=(r>>3)<<11|(g>>2)<<5|(b>>3);break;
        case GSP_RGB5_A1_OES:v=(r>>3)<<11|(g>>3)<<6|(b>>3)<<1|1;break;
        case GSP_RGBA4_OES:v=(r>>4)<<12|(g>>4)<<8|(b>>4)<<4|15;break;
        default:return;
        }p[0]=uint8_t(v);p[1]=uint8_t(v>>8);
    }
};
inline uint64_t dump_paint_tick=0;
inline bool dump_progress_full=true; // subtitle of the current capture
inline void dump_progress(unsigned percent,bool force=false,bool failed=false){
    const auto now=svcGetSystemTick();
    if(!force&&now-dump_paint_tick<SYSCLOCK_ARM11/4)return;
    dump_paint_tick=now;
    u16 w=0,h=0;auto* pixels=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
    if(!pixels||h!=320||w!=240)return;
    DumpSurface surface{pixels,h,w,gfxGetScreenFormat(GFX_BOTTOM)};
    for(int y=0;y<240;++y)for(int x=0;x<320;++x){
        unsigned r=80+unsigned(y)*40/239,g=110+unsigned(y)*110/239,b=255;
        surface.pixel(x,y,0xff000000|r<<16|g<<8|b);
    }
    native_lower::dump(surface,percent,failed,dump_progress_full);
    GSPGPU_FlushDataCache(pixels,size_t(w)*h*surface.stride());
    gfxScreenSwapBuffers(GFX_BOTTOM,false);
}
}
#endif
