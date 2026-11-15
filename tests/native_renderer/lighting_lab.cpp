#include "citro3d_backend.hpp"
#include "native_rigid_shader.h"
#include "lighting_cases.h"
#include "gpu_readback.hpp"
#include <array>
#include <cstdio>
#include <sys/stat.h>
#ifdef SM64DS_LIGHTING_MODEL_LAB
#include "native_model.hpp"
#include "scene_frame.hpp"
#include "scene_renderer.hpp"
#include "lighting_model_fixture.h"
#include <cstring>
#endif

using namespace sm64ds::render;
using native_lab_cases::lighting_cases;
namespace {
Citro3DBackend renderer;
bool okay=true;FILE* log_file=nullptr;
bool check(Error e,const char* label){
    if(e==Error::none)return true;
    std::fprintf(log_file,"FAIL %s: %s\n",label,describe(e));std::fflush(log_file);okay=false;return false;
}
void expect(bool condition,const char* label){
    if(condition)return;
    std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);okay=false;
}
Matrix rectangle(float x,float y,float w,float h){
    C3D_Mtx projection,object,result;Mtx_OrthoTilt(&projection,0,400,240,0,0,1,false);
    Mtx_Identity(&object);Mtx_Translate(&object,x,y,-.5f,true);Mtx_Scale(&object,w,h,1);Mtx_Multiply(&result,&projection,&object);
    Matrix m{};
    for(unsigned i=0;i<4;++i){m.row[i][0]=result.r[i].x;m.row[i][1]=result.r[i].y;m.row[i][2]=result.r[i].z;m.row[i][3]=result.r[i].w;}
    return m;
}
Mesh quad(const std::array<std::int16_t,3>* normal){
    const float xy[4][2]={{0,0},{1,0},{1,1},{0,1}};NormalVertex vertices[4]{};Vertex ordinary[4]{};
    for(unsigned i=0;i<4;++i){
        ordinary[i]={xy[i][0],xy[i][1],0,0,0,49,115,181,255};
        if(normal)vertices[i]={ordinary[i],(*normal)[0],(*normal)[1],(*normal)[2],0};
    }
    const std::uint16_t indices[]={0,1,2,0,2,3};Mesh mesh;
    const MeshBytes bytes{reinterpret_cast<const std::uint8_t*>(normal?static_cast<void*>(vertices):static_cast<void*>(ordinary)),
        normal?sizeof(vertices):sizeof(ordinary),reinterpret_cast<const std::uint8_t*>(indices),sizeof(indices),bool(normal)};
    check(renderer.create_mesh_bytes(bytes,mesh),"case mesh upload");return mesh;
}
}
int main(){
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);
    std::puts("SM64DS native GPU lighting\n128 signed/material/table cases\nNo DS graphics interpreter");
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto query=APT_CheckNew3DS(&new_model);
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(query),unsigned(new_model));std::fflush(log_file);
    expect(R_SUCCEEDED(query),"model query");
    if(!C3D_Init(1024*1024)){std::fputs("FATAL: C3D init\nFAIL\n",log_file);std::fclose(log_file);gfxExit();return 1;}
    constexpr u32 transfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    auto* target=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    if(target)C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,transfer);
    auto* pixels=static_cast<std::uint8_t*>(linearAlloc(288000));expect(target&&pixels,"target/readback allocation");
    check(renderer.init(scene_shader,sizeof(scene_shader),1024*1024,true),"native backend init");
    constexpr unsigned count=sizeof(lighting_cases)/sizeof(lighting_cases[0]);Mesh footer{};
#ifndef SM64DS_LIGHTING_MODEL_LAB
    std::array<Mesh,count> meshes{};
