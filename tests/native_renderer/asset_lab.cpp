// Prepared-resource preview. No original model parser or DS command decoder
// is linked here. This is deliberately separate from whole-game acceptance.
#include "native_model.hpp"
#include "model_catalog.hpp"
#include "game_model_pose.hpp"
#include "native_rigid_shader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sys/stat.h>

using namespace sm64ds::render;
namespace {
Citro3DBackend renderer;
FILE* log_file=nullptr;
bool okay=true;
bool check(Error e,const char* label) {
    if(e==Error::none)return true;
    if(log_file){std::fprintf(log_file,"FAIL %s: %s\n",label,describe(e));std::fflush(log_file);}
    okay=false;return false;
}
void expect(bool value,const char* label) {
    if(value)return;
    if(log_file){std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
    okay=false;
}
float scalar(const std::uint8_t* p) {
    const std::uint32_t v=std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|
        (std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
    float result;std::memcpy(&result,&v,4);return result;
}
Matrix fitted(const float* lo,const float* hi,float offset) {
    C3D_Mtx projection,object,result;
    Mtx_OrthoTilt(&projection,0,400,240,0,0,1,false);
    // Integral odd magnification (5x3 for the first 32x32 texture) keeps
    // pixel centres away from exact nearest-sampler texel boundaries.
    const float sx=160/(hi[0]-lo[0]),sy=-96/(hi[1]-lo[1]);
    Mtx_Identity(&object);
    Mtx_Translate(&object,220+offset-lo[0]*sx,120-hi[1]*sy,-.5f-lo[2],true);
    Mtx_Scale(&object,sx,sy,1);
    Mtx_Multiply(&result,&projection,&object);
    Matrix matrix{};
    for(unsigned i=0;i<4;++i){
        matrix.row[i][0]=result.r[i].x;matrix.row[i][1]=result.r[i].y;
        matrix.row[i][2]=result.r[i].z;matrix.row[i][3]=result.r[i].w;
    }
    return matrix;
}
bool capture(C3D_RenderTarget* target,std::uint8_t* pixels,const char* name) {
    C3D_SyncDisplayTransfer(static_cast<u32*>(target->frameBuf.colorBuf),GX_BUFFER_DIM(240,400),
        reinterpret_cast<u32*>(pixels),GX_BUFFER_DIM(240,400),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    if(!check(renderer.begin(false),"capture fence"))return false;
    const Result result=GSPGPU_InvalidateDataCache(pixels,400*240*3);
    check(renderer.end(),"capture end");expect(R_SUCCEEDED(result),"capture invalidation");
    if(!okay)return false;
    // Preserve the laboratory's complete-read-before-export protocol. Its
    // historical checksum-free export failure is not a solved hardware issue.
    std::uint32_t checksum=2166136261u;
    for(unsigned i=0;i<400*240*3;++i)checksum=(checksum^pixels[i])*16777619u;
    std::fprintf(log_file,"readback=%s fnv1a=%08lx\n",name,static_cast<unsigned long>(checksum));
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);
    FILE* output=std::fopen(path,"wb");expect(output!=nullptr,"capture open");
    if(!output)return false;
    const bool written=std::fwrite(pixels,1,400*240*3,output)==400*240*3;
    const bool closed=std::fclose(output)==0;expect(written&&closed,"capture export");
    std::fflush(log_file);return okay;
}
}
int main() {
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);
    std::puts("SM64DS native asset preview\nPrepared geometry / native texture\nResource test; actor integration pending");
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");
    if(!log_file){gfxExit();return 1;}
    bool new_model=false;const Result query=APT_CheckNew3DS(&new_model);
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",
        static_cast<unsigned long>(query),unsigned(new_model));std::fflush(log_file);
    expect(R_SUCCEEDED(query),"model query");
    if(!C3D_Init(256*1024)){std::fputs("FAIL C3D init\nFAIL\n",log_file);std::fclose(log_file);gfxExit();return 1;}
    constexpr u32 transfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_RenderTarget* targets[2]{};
    for(unsigned i=0;i<2;++i){
        targets[i]=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        if(targets[i])C3D_RenderTargetSetOutput(targets[i],GFX_TOP,i?GFX_RIGHT:GFX_LEFT,transfer);
    }
    auto* pixels=static_cast<std::uint8_t*>(linearAlloc(400*240*3));
    expect(targets[0]&&targets[1]&&pixels,"targets/readback");
    void* source=nullptr;std::size_t source_bytes=0;ModelBank bank;NativeModel asset;
    FILE* input=std::fopen("sdmc:/sm64ds/model.nmd","rb");expect(input!=nullptr,"prepared model open");
    if(input){
        expect(std::fseek(input,0,SEEK_END)==0,"model seek");const long length=std::ftell(input);
        // This isolated preview accepts at most 8 MiB. Production streaming is
        // a separate budget contract; never allocate an unchecked file size.
        expect(length>=128&&length<=8*1024*1024,"preview file size");
        if(okay){source_bytes=static_cast<std::size_t>(length);source=std::malloc(source_bytes);expect(source!=nullptr,"model CPU allocation");}
        if(okay)expect(std::fseek(input,0,SEEK_SET)==0&&std::fread(source,1,source_bytes,input)==source_bytes,"model read");
        expect(std::fclose(input)==0,"model close");
    }
    if(okay)check(ModelBank::open(source,source_bytes,bank),"native bank validation");
    const auto catalog=prepared_model_catalog();const ModelBindings* bindings=nullptr;
    if(okay&&catalog.size()){
        unsigned matches=0;
        for(std::size_t i=0;i<catalog.size();++i)if(!std::memcmp(catalog.at(i)->source_sha256.data(),bank.source_sha256(),32)){
            bindings=catalog.at(i);++matches;
        }
        expect(matches==1,"unambiguous prepared source identity");
        if(okay)check(validate(bank,*bindings),"native source association validation");
        expect(!bindings||bindings->source_materials<=32,"preview source material capacity");
    }
    float lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};
    if(okay){
        expect(bank.matrices()==1&&bank.materials()<=32,"preview pose/material scope");
        for(unsigned i=0;i<bank.meshes();++i){const auto mesh=bank.mesh(i);
            for(std::size_t at=0;at<mesh.vertex_bytes;at+=24)for(unsigned c=0;c<3;++c){
                const float v=scalar(static_cast<const std::uint8_t*>(mesh.vertices)+at+c*4);
                lo[c]=std::min(lo[c],v);hi[c]=std::max(hi[c],v);
            }
        }
        expect(hi[0]>lo[0]&&hi[1]>lo[1]&&hi[2]==lo[2],"preview planar XY geometry");
    }
    if(okay)check(renderer.init(scene_shader,sizeof(scene_shader),2*1024*1024,true),"backend init");
    if(okay&&check(renderer.begin(false),"upload begin")){
        check(asset.upload(renderer,bank),"native model upload");check(renderer.end(),"upload end");
    }
    // No borrowed bank bytes survive; all subsequent geometry/images are the
    // resident native asset, shared by instances and both eye targets.
    bank={};if(source){std::memset(source,0,source_bytes);std::free(source);source=nullptr;}
    std::array<Material,32> materials{};
    for(unsigned i=0;i<asset.materials();++i)materials[i]=asset.material(i);
    std::array<LiveMaterial,32> live{};
    std::array<std::uint32_t,32> initial_diffuse{},initial_emission{};
    std::array<Texture,ModelBank::max_images> handles{};
    for(unsigned i=0;i<asset.images();++i)handles[i]=asset.image(i);
    auto* instance=bindings?static_cast<PreparedPart*>(std::malloc(asset.parts()*sizeof(PreparedPart))):nullptr;
    if(bindings){
        expect(instance!=nullptr,"instance scratch allocation");
        if(instance)for(unsigned i=0;i<asset.parts();++i)new(instance+i)PreparedPart{};
        for(unsigned i=0;i<asset.materials();++i){
            const auto source_id=bindings->material[i].source_material;
            live[source_id].state=materials[i];
            if(bindings->material[i].colour==ColourSource::diffuse)live[source_id].diffuse=materials[i].tint;
            if(bindings->material[i].colour==ColourSource::emission)live[source_id].emission=materials[i].tint;
            if(materials[i].texture)for(unsigned j=0;j<bindings->images;++j){
                const auto& image=bindings->image[j];
                if(handles[image.image]==materials[i].texture){live[source_id].texture=image.texture;live[source_id].palette=image.palette;break;}
            }
        }
        for(unsigned i=0;i<bindings->source_materials && i<live.size();++i){initial_diffuse[i]=live[i].diffuse;initial_emission[i]=live[i].emission;}
        std::fprintf(log_file,"instance_source=prepared-bindings file=%lu authored-live-state=1 original-texgen-accepted=0\n",
            static_cast<unsigned long>(bindings->file_id));
    }
    const auto resident_bytes=renderer.stats().live_linear_bytes;
    std::fprintf(log_file,"native_resource sourceBytes=%u gpuBytes=%u metadataBytes=%u sourceBlobReleased=1\n",
        unsigned(source_bytes),unsigned(resident_bytes),unsigned(asset.metadata_bytes()));std::fflush(log_file);
    unsigned frames=0;
    for(;okay&&frames<600&&aptMainLoop();++frames){
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        const bool stereo=frames>=300;gfxSet3D(stereo);
        if(!check(renderer.begin(),"frame begin"))break;
        std::uint64_t producer_ticks=0;
        for(unsigned eye=0;eye<(stereo?2u:1u);++eye){
            check(renderer.pass(targets[eye],true,0x182028ff),"preview pass");
            const Matrix m=fitted(lo,hi,stereo?(eye?3.f:-3.f):0.f);
            if(frames==0){const auto before=renderer.stats().draws;
                expect(asset.draw(&m,1)==Error::invalid_data&&renderer.stats().draws==before,
                       "source asset requires explicit live native material bindings");}
            // Explicit resource-preview bindings: ordinary UV identity and
            // default tint. This does not claim original actor texgen/pose.
            if(bindings){
                const auto producer_start=svcGetSystemTick();
                const auto pose=game::unit_affine(),view=game::unit_affine();Matrix ordinary{},facing{};
                check(game::build_poses(*bindings,&pose,1,&view,nullptr,m,&ordinary,&facing,1),"live ordinary/billboard pose");
                for(unsigned i=0;i<bindings->source_materials;++i){
                    live[i].visible=frames!=240;
                    live[i].diffuse=frames==360?0xff0000ff:initial_diffuse[i];
                    live[i].emission=frames==360?0xff0000ff:initial_emission[i];
                }
                const InstanceInputs inputs{&ordinary,&facing,1,live.data(),bindings->source_materials,nullptr,0};
                check(capture_prepared(*bindings,inputs,instance,asset.parts()),"native scene snapshot without GPU handles");
                producer_ticks+=svcGetSystemTick()-producer_start;
                check(asset.draw_prepared(instance,asset.parts()),"native prepared image resolution and draw");
            }else check(asset.draw(&m,1,materials.data(),asset.materials()),"prepared asset draw");
        }
        check(renderer.end(),"frame end");const auto stats=renderer.stats();
        expect(stats.mesh_upload_bytes==0&&stats.texture_upload_bytes==0&&stats.resource_creates==0&&
            stats.live_linear_bytes==resident_bytes,"resident asset has zero uploads and stable bytes");
        if(frames%120==0){std::fprintf(log_file,"frame=%u eyes=%u draws=%llu meshBinds=%llu materialBinds=%llu matrices=%llu bytes=%u waitTicks=%llu submitTicks=%llu previousQueueMs=%.6f\n",
            frames,stereo?2:1,stats.draws,stats.mesh_binds,stats.material_binds,stats.matrix_uploads,unsigned(stats.live_linear_bytes),
            stats.wait_ticks,stats.submit_ticks,double(stats.previous_gpu_queue_ms));std::fflush(log_file);}
        if(bindings&&frames%120==0){std::fprintf(log_file,"producer_frame=%u poseCaptureTicks=%llu scope=pose-snapshot-only excludes-GPU-submit=1\n",frames,
            static_cast<unsigned long long>(producer_ticks));std::fflush(log_file);}
        if(frames==120)capture(targets[0],pixels,"asset-mono");
        if(bindings&&frames==240){expect(stats.draws==0,"hidden material skips every part");capture(targets[0],pixels,"asset-hidden");}
        if(bindings&&frames==360)capture(targets[0],pixels,"asset-tint");
        if(frames==420){capture(targets[0],pixels,"asset-left");capture(targets[1],pixels,"asset-right");}
    }
    expect(frames==600,"all resource-preview frames");
    check(renderer.begin(false),"cache eviction fence");asset.reset();
    expect(renderer.stats().live_linear_bytes==256,"cache eviction immediately reclaims completed asset budget");
    check(renderer.end(),"cache eviction end");
    std::free(instance);
    check(renderer.shutdown(),"backend shutdown");expect(renderer.stats().live_linear_bytes==0,"all owned bytes released");
    if(pixels)linearFree(pixels);
    for(auto* target:targets)if(target)C3D_RenderTargetDelete(target);
    C3D_Fini();std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");
    std::fclose(log_file);gfxExit();return okay?0:1;
}
