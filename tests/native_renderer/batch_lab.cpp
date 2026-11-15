#include "citro3d_backend.hpp"
#include "native_rigid_shader.h"
#include "lighting_cases.h"
#include "gpu_readback.hpp"
#include "model_bank.hpp"
#include "native_model.hpp"
#include <utility>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
using namespace sm64ds::render;
namespace {
Citro3DBackend renderer;
NativeBatchPlan plan,stale_plan;
FILE* log_file=nullptr;bool okay=true;
void check(bool value,const char* label){if(value)return;okay=false;std::fprintf(log_file,"FAIL %s\n",label);std::fflush(log_file);}
void expect(Error error,const char* label){if(error==Error::none)return;okay=false;std::fprintf(log_file,"FAIL %s: %s\n",label,describe(error));std::fflush(log_file);}
Matrix rectangle(float x,float y,float width,float height){
    C3D_Mtx projection,object,result;Mtx_OrthoTilt(&projection,0,400,240,0,0,1,false);
    Mtx_Identity(&object);Mtx_Translate(&object,x,y,-.5f,true);Mtx_Scale(&object,width,height,1);Mtx_Multiply(&result,&projection,&object);
    Matrix out;for(unsigned r=0;r<4;++r){out.row[r][0]=result.r[r].x;out.row[r][1]=result.r[r].y;out.row[r][2]=result.r[r].z;out.row[r][3]=result.r[r].w;}return out;
}
struct Command {int source;unsigned span;};
}
int main(){
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);mkdir("sdmc:/sm64ds",0777);
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");if(!log_file){gfxExit();return 1;}
    std::fputs("Resident palette128-case GPU parity; one dynamic upload; no hardware acceptance\n",log_file);
    if(!C3D_Init(1024*1024)){std::fclose(log_file);gfxExit();return 1;}
    auto* target=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    constexpr u32 transfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    if(target)C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,transfer);
    auto* pixels=static_cast<std::uint8_t*>(linearAlloc(288000));check(target&&pixels,"target/readback");
    expect(renderer.init(scene_shader,sizeof(scene_shader),1024*1024,true),"backend init");check(renderer.batches_ready(),"third DVLE");
    constexpr unsigned count=128;
    static std::array<Mesh,count> original{};static std::array<Mesh,16> groups{};
    static std::array<BatchRange,count> ranges{};static std::array<Matrix,count> matrices{};
    static std::array<Material,count> materials{};static std::array<LightingInput,count> inputs{};static std::array<LightSet,count> lights{};
    Mesh skin,quads,footer;Material material;material.cull=Cull::none;material.depth_test=material.depth_write=false;
    expect(renderer.begin(false),"upload begin");
    for(unsigned g=0;g<16&&okay;++g){
        // Eight independent meshes/parts per authored NMD3, source vertices32.
        std::array<std::uint8_t,1888> bytes{};std::memcpy(bytes.data(),"SM64NMD3",8);
        const auto put=[&](unsigned at,std::uint32_t value){for(unsigned b=0;b<4;++b)bytes[at+b]=std::uint8_t(value>>(8*b));};
        const unsigned fields[][2]={{8,3},{12,1888},{16,8},{24,1},{28,8},{32,128},{36,384},{40,384},{44,448},{48,8},
            {384,0xffffffffu},{396,3},{404,0x3f800000u},{420,0x3f800000u},{428,0xffffffffu}};
        for(const auto& f:fields)put(f[0],f[1]);
        for(unsigned p=0;p<8;++p){const unsigned i=g*8+p;const auto& c=native_lab_cases::lighting_cases[g];
            NormalVertex vertices[4];const float xy[4][2]={{0,0},{1,0},{1,1},{0,1}};
            for(unsigned v=0;v<4;++v)vertices[v]={{xy[v][0],xy[v][1],0,.25f,.75f,49,115,181,255},c.normal[0],c.normal[1],c.normal[2],2};
            const std::uint16_t indices[]={0,1,2,0,2,3};
            const unsigned v=768+p*140,n=v+128,m=128+p*32,part=448+p*32;
            put(m,v);put(m+4,128);put(m+8,n);put(m+12,12);put(m+16,1);put(part,p);put(part+8,p);put(part+16,6);
            std::memcpy(bytes.data()+v,vertices,sizeof(vertices));std::memcpy(bytes.data()+n,indices,sizeof(indices));
            materials[i]=material;
            if(g%3==1){materials[i].tint=0xdfefffcc;materials[i].blend=Blend::alpha;materials[i].brightness=2;}
            if(g%3==2){materials[i].tint=0xe9c0ffff;materials[i].brightness=-2;}
            matrices[i]=rectangle(8+(i%16)*24,8+(i/16)*24,20,20);lights[i]=c.lights;
            auto basis=native_lab_cases::lighting_cases[i].basis;
            for(auto& value:basis)value=std::int32_t((std::uint32_t(value)+32768u)&65535u)-32768;
            inputs[i]={&lights[i],basis,c.material};
            if(g==13&&inputs[i].material.mask){inputs[i].material.use_shininess=false;inputs[i].material.toon=true;lights[i].toon_known=true;
                for(unsigned n2=0;n2<32;++n2)lights[i].toon[n2]=std::uint16_t(n2|((31-n2)<<5)|((n2*7%32)<<10));}
            if(g==14&&inputs[i].material.mask){inputs[i].material.use_shininess=false;inputs[i].material.grey=true;lights[i].toon_known=true;}
        }
        put(52,ModelBank::checksum(bytes.data(),bytes.size()));ModelBank bank;
        expect(ModelBank::open(bytes.data(),bytes.size(),bank),"authored eight-part bank");
        BatchGeometry packed;expect(packed.prepare(bank,65536),"compact geometry");
        for(unsigned p=0;p<8&&okay;++p)expect(renderer.create_mesh_bytes(bank.mesh(p),original[g*8+p]),"independent reference mesh");
        expect(renderer.create_resident_batch(packed,0,8,groups[g],ranges.data()+g*8),"resident palette upload");
        const auto live=renderer.stats().live_linear_bytes;Mesh refused;BatchRange invalid[8];
        check(renderer.create_resident_batch(packed,7,2,refused,invalid)==Error::invalid_data&&!refused&&renderer.stats().live_linear_bytes==live,"invalid group leaves residency unchanged");
        if(g==0){
            const auto before=renderer.stats().live_linear_bytes;NativeModel owner;
            expect(owner.upload(renderer,bank,65536),"model owns resident groups");
            check(bool(owner.resident_range(7)),"resident model range published");
            NativeModel moved(std::move(owner));check(!owner.resident_range(0)&&bool(moved.resident_range(0)),"moving model transfers sole range owner");
            const auto stale=moved.resident_range(0);stale_plan.reset();unsigned span;
            expect(stale_plan.append(stale,material,&matrices[0],nullptr,span),"save old generation");expect(stale_plan.seal(),"seal old generation");
            moved.reset();check(renderer.stats().live_linear_bytes==before,"model reset releases original and resident geometry");
            NativeModel fallback;expect(fallback.upload(renderer,bank,1),"optional cache budget preserves original model");
            check(fallback.geometry_ready()&&!fallback.resident_range(0),"original remains ready without resident group");fallback.reset();
            check(renderer.stats().live_linear_bytes==before,"optional failure leaves no leaked GPU allocation");
        }
        packed.reset(); // Rendering must never depend on this CPU template/bank.
    }
    Vertex plain[]={{0,0,0,0,0,49,115,181,255},{1,0,0,0,0,49,115,181,255},{0,1,0,0,0,49,115,181,255}};
    const std::uint16_t triangle[]={0,1,2};expect(renderer.create_mesh({plain,3,triangle,3},footer),"affine footer");
    expect(renderer.create_skin_stream(6,skin,true),"single palette skin stream");expect(renderer.create_quad_stream(2,quads,true),"single palette quad stream");
    check(renderer.update_skin_stream(skin,nullptr,3)==Error::invalid_data,"missing current vertices rejected at boundary");
    NormalVertex invalid[3]{};invalid[0].x=512;
    expect(renderer.update_skin_stream(skin,invalid,3),"legacy lenient prefix remains available");
    plan.reset();unsigned invalid_span;const Matrix unit=identity(); // the plan borrows its matrices (BUILD134)
    expect(plan.append({skin,0,3,0},material,&unit,nullptr,invalid_span),"invalid-prefix range plan");expect(plan.seal(),"invalid-prefix plan seal");
    expect(renderer.pass(target,true,0x182028ff),"invalid-prefix test pass");
    check(renderer.draw_batch(plan,invalid_span)==Error::invalid_data,"invalid current normal excludes palette draw");
    expect(renderer.end(),"upload end");
    const auto resident=renderer.stats().live_linear_bytes;
    unsigned frames=0;
    for(;okay&&frames<120&&aptMainLoop();++frames){
        expect(renderer.begin(),"frame begin");const auto upload_before=renderer.stats().mesh_upload_bytes;
        const unsigned pose=(frames/2)%3;NormalVertex animated[6];
        for(unsigned i=0;i<6;++i){const float xy[6][2]={{0,0},{1,0},{0,1},{0,0},{1,0},{1,1}};
            animated[i]={{xy[i][0]+pose*.125f,xy[i][1],0,.25f,.75f,49,115,181,255},std::int16_t(-400+int(pose)*400),0,511,2};}
        const std::uint8_t skin_slots[]={0,0,0,1,1,1};expect(renderer.update_skin_stream(skin,animated,6,skin_slots),"one current skin upload");
        Vertex quad_vertices[8];for(unsigned i=0;i<8;++i){const float xy[4][2]={{0,0},{1,0},{1,1},{0,1}};
            quad_vertices[i]={xy[i%4][0]+pose*.125f,xy[i%4][1],0,.25f,.75f,49,115,181,255};}
        const std::uint8_t quad_slots[]={0,0,0,0,1,1,1,1};expect(renderer.update_quad_stream(quads,quad_vertices,8,quad_slots),"one current quad upload");
        check(renderer.stats().mesh_upload_bytes-upload_before==14*sizeof(BatchVertex),"exactly one upload per dynamic prefix, zero rigid uploads");
        plan.reset();std::array<Command,136> commands{};unsigned commands_used=0;
        const auto command=[&](int source,unsigned span){if(source<0&&commands_used&&commands[commands_used-1].source<0&&commands[commands_used-1].span==span)return;
            commands[commands_used++]={source,span};};
        for(unsigned i=0;i<count&&okay;++i){if(i==(pose?3u:2u))continue;unsigned span;const auto e=plan.append(ranges[i],materials[i],&matrices[i],&inputs[i],span);
            if(e==Error::unsupported){plan.barrier();command(int(i),0);}else{expect(e,"static plan append");command(-1,span);}}
        LightingInput green;green.material.emission=31<<5;
        const Matrix left=rectangle(232,210,64,20),right=rectangle(312,210,64,20),qleft=rectangle(8,200,96,6),qright=rectangle(120,200,96,6);
        unsigned span;
        expect(plan.append({skin,0,3,0},material,&left,&inputs[16],span),"first actor uniforms");command(-1,span);
        expect(plan.append({skin,3,3,1},material,&right,&green,span),"second actor uniforms");command(-1,span);
        expect(plan.append({quads,0,6,0},material,&qleft,nullptr,span),"first quad uniforms");command(-1,span);
        expect(plan.append({quads,6,6,1},material,&qright,nullptr,span),"second quad uniforms");command(-1,span);
        auto repeated=material;repeated.tint=0xdfffdfff;repeated.brightness=1;
        unsigned repeat_quad_span;
        expect(plan.append({quads,0,6,0},repeated,&qleft,nullptr,repeat_quad_span),"repeat quad keeps matrix slot");command(-1,repeat_quad_span);
        expect(plan.append({skin,0,3,0},repeated,&left,&inputs[16],span),"repeat actor keeps normal slot");command(-1,span);
        expect(plan.seal(),"immutable plan seal");
        expect(renderer.pass(target,true,0x182028ff),"native pass");
        check(renderer.update_skin_stream(skin,animated,6,skin_slots)==Error::invalid_phase,"queued skin cannot be overwritten");
        check(renderer.update_quad_stream(quads,quad_vertices,8,quad_slots)==Error::invalid_phase,"queued quads cannot be overwritten");
        if(frames==0)check(renderer.draw_batch(stale_plan,0)==Error::stale_handle,"old range rejected after model destruction and slot reuse");
        const bool batched=frames%2;
        if(batched){for(unsigned c=0;c<commands_used&&okay;++c){const auto command2=commands[c];
            const auto matrices_before=renderer.stats().matrix_uploads;
            expect(command2.source<0?renderer.draw_batch(plan,command2.span):renderer.draw(original[command2.source],materials[command2.source],matrices[command2.source],0,0,&inputs[command2.source]),"resident/fallback draw");
            if(command2.source<0&&command2.span==repeat_quad_span)check(renderer.stats().matrix_uploads==matrices_before,"identical live palette matrix is reused");}}
        else{
            for(unsigned i=0;i<count&&okay;++i)if(i!=(pose?3u:2u))expect(renderer.draw(original[i],materials[i],matrices[i],0,0,&inputs[i]),"reference draw");
            expect(renderer.draw(skin,material,left,0,3,&inputs[16]),"reference actor one");expect(renderer.draw(skin,material,right,3,3,&green),"reference actor two");
            expect(renderer.draw(quads,material,qleft,0,6),"reference quad one");expect(renderer.draw(quads,material,qright,6,6),"reference quad two");
            expect(renderer.draw(quads,repeated,qleft,0,6),"repeat reference quad");expect(renderer.draw(skin,repeated,left,0,3,&inputs[16]),"repeat reference actor");
        }
        expect(renderer.draw(original[48],material,rectangle(8,210,96,20),0,0,&inputs[48]),"restore general shininess table");
        expect(renderer.draw(footer,material,rectangle(120,210,96,20)),"restore affine entry");
        expect(renderer.end(),"frame end");check(renderer.stats().live_linear_bytes==resident,"constant resident bytes");
        if(frames==30)check(native_lab::capture(renderer,target,pixels,log_file,"batch-reference"),"reference capture");
        if(frames==31)check(native_lab::capture(renderer,target,pixels,log_file,"batch-native"),"resident capture");
        if(frames==32)check(native_lab::capture(renderer,target,pixels,log_file,"batch-pose-reference"),"changed pose reference");
        if(frames==33)check(native_lab::capture(renderer,target,pixels,log_file,"batch-pose-native"),"changed pose resident");
        if(frames==91)check(native_lab::capture(renderer,target,pixels,log_file,"batch-last"),"repeat capture");
    }
    expect(renderer.begin(false),"cleanup fence");
    for(auto mesh:original)expect(renderer.destroy(mesh),"destroy reference");
    for(auto mesh:groups)expect(renderer.destroy(mesh),"destroy resident");
    // Captured handles never alias a newly created resource in a reused slot.
    check(renderer.draw_batch(plan,0)==Error::invalid_phase,"draw requires a pass");
    expect(renderer.destroy(skin),"destroy skin");expect(renderer.destroy(quads),"destroy quads");expect(renderer.destroy(footer),"destroy footer");expect(renderer.end(),"cleanup end");
    expect(renderer.shutdown(),"shutdown");if(target)C3D_RenderTargetDelete(target);C3D_Fini();if(pixels)linearFree(pixels);
    std::fprintf(log_file,"frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);gfxExit();return okay?0:1;
}
