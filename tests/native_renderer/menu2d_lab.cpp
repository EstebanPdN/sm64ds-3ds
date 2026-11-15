// Private original-menu image input, native GPU composition only. This lab
// does not replace a game frame, animate source objects or certify a full menu.
#include "screen_compositor.hpp"
#include "native_images.hpp"
#include "native_rigid_shader.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool value,const char* label){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
bool check(Error error,const char* label){if(error==Error::none)return true;okay=false;
    std::fprintf(log_file,"FAIL %s: %s\n",label,describe(error));std::fflush(log_file);return false;}
void capture(ScreenCompositor& owner,Screen side,std::uint8_t* pixels,const char* name){
    const auto bytes=screen_width(side)*240*3;if(!check(owner.capture(side,pixels,bytes),"native 2D completed target"))return;
    std::uint32_t sum=2166136261u;for(unsigned i=0;i<bytes;++i)sum=(sum^pixels[i])*16777619u;
    std::fprintf(log_file,"readback=%s fnv1a=%08lx width=%u\n",name,static_cast<unsigned long>(sum),screen_width(side));std::fflush(log_file);
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);FILE* file=std::fopen(path,"wb");expect(file,"readback file");
    if(file){const bool written=std::fwrite(pixels,1,bytes,file)==bytes;expect(std::fclose(file)==0&&written,"readback write");}
}
}
int main(){
    gfxInitDefault();gfxSet3D(false);consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);log_file=std::fopen("sdmc:/sm64ds/log.txt","w");
    if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));
    std::fputs("scope=prepared-original-menu-cell-only originalSceneAccepted=0 physicalCapture=0\n",log_file);
    FILE* source=std::fopen("sdmc:/sm64ds/04096.nim","rb");expect(source,"private menu2D bank");long count=-1;void* bytes=nullptr;
    if(source){if(std::fseek(source,0,SEEK_END)==0)count=std::ftell(source);if(count>=128&&count<=1024*1024){bytes=std::malloc(std::size_t(count));expect(bytes,"bank storage");}
        else expect(false,"bank size bound");
        if(bytes)expect(std::fseek(source,0,SEEK_SET)==0&&std::fread(bytes,1,std::size_t(count),source)==std::size_t(count),"complete bank read");
        expect(std::fclose(source)==0,"bank close");}
    ImageBank bank;if(bytes)check(ImageBank::open(bytes,std::size_t(count),bank),"native image bank CRC/layout");
    expect(bank&&bank.images()==5&&bank.image(0).width==256&&bank.image(0).height==16,"reviewed first menu cell roster");
    expect(C3D_Init(256*1024),"SDK init");static Citro3DBackend renderer;static ScreenCompositor compositor;static NativeImages images;
    check(renderer.init(scene_shader,sizeof(scene_shader),256*1024,true),"native 2D backend");check(compositor.init(renderer),"paired native LCDs");
    check(renderer.begin(false),"native asset upload phase");std::array<bool,ImageBank::max_images> required{};required[0]=true;
    if(bank)check(images.upload(renderer,bank,required),"selective original cell upload");
    check(renderer.end(),"upload end");
    expect(renderer.stats().texture_upload_bytes==16384,"only title caption resident, other four cells stay on SD");
    std::free(bytes);bytes=nullptr;
    auto* pixels=static_cast<std::uint8_t*>(linearMemAlign(288000,128));expect(pixels,"readback storage");
    ScreenLayer top,bottom;top.rectangle={40,160,320,20};bottom.rectangle={0,160,320,20};
    top.material.texture=bottom.material.texture=images.image(0);top.material.blend=bottom.material.blend=Blend::alpha;
    // At1.25x scale, some pixel centres land exactly on source texel edges.
    // Bias1/64 of one texel toward the floor(S),floor(T) side of those ties;
    // f24 interpolation otherwise selects the previous texel inconsistently.
    top.material.uv=bottom.material.uv={1,0,1.f/(64*256),0,1,1.f/(64*16)};
    ScreenCanvas upper{0xff,&top,1},lower{0xff,&bottom,1};unsigned frames=0;
    for(;okay&&frames<60&&aptMainLoop();++frames){
        hidScanInput();check(renderer.begin(false),"native2D begin");check(compositor.draw(upper,lower),"original caption on both LCDs");
        expect(renderer.stats().draws==2&&!renderer.stats().resource_creates&&!renderer.stats().mesh_upload_bytes&&!renderer.stats().texture_upload_bytes,
            "one draw per caption; no warm geometry/pixel uploads or creates");
        if(frames%20==0){const auto& s=renderer.stats();std::fprintf(log_file,
            "menu2DFrame=%u draws=%llu meshUpload=%llu textureUpload=%llu linearBytes=%u outputVRAM=%u submitTicks=%llu guestOnly=1\n",
            frames,s.draws,s.mesh_upload_bytes,s.texture_upload_bytes,unsigned(s.live_linear_bytes),unsigned(compositor.metrics().output_vram_bytes),s.submit_ticks);std::fflush(log_file);}
        check(renderer.end(),"native2D end");
        if(frames==0||frames==59){
            capture(compositor,Screen::top,pixels,frames==0?"menu2d-top-first":"menu2d-top-last");
            capture(compositor,Screen::bottom,pixels,frames==0?"menu2d-bottom-first":"menu2d-bottom-last");
        }
    }
    expect(frames==60,"all native2D frames completed");check(compositor.shutdown(),"compositor shutdown");images.reset();check(renderer.shutdown(),"backend shutdown");
    expect(renderer.stats().owned_gpu_bytes()==0&&!compositor.metrics().output_vram_bytes,"native storage released");
    if(pixels)linearFree(pixels);
    C3D_Fini();std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
