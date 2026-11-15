// Prepared original palette variants; no source graphics decoding on3DS.
#include "scene_renderer.hpp"
#include "native_rigid_shader.h"
#include <cstdio>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool value,const char* what){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",what);std::fflush(log_file);}
bool check(Error value,const char* what){if(value==Error::none)return true;okay=false;std::fprintf(log_file,"FAIL %s: %s\n",what,describe(value));std::fflush(log_file);return false;}
void capture(SceneRenderer& renderer,Screen screen,void* pixels,unsigned part){
    const auto bytes=screen_width(screen)*240*3;if(!check(renderer.capture_rgb8(screen,pixels,bytes),"palette completed readback"))return;
    std::uint32_t sum=2166136261u;for(unsigned i=0;i<bytes;++i)sum=(sum^static_cast<const std::uint8_t*>(pixels)[i])*16777619u;
    char name[64];std::snprintf(name,sizeof(name),"palette-%u-%s",part,screen==Screen::top?"top":"bottom");
    std::fprintf(log_file,"readback=%s fnv1a=%08lx width=%u\n",name,static_cast<unsigned long>(sum),screen_width(screen));std::fflush(log_file);
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);FILE* f=std::fopen(path,"wb");expect(f,"palette export open");if(!f)return;
    const bool written=std::fwrite(pixels,1,bytes,f)==bytes;expect(std::fclose(f)==0&&written,"palette export write");
}
}
int main(){
    gfxInitDefault();gfxSet3D(false);consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));
    std::fputs("scope=prepared-original-palette-variants originalSceneAccepted=0 physicalCapture=0 authoredBlackBackdrop=1\n",log_file);
    static SceneRenderer renderer;check(renderer.init({},"sdmc:/sm64ds/native",scene_shader,sizeof(scene_shader),8*1024*1024,true,true,{},prepared_canvas_catalog()),"palette renderer");
    ImageLayer layers[2];for(unsigned i=0;i<2;++i){auto& l=layers[i];l.bank_id=5001;l.source=LayerSource::canvas;l.screen=i?Screen::bottom:Screen::top;l.rectangle={i?0.f:40.f,0,320,320};l.material.blend=Blend::alpha;l.order=1;}
    SceneView scene;scene.clear={0xff,63,false};scene.scene=1;scene.image_layer=layers;scene.image_layers=2;
    void* pixels=linearMemAlign(288000,128);expect(pixels,"readback workspace");unsigned frames=0;
    for(unsigned part=0;okay&&part<5;++part){layers[0].part=layers[1].part=std::uint16_t(part);
        for(unsigned repeat=0;okay&&repeat<10&&aptMainLoop();++repeat,++frames){hidScanInput();scene.serial=frames+1;
            if(!check(renderer.draw(scene),"palette frame"))break;
            const auto& s=renderer.backend_metrics();if(frames)expect(!s.resource_creates&&!s.texture_upload_bytes&&!s.mesh_upload_bytes,"warm variant switch uploads nothing");
            expect(renderer.metrics().loads==1,"one resident canvas holds all variants");
            if(repeat==9){std::fprintf(log_file,"palettePart=%u draws=%llu triangles=%llu ownedLinear=%u compositionBytes=%u meshUpload=%llu imageUpload=%llu creates=%llu\n",part,s.draws,s.triangles,unsigned(s.live_linear_bytes),unsigned(renderer.metrics().composition_bytes),s.mesh_upload_bytes,s.texture_upload_bytes,s.resource_creates);
                capture(renderer,Screen::top,pixels,part);capture(renderer,Screen::bottom,pixels,part);
            }
        }
    }
    if(okay){layers[1].part=5;expect(renderer.draw(scene)==Error::invalid_data,"invalid lower variant refuses pair");expect(renderer.capture_rgb8(Screen::top,pixels,288000)==Error::invalid_phase,"invalid palette pair is not captured");}
    expect(frames==50,"all variants");check(renderer.shutdown(),"palette shutdown");expect(!renderer.backend_metrics().owned_gpu_bytes()&&!renderer.output_vram_bytes(),"palette resources released");
    if(pixels)linearFree(pixels);
    std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
