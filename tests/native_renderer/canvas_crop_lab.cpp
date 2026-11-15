// Native viewport clipping of prepared geometry; original crop semantics are
// verified independently by ARM and host PPU oracles, never linked here.
#include "scene_renderer.hpp"
#include "canvas_windows.hpp"
#include "canvas_crop_cases.hpp"
#include "native_rigid_shader.h"
#include <cstdio>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool value,const char* label){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
bool check(Error e,const char* label){if(e==Error::none)return true;okay=false;std::fprintf(log_file,"FAIL %s: %s\n",label,describe(e));std::fflush(log_file);return false;}
int floor8(int v){return v>=0?v/8:-((-v+7)/8);}
void capture(SceneRenderer& renderer,Screen side,void* pixels,unsigned id){
    const auto bytes=screen_width(side)*240*3;if(!check(renderer.capture_rgb8(side,pixels,bytes),"completed crop readback"))return;
    std::uint32_t sum=2166136261u;for(unsigned i=0;i<bytes;++i)sum=(sum^static_cast<std::uint8_t*>(pixels)[i])*16777619u;
    char name[64];std::snprintf(name,sizeof(name),"crop-%u-%s",id,side==Screen::top?"top":"bottom");
    std::fprintf(log_file,"readback=%s fnv1a=%08lx width=%u\n",name,static_cast<unsigned long>(sum),screen_width(side));std::fflush(log_file);
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);FILE* f=std::fopen(path,"wb");expect(f,"capture open");if(!f)return;
    const bool written=std::fwrite(pixels,1,bytes,f)==bytes;expect(std::fclose(f)==0&&written,"capture write");
}
}
int main(){
    gfxInitDefault();gfxSet3D(false);consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));
    std::fputs("scope=native-canvas-crop originalSceneAccepted=0 physicalCapture=0\n",log_file);
    static SceneRenderer renderer;check(renderer.init({},"sdmc:/sm64ds/native",scene_shader,sizeof(scene_shader),8*1024*1024,true,true,{},prepared_canvas_catalog()),"native crop owner");
    void* pixels=linearMemAlign(288000,128);expect(pixels,"capture workspace");unsigned frames=0;
    for(unsigned id=0;okay&&id<canvas_crop_fixture::count;++id){const auto c=canvas_crop_fixture::cases[id];
        CanvasCrop crop{256,256,256,192,floor8(c.x)*8,floor8(c.y)*8,-(c.x-floor8(c.x)*8)+c.extra_x,-(c.y-floor8(c.y)*8)+c.extra_y};
        CanvasWindows windows;check(canvas_windows(crop,windows),"native crop plan");
        ImageLayer layers[8];unsigned used=0;
        for(unsigned side=0;side<2;++side)for(unsigned n=0;n<windows.count;++n){const auto& w=windows.window[n];auto& l=layers[used++];
            const auto left=side?0u:40u;l.bank_id=5001;l.source=LayerSource::canvas;l.screen=side?Screen::bottom:Screen::top;l.order=1;
            CanvasPlacement placement;check(canvas_placement(crop,w,l.screen,{float(left),0,320,240},placement),"native physical viewport placement");
            l.rectangle=placement.rectangle;l.clip=placement.clip;
            l.material.blend=Blend::alpha;
        }
        SceneView scene;scene.clear={0xff,63,false};scene.image_layer=layers;scene.image_layers=used;scene.scene=1;
        for(unsigned repeat=0;okay&&repeat<5&&aptMainLoop();++repeat,++frames){hidScanInput();scene.serial=frames+1;
            if(!check(renderer.draw(scene),"native clipped canvas frame"))break;
            const auto& s=renderer.backend_metrics();if(frames)expect(!s.mesh_upload_bytes&&!s.texture_upload_bytes&&!s.resource_creates,"no warm crop resource uploads");
            if(id==0&&repeat==0){const auto saved=layers[used-1].clip;layers[used-1].clip.right=321;
                expect(renderer.draw(scene)==Error::invalid_data,"invalid lower LCD clip refuses pair");
                expect(renderer.capture_rgb8(Screen::top,pixels,288000)==Error::invalid_phase,"invalid clip cannot publish upper LCD");
                layers[used-1].clip=saved;check(renderer.draw(scene),"paired crop recovers after invalid clip");
            }
            if(repeat==4){std::fprintf(log_file,"cropCase=%u translate=%d,%d scroll=%ld,%ld windows=%u draws=%llu ownedLinear=%u compositionBytes=%u\n",id,c.x,c.y,static_cast<long>(crop.scroll_x),static_cast<long>(crop.scroll_y),unsigned(windows.count),s.draws,unsigned(s.live_linear_bytes),unsigned(renderer.metrics().composition_bytes));
                capture(renderer,Screen::top,pixels,id);capture(renderer,Screen::bottom,pixels,id);
            }
        }
    }
    expect(frames==canvas_crop_fixture::count*5,"all crop cases");check(renderer.shutdown(),"crop shutdown");expect(!renderer.backend_metrics().owned_gpu_bytes()&&!renderer.output_vram_bytes(),"all crop resources released");
    if(pixels)linearFree(pixels);
    std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
