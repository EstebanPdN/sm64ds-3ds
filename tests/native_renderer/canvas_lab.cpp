// Native2D geometry consumer only. No source graphics types/interpreter.
#include "scene_renderer.hpp"
#include "native_rigid_shader.h"
#include <cstdio>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool value,const char* label){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
bool check(Error e,const char* label){if(e==Error::none)return true;okay=false;std::fprintf(log_file,"FAIL %s: %s\n",label,describe(e));std::fflush(log_file);return false;}
void capture(SceneRenderer& renderer,Screen side,void* pixels,const char* name){
    const auto bytes=screen_width(side)*240*3;if(!check(renderer.capture_rgb8(side,pixels,bytes),"completed canvas readback"))return;
    std::uint32_t sum=2166136261u;for(unsigned i=0;i<bytes;++i)sum=(sum^static_cast<std::uint8_t*>(pixels)[i])*16777619u;
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
    std::fputs("scope=prepared-native2D-mesh originalSceneAccepted=0 physicalCapture=0 authoredBlackBackdrop=1\n",log_file);
    ImageAsset probe_asset{8,8,ImageBank::Alpha::independent};ImageBankBinding probe{5001,{},&probe_asset,1};probe.source_sha256.fill(0x55);ImageCatalog images;
    check(ImageCatalog::open(&probe,1,images),"authored independent image identity");
    static SceneRenderer renderer;check(renderer.init({},"sdmc:/sm64ds/native",scene_shader,sizeof(scene_shader),8*1024*1024,true,true,images,prepared_canvas_catalog()),"native canvas owner");
    ImageLayer layers[3];for(unsigned i=0;i<2;++i){auto& l=layers[i];l.bank_id=5001;l.source=LayerSource::canvas;l.screen=i?Screen::bottom:Screen::top;
        l.rectangle={i?0.f:40.f,0,320,320};l.order=1;l.material.blend=Blend::alpha;
    }
    layers[2].bank_id=5001;layers[2].screen=Screen::bottom;layers[2].rectangle={304,224,16,16};layers[2].order=2;
    // Artwork ends above this clip. The later unclipped blue image must still
    // cover all16 rows, proving scissor restoration within the same pass.
    layers[1].clip={0,0,320,225};
    std::fputs("authoredProbe=blue16x16 bottom304,224 aliasImageAndCanvasID=5001\n",log_file);
    SceneView scene;scene.clear={0xff,63,false};scene.image_layer=layers;scene.image_layers=3;scene.scene=1;
    void* pixels=linearMemAlign(288000,128);expect(pixels,"capture workspace");unsigned frames=0;
    for(;okay&&frames<60&&aptMainLoop();++frames){hidScanInput();scene.serial=frames+1;
        if(!check(renderer.draw(scene),"native canvas frame"))break;
        const auto& s=renderer.backend_metrics();
        if(!frames)expect(s.resource_creates==3,"canvas owns one mesh and atlas plus the independent image, without an unused palette mesh");
        if(frames)expect(!s.mesh_upload_bytes&&!s.texture_upload_bytes&&!s.resource_creates,"warm canvas no geometry/image uploads or creates");
        expect(renderer.metrics().loads==2,"one canvas shares geometry/atlas across LCDs; same-ID image remains separate");
        if(frames%10==0||frames==59){std::fprintf(log_file,"canvasFrame=%u draws=%llu triangles=%llu ownedLinear=%u surfaceVRAM=%u outputVRAM=%u meshUpload=%llu imageUpload=%llu creates=%llu submitTicks=%llu previousQueueMs=%.6f compositionBytes=%u\n",
            frames,s.draws,s.triangles,unsigned(s.live_linear_bytes),unsigned(s.live_vram_bytes),unsigned(renderer.output_vram_bytes()),s.mesh_upload_bytes,s.texture_upload_bytes,s.resource_creates,s.submit_ticks,double(s.previous_gpu_queue_ms),unsigned(renderer.metrics().composition_bytes));std::fflush(log_file);}
        if(frames==0||frames==59){capture(renderer,Screen::top,pixels,frames==0?"canvas-top-first":"canvas-top-last");capture(renderer,Screen::bottom,pixels,frames==0?"canvas-bottom-first":"canvas-bottom-last");}
        if(frames==0){layers[1].part=1;expect(renderer.draw(scene)==Error::invalid_data,"invalid lower part refuses paired output");
            expect(renderer.capture_rgb8(Screen::top,pixels,288000)==Error::invalid_phase,"refused canvas pair has no completed capture");layers[1].part=0;
            const auto saved_clip=layers[1].clip;layers[1].clip={};
            layers[1].source=LayerSource::images;check(renderer.draw(scene),"same numeric ID selects independent image");capture(renderer,Screen::bottom,pixels,"canvas-alias-bottom");
            layers[1].bank_id=5002;expect(renderer.draw(scene)==Error::invalid_data,"unknown image never falls back to canvas geometry");layers[1].bank_id=5001;layers[1].source=LayerSource::canvas;layers[1].clip=saved_clip;
        }
    }
    expect(frames==60,"all native canvas frames");check(renderer.shutdown(),"canvas shutdown");expect(!renderer.backend_metrics().owned_gpu_bytes()&&!renderer.output_vram_bytes(),"all owned resources released");
    if(pixels)linearFree(pixels);
    std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
