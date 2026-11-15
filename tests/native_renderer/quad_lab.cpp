#include "scene_renderer.hpp"
#include "game_sprite_geometry.hpp"
#include "native_rigid_shader.h"
#include "native_quad_fixture.h"
#include "gpu_readback.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
FILE* log_file=nullptr;bool okay=true;
bool check(Error e,const char* label){if(e==Error::none)return true;
    std::fprintf(log_file,"FAIL %s: %s\n",label,describe(e));std::fflush(log_file);okay=false;return false;}
void expect(bool condition,const char* label){if(condition)return;
    std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);okay=false;}
Matrix rectangle(float x,float y,float w,float h){
    C3D_Mtx p,o,m;Mtx_OrthoTilt(&p,0,400,240,0,0,1,false);Mtx_Identity(&o);
    Mtx_Translate(&o,x,y,-.5f,true);Mtx_Scale(&o,w,h,1);Mtx_Multiply(&m,&p,&o);Matrix result{};
    for(unsigned i=0;i<4;++i){result.row[i][0]=m.r[i].x;result.row[i][1]=m.r[i].y;result.row[i][2]=m.r[i].z;result.row[i][3]=m.r[i].w;}return result;
}
std::array<Vertex,4> quad(std::uint8_t r,std::uint8_t g,std::uint8_t b,std::uint8_t a=255){
    return {{{0,0,0,0,0,r,g,b,a},{1,0,0,1,0,r,g,b,a},{1,1,0,1,1,r,g,b,a},{0,1,0,0,1,r,g,b,a}}};
}
bool capture(SceneRenderer& owner,std::uint8_t* bytes,const char* name){
    if(!check(owner.capture_rgb8(bytes,288000),"completed scene capture"))return false;
    std::uint32_t sum=2166136261u;for(unsigned i=0;i<288000;++i)sum=(sum^bytes[i])*16777619u;
    std::fprintf(log_file,"readback=%s fnv1a=%08lx\n",name,static_cast<unsigned long>(sum));std::fflush(log_file);
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);FILE* f=std::fopen(path,"wb");
    if(!f)return false;
    const bool written=std::fwrite(bytes,1,288000,f)==288000;const bool closed=std::fclose(f)==0;return written&&closed;
}
}
int main(){
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);log_file=std::fopen("sdmc:/sm64ds/log.txt","w");
    if(!log_file){gfxExit();return 1;}
    bool new_model=false;const auto model_query=APT_CheckNew3DS(&new_model);
    std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",static_cast<unsigned long>(model_query),unsigned(new_model));
    expect(R_SUCCEEDED(model_query),"model query");
    FILE* bank_file=std::fopen("sdmc:/sm64ds/00001.nmd","wb");
    if(bank_file){const bool written=std::fwrite(native_model_fixture,1,sizeof(native_model_fixture),bank_file)==sizeof(native_model_fixture);
        expect(std::fclose(bank_file)==0&&written,"authored bank write");}else expect(false,"authored bank open");
    ImageBinding images[]={{0,0,0,8,8},{1,1,1,8,8,ImageUse::sprite}};
    MaterialBinding material{0,8,8,ColourSource::diffuse,false};PartBinding part{0,0,false};
    ModelBindings binding{1,{},1,1,images,2,&material,1,&part,1};std::memcpy(binding.source_sha256.data(),native_model_fixture+56,32);
    ModelCatalog catalog;check(ModelCatalog::open(&binding,1,catalog),"authored catalog");
    auto* pixels=static_cast<std::uint8_t*>(linearMemAlign(288000,128));expect(pixels,"readback allocation");
    if(okay){
        static Citro3DBackend stream_test;expect(C3D_Init(256*1024),"stream contract C3D init");
        check(stream_test.init(scene_shader,sizeof(scene_shader),4096),"stream contract backend");
        check(stream_test.begin(false),"stream contract upload begin");Mesh stream;check(stream_test.create_quad_stream(2,stream),"persistent stream allocate");
        auto unchanged=stream;expect(stream_test.create_quad_stream(0,unchanged)==Error::invalid_data&&unchanged==stream,"invalid capacity preserves handle");
        auto v=quad(255,255,255);check(stream_test.update_quad_stream(stream,v.data(),4),"stream vertex update");
        const auto uploaded=stream_test.stats().mesh_upload_bytes;
        v[0].x=std::numeric_limits<float>::infinity();
        expect(stream_test.update_quad_stream(stream,v.data(),4)==Error::invalid_data&&stream_test.stats().mesh_upload_bytes==uploaded,"invalid vertices leave stream untouched");
        expect(stream_test.update_quad_stream(stream,v.data(),12)==Error::invalid_data,"oversize span refused before reading");
        check(stream_test.end(),"stream upload end");
        expect(stream_test.update_quad_stream(stream,v.data(),4)==Error::invalid_phase,"vertices cannot change while queued");
        check(stream_test.begin(false),"stream retirement fence");check(stream_test.retire(stream),"stream retire");
        expect(stream_test.update_quad_stream(stream,v.data(),4)==Error::stale_handle,"retired stream cannot be mutated");
        check(stream_test.end(),"stream retirement end");
        // An ordinary unlit draw uses the short DVLE. Returning from it to a
        // lit affine draw must clear fixed/normal UV modes left by an earlier
        // general-kernel draw, even if the latest two materials are identical.
        auto* control_target=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        expect(control_target,"mode transition target");check(stream_test.begin(false),"mode transition upload");
        auto base=quad(255,255,255);std::array<NormalVertex,4> normal_vertices{};
        for(unsigned i=0;i<4;++i)normal_vertices[i]={base[i],0,0,511,0};
        const std::uint16_t indices[]={0,1,2,0,2,3};Mesh normal_mesh;Texture mask;ModelBank bank;
        check(ModelBank::open(native_model_fixture,sizeof(native_model_fixture),bank),"mode transition bank");
        const auto image=bank.image(1);
        check(stream_test.create_texture(image.width,image.height,image.data,image.bytes,mask),"mode transition shared mask");
        check(stream_test.create_mesh_bytes({reinterpret_cast<const std::uint8_t*>(normal_vertices.data()),sizeof(normal_vertices),
            reinterpret_cast<const std::uint8_t*>(indices),sizeof(indices),true},normal_mesh),"mode transition normal mesh");
        check(stream_test.pass(control_target,true,0x182028ff),"mode transition pass");
        LightingInput emission;emission.material.mask=0;emission.material.emission=32767;
        for(unsigned mode=0;mode<2;++mode){
            Material affine;affine.texture=mask;affine.cull=Cull::none;affine.depth_test=affine.depth_write=false;affine.sample_alpha=false;
            auto poison=affine;poison.uv_mode=mode?UvMode::normal:UvMode::fixed;poison.fixed_uv.coefficient.fill(0);
            check(stream_test.draw(normal_mesh,poison,rectangle(-8,-8,1,1)),"set special UV mode");
            check(stream_test.draw(normal_mesh,affine,rectangle(-8,-8,1,1)),"intervening short affine draw");
            check(stream_test.draw(normal_mesh,affine,rectangle(mode?220:20,20,160,80),0,0,&emission),"restore lit affine mode");
        }
        check(stream_test.end(),"mode transition end");
        expect(native_lab::capture(stream_test,control_target,pixels,log_file,"quads-mode-controls"),"mode transition readback");
        check(stream_test.shutdown(),"stream contract shutdown");
        if(control_target)C3D_RenderTargetDelete(control_target);
        expect(stream_test.stats().live_linear_bytes==0,"stream contract releases storage");C3D_Fini();
    }
    static std::array<Occurrence,8> occurrences;static std::array<PreparedPart,1> parts;
    static std::array<Vertex,64> vertices;static std::array<QuadState,8> states;
    SceneFrame snapshot(occurrences.data(),8,parts.data(),1,nullptr,0,nullptr,0,{vertices.data(),64,states.data(),8});
    unsigned completed=0;
    // The same immutable scenes go through the short DVLE and the general
    // kernel in separate owners. Exact output comparison is performed on host.
    for(unsigned mode=0;okay&&mode<2;++mode){
        static SceneRenderer owner;check(owner.init(catalog,"sdmc:/sm64ds",scene_shader,sizeof(scene_shader),2*1024*1024,true,mode==0),"scene owner init");
        expect(owner.capture_rgb8(pixels,288000)==Error::invalid_phase,"capture needs accepted target");
        {
            auto v=quad(255,255,255);QuadState state;state.object_to_clip=rectangle(20,20,40,40);state.image=1;state.material.cull=Cull::none;
            state.material.depth_test=state.material.depth_write=false;Occurrence first{1,0,4,Occurrence::Kind::quads,0};SceneView only;
            only.scene=1;only.occurrence=&first;only.occurrences=1;only.quad_vertex=v.data();only.quad_vertices=4;only.quad_state=&state;only.quad_states=1;
            check(owner.draw(only),"sprite-only residency before model use");
            expect(owner.backend_metrics().texture_upload_bytes==256&&owner.metrics().loads==1,"only requested sprite image is uploaded");
            expect(owner.backend_metrics().live_linear_bytes==1024,"sprite-only bank has no model geometry allocation plus compositor quad");
        }
        for(unsigned frame=0;okay&&frame<60&&aptMainLoop();++frame){
            hidScanInput();if(hidKeysDown()&KEY_START)break;
            check(snapshot.begin(1,frame),"capture begin");
            auto v=quad(255,0,0);QuadState state;state.object_to_clip=rectangle(20,20,80,80);
            state.material.cull=Cull::none;state.material.depth_test=state.material.depth_write=false;
            check(snapshot.append_quads(untextured_quads,state,v.data(),4),"red quad A");
            for(auto& p:v)p.x+=1;
            check(snapshot.append_quads(untextured_quads,state,v.data(),4),"red quad B coalesced");
            auto pose=rectangle(60,40,80,40);LiveMaterial live;live.texture=live.palette=0;live.state=state.material;
            live.diffuse_available=true;live.diffuse=0x0000ffffu;
            check(snapshot.append(binding,{&pose,nullptr,1,&live,1,nullptr,0}),"interleaved original-format native model");
            v=quad(0,255,0,frame<30?128:64);state.object_to_clip=rectangle(20,60,160,40);state.image=0;state.material.blend=Blend::alpha;
            check(snapshot.append_quads(1,state,v.data(),4),"textured alpha quad after model");
            state.image=QuadState::no_image;state.material.blend=Blend::opaque;state.object_to_clip=rectangle(220,20,160,80);
            v=quad(255,255,255);v[0].g=v[0].b=0;v[1].r=v[1].b=0;v[2].r=v[2].g=0;
            check(snapshot.append_quads(untextured_quads,state,v.data(),4),"per-corner gradient");
            game::SpriteGeometry sprite;sprite.sample_width=sprite.sample_height=24;sprite.colour.fill(0x7fe0);
            game::SpriteGeometryResult geometry;check(game::sprite_geometry(sprite,v.data(),4,geometry),"semantic sprite grid collapse");
            expect(geometry.source_quads==9&&geometry.vertices==4&&geometry.collapsed,"nine cells become one continuous quad");
            state.object_to_clip=rectangle(20,130,160,80);check(snapshot.append_quads(untextured_quads,state,v.data(),4),"collapsed sprite capture");
            v=quad(255,255,255);state.image=1;state.object_to_clip=rectangle(220,130,80,80);
            check(snapshot.append_quads(1,state,v.data(),4),"masked palette-zero quad");
            state.material.sample_alpha=false;state.object_to_clip=rectangle(300,130,80,80);
            check(snapshot.append_quads(1,state,v.data(),4),"same image with opaque palette zero");
            expect(snapshot.captured_occurrences()==7&&snapshot.captured_quads()==7,"only adjacent identical states coalesce");
            check(snapshot.seal(true,0),"seal scene");SceneView view;check(snapshot.acquire(view),"acquire scene");
            v[0].r=3;live.diffuse=0;state.object_to_clip.row[0][0]=999; // producer scratch may be reused
            check(owner.draw(view),"ordered scene draw");const auto stats=owner.backend_metrics();
            expect(stats.draws==8&&stats.triangles==18,"seven ordered scene batches plus one LCD composition draw");
            expect(stats.affine_draws==(mode==0?8u:0u),"chosen shader entry used including LCD composition");
            if(frame){expect(stats.texture_upload_bytes==0&&stats.resource_creates==0&&owner.metrics().loads==2,"promoted model and sprite resources stay allocated");
                expect(stats.shader_binds==3,"one program publication per 3D/top/bottom pass");
                expect(stats.mesh_upload_bytes==28*sizeof(Vertex),"only active quad vertices uploaded; indices and model stay resident");}
            check(snapshot.release(),"release scene");
            if(frame==20||frame==59){char name[64];std::snprintf(name,sizeof(name),"quads-%s-%s",mode==0?"affine":"general",frame==20?"first":"last");
                expect(capture(owner,pixels,name),"readback export");}
            if(frame%30==0){std::fprintf(log_file,"quadFrame=%u mode=%u draws=%llu affine=%llu shaderBinds=%llu meshBytes=%llu liveBytes=%u sourceGridQuads=9 nativeGridQuads=1 submitTicks=%llu previousQueueMs=%.6f diagnosticTimingsOnly=1\n",
                frame,mode,stats.draws,stats.affine_draws,stats.shader_binds,stats.mesh_upload_bytes,unsigned(stats.live_linear_bytes),stats.submit_ticks,double(stats.previous_gpu_queue_ms));std::fflush(log_file);}
            ++completed;
        }
        QuadState unresolved;unresolved.object_to_clip=identity();unresolved.polygon_id=35;
        auto v=quad(255,255,255);Occurrence only{untextured_quads,0,4,Occurrence::Kind::quads,0};SceneView unsupported;
        unsupported.occurrence=&only;unsupported.occurrences=1;unsupported.quad_vertex=v.data();unsupported.quad_vertices=4;
        unsupported.quad_state=&unresolved;unsupported.quad_states=1;
        const auto accepted=owner.metrics().frames;
        expect(owner.draw(unsupported)==Error::unsupported&&owner.metrics().frames==accepted,"original polygon-ID policy cannot be silently ignored");
        expect(owner.capture_rgb8(pixels,288000)==Error::invalid_phase,"refused target cannot be exported as an accepted scene");
        check(owner.shutdown(),"scene owner shutdown");expect(owner.backend_metrics().live_linear_bytes==0,"owned GPU storage released");
    }
    expect(completed==120,"both shader scenarios completed");
    if(pixels)linearFree(pixels);
    std::fprintf(log_file,"completed_frames=%u\n%s\n",completed,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
