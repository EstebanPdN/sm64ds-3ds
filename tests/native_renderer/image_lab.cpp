#include "scene_renderer.hpp"
#include "native_rigid_shader.h"
#include "native_quad_fixture.h"
#include "native_image_fixture.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
void expect(bool condition,const char* label){if(condition)return;
    std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);okay=false;}
bool check(Error error,const char* label){if(error==Error::none)return true;
    std::fprintf(log_file,"FAIL %s: %s\n",label,describe(error));std::fflush(log_file);okay=false;return false;}
Matrix rectangle(float x,float y,float w,float h){
    C3D_Mtx p,o,m;Mtx_OrthoTilt(&p,0,400,240,0,0,1,false);Mtx_Identity(&o);
    Mtx_Translate(&o,x,y,-.5f,true);Mtx_Scale(&o,w,h,1);Mtx_Multiply(&m,&p,&o);Matrix result{};
    for(unsigned i=0;i<4;++i){result.row[i][0]=m.r[i].x;result.row[i][1]=m.r[i].y;result.row[i][2]=m.r[i].z;result.row[i][3]=m.r[i].w;}return result;
}
const std::array<Vertex,4> white{{{0,0,0,0,0,255,255,255,255},{1,0,0,1,0,255,255,255,255},
                               {1,1,0,1,1,255,255,255,255},{0,1,0,0,1,255,255,255,255}}};
