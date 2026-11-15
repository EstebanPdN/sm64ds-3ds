// Native PICA map versus the existing sharp CPU-image presentation. Authored
// pixels only; no game resources, graphics packets or source-engine renderer.
#include "scene_renderer.hpp"
#include "native_rigid_shader.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
bool check(Error error,const char* label){if(error==Error::none)return true;
    okay=false;std::fprintf(log_file,"FAIL %s: %s\n",label,describe(error));std::fflush(log_file);return false;}
void expect(bool value,const char* label){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
void write(const char* name,const void* bytes,std::size_t size){char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);
    FILE* f=std::fopen(path,"wb");expect(f,"capture open");if(!f)return;
    expect(std::fwrite(bytes,1,size,f)==size,"capture write");expect(std::fclose(f)==0,"capture close");}
std::uint32_t raster[512*256],foreground[256*192],reference[256*192];
}
int main(){
    gfxInitDefault();gfxSet3D(false);consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    std::fprintf(log_file,"scope=authored-native-map GPU-vs-sharp-CPU-image physicalCapture=0\n");
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<512;++x)
        raster[y*512+x]=((x/17+y/23)%11)?0xff000000u|((x&255)<<16)|(y<<8)|((x*7+y*13)&255):0;
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)
        foreground[y*256+x]=(x<8||y<4||(x>48&&x<73&&y>30&&y<75))?0xff31eb67u:0;
    ImageAsset asset{8,8,ImageBank::Alpha::independent};ImageBankBinding binding{1,{},&asset,1};binding.source_sha256.fill(0x71);
    ImageCatalog catalog;check(ImageCatalog::open(&binding,1,catalog),"authored catalog identity");
    AffineImage image{raster,foreground,1,512,256};image.backdrop=0xff182028;
    SceneView scene;scene.clear={0x000000ff,63,false};scene.scene=1;
    {
        static SceneRenderer small;
        check(small.init({},"sdmc:/sm64ds/native",scene_shader,sizeof(scene_shader),9*256*1024,true,true,catalog),"partial-map budget owner");
        const auto owned=small.backend_metrics().owned_gpu_bytes();
        SceneRenderer::Bridge2D probe;probe.geometry=false;probe.other_affine=&image;small.bridge_2d(probe);
        expect(small.draw(scene)==Error::budget,"insufficient map surface budget refuses publication");
        expect(small.backend_metrics().owned_gpu_bytes()==owned,"failed second surface releases first surface");
        probe.other_affine=nullptr;probe.other=reference;small.bridge_2d(probe);
        check(small.draw(scene),"ordinary CPU image remains publishable after map allocation refusal");
        check(small.shutdown(),"partial-map budget shutdown");
    }
    static SceneRenderer renderer;check(renderer.init({},"sdmc:/sm64ds/native",scene_shader,sizeof(scene_shader),12*1024*1024,true,true,catalog),"native map owner");
    auto* gpu=static_cast<std::uint8_t*>(linearMemAlign(320*240*3,128));
    auto* cpu=static_cast<std::uint8_t*>(linearMemAlign(320*240*3,128));expect(gpu&&cpu,"readback buffers");
    unsigned completed=0;
    for(unsigned k=0;okay&&k<60&&aptMainLoop();++k){
        hidScanInput();const int movement=k?int(k%17)-8:0,turn=k?int(k%11)-5:0;
        image.x=movement*431;image.y=movement*-719;image.xx=image.yy=std::int16_t(256+movement*3);
        image.xy=std::int16_t(turn*7);image.yx=std::int16_t(turn*-7);image.wrap=bool(k&1);
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
            int sx=(image.x+int(x)*image.xx+int(y)*image.xy)>>8,sy=(image.y+int(x)*image.yx+int(y)*image.yy)>>8;
            if(image.wrap){sx&=511;sy&=255;}
            std::uint32_t pixel=0;if(unsigned(sx)<512&&unsigned(sy)<256)pixel=raster[unsigned(sy)*512+unsigned(sx)];
            if(!(pixel>>24))pixel=image.backdrop;
            reference[y*256+x]=(foreground[y*256+x]>>24)?foreground[y*256+x]:pixel;
        }
        SceneRenderer::Bridge2D bridge;bridge.geometry=false;bridge.other_affine=&image;
        renderer.bridge_2d(bridge);scene.serial=k*2+1;
        if(!check(renderer.draw(scene),"GPU affine frame"))break;
        const auto uploads=renderer.backend_metrics().texture_upload_bytes;
        if(!check(renderer.capture_rgb8(Screen::bottom,gpu,320*240*3),"GPU map readback"))break;
        bridge.other_affine=nullptr;bridge.other=reference;renderer.bridge_2d(bridge);scene.serial=k*2+2;
        if(!check(renderer.draw(scene),"sharp CPU oracle frame")||!check(renderer.capture_rgb8(Screen::bottom,cpu,320*240*3),"CPU oracle readback"))break;
        unsigned differences=0,first=~0u;
        for(unsigned i=0;i<320*240;++i)if(std::memcmp(gpu+i*3,cpu+i*3,3)){if(first==~0u)first=i;++differences;}
        std::fprintf(log_file,"mapCase=%u differentPixels=%u first=%u,%u GPUuploadBytes=%llu\n",k,differences,first/240,239-first%240,(unsigned long long)uploads);std::fflush(log_file);
        if(!k||differences){write("affine-map-gpu",gpu,320*240*3);write("affine-map-cpu",cpu,320*240*3);}
        expect(!differences,"GPU map equals complete sharp CPU image");++completed;
    }
    check(renderer.shutdown(),"map shutdown");expect(!renderer.backend_metrics().owned_gpu_bytes(),"all map allocations released");
    if(gpu)linearFree(gpu);
    if(cpu)linearFree(cpu);
    std::fprintf(log_file,"completed_frames=%u\n%s\n",completed,okay&&completed==60?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay&&completed==60?0:1;
}
