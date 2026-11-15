// Actual PICA A4 sampling of all reviewed glyphs/selectors. The backdrop is
// authored, not an original scene or source text-write/lifetime acceptance.
#include "scene_renderer.hpp"
#include "native_rigid_shader.h"
#include "native_font_fixture.hpp"
#include <cstdio>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool v,const char* what){if(v)return;okay=false;std::fprintf(log_file,"FAIL %s\n",what);std::fflush(log_file);}
bool check(Error e,const char* what){if(e==Error::none)return true;okay=false;std::fprintf(log_file,"FAIL %s: %s\n",what,describe(e));std::fflush(log_file);return false;}
void capture(SceneRenderer& renderer,Screen side,void* pixels,unsigned selector){
    const auto bytes=screen_width(side)*240*3;if(!check(renderer.capture_rgb8(side,pixels,bytes),"completed font target"))return;
    std::uint32_t sum=2166136261u;for(unsigned i=0;i<bytes;++i)sum=(sum^static_cast<const std::uint8_t*>(pixels)[i])*16777619u;
    bool coloured=false;for(unsigned i=0;i<bytes;++i)coloured|=static_cast<const std::uint8_t*>(pixels)[i]!=0;
    const auto tint=font_fixture::colours[selector];
    expect(coloured==bool((tint&255u)&&(tint>>8)),"glyph coverage follows nonblack opaque selector");
    char name[64];std::snprintf(name,sizeof name,"font-%u-%s",selector,side==Screen::top?"top":"bottom");
    std::fprintf(log_file,"readback=%s fnv1a=%08lx width=%u\n",name,static_cast<unsigned long>(sum),screen_width(side));std::fflush(log_file);
    char path[128];std::snprintf(path,sizeof path,"sdmc:/sm64ds/%s.bgr",name);FILE* f=std::fopen(path,"wb");expect(f,"font export");if(!f)return;
    const bool written=std::fwrite(pixels,1,bytes,f)==bytes;expect(std::fclose(f)==0&&written,"complete font export");
}
}
int main(){
    // consoleInit changes the bottom LCD to RGB565; both compositor outputs
    // are RGB8. Logs use SD, so keep gfxInitDefault's matching LCD formats.
    gfxInitDefault();mkdir("sdmc:/sm64ds",0777);
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));
#ifdef SM64DS_FONT_BATCHES
    constexpr bool batches=true;
#else
    constexpr bool batches=false;
