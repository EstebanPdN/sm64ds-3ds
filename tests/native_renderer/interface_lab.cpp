// Native UI texture lifecycle and real maintained menu artwork. The backdrop
// is authored; this lab never claims acceptance of the original game scene.
#include "scene_renderer.hpp"
#include "native_rigid_shader.h"
#include "native_pause_art.hpp"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool v,const char* what){if(v)return;okay=false;std::fprintf(log_file,"FAIL %s\n",what);std::fflush(log_file);}
bool check(Error e,const char* what){if(e==Error::none)return true;okay=false;std::fprintf(log_file,"FAIL %s: %s\n",what,describe(e));std::fflush(log_file);return false;}
void write(const char* name,const void* data,std::size_t bytes){char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s",name);
    std::uint32_t checksum=2166136261u;
    for(std::size_t i=0;i<bytes;++i)checksum=(checksum^static_cast<const std::uint8_t*>(data)[i])*16777619u;
    std::fprintf(log_file,"export=%s bytes=%u fnv1a=%08lx\n",name,unsigned(bytes),static_cast<unsigned long>(checksum));std::fflush(log_file);
    FILE* f=std::fopen(path,"wb");expect(f,"export open");if(!f)return;
    const bool written=std::fwrite(data,1,bytes,f)==bytes;expect(std::fclose(f)==0&&written,"export write");}
std::uint32_t image[320*240],fps[256*16];
}
int main(){
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));
    std::fputs("scope=native-interface-artwork authoredBackdrop=1 originalSceneAccepted=0 physicalCapture=0\n",log_file);
    static SceneRenderer renderer;check(renderer.init({},"sdmc:/unused",scene_shader,sizeof(scene_shader),8*1024*1024,true,true,{},prepared_canvas_catalog()),"init");
    void* readback=linearMemAlign(288000,128);expect(readback,"readback memory");
    native_lower::Canvas canvas{image};enhance::native_pause::Menu menu;enhance::Settings settings;
    SceneView scene;scene.scene=1;scene.scene_screen=Screen::bottom;scene.clear={0x173451ff,63,false};
    unsigned frames=0;
    for(unsigned state=0;okay&&state<7;++state){
        std::memset(image,0,sizeof image);std::memset(fps,0,sizeof fps);
        if(state==0)native_lower::version(canvas);
        else if(state<5){
            menu.begin_general(settings,0);
            if(state>=2){menu.update({enhance::native_pause::KeyDown,false,0,0});menu.update({enhance::native_pause::KeyA,false,0,0});}
            if(state==3){menu.update({enhance::native_pause::KeyDown,false,0,0});menu.update({enhance::native_pause::KeyRight,false,0,0});}
            bool live=false;
            if(state==4){FILE* f=std::fopen("sdmc:/sm64ds/live-interface.argb","rb");
                if(f){const bool read=std::fread(image,1,sizeof image,f)==sizeof image;const int extra=std::fgetc(f);
                    expect(std::fclose(f)==0&&read&&extra==EOF,"complete live source UI");live=read;}
                else for(unsigned i=0;i<320*240;++i)image[i]=0xff62513c;
                std::fprintf(log_file,"live_source_interface=%u\n",unsigned(live));
            }
            if(!live)native_lower::menu(canvas,menu);
        }
        if(state==6)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<256;++x)fps[y*256+x]=0xff000000u|(x<<16)|(y<<8)|0x65;
        const InterfaceImage lower{image,320*240,320,240,320},upper{fps,256*16,256,16,256};
        check(renderer.interface_image(0,state+1,lower),"lower publication");
        check(renderer.interface_image(1,state+1,upper),"upper publication");
        char path[64];std::snprintf(path,sizeof path,"interface-%u.argb",state);write(path,image,sizeof image);
        std::snprintf(path,sizeof path,"interface-%u-fps.argb",state);write(path,fps,sizeof fps);
        for(unsigned repeat=0;okay&&repeat<10&&aptMainLoop();++repeat,++frames){
            hidScanInput();scene.serial=frames+1;
            if(repeat==5){renderer.hide_interface(0);renderer.hide_interface(1);}
            const auto uploads=renderer.interface_metrics().uploads,bytes=renderer.interface_metrics().upload_bytes;
            check(renderer.interface_image(0,state+1,lower),"unchanged lower");
            check(renderer.interface_image(1,state+1,upper),"unchanged upper");
            expect(renderer.interface_metrics().uploads==uploads&&renderer.interface_metrics().upload_bytes==bytes,"unchanged publications do not scan/upload");
            if(!check(renderer.draw(scene),"native composed frame"))break;
            const auto s=renderer.backend_metrics();expect(!s.texture_upload_bytes&&!s.resource_creates,"draw does not upload interface");
            if(repeat==9){
                for(auto screen:{Screen::top,Screen::bottom}){check(renderer.capture_rgb8(screen,readback,screen_width(screen)*240*3),"completed capture");
                    std::snprintf(path,sizeof path,"interface-%u-%s.bgr",state,screen==Screen::top?"top":"bottom");write(path,readback,screen_width(screen)*240*3);}
                std::fprintf(log_file,"state=%u liveLinear=%u draws=%llu uploads=%llu uploadBytes=%llu scanPixels=%llu uploadTicks=%llu\n",state,unsigned(s.live_linear_bytes),s.draws,
                    renderer.interface_metrics().uploads,renderer.interface_metrics().upload_bytes,renderer.interface_metrics().scan_pixels,renderer.interface_metrics().upload_ticks);std::fflush(log_file);
            }
        }
    }
    expect(renderer.interface_image(2,1,{image,320*240,320,240,320})==Error::invalid_data,"invalid channel");
    expect(renderer.interface_image(0,0,{image,320*240,320,240,320})==Error::invalid_data,"reserved zero revision");
    expect(renderer.interface_image(0,8,{image,1,320,240,320})==Error::invalid_data,"short publication");
    renderer.hide_interface(0);renderer.hide_interface(1);
    check(renderer.shutdown(),"shutdown");expect(!renderer.backend_metrics().owned_gpu_bytes()&&!renderer.output_vram_bytes(),"all owned resources freed");
    // A resize must respect the common budget, hide its failed successor and
    // permit a small retry without leaked storage or an invalid old binding.
    check(renderer.init({},"sdmc:/unused",scene_shader,sizeof(scene_shader),1048576+4096,false,true,{},prepared_canvas_catalog()),"bounded init");
    std::memset(image,0,sizeof image);native_lower::version(canvas);
    check(renderer.interface_image(0,1,{image,320*240,320,240,320}),"bounded badge");
    for(auto& pixel:image)pixel=0xffffffff;
    expect(renderer.interface_image(0,2,{image,320*240,320,240,320})==Error::budget,"oversized successor refuses");
    std::memset(image,0,sizeof image);native_lower::version(canvas);
    check(renderer.interface_image(0,3,{image,320*240,320,240,320}),"bounded retry");
    check(renderer.shutdown(),"bounded shutdown");expect(!renderer.backend_metrics().owned_gpu_bytes()&&!renderer.output_vram_bytes(),"bounded resources freed");
    if(readback)linearFree(readback);
    expect(frames==70,"all interface cases");std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
