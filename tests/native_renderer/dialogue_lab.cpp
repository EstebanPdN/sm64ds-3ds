// Exercise the production retained text path with UVs unrelated to placement.
// Export inputs and completed PICA targets for an independent bitmap oracle.
#include "dialogue_text.hpp"
#include "scene_renderer.hpp"
#include "native_rigid_shader.h"
#include "native_font_fixture.hpp"
#include <cstdio>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file;bool okay=true;
void check(Error e,const char* operation){if(e==Error::none)return;okay=false;std::fprintf(log_file,"FAIL %s: %s\n",operation,describe(e));std::fflush(log_file);}
void input(unsigned test,unsigned row,DialogueCharacter* chars){
    for(unsigned i=0;i<32;++i){const auto code=std::uint8_t((i*73+row*41+test*19)&255);
        chars[i]={code,std::uint8_t(3+(i*5+row+test)%6),test!=3||i%3!=0};}
}
}
int main(){
    gfxInitDefault();mkdir("sdmc:/sm64ds",0777);log_file=std::fopen("sdmc:/sm64ds/log.txt","w");
    if(!log_file){gfxExit();return 1;}
    ImageAsset asset{256,128,ImageBank::Alpha::independent,TextureFormat::alpha4};
    ImageBankBinding binding{6000,font_fixture::source_sha256,&asset,1};ImageCatalog images;
    check(ImageCatalog::open(&binding,1,images),"font catalog");static SceneRenderer renderer;
    check(renderer.init({},"sdmc:/sm64ds",scene_shader,sizeof(scene_shader),2*1024*1024,true,true,images),"renderer");
    static std::array<Vertex,4096> arena;static std::array<ImageLayer,4> layers;
    static std::array<Occurrence,1> occurrences;static std::array<PreparedPart,1> parts;
    static SceneFrame frame(occurrences.data(),1,parts.data(),1,nullptr,0,nullptr,0,
        {arena.data(),arena.size(),nullptr,0},layers.data(),layers.size());
    static DialogueText text;DialogueCharacter chars[32];
    void* pixels=linearMemAlign(288000,128);if(!pixels)okay=false;
    unsigned serial=0;
    for(unsigned test=0;okay&&test<4;++test){
        text.reset();
        for(unsigned row=0;row<6;++row){input(test,row,chars);check(text.line(chars,32,std::uint8_t(test),row),"line");}
        for(unsigned step=0;okay&&step<5;++step){
            if(step){input(test,6,chars);check(text.scroll(chars,32,std::uint8_t(test),6,(step-1)*4),"scroll");}
            check(frame.begin(1,++serial,{0xff,63,false}),"frame");
            for(unsigned side=0;side<2;++side){const auto screen=side?Screen::bottom:Screen::top;const float origin=side?0:40;
                ImageLayer panel;panel.source=LayerSource::solid;panel.screen=screen;panel.rectangle={origin,24,320,192};panel.material.tint=0x203050ff;
                check(frame.append_image(panel),"panel");
                check(text.capture(frame,screen,origin+1.25f*test,40+1.25f*test,
                    {std::uint16_t(origin+(5*test+1)/4),std::uint16_t(40+(5*test+1)/4),
                     std::uint16_t(origin+320),std::uint16_t(160+(5*test+1)/4)},6,256),"text capture");
            }
            check(frame.seal(true,0),"seal");SceneView draw;check(frame.acquire(draw),"acquire");
            for(unsigned repeat=0;okay&&repeat<3&&aptMainLoop();++repeat)check(renderer.draw(draw),"draw");
            for(unsigned side=0;okay&&side<2;++side){const auto screen=side?Screen::bottom:Screen::top;const unsigned bytes=screen_width(screen)*240*3;
                check(renderer.capture_rgb8(screen,pixels,bytes),"completed target");
                std::uint32_t hash=2166136261u;
                for(unsigned i=0;i<bytes;++i)hash=(hash^static_cast<const unsigned char*>(pixels)[i])*16777619u;
                std::fprintf(log_file,"case=%u step=%u side=%u hash=%08lx\n",test,step,side,static_cast<unsigned long>(hash));
                char path[96];std::snprintf(path,sizeof path,"sdmc:/sm64ds/dialogue-%u-%u-%s.bgr",test,step,side?"bottom":"top");
                FILE* f=std::fopen(path,"wb");if(!f){okay=false;break;}
                const bool written=std::fwrite(pixels,1,bytes,f)==bytes;const int closed=std::fclose(f);if(!written||closed)okay=false;
            }
            check(frame.release(),"release");
            std::fprintf(log_file,"case=%u step=%u glyphs=%u\n",test,step,unsigned(text.size()));std::fflush(log_file);
        }
    }
    // Keep a completed text target visible for a real desktop screenshot.
    const auto until=svcGetSystemTick()+120ull*SYSCLOCK_ARM11;
    while(okay&&aptMainLoop()&&svcGetSystemTick()<until){hidScanInput();gspWaitForVBlank();}
    check(renderer.shutdown(),"shutdown");if(pixels)linearFree(pixels);
    std::fprintf(log_file,"%s\n",okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
