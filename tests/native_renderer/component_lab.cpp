// Explicitly incomplete native 3D diagnostic. Source pixel IDs are retained in
// the capture/header; ONLY this standalone lab substitutes ordinary policies.
// Never compiled into the game executable or used as whole-scene acceptance.
#include "scene_renderer.hpp"
#include "native_component_fixture.h"
#include "native_rigid_shader.h"
#include <cstdio>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool value,const char* label){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
bool check(Error error,const char* label){if(error==Error::none)return true;okay=false;
    std::fprintf(log_file,"FAIL %s: %s\n",label,describe(error));std::fflush(log_file);return false;}
void capture(SceneRenderer& renderer,Screen screen,void* pixels,const char* name){
    const auto count=screen_width(screen)*240*3;
    if(!check(renderer.capture_rgb8(screen,pixels,count),"component completed GPU target"))return;
    std::uint32_t sum=2166136261u;const auto* bytes=static_cast<const std::uint8_t*>(pixels);
    for(unsigned i=0;i<count;++i)sum=(sum^bytes[i])*16777619u;
    std::fprintf(log_file,"readback=%s fnv1a=%08lx width=%u\n",name,static_cast<unsigned long>(sum),screen_width(screen));std::fflush(log_file);
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);
    FILE* file=std::fopen(path,"wb");expect(file,"capture open");if(!file)return;
    const bool written=std::fwrite(pixels,1,count,file)==count;expect(std::fclose(file)==0&&written,"capture write");
}
}
int main(){
    gfxInitDefault();gfxSet3D(false);consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));
    // The render surface now participates in the shared native budget. A
    // failed init must release its SDK/context ownership for the next owner.
    static SceneRenderer tiny;
    expect(tiny.init(prepared_model_catalog(),"sdmc:/sm64ds/native",scene_shader,sizeof(scene_shader),256)==Error::budget,
        "scene surface budget refusal");
    expect(tiny.backend_metrics().owned_gpu_bytes()==0&&tiny.output_vram_bytes()==0,"failed scene init releases resources");
    static SceneRenderer renderer;auto scene=component_fixture::init();
    const bool captured_layout=scene.image_layers!=0;
    std::fprintf(log_file,"scope=%s originalSceneAccepted=0 physicalCapture=0 images=%u sceneScreen=%u sceneOrder=%u\n",
        captured_layout?"native-component":"native-3d-component",unsigned(scene.image_layers),unsigned(scene.scene_screen),unsigned(scene.scene_order));
    std::fprintf(log_file,"capturedOccurrences=%u parts=%u quads=%u clear=%08lx clearPolygon=%u fogMark=%u\n",
        unsigned(scene.occurrences),unsigned(scene.parts),unsigned(scene.quad_vertices/4),
        static_cast<unsigned long>(scene.clear.rgba),unsigned(scene.clear.polygon_id),unsigned(scene.clear.fog_mark));
    check(renderer.init(prepared_model_catalog(),"sdmc:/sm64ds/native",scene_shader,sizeof(scene_shader),8*1024*1024,true,true,prepared_image_catalog()),"native component owner");
    expect(renderer.draw(scene)==Error::unsupported&&!renderer.metrics().frames,"original incomplete scene stays refused");
    for(std::size_t i=0;i<component_fixture::quad_states.size();++i){auto& q=component_fixture::quad_states[i];
        std::fprintf(log_file,"diagnosticSubstitution: quad=%u sourcePolygon=%u sourceFog=%u -> polygon=255 fog=0\n",unsigned(i),unsigned(q.polygon_id),unsigned(q.fog));
        q.polygon_id=255;q.fog=false;
    }
    // An authored black background permits inspection of the native 3D shapes;
    // original transparency/backdrop/2D routing must still be integrated.
    scene.clear={0x000000ffu,63,false};
    std::fprintf(log_file,"diagnosticSubstitution: clear=opaqueBlack missing2D=omitted preparedImages=%u output=%s fixedCapturedCamera=1\n",
        unsigned(scene.image_layers),captured_layout?"capturedLCD":"top400x240ThenBottom320x240");
    void* pixels=linearMemAlign(288000,128);expect(pixels,"readback allocation");unsigned frames=0;
    for(;okay&&frames<60&&aptMainLoop();++frames){
        const auto screen=captured_layout?scene.scene_screen:(frames<30?Screen::top:Screen::bottom);
        if(frames==30&&!captured_layout){
            // Same 4:3 camera on the narrower LCD: adjust only the clip matrix,
            // exactly as native_projection(width=1) versus width=.8 would.
            for(auto& p:component_fixture::parts)for(auto& v:p.object_to_clip.row[1])v*=1.25f;
            for(auto& q:component_fixture::quad_states)for(auto& v:q.object_to_clip.row[1])v*=1.25f;
        }
        hidScanInput();scene.serial=frames+1;
        if(!check(renderer.draw(scene,screen),"native component frame"))break;
        const auto& stats=renderer.backend_metrics();
        if(frames)expect(!stats.texture_upload_bytes&&!stats.resource_creates,"warm component has no static uploads or creates");
        expect(stats.live_vram_bytes==1048576&&renderer.output_vram_bytes()==518400,"surface and LCD payload accounting");
        if(frames%10==0||frames==29||frames==59){std::fprintf(log_file,
            "componentFrame=%u draws=%llu triangles=%llu matrixUploads=%llu lightRows=%llu quadUploadBytes=%llu textureUploadBytes=%llu creates=%llu ownedLinear=%u ownedSurfaceVRAM=%u outputVRAM=%u waitTicks=%llu submitTicks=%llu previousQueueMs=%.6f compositionBytes=%u emulatorGuestOnly=1\n",
            frames,stats.draws,stats.triangles,stats.matrix_uploads,stats.lighting_uniform_rows,stats.mesh_upload_bytes,stats.texture_upload_bytes,stats.resource_creates,
            unsigned(stats.live_linear_bytes),unsigned(stats.live_vram_bytes),unsigned(renderer.output_vram_bytes()),stats.wait_ticks,stats.submit_ticks,double(stats.previous_gpu_queue_ms),
            unsigned(renderer.metrics().composition_bytes));std::fflush(log_file);}
        if(captured_layout){
            if(frames==0||frames==59){
                capture(renderer,Screen::top,pixels,frames==0?"captured-top-first":"captured-top-last");
                capture(renderer,Screen::bottom,pixels,frames==0?"captured-bottom-first":"captured-bottom-last");
            }
        }else{
            if(frames==0||frames==29)capture(renderer,screen,pixels,frames==0?"component-top-first":"component-top-last");
            if(frames==30||frames==59)capture(renderer,screen,pixels,frames==30?"component-bottom-first":"component-bottom-last");
            if(frames==29)capture(renderer,Screen::bottom,pixels,"component-bottom-idle");
            if(frames==59)capture(renderer,Screen::top,pixels,"component-top-idle");
        }
    }
    expect(frames==60,"all fixed component frames completed");
    check(renderer.shutdown(),"component shutdown");expect(renderer.backend_metrics().owned_gpu_bytes()==0&&renderer.output_vram_bytes()==0,"owned native resources released");
    if(pixels)linearFree(pixels);
    std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