void save(const char* path,const std::uint8_t* data,std::size_t size){
    FILE* f=std::fopen(path,"wb");expect(f,"fixture file open");if(!f)return;
    const bool written=std::fwrite(data,1,size,f)==size;expect(std::fclose(f)==0&&written,"fixture file write");
}
void capture(SceneRenderer& renderer,std::uint8_t* pixels,const char* name){
    if(!check(renderer.capture_rgb8(pixels,288000),"native image completed target"))return;
    std::uint32_t sum=2166136261u;for(unsigned i=0;i<288000;++i)sum=(sum^pixels[i])*16777619u;
    std::fprintf(log_file,"readback=%s fnv1a=%08lx\n",name,static_cast<unsigned long>(sum));std::fflush(log_file);
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);save(path,pixels,288000);
}
}
int main(){
    // Exact byte assertions below exercise the ordinary image-stream layout.
    setenv("SM64DS_NATIVE_BATCHES","0",1);
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);log_file=std::fopen("sdmc:/sm64ds/log.txt","w");
    if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto model_query=APT_CheckNew3DS(&new_model);expect(R_SUCCEEDED(model_query),"model query");
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(model_query),unsigned(new_model));
    ImageBank bank;check(ImageBank::open(native_image_fixture,sizeof(native_image_fixture),bank),"image bank open");
    if(okay){
        expect(C3D_Init(256*1024),"ownership C3D init");static Citro3DBackend backend;
        check(backend.init(scene_shader,sizeof(scene_shader),768),"small budget backend");
        static NativeImages images;std::array<bool,ImageBank::max_images> required{};required[0]=true;
        check(backend.begin(false),"ownership upload phase");check(images.upload(backend,bank,required),"first selective image");
        const auto first=images.image(0);expect(first&&!images.image(1)&&backend.stats().live_linear_bytes==512,"only one image resident");
        required[1]=required[2]=true;
        expect(images.upload(backend,bank,required)==Error::budget,"mid-extension budget failure");
        expect(images.image(0)==first&&!images.image(1)&&!images.image(2)&&backend.stats().live_linear_bytes==512,
               "extension rolls back new image while retaining original handle and bytes");
        required[2]=false;check(images.upload(backend,bank,required),"retry smaller extension");
        expect(images.image(0)==first&&images.image(1)&&backend.stats().live_linear_bytes==768,"extension preserves handles");
        const auto created=backend.stats().resource_creates;check(images.upload(backend,bank,required),"already resident upload");
        expect(backend.stats().resource_creates==created,"resident upload creates nothing");
        required[127]=true;expect(images.upload(backend,bank,required)==Error::invalid_data,"out of range request");required[127]=false;
        check(backend.end(),"ownership queue");expect(images.upload(backend,bank,required)==Error::invalid_phase,"queued upload forbidden");
        images.reset();expect(!backend.texture_alive(first),"deferred image retirement invalidates handle");
        check(backend.begin(false),"ownership retirement fence");expect(backend.stats().live_linear_bytes==256,"retirement releases all image bytes");
        check(backend.end(),"ownership retirement end");check(backend.shutdown(),"ownership shutdown");C3D_Fini();
    }
    save("sdmc:/sm64ds/00001.nim",native_image_fixture,sizeof(native_image_fixture));
    save("sdmc:/sm64ds/00001.nmd",native_model_fixture,sizeof(native_model_fixture));
    ImageAsset assets[]={{8,8,ImageBank::Alpha::independent},{8,8,ImageBank::Alpha::independent},{8,8,ImageBank::Alpha::palette_zero_mask}};
    ImageBankBinding image_binding{1,{},assets,3};std::memcpy(image_binding.source_sha256.data(),bank.source_sha256(),32);
    ImageCatalog image_catalog;check(ImageCatalog::open(&image_binding,1,image_catalog),"image catalog");
    ImageBinding model_images[]={{0,0,0,8,8},{1,1,1,8,8,ImageUse::sprite}};
    MaterialBinding material{0,8,8,ColourSource::diffuse,false};PartBinding part{0,0,false};
    ModelBindings binding{1,{},1,1,model_images,2,&material,1,&part,1};std::memcpy(binding.source_sha256.data(),native_model_fixture+56,32);
    ModelCatalog models;check(ModelCatalog::open(&binding,1,models),"model catalog with same numeric ID");
    auto* pixels=static_cast<std::uint8_t*>(linearMemAlign(288000,128));expect(pixels,"capture allocation");
    static std::array<Occurrence,8> occurrences;static std::array<PreparedPart,1> parts;
    static std::array<Vertex,32> vertices;static std::array<QuadState,8> states;
    SceneFrame frame(occurrences.data(),8,parts.data(),1,nullptr,0,nullptr,0,{vertices.data(),32,states.data(),8});
    static SceneRenderer renderer;unsigned completed=0;
    if(okay){
        check(renderer.init(models,"sdmc:/sm64ds",scene_shader,sizeof(scene_shader),2*1024*1024,true,true,image_catalog),"image/model scene owner");
        for(unsigned index=0;okay&&index<90&&aptMainLoop();++index){
            hidScanInput();check(frame.begin(1,index),"scene capture");
            QuadState state;state.bank=QuadState::Bank::images;state.image=0;state.object_to_clip=rectangle(20,20,100,80);
            state.material.cull=Cull::none;state.material.depth_test=state.material.depth_write=false;
            check(frame.append_quads(1,state,white.data(),4),"red image-only quad");
            if(index){
                auto pose=rectangle(140,20,100,80);LiveMaterial live;live.texture=live.palette=0;live.state=state.material;
                live.diffuse_available=true;live.diffuse=0x0000ffffu;
                check(frame.append(binding,{&pose,nullptr,1,&live,1,nullptr,0}),"same-ID independent model namespace");
                state.image=1;state.object_to_clip=rectangle(260,20,100,80);check(frame.append_quads(1,state,white.data(),4),"green image extension");
                state.image=2;state.object_to_clip=rectangle(20,130,160,80);check(frame.append_quads(1,state,white.data(),4),"mask image extension");
                state.material.sample_alpha=false;state.object_to_clip=rectangle(200,130,160,80);
                check(frame.append_quads(1,state,white.data(),4),"mask ignored without another texture");
            }
            check(frame.seal(true,0),"scene seal");SceneView view;check(frame.acquire(view),"scene acquire");check(renderer.draw(view),"native image scene draw");
            const auto& stats=renderer.backend_metrics();
            if(!index)expect(stats.texture_upload_bytes==256&&renderer.metrics().loads==1&&stats.live_linear_bytes==1024,"cold image has no model geometry plus compositor quad");
            else expect(stats.draws==6&&stats.triangles==12,"image/model source ordering plus LCD composition");
            if(index==1)expect(stats.texture_upload_bytes==1024&&renderer.metrics().loads==3,"extension uploads only missing images plus model bank");
            expect(renderer.metrics().image_source_reads==1,"one source-bank read despite selective extensions");
            if(index==1)expect(renderer.metrics().image_source_hits==1,"extension uses owned validated bank");
            if(index>1){expect(stats.texture_upload_bytes==0&&stats.resource_creates==0&&renderer.metrics().loads==3,"warm scene has no image reads or creates");
                expect(stats.mesh_upload_bytes==16*sizeof(Vertex),"warm scene uploads only dynamic quad vertices");}
            if(index==0||index==1||index==89){char name[48];std::snprintf(name,sizeof(name),"native-images-%u",index);capture(renderer,pixels,name);}
            if(index==0)expect(std::remove("sdmc:/sm64ds/00001.nim")==0,"remove synthetic file after first source read");
            check(frame.release(),"scene release");++completed;
        }
        expect(completed==90,"all image residency frames completed");
        check(renderer.shutdown(),"scene owner shutdown");expect(renderer.backend_metrics().live_linear_bytes==0,"scene owner releases all bytes");
        expect(renderer.metrics().image_source_bytes==0,"shutdown releases CPU bank cache");
        save("sdmc:/sm64ds/00001.nim",native_image_fixture,sizeof(native_image_fixture));
        // An image-only engine owner does not require an invented model.
        check(renderer.init({},"sdmc:/sm64ds",scene_shader,sizeof(scene_shader),2*1024*1024,false,true,image_catalog),"image-only catalog init");
        check(frame.begin(2,1),"image-only capture");QuadState state;state.bank=QuadState::Bank::images;state.image=1;
        state.object_to_clip=rectangle(20,20,100,80);state.material.cull=Cull::none;
        check(frame.append_quads(1,state,white.data(),4),"image-only quad");check(frame.seal(true,0),"image-only seal");
        SceneView view;check(frame.acquire(view),"image-only acquire");check(renderer.draw(view),"image-only owner draw");
        capture(renderer,pixels,"native-images-only");check(frame.release(),"image-only release");
        check(renderer.shutdown(),"image-only shutdown");expect(renderer.backend_metrics().live_linear_bytes==0,"image-only owner releases bytes");
    }
    if(pixels)linearFree(pixels);
    std::fprintf(log_file,"completed_frames=%u\n%s\n",completed,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
