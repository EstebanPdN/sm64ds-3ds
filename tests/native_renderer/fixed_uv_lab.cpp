#include "citro3d_backend.hpp"
#include "native_rigid_shader.h"
#include "fixed_uv_cases.h"
#include "gpu_readback.hpp"
#include <array>
#include <cstdio>
#include <sys/stat.h>

using namespace sm64ds::render;
namespace {
Citro3DBackend renderer;
bool okay=true;FILE* log_file=nullptr;
bool check(Error e,const char* label){
    if(e==Error::none)return true;
    std::fprintf(log_file,"FAIL %s: %s\n",label,describe(e));std::fflush(log_file);okay=false;return false;
}
void expect(bool condition,const char* label){
    if(condition)return;
    std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);okay=false;
}
Matrix rectangle(float x,float y,float w,float h){
    C3D_Mtx projection,object,result;Mtx_OrthoTilt(&projection,0,400,240,0,0,1,false);
    Mtx_Identity(&object);Mtx_Translate(&object,x,y,-.5f,true);Mtx_Scale(&object,w,h,1);Mtx_Multiply(&result,&projection,&object);
    Matrix m{};
    for(unsigned i=0;i<4;++i){m.row[i][0]=result.r[i].x;m.row[i][1]=result.r[i].y;m.row[i][2]=result.r[i].z;m.row[i][3]=result.r[i].w;}
    return m;
}
Mesh quad(float u,float v){
    const Vertex vertices[]={{0,0,0,u,v,255,255,255,255},{1,0,0,u,v,255,255,255,255},
        {1,1,0,u,v,255,255,255,255},{0,1,0,u,v,255,255,255,255}};
    const std::uint16_t indices[]={0,1,2,0,2,3};Mesh mesh;
    check(renderer.create_mesh({vertices,4,indices,6},mesh),"case mesh upload");return mesh;
}
}
int main(){
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);
    std::puts("SM64DS native fixed UV\n240 signed/carry/wrap cases\nNo DS graphics interpreter");
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));std::fflush(log_file);
    expect(R_SUCCEEDED(query),"model query");
    if(!C3D_Init(256*1024)){std::fputs("FATAL: C3D init\nFAIL\n",log_file);std::fclose(log_file);gfxExit();return 1;}
    constexpr u32 transfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    auto* target=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    if(target)C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,transfer);
    auto* pixels=static_cast<std::uint8_t*>(linearAlloc(288000));expect(target&&pixels,"target/readback allocation");
    check(renderer.init(scene_shader,sizeof(scene_shader),1024*1024,true),"native backend init");
    constexpr unsigned count=sizeof(fixed_cases)/sizeof(fixed_cases[0]);std::array<Mesh,count> meshes{};
    Mesh footer{},bad{};Texture image;
    if(okay&&check(renderer.begin(false),"upload begin")){
        for(unsigned i=0;i<count;++i){const auto& c=fixed_cases[i];meshes[i]=quad(float(c.s)/(16*c.width),float(c.t)/(16*c.height));}
        footer=quad(9.f/32,9.f/32);bad=quad(.1f,.1f);
        std::array<std::uint8_t,16*16*4> rgba{},tiled{};
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){auto* p=rgba.data()+(y*16+x)*4;
            p[0]=x*17;p[1]=y*17;p[2]=(x^y)*17;p[3]=255;}
        check(cook_rgba8(rgba.data(),rgba.size(),16,16,tiled.data(),tiled.size()),"native gradient cooking");
        check(renderer.create_texture(16,16,tiled.data(),tiled.size(),image),"gradient upload");check(renderer.end(),"upload end");
    }
    const auto resident=renderer.stats().live_linear_bytes;
    unsigned frame=0;
    for(;okay&&frame<120&&aptMainLoop();++frame){
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        if(!check(renderer.begin(),"frame begin"))break;
        check(renderer.pass(target,true,0x182028ff),"native pass");
        const auto wrap=frame%3==0?Wrap::clamp:frame%3==1?Wrap::repeat:Wrap::mirror;
        for(unsigned i=0;i<count;++i){
            Material material;material.texture=image;material.cull=Cull::none;material.depth_test=material.depth_write=false;
            material.uv_mode=UvMode::fixed;material.fixed_uv={fixed_cases[i].coefficient,fixed_cases[i].width,fixed_cases[i].height};
            material.wrap_s=material.wrap_t=wrap;
            check(renderer.draw(meshes[i],material,rectangle(8+(i%16)*24,8+(i/16)*14,20,10)),"fixed UV case draw");
        }
        Material invalid;invalid.texture=image;invalid.uv_mode=UvMode::fixed;invalid.cull=Cull::none;
        const auto draws=renderer.stats().draws;
        expect(renderer.draw(bad,invalid,rectangle(0,0,20,10))==Error::invalid_data&&renderer.stats().draws==draws,
            "noninteger raw UV lattice refused before changing GPU state");
        Material floating;floating.texture=image;floating.cull=Cull::none;floating.depth_test=floating.depth_write=false;
        check(renderer.draw(footer,floating,rectangle(8,224,384,12)),"restore ordinary UV path");
        check(renderer.end(),"frame end");const auto stats=renderer.stats();
        expect(stats.mesh_upload_bytes==0&&stats.texture_upload_bytes==0&&stats.live_linear_bytes==resident,"no resident uploads or growth");
        expect(stats.sampler_binds==(wrap==Wrap::clamp?1u:2u)&&stats.tint_uploads==1&&stats.pipeline_changes==4,
            "UV-only changes preserve sampler, tint and pipeline state");
        if(frame%30==0){std::fprintf(log_file,"frame=%u cases=%u draws=%llu uvUploads=%llu samplerBinds=%llu tintUploads=%llu pipelineChanges=%llu bytes=%u submitTicks=%llu previousQueueMs=%.6f\n",
            frame,count,stats.draws,stats.uv_uploads,stats.sampler_binds,stats.tint_uploads,stats.pipeline_changes,unsigned(stats.live_linear_bytes),stats.submit_ticks,double(stats.previous_gpu_queue_ms));std::fflush(log_file);}
        if(frame==30)expect(native_lab::capture(renderer,target,pixels,log_file,"fixed-clamp"),"clamp capture");
        if(frame==61)expect(native_lab::capture(renderer,target,pixels,log_file,"fixed-repeat"),"repeat capture");
        if(frame==92)expect(native_lab::capture(renderer,target,pixels,log_file,"fixed-mirror"),"mirror capture");
    }
    expect(frame==120,"all fixed-UV frames completed");check(renderer.shutdown(),"native shutdown");
    expect(renderer.stats().live_linear_bytes==0,"all owned GPU storage released");
    if(pixels)linearFree(pixels);
    if(target)C3D_RenderTargetDelete(target);
    C3D_Fini();std::fprintf(log_file,"completed_frames=%u\n%s\n",frame,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
