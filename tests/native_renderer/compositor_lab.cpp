#include "screen_compositor.hpp"
#include "native_images.hpp"
#include "native_image_fixture.h"
#include "native_rigid_shader.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool value,const char* label){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
bool check(Error error,const char* label){if(error==Error::none)return true;okay=false;
    std::fprintf(log_file,"FAIL %s: %s\n",label,describe(error));std::fflush(log_file);return false;}
void capture(ScreenCompositor& owner,Screen side,std::uint8_t* data,const char* name){
    const auto bytes=screen_width(side)*240*3;if(!check(owner.capture(side,data,bytes),"LCD composition readback"))return;
    std::uint32_t sum=2166136261u;for(unsigned i=0;i<bytes;++i)sum=(sum^data[i])*16777619u;
    std::fprintf(log_file,"readback=%s fnv1a=%08lx width=%u\n",name,static_cast<unsigned long>(sum),screen_width(side));std::fflush(log_file);
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);FILE* output=std::fopen(path,"wb");
    expect(output,"readback file open");if(!output)return;
    const bool written=std::fwrite(data,1,bytes,output)==bytes;expect(std::fclose(output)==0&&written,"readback file write");
}
}
int main(){
    gfxInitDefault();gfxSet3D(false);consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);log_file=std::fopen("sdmc:/sm64ds/log.txt","w");
    if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));
    expect(C3D_Init(256*1024),"C3D init");
    {
        static Citro3DBackend small;static ScreenCompositor refused;
        // libctru initializes its VRAM pools lazily. SpaceFree before the first
        // allocation is0, so establish the allocator before a leak comparison.
        void* probe=vramAlloc(128);expect(probe,"VRAM allocator initialization");if(probe)vramFree(probe);
        const auto before=vramSpaceFree();check(small.init(scene_shader,sizeof(scene_shader),256),"small budget backend");
        expect(refused.init(small)==Error::budget&&refused.metrics().output_vram_bytes==0,"failed mesh upload leaves compositor uninitialized");
        expect(vramSpaceFree()==before,"failed init releases both SDK targets");check(small.shutdown(),"failed-init backend shutdown");
    }
    static Citro3DBackend renderer;static ScreenCompositor compositor;
    check(renderer.init(scene_shader,sizeof(scene_shader),1024*1024,true),"compositor backend");
    Texture intermediate;check(renderer.create_surface(64,64,true,intermediate),"GPU intermediate for RGB8 composition");
    check(compositor.init(renderer),"RGB8 top and bottom targets");
    expect(compositor.metrics().output_vram_bytes==518400,"RGB8 outputs save172800 bytes versus RGBA8");
    ImageBank bank;check(ImageBank::open(native_image_fixture,sizeof(native_image_fixture),bank),"authored source images");
    static NativeImages images;std::array<bool,ImageBank::max_images> required{};required[0]=required[1]=required[2]=true;
    check(renderer.begin(false),"source upload");check(images.upload(renderer,bank,required),"native static image upload");
    const Vertex vertices[]={{0,0,0,0,0,255,0,255,255},{16,0,0,1,0,255,0,255,255},
        {16,16,0,1,1,255,0,255,255},{0,16,0,0,1,255,0,255,255},
        {24,0,0,0,0,0,255,255,255},{40,0,0,1,0,0,255,255,255},
        {40,16,0,1,1,0,255,255,255},{24,16,0,0,1,0,255,255,255}};
    const std::uint16_t indices[]={0,1,2,0,2,3,4,5,6,4,6,7};Mesh batch;
    check(renderer.create_mesh({vertices,8,indices,12},batch),"native batched2D geometry");check(renderer.end(),"source upload end");
    auto* pixels=static_cast<std::uint8_t*>(linearMemAlign(288000,128));expect(pixels,"capture storage");
    std::array<ScreenLayer,3> top;
    top[0].rectangle={20,20,100,80};top[0].material.texture=images.image(0);
    top[1].rectangle={60,60,100,80};top[1].material.texture=images.image(1);top[1].material.blend=Blend::alpha;top[1].material.tint=0xffffff80;
    top[2].rectangle={200,20,128,128};top[2].material.texture=intermediate;top[2].material.blend=Blend::premultiplied;
    top[2].material.uv={1,0,0,0,-1,1};
    std::array<ScreenLayer,4> bottom;
    bottom[0].rectangle={20,20,100,80};bottom[0].material.tint=0x0000ffff;
    bottom[1].rectangle={160,20,128,80};bottom[1].material.texture=images.image(2);
    bottom[2]=bottom[1];bottom[2].rectangle.y=120;bottom[2].material.sample_alpha=false;
    bottom[3].rectangle={20,140,1,1};bottom[3].mesh=batch;
    ScreenCanvas upper{0x182028ff,top.data(),top.size()},lower{0x283038ff,bottom.data(),bottom.size()};
    // Independent public-SDK matrix check, rather than only mirroring the
    // native formula in the host test. Both LCD widths must use their own clip.
    for(auto side:{Screen::top,Screen::bottom}){
        C3D_Mtx p,o,m;Mtx_OrthoTilt(&p,0,screen_width(side),240,0,0,1,false);Mtx_Identity(&o);
        Mtx_Translate(&o,20,20,-.5f,true);Mtx_Scale(&o,100,80,1);Mtx_Multiply(&m,&p,&o);Matrix native;
        check(screen_pose(side,top[0].rectangle,native),"LCD matrix");
        for(unsigned row=0;row<4;++row){const float expected[]={m.r[row].x,m.r[row].y,m.r[row].z,m.r[row].w};
            for(unsigned column=0;column<4;++column)expect(std::fabs(expected[column]-native.row[row][column])<1e-6f,"native clip matches SDK for physical LCD width");}
    }
    unsigned completed=0;
    for(unsigned frame=0;okay&&frame<60&&aptMainLoop();++frame){
        hidScanInput();lower.brightness=frame<30?0:-8;
        check(renderer.begin(false),"compositor frame begin");
        check(renderer.pass(renderer.surface(intermediate),true,0),"transparent GPU intermediate");
        Matrix source{};source.row[0][0]=source.row[1][1]=1.f/32;source.row[0][3]=source.row[1][3]=-1;
        source.row[2][2]=-1;source.row[2][3]=-.5f;source.row[3][3]=1;
        auto source_material=screen_material();source_material.blend=Blend::alpha;source_material.tint=0xffffff80;
        check(renderer.draw(batch,source_material,source),"GPU-generated partial-alpha batched content");
        check(compositor.draw(upper,lower),"paired native LCD composition");
        expect(renderer.stats().draws==(frame<30?8u:9u),"GPU intermediate plus batched source and native tone draw counts");
        expect(renderer.stats().texture_upload_bytes==0&&renderer.stats().mesh_upload_bytes==0&&renderer.stats().resource_creates==0,
               "warm composition has no source image/geometry uploads or creates");
        expect(compositor.shutdown()==Error::invalid_phase,"target shutdown requires closed frame");
        if(frame%30==0){const auto& stats=renderer.stats();
            std::fprintf(log_file,"compositionFrame=%u draws=%llu submitTicks=%llu previousQueueMs=%.6f ownedLinearBytes=%u ownedSurfaceVRAM=%u outputVRAM=%u guestDiagnosticOnly=1\n",
                frame,stats.draws,stats.submit_ticks,double(stats.previous_gpu_queue_ms),unsigned(stats.live_linear_bytes),unsigned(stats.live_vram_bytes),
                unsigned(compositor.metrics().output_vram_bytes));std::fflush(log_file);}
        check(renderer.end(),"compositor frame end");
        if(frame==20||frame==59){
            capture(compositor,Screen::top,pixels,frame==20?"compositor-top-first":"compositor-top-last");
            capture(compositor,Screen::bottom,pixels,frame==20?"compositor-bottom-first":"compositor-bottom-last");
        }
        ++completed;
    }
    expect(completed==60,"all compositor frames completed");
    ScreenLayer hidden;hidden.rectangle={0,0,100,100};hidden.material.texture={42,0,reinterpret_cast<void*>(0x1234)};
    ScreenCanvas white{0xff,&hidden,1,16},black{0xff,&hidden,1,-16};
    check(renderer.begin(false),"solid output begin");check(compositor.draw(white,black),"fully covered outputs need no hidden sampler");
    expect(renderer.stats().draws==0&&compositor.metrics().clear_only_screens==2,"full whole-output tone skips all draws");check(renderer.end(),"solid output end");
    capture(compositor,Screen::top,pixels,"compositor-top-solid");capture(compositor,Screen::bottom,pixels,"compositor-bottom-solid");
    const auto accepted=compositor.metrics().frames;
    check(renderer.begin(false),"refusal begin");ScreenCanvas bad{0xff,&hidden,1,0};
    expect(compositor.draw(upper,bad)==Error::stale_handle&&renderer.stats().draws==0&&compositor.metrics().frames==accepted,
           "bad lower screen cannot publish a valid upper prefix");check(renderer.end(),"refusal end");
    expect(compositor.capture(Screen::top,pixels,288000)==Error::invalid_phase,"refused pair cannot be exported as accepted");
    check(renderer.begin(false),"recovery begin");lower.brightness=0;check(compositor.draw(upper,lower),"recovery after refused pair");check(renderer.end(),"recovery end");
    capture(compositor,Screen::top,pixels,"compositor-top-recovered");capture(compositor,Screen::bottom,pixels,"compositor-bottom-recovered");
    check(compositor.shutdown(),"native compositor shutdown");expect(compositor.metrics().output_vram_bytes==0,"both output targets released");
    images.reset();check(renderer.shutdown(),"native backend shutdown");expect(renderer.stats().owned_gpu_bytes()==0,"all backend storage released");
    if(pixels)linearFree(pixels);
    C3D_Fini();std::fprintf(log_file,"completed_frames=%u\n%s\n",completed,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
