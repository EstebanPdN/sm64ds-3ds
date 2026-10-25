#pragma once
#include "loading_screen.hpp"
#include "setup_power.hpp"
#include <3ds.h>
#include <cstring>

namespace loading_screen {
struct CfguLanguageService {
    bool open()const{return R_SUCCEEDED(cfguInit());}
    bool read(unsigned& value)const{u8 raw=255;const Result result=CFGU_GetSystemLanguage(&raw);value=raw;return R_SUCCEEDED(result);}
    void close()const{cfguExit();}
};
inline Language detect_language(){CfguLanguageService service;return query_language(service);}

struct CtrBackend {
    unsigned static_frames=0;
    mutable CancelTouch cancel_touch;
    bool error_mode=false;
    const Artwork* last_artwork=nullptr;
    const uint16_t* last_castle=nullptr;
    const uint16_t* last_logo=nullptr;
    Language last_language=Language::English;
    static Format pixel_format(GSPGPU_FramebufferFormat format) {
        switch(format){case GSP_RGBA8_OES:return Format::Rgba8;case GSP_RGB565_OES:return Format::Rgb565;
            case GSP_RGB5_A1_OES:return Format::Rgb5A1;case GSP_RGBA4_OES:return Format::Rgba4;
            default:return Format::Bgr8;}
    }
    static Surface surface(gfxScreen_t screen,gfx3dSide_t side=GFX_LEFT) {
        u16 physical_width=0,physical_height=0;
        auto* data=gfxGetFramebuffer(screen,side,&physical_width,&physical_height);
        const Format format=pixel_format(gfxGetScreenFormat(screen));
        const unsigned pixel_bytes=format==Format::Bgr8?3u:format==Format::Rgba8?4u:2u;
        return {data,size_t(physical_width)*physical_height*pixel_bytes,physical_height,physical_width,format,true};
    }
    uint64_t milliseconds()const{return osGetTime();}
    bool poll()const {
        if(!aptMainLoop())return false;
        const bool display=setup_power::display_allowed();
        hidScanInput();const u32 down=hidKeysDown();
        touchPosition point{};if(down&KEY_TOUCH)hidTouchRead(&point);
        const bool touched=cancel_touch.update((down&KEY_TOUCH)!=0,(hidKeysHeld()&KEY_TOUCH)!=0,display,point.px,point.py,error_mode?exit_button:cancel_button);
        if(!display)return true;
        return !(down&KEY_B)&&!touched;
    }
    void paint(const State& state,uint64_t now) {
        error_mode=state.failed;
        if(!setup_power::display_allowed())return;
        const uint16_t* castle=state.artwork?state.artwork->castle.pixels:nullptr;
        const uint16_t* logo=state.artwork?state.artwork->logo.pixels:nullptr;
        if(state.artwork!=last_artwork||castle!=last_castle||logo!=last_logo||state.language!=last_language){
            last_artwork=state.artwork;last_castle=castle;last_logo=logo;last_language=state.language;static_frames=0;}
        if(static_frames<2){paint_top(surface(GFX_TOP),state.artwork,state.language);
            if(gfxIs3D())paint_top(surface(GFX_TOP,GFX_RIGHT),state.artwork,state.language);
            ++static_frames;}
        paint_bottom(surface(GFX_BOTTOM),state,now);
        // Progress is already throttled to 250 ms. A lid transition can stop
        // display interrupts between a shell query and this swap; never wait
        // for VBlank on the file-extraction path.
        gfxFlushBuffers();gfxSwapBuffers();
    }
    void clear_both()const {
        if(!setup_power::display_allowed())return;
        // No format, stereo, buffering or graphics-service ownership is changed.
        // Two swaps clear the two default buffers before native renderer startup.
        for(unsigned pass=0;pass<2;++pass){
            const auto top=surface(GFX_TOP),bottom=surface(GFX_BOTTOM);
            if(top.valid())std::memset(top.pixels,0,top.bytes);
            if(bottom.valid())std::memset(bottom.pixels,0,bottom.bytes);
            if(gfxIs3D()){const auto right=surface(GFX_TOP,GFX_RIGHT);if(right.valid())std::memset(right.pixels,0,right.bytes);}
            gfxFlushBuffers();gfxSwapBuffers();
        }
    }
};
using Session=BasicSession<CtrBackend>;
} // namespace loading_screen