#endif
#ifdef SM64DS_LIGHTING_MODEL_LAB
    static std::array<NativeModel,count> models;
    static std::array<ModelBindings,count> bindings;
    static std::array<Occurrence,count> occurrences;
    static std::array<PreparedPart,count> parts;
    static std::array<LightSet,128> light_sets;
    static std::array<PreparedLighting,count> light_states;
    static SceneFrame snapshot(occurrences.data(),count,parts.data(),count,light_sets.data(),light_sets.size(),light_states.data(),count);
    const MaterialBinding lit_material{0,1,1,ColourSource::lighting,false};
    const PartBinding lit_part{0,0,false,0xffffu,0};SceneView view;
    std::fputs("pipeline=prepared_model_scene\n",log_file);
#endif
    if(okay&&check(renderer.begin(false),"upload begin")){
        for(unsigned i=0;i<count;++i){
#ifdef SM64DS_LIGHTING_MODEL_LAB
            const auto& f=native_lab_cases::lighting_model_fixtures[i];ModelBank bank;
            bindings[i]={i,f.sha,1,1,nullptr,0,&lit_material,1,&lit_part,1};
            if(check(ModelBank::open(native_lab_cases::lighting_model_bytes+f.offset,f.bytes,bank),"bank open")&&
               check(validate(bank,bindings[i]),"lighting model associations"))check(models[i].upload(renderer,bank),"resident lit model upload");
#else
            meshes[i]=quad(&lighting_cases[i].normal);
#endif
        }
        footer=quad(nullptr);check(renderer.end(),"upload end");
    }
    const auto resident=renderer.stats().live_linear_bytes;unsigned frame=0;
    for(;okay&&frame<120&&aptMainLoop();++frame){
        hidScanInput();if(hidKeysDown()&KEY_START)break;
#ifdef SM64DS_LIGHTING_MODEL_LAB
        check(snapshot.begin(1,frame),"scene begin");
        for(unsigned i=0;i<count;++i){const auto& c=lighting_cases[i];
            auto pose=rectangle(8+(i%16)*24,8+(i/16)*24,20,20);LiveMaterial live;live.state.cull=Cull::none;
            live.state.depth_test=live.state.depth_write=false;live.lighting=c.material;
            NormalBasis basis;std::memcpy(basis.value,c.basis.data(),sizeof(basis));LightSet source=c.lights;
            check(snapshot.append(bindings[i],{&pose,nullptr,1,&live,1,nullptr,0,nullptr,&basis,nullptr,&source}),"capture lit model");
            source.light[0].colour[0]=0;basis.value[0]=0;live.lighting.emission=0; // input may die/change after capture
        }
        check(snapshot.seal(true,0),"scene seal");check(snapshot.acquire(view),"scene acquire");
        expect(view.parts==count&&view.lighting.state_count<=count,"bounded shared lighting palette");
#endif
        if(!check(renderer.begin(),"frame begin"))break;
        check(renderer.pass(target,true,0x182028ff),"native pass");
        Material material;material.cull=Cull::none;material.depth_test=material.depth_write=false;
        for(unsigned i=0;i<count;++i){
#ifdef SM64DS_LIGHTING_MODEL_LAB
            const auto draw_case=[&]{return models[i].draw_prepared(view.part+i,1,view.lighting);};
#else
            const auto& c=lighting_cases[i];LightingInput input{&c.lights,c.basis,c.material};
            const auto mvp=rectangle(8+(i%16)*24,8+(i/16)*24,20,20);
            const auto draw_case=[&]{return renderer.draw(meshes[i],material,mvp,0,0,&input);};
#endif
            check(draw_case(),"native lit case");
            const auto before=renderer.stats();
            check(draw_case(),"identical lit case");
            expect(renderer.stats().lighting_uniform_rows==before.lighting_uniform_rows&&
                renderer.stats().lighting_preparations==before.lighting_preparations,"identical lighting avoids packing and uniforms");
            if(i%16==15){
                check(renderer.draw(footer,material,rectangle(8,216,384,20)),"restore unlit colour24");
                const auto rows=renderer.stats().lighting_uniform_rows;
                check(draw_case(),"restore lit after unlit");
                expect(renderer.stats().lighting_uniform_rows==rows,"unlit switch preserves light uniforms");
            }
        }
        LightingInput invalid{nullptr,{},lighting_cases[1].material};invalid.material.mask=1;
        auto draws=renderer.stats().draws;
#ifdef SM64DS_LIGHTING_MODEL_LAB
        auto broken=view.lighting;broken.lights=nullptr;
        expect(models[1].draw_prepared(view.part+1,1,broken)==Error::invalid_data&&renderer.stats().draws==draws,
            "missing published light set refused before GPU changes");
        check(snapshot.release(),"scene release");
#else
        expect(renderer.draw(meshes[0],material,rectangle(0,0,1,1),0,0,&invalid)==Error::unsupported&&renderer.stats().draws==draws,
            "unknown lights refused before GPU changes");
#endif
        expect(renderer.draw(footer,material,rectangle(0,0,1,1),0,0,&invalid)==Error::invalid_data,
            "lighting requires normal32 mesh");
        check(renderer.draw(footer,material,rectangle(8,216,384,20)),"final unlit footer");
        check(renderer.end(),"frame end");const auto stats=renderer.stats();
        expect(stats.mesh_upload_bytes==0&&stats.texture_upload_bytes==0&&stats.live_linear_bytes==resident,"no resident uploads or growth");
        if(frame%30==0){std::fprintf(log_file,"frame=%u cases=%u draws=%llu lightPreparations=%llu lightRows=%llu lightStateChanges=%llu bytes=%u submitTicks=%llu previousQueueMs=%.6f\n",
            frame,count,stats.draws,stats.lighting_preparations,stats.lighting_uniform_rows,stats.lighting_state_changes,
            unsigned(stats.live_linear_bytes),stats.submit_ticks,double(stats.previous_gpu_queue_ms));std::fflush(log_file);}
        if(frame==30)expect(native_lab::capture(renderer,target,pixels,log_file,"lighting-first"),"first capture");
        if(frame==91)expect(native_lab::capture(renderer,target,pixels,log_file,"lighting-last"),"last capture");
    }
    expect(frame==120,"all lighting frames completed");
