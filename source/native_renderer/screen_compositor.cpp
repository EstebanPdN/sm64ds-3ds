#include "screen_compositor.hpp"

namespace sm64ds::render {
namespace {
constexpr u32 transfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
constexpr Vertex vertices[]={{0,0,0,0,0,255,255,255,255},{1,0,0,1,0,255,255,255,255},
                             {1,1,0,1,1,255,255,255,255},{0,1,0,0,1,255,255,255,255}};
constexpr std::uint16_t indices[]={0,1,2,0,2,3};
}
Error ScreenCompositor::init(Citro3DBackend& renderer) noexcept {
    if(owner_||!renderer.between_frames())return Error::invalid_phase;
    target_[0]=C3D_RenderTargetCreate(240,400,GPU_RB_RGB8,-1);
    if(!target_[0])return Error::out_of_memory;
    target_[1]=C3D_RenderTargetCreate(240,320,GPU_RB_RGB8,-1);
    if(!target_[1]){C3D_RenderTargetDelete(target_[0]);target_={};return Error::out_of_memory;}
    auto error=renderer.begin(false);
    if(error==Error::none){
        error=renderer.create_mesh({vertices,4,indices,6},quad_);
        const auto ended=renderer.end();if(error==Error::none)error=ended;
    }
    if(error!=Error::none){
        C3D_RenderTargetDelete(target_[1]);C3D_RenderTargetDelete(target_[0]);target_={};return error;
    }
    owner_=&renderer;completed_=false;metrics_={};metrics_.output_vram_bytes=(400+320)*240*3;
    return Error::none;
}
Error ScreenCompositor::draw(const ScreenCanvas& top,const ScreenCanvas& bottom) noexcept {
    if(!owner_||(!owner_->can_upload()&&!owner_->can_draw()))return Error::invalid_phase;
    completed_=false;
    // BUILD134: an overlapped second image keeps the first image's links
    // (relinking outside a C3D frame waits for the GPU and stops its queue);
    // it is presented by Citro3DBackend::submit_overlapped instead.
    const bool overlapped=owner_->overlapped();
    if(!overlapped){C3D_RenderTargetSetOutput(nullptr,GFX_TOP,GFX_LEFT,0);C3D_RenderTargetSetOutput(nullptr,GFX_BOTTOM,GFX_LEFT,0);}
    for(auto* target:target_)target->used=false;
    const ScreenCanvas* canvases[]={&top,&bottom};
    // Validate both screens before clearing/drawing either. Publication stays
    // detached on any subsequent resource/command-capacity failure as well.
    for(unsigned screen=0;screen<2;++screen){const auto* canvas=canvases[screen];
        if(only_>=0&&int(screen)!=only_)continue;
        auto error=validate(*canvas);if(error!=Error::none){++metrics_.refused;return error;}
        for(std::size_t i=0;i<canvas->layers;++i)if(!valid(canvas->layer[i].clip,screen_width(screen?Screen::bottom:Screen::top))){
            ++metrics_.refused;return Error::invalid_data;
        }
        if(canvas->brightness==-16||canvas->brightness==16)continue;
        for(std::size_t i=0;i<canvas->layers;++i)if(!owner_->texture_ready(canvas->layer[i].material.texture)){
            ++metrics_.refused;return Error::stale_handle;
        }
    }
    for(unsigned screen=0;screen<2;++screen){const auto& canvas=*canvases[screen];
        if(only_>=0&&int(screen)!=only_)continue;
        const bool solid=canvas.brightness==-16||canvas.brightness==16;
        const auto colour=solid?(canvas.brightness<0?0x000000ffu:0xffffffffu):canvas.clear;
        auto error=owner_->pass(target_[screen],true,colour);
        if(error!=Error::none){++metrics_.refused;return error;}
        if(solid){++metrics_.clear_only_screens;continue;}
        const auto side=screen?Screen::bottom:Screen::top;
        ScreenClip applied;
        for(std::size_t i=0;i<canvas.layers;++i){const auto& layer=canvas.layer[i];Matrix pose;
            if(layer.clip!=applied){applied=layer.clip;
                if(applied.enabled())C3D_SetScissor(GPU_SCISSOR_NORMAL,240-applied.bottom,screen_width(side)-applied.right,240-applied.top,screen_width(side)-applied.left);
                else C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
            }
            error=screen_pose(side,layer.rectangle,pose);
            if(error==Error::none)error=owner_->draw(layer.mesh?layer.mesh:quad_,layer.material,pose,layer.first_index,layer.index_count);
            if(error!=Error::none){++metrics_.refused;return error;}
            ++metrics_.draws;
        }
        if(applied.enabled())C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
        if(canvas.brightness){
            auto material=screen_material();material.blend=Blend::alpha;
            const auto strength=unsigned(canvas.brightness<0?-canvas.brightness:canvas.brightness);
            const auto rgb=canvas.brightness>0?0xffffff00u:0u;
            material.tint=rgb|((strength*255+8)/16);Matrix pose;
            error=screen_pose(side,{0,0,float(screen_width(side)),240},pose);
            if(error==Error::none)error=owner_->draw(quad_,material,pose);
            if(error!=Error::none){++metrics_.refused;return error;}
            ++metrics_.draws;
        }
    }
    if(!overlapped){
        C3D_RenderTargetSetOutput(target_[0],GFX_TOP,GFX_LEFT,transfer);
        C3D_RenderTargetSetOutput(target_[1],GFX_BOTTOM,GFX_LEFT,transfer);
    }else for(auto* target:target_)target->used=false; // no SDK transfer of this image
    completed_=true;++metrics_.frames;return Error::none;
}
Error ScreenCompositor::capture(Screen screen,void* destination,std::size_t bytes) noexcept {
    if(!owner_||!owner_->between_frames()||!completed_)return Error::invalid_phase;
    const auto width=screen_width(screen);
    if(!width||!destination||(reinterpret_cast<std::uintptr_t>(destination)&127)||bytes!=width*240*3)return Error::invalid_data;
    auto* target=target_[screen==Screen::bottom];
    // The destination may contain dirty allocator metadata. Discard those
    // lines before the GPU writes, as well as after its completed transfer.
    if(R_FAILED(GSPGPU_InvalidateDataCache(destination,bytes)))return Error::gpu_failure;
    C3D_SyncDisplayTransfer(static_cast<u32*>(target->frameBuf.colorBuf),GX_BUFFER_DIM(240,width),
        static_cast<u32*>(destination),GX_BUFFER_DIM(240,width),transfer);
    const auto begun=owner_->begin(false);if(begun!=Error::none)return begun;
    const auto result=GSPGPU_InvalidateDataCache(destination,bytes);
    const auto ended=owner_->end();
    return R_FAILED(result)?Error::gpu_failure:ended;
}
Error ScreenCompositor::shutdown() noexcept {
    if(!owner_||!owner_->between_frames())return Error::invalid_phase;
    // SDK target deletion drains queued transfers/sampling before mesh release.
    for(auto* target:target_)if(target)C3D_RenderTargetDelete(target);
    target_={};auto error=owner_->begin(false);if(error!=Error::none)return error;
    if(quad_){error=owner_->destroy(quad_);if(error==Error::none)quad_={};}
    const auto ended=owner_->end();if(error==Error::none)error=ended;
    if(error!=Error::none)return error;
    quad_={};owner_=nullptr;completed_=false;metrics_.output_vram_bytes=0;return Error::none;
}
}
