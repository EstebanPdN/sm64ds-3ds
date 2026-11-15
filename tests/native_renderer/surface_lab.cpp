#include "citro3d_backend.hpp"
#include "native_rigid_shader.h"
#include "gpu_readback.hpp"
#include <array>
#include <cstdio>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool value,const char* label){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
bool check(Error error,const char* label){if(error==Error::none)return true;okay=false;
    std::fprintf(log_file,"FAIL %s: %s\n",label,describe(error));std::fflush(log_file);return false;}
Matrix rectangle(bool tilted,float x,float y,float w,float h){
    C3D_Mtx projection,object,result;
    if(tilted)Mtx_OrthoTilt(&projection,0,400,240,0,0,1,false);
    else Mtx_Ortho(&projection,0,64,0,64,0,1,false);
    Mtx_Identity(&object);Mtx_Translate(&object,x,y,-.5f,true);Mtx_Scale(&object,w,h,1);Mtx_Multiply(&result,&projection,&object);
    Matrix matrix{};for(unsigned row=0;row<4;++row){matrix.row[row][0]=result.r[row].x;matrix.row[row][1]=result.r[row].y;
        matrix.row[row][2]=result.r[row].z;matrix.row[row][3]=result.r[row].w;}return matrix;
}
}
int main(){
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);log_file=std::fopen("sdmc:/sm64ds/log.txt","w");
    if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));
    expect(C3D_Init(256*1024),"C3D init");static Citro3DBackend renderer;
    check(renderer.init(scene_shader,sizeof(scene_shader),1024*1024,true),"surface backend init");
    Texture first,second;check(renderer.create_surface(64,64,true,first),"RGBA8 plus depth surface");
    check(renderer.create_surface(64,64,false,second),"RGBA8 colour-only surface");
    expect(renderer.stats().live_vram_bytes==49152&&renderer.stats().live_linear_bytes==256&&
           renderer.stats().owned_gpu_bytes()==49408,"separate owned VRAM and linear payload accounting");
    auto unchanged=first;expect(renderer.create_surface(1024,1024,true,unchanged)==Error::budget&&unchanged==first,"surface budget failure preserves handle");
    expect(renderer.create_surface(7,64,true,unchanged)==Error::invalid_data&&unchanged==first,"invalid dimensions preserve handle");
    const Vertex vertices[]={{0,0,0,0,0,255,255,255,255},{1,0,0,1,0,255,255,255,255},
                             {1,1,0,1,1,255,255,255,255},{0,1,0,0,1,255,255,255,255}};
    const std::uint16_t indices[]={0,1,2,0,2,3};Mesh mesh;
    check(renderer.begin(false),"mesh upload begin");check(renderer.create_mesh({vertices,4,indices,6},mesh),"shared fullscreen mesh");
    expect(renderer.create_surface(64,64,true,unchanged)==Error::invalid_phase&&unchanged==first,"surface creation requires outside frame");
    expect(renderer.destroy(first)==Error::invalid_phase&&renderer.surface(first),"SDK target deletion forbidden inside frame");
    check(renderer.end(),"mesh upload end");
    auto* output=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);expect(output,"LCD-sized output target");
    auto* pixels=static_cast<std::uint8_t*>(linearMemAlign(288000,128));expect(pixels,"readback storage");
    Material flat;flat.cull=Cull::none;flat.depth_test=flat.depth_write=false;
    unsigned completed=0;
    for(unsigned frame=0;okay&&frame<60&&aptMainLoop();++frame){
        hidScanInput();check(renderer.begin(false),"surface frame begin");
        if(!frame){
            check(renderer.pass(output,true,0x182028ff),"initial destination");auto premature=flat;premature.texture=first;
            expect(renderer.draw(mesh,premature,rectangle(true,0,0,64,64))==Error::invalid_data,"uninitialized surface cannot be sampled");
            expect(renderer.pass(renderer.surface(second),false,0)==Error::invalid_data,"first surface pass requires clear");
            const Citro3DBackend::Viewport invalid{60,60,8,8};
            expect(renderer.pass(renderer.surface(first),true,0,&invalid)==Error::invalid_data,"out of bounds viewport rejected");
        }
        check(renderer.pass(renderer.surface(first),true,0),"transparent native intermediate");
        auto feedback=flat;feedback.texture=first;const auto before=renderer.stats().draws;
        expect(renderer.draw(mesh,feedback,rectangle(false,0,0,64,64))==Error::invalid_phase&&renderer.stats().draws==before,
               "render target cannot sample itself");
        auto solid=flat;solid.tint=frame<30?0xff0000ffu:0x00ffffffu;
        check(renderer.draw(mesh,solid,rectangle(false,0,8,32,48)),"changing left strip");
        solid.tint=0xffff00ff;check(renderer.draw(mesh,solid,rectangle(false,32,32,32,32)),"opaque upper right");
        solid.tint=0x0000ff80;solid.blend=Blend::alpha;
        check(renderer.draw(mesh,solid,rectangle(false,32,0,32,32)),"partial alpha on transparent target");
        const Citro3DBackend::Viewport cropped{8,16,32,16};
        check(renderer.pass(renderer.surface(second),true,0x0000ffff,&cropped),"colour-only target with cropped viewport");
        auto depth=flat;depth.depth_test=true;
        expect(renderer.draw(mesh,depth,rectangle(false,0,0,64,64))==Error::invalid_data,"colour-only pass rejects depth testing");
        solid=flat;solid.tint=0x00ff00ff;check(renderer.draw(mesh,solid,rectangle(false,0,0,64,64)),"cropped green output");
        check(renderer.pass(output,true,0x182028ff),"native composition output");
        auto sampled=flat;sampled.texture=first;sampled.blend=Blend::premultiplied;
        // These intermediates use an unrotated Y-up projection. Flip V when
        // displaying them in a top-left-origin LCD rectangle; imported bank
        // images already have their offline orientation and need no such flip.
        sampled.uv={1,0,0,0,-1,1};
        check(renderer.draw(mesh,sampled,rectangle(true,20,20,128,128)),"premultiplied intermediate composition");
        sampled.texture=second;sampled.blend=Blend::opaque;
        check(renderer.draw(mesh,sampled,rectangle(true,200,20,128,128)),"viewport-padded intermediate composition");
        expect(renderer.stats().draws==6,"exactly six native draws");
        expect(renderer.stats().texture_upload_bytes==0&&renderer.stats().resource_creates==0&&renderer.stats().mesh_upload_bytes==0,
               "warm render-to-texture has no CPU pixel or mesh uploads");
        expect(renderer.stats().live_vram_bytes==49152,"surface storage stays resident");
        check(renderer.end(),"surface frame end");
        if(frame==20||frame==59)expect(native_lab::capture(renderer,output,pixels,log_file,frame==20?"surface-first":"surface-last"),"surface readback");
        ++completed;
    }
    expect(completed==60,"all surface frames completed");
    check(renderer.retire(first),"queued surface retirement");expect(!renderer.surface(first)&&!renderer.texture_alive(first),"retirement invalidates borrowed target and sampler");
    check(renderer.begin(false),"retired target reclaimed outside SDK frame");
    expect(renderer.stats().live_vram_bytes==16384,"retirement frees colour and depth together");check(renderer.end(),"retirement fence end");
    Texture replacement;check(renderer.create_surface(64,64,true,replacement),"surface slot reuse");
    expect(replacement!=first&&!renderer.texture_alive(first),"old generations stay invalid");
    check(renderer.destroy(second),"explicit outside-frame surface destruction");expect(renderer.stats().live_vram_bytes==32768,"explicit destruction accounts VRAM");
    check(renderer.shutdown(),"automatic surface shutdown outside SDK frame");expect(renderer.stats().owned_gpu_bytes()==0,"all owned storage released");
    if(output)C3D_RenderTargetDelete(output);
    if(pixels)linearFree(pixels);
    C3D_Fini();
    std::fprintf(log_file,"completed_frames=%u\n%s\n",completed,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