#endif
    std::fprintf(log_file,"scope=native-A4-font-atlas glyphs=256 selectors=16 originalSceneAccepted=0 sourceGlyphWritesAccepted=0 physicalCapture=0 authoredBlackBackdrop=1 screenBatches=%u\n",unsigned(batches));
    ImageAsset asset{256,128,ImageBank::Alpha::independent,TextureFormat::alpha4};
    ImageBankBinding binding{font_fixture::bank,font_fixture::source_sha256,&asset,1};
    ImageCatalog images;check(ImageCatalog::open(&binding,1,images),"font binding");
    static SceneRenderer renderer;check(renderer.init({},"sdmc:/sm64ds",scene_shader,sizeof(scene_shader),2*1024*1024,true,true,images),"font renderer");
    ImageLayer layers[2];for(unsigned i=0;i<2;++i){auto& l=layers[i];l.bank_id=font_fixture::bank;l.screen=i?Screen::bottom:Screen::top;
        l.rectangle={i?0.f:40.f,40,320,160};l.material.blend=Blend::alpha;l.order=1;l.material.uv={1,0,1.f/16384,0,1,1.f/8192};}
    static std::array<Vertex,1024> glyphs;
    static std::array<Vertex,2048> arena;
    static std::array<Occurrence,1> occurrences;
    static std::array<PreparedPart,1> parts;
    static std::array<ImageLayer,4> captured_layers;
    static SceneFrame frame(occurrences.data(),1,parts.data(),1,nullptr,0,nullptr,0,
        {arena.data(),arena.size(),nullptr,0},captured_layers.data(),captured_layers.size());
    if(batches){
        for(unsigned c=0;c<256;++c){const float x=float(c%32)*8,y=float(c/32)*16;
            const float xy[4][2]={{x,y},{x+8,y},{x+8,y+16},{x,y+16}};
            for(unsigned v=0;v<4;++v)glyphs[c*4+v]={xy[v][0],xy[v][1],0,xy[v][0]/256,xy[v][1]/128,255,255,255,255};
        }
        for(auto& l:layers){l.rectangle.width=1.25f;l.rectangle.height=1.25f;}
    }
    SceneView scene;scene.scene=1;scene.clear={0xff,63,false};scene.image_layer=layers;scene.image_layers=2;
    void* pixels=linearMemAlign(288000,128);expect(pixels,"readback workspace");unsigned frames=0;
    for(unsigned selector=0;okay&&selector<16;++selector){layers[0].material.tint=layers[1].material.tint=font_fixture::colours[selector];
        for(unsigned repeat=0;okay&&repeat<10&&aptMainLoop();++repeat,++frames){hidScanInput();scene.serial=frames+1;
            SceneView draw=scene;
            if(batches){
                if(!check(frame.begin(1,scene.serial,scene.clear),"begin owned screen batches"))break;
                for(const auto& layer:layers)check(frame.append_image_quads(layer,glyphs.data(),glyphs.size()),"capture atlas glyphs");
                if(!check(frame.seal(true,0),"seal screen batches")||!check(frame.acquire(draw),"acquire screen batches"))break;
            }
            const auto result=renderer.draw(draw);
            if(batches)check(frame.release(),"release screen batches");
            if(!check(result,"native font frame"))break;
            const auto stats=renderer.backend_metrics();
            if(!batches)expect(stats.live_linear_bytes==16896,"A4 atlas uses16KiB plus compositor");
            if(frames)expect(!stats.texture_upload_bytes&&!stats.resource_creates&&
                stats.mesh_upload_bytes==(batches?arena.size()*sizeof(Vertex):0),"warm atlas reused; only owned glyph geometry updates");
            expect(renderer.metrics().loads==1,"one native atlas for both LCDs/all selectors");
            if(repeat==9){std::fprintf(log_file,"selector=%u draws=%llu triangles=%llu linear=%u composition=%u creates=%llu uploads=%llu\n",selector,stats.draws,stats.triangles,unsigned(stats.live_linear_bytes),unsigned(renderer.metrics().composition_bytes),stats.resource_creates,stats.texture_upload_bytes);
                capture(renderer,Screen::top,pixels,selector);capture(renderer,Screen::bottom,pixels,selector);}
        }
    }
    expect(frames==160,"all glyphs and selectors");
#ifdef SM64DS_FONT_BATCHES
    // Leave an opaque coloured selector visible for native UI inspection.
    // Completed-target exports are diagnostic: an all-black export cannot
    // establish that the glyphs were visible on the actual LCD.
    layers[0].material.tint=layers[1].material.tint=font_fixture::colours[14];
    check(frame.begin(1,1000,scene.clear),"begin visible inspection");
    for(unsigned i=0;i<2;++i){ImageLayer panel;panel.source=LayerSource::solid;panel.screen=i?Screen::bottom:Screen::top;
        panel.rectangle={i?0.f:40.f,24,320,192};panel.material.tint=0x203050ffu;
        check(frame.append_image(panel),"capture native panel without a texture");
    }
    for(const auto& layer:layers)check(frame.append_image_quads(layer,glyphs.data(),glyphs.size()),"capture visible inspection");
    check(frame.seal(true,0),"seal visible inspection");SceneView held;
    check(frame.acquire(held),"acquire visible inspection");
    const auto until=svcGetSystemTick()+15ull*SYSCLOCK_ARM11;
    while(okay&&aptMainLoop()&&svcGetSystemTick()<until){hidScanInput();if(hidKeysDown()&KEY_START)break;check(renderer.draw(held),"visible inspection");}
    check(frame.release(),"release visible inspection");
#endif
    check(renderer.shutdown(),"font shutdown");expect(!renderer.backend_metrics().owned_gpu_bytes()&&!renderer.output_vram_bytes(),"font resources freed");
    if(pixels)linearFree(pixels);
    std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