#ifdef SM64DS_LIGHTING_MODEL_LAB
    for(auto& model:models)model.reset();
#endif
    check(renderer.shutdown(),"native shutdown");
    expect(renderer.stats().live_linear_bytes==0,"all owned GPU storage released");
    if(pixels)linearFree(pixels);
    if(target)C3D_RenderTargetDelete(target);
    C3D_Fini();
#ifdef SM64DS_LIGHTING_MODEL_LAB
    // Exercise the actual scene owner: a transparent 3D clear cannot be
    // transferred to RGB8 without a native background compositor. It must
    // fail before queue submission, even when no 2D layer flag is present.
    static SceneRenderer scene_owner;ModelCatalog catalog;
    if(check(ModelCatalog::open(bindings.data(),count,catalog),"scene-owner catalog")&&
       check(scene_owner.init(catalog,"sdmc:/sm64ds",scene_shader,sizeof(scene_shader),2*1024*1024),"scene-owner reinit")){
        SceneView empty;empty.clear.rgba=0xffffff00u;
        expect(scene_owner.draw(empty)==Error::unsupported&&!scene_owner.metrics().frames,"transparent clear requires compositor");
        empty.clear.rgba=0x123456ffu;empty.clear.polygon_id=62;
        expect(scene_owner.draw(empty)==Error::unsupported&&!scene_owner.metrics().frames,"nondefault clear polygon refused");
        empty.clear.polygon_id=63;
        check(scene_owner.draw(empty),"opaque empty scene clear");expect(scene_owner.metrics().frames==1,"opaque clear admitted");
        check(scene_owner.shutdown(),"scene-owner shutdown");
        expect(scene_owner.backend_metrics().live_linear_bytes==0,"scene-owner storage released");
        std::fputs("clearAdmission: transparent=refused nondefaultPolygon=refused opaqueEmpty=accepted\n",log_file);
    }
#endif
    std::fprintf(log_file,"completed_frames=%u\n%s\n",frame,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
