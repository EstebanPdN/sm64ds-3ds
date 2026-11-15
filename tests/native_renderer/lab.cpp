// ROM-free GPU exercise of the new production backend. Original renderer and
// DS command interpreters are deliberately absent from this link target.
#include "citro3d_backend.hpp"
#include "native_rigid_shader.h"
#include "native_model.hpp"
#include "native_model_fixture.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <sys/stat.h>

using namespace sm64ds::render;
namespace {
Citro3DBackend renderer;
Citro3DBackend limited_renderer; // metadata is static, not on the 3DS stack
FILE* log_file = nullptr;
bool okay = true;
bool check(Error e, const char* operation) {
    if (e == Error::none) return true;
    std::printf("%s: %s\n", operation, describe(e));
    if (log_file) { std::fprintf(log_file,"FAIL %s: %s\n",operation,describe(e)); std::fflush(log_file); }
    okay = false; return false;
}
void expect(bool condition, const char* message) {
    if (condition) return;
    std::printf("FAIL %s\n", message);
    if (log_file) std::fprintf(log_file,"FAIL %s\n",message);
    okay = false;
}
Matrix rectangle(float x, float y, float w, float h, float depth) {
    C3D_Mtx projection, object, result;
    // Fixture positions use negative camera Z: use the right-handed form.
    Mtx_OrthoTilt(&projection, 0, 400, 240, 0, 0, 1, false);
    Mtx_Identity(&object);
    Mtx_Translate(&object, x, y, depth, true);
    Mtx_Scale(&object, w, h, 1);
    Mtx_Multiply(&result, &projection, &object);
    Matrix m{};
    for (unsigned i=0; i<4; ++i) {
        m.row[i][0]=result.r[i].x; m.row[i][1]=result.r[i].y;
        m.row[i][2]=result.r[i].z; m.row[i][3]=result.r[i].w;
    }
    return m;
}
Mesh quad(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const Vertex vertices[]={{0,0,0,0,0,r,g,b,a},{1,0,0,1,0,r,g,b,a},
        {1,1,0,1,1,r,g,b,a},{0,1,0,0,1,r,g,b,a}};
    const std::uint16_t indices[]={0,1,2,0,2,3};
    Mesh handle;
    if (r==0 && g==0 && b==255) {
        std::array<std::uint8_t,sizeof(vertices)+1> encoded_vertices{};
        std::array<std::uint8_t,sizeof(indices)+1> encoded_indices{};
        std::memcpy(encoded_vertices.data()+1,vertices,sizeof(vertices));
        std::memcpy(encoded_indices.data()+1,indices,sizeof(indices));
        check(renderer.create_mesh_bytes({encoded_vertices.data()+1,sizeof(vertices),
            encoded_indices.data()+1,sizeof(indices)},handle),"unaligned native payload upload");
    } else check(renderer.create_mesh({vertices,4,indices,6},handle),"create quad");
    return handle;
}
bool capture(C3D_RenderTarget* target, std::uint8_t* readback, const char* name) {
    // Outside a C3D frame this API drains the old queue AND waits for the
    // transfer. Inside a frame it merely enqueues a transfer: reading then
    // would race the GPU even though the function is called "Sync".
    C3D_SyncDisplayTransfer(static_cast<u32*>(target->frameBuf.colorBuf),GX_BUFFER_DIM(240,400),
        reinterpret_cast<u32*>(readback),GX_BUFFER_DIM(240,400),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    // Drain the command queue as well as the transfer event. A prior display
    // event is not a certificate that this particular destination is ready.
    if (!check(renderer.begin(false),"readback completion fence")) return false;
    const Result invalidated=GSPGPU_InvalidateDataCache(readback,400*240*3);
    check(renderer.end(),"readback end");
    if (R_FAILED(invalidated)) { okay=false; return false; }
    std::uint32_t digest=2166136261u;
    for(unsigned i=0;i<400*240*3;++i)digest=(digest^readback[i])*16777619u;
    if(log_file){
        std::fprintf(log_file,"readback=%s fnv1a=%08lx bytes1020=%02x%02x%02x%02x%02x%02x%02x%02x\n",name,
            static_cast<unsigned long>(digest),readback[1020],readback[1021],readback[1022],readback[1023],
            readback[1024],readback[1025],readback[1026],readback[1027]);
        std::fflush(log_file);
    }
    char path[128]; std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);
    auto* f=std::fopen(path,"wb");
    if (!f) { okay=false; return false; }
    const bool written=std::fwrite(readback,1,400*240*3,f)==400*240*3;
    const bool closed=std::fclose(f)==0;
    expect(written && closed,"capture file write");
    if (log_file) {std::fprintf(log_file,"capture=%s bytes=288000 layout=portrait-bgr8\n",name);std::fflush(log_file);}
    return written && closed;
}
}
int main() {
    gfxInitDefault(); consoleInit(GFX_BOTTOM,nullptr);
    std::puts("SM64DS native renderer lab\nModels / UV / depth / alpha\nROM-free GPU validation");
    mkdir("sdmc:/sm64ds",0777);
    log_file=std::fopen("sdmc:/sm64ds/log.txt","w");
    bool new_model=false; const Result model_result=APT_CheckNew3DS(&new_model);
    if (log_file) {
        std::fprintf(log_file,"CPU mode: model_query=%08lx new3ds=%u speedup_requested=0\n",
            static_cast<unsigned long>(model_result),unsigned(new_model));
        std::fflush(log_file);
    }
    expect(R_SUCCEEDED(model_result),"model query");
    if (!C3D_Init(256*1024)) { if(log_file){std::fputs("FATAL: C3D init\n",log_file);std::fclose(log_file);} gfxExit();return 1; }
    C3D_RenderTarget* targets[2]{};
    constexpr u32 transfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    for (unsigned i=0;i<2;++i) {
        targets[i]=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        if (targets[i]) C3D_RenderTargetSetOutput(targets[i],GFX_TOP,i?GFX_RIGHT:GFX_LEFT,transfer);
    }
    auto* readback=static_cast<std::uint8_t*>(linearAlloc(400*240*4));
    expect(targets[0] && targets[1] && readback,"laboratory target allocation");
    std::array<std::uint8_t,sizeof(native_model_fixture)+1> bank_bytes{};
    std::memcpy(bank_bytes.data()+1,native_model_fixture,sizeof(native_model_fixture));
    ModelBank bank;
    check(ModelBank::open(bank_bytes.data()+1,sizeof(native_model_fixture),bank),"unaligned native bank");
    check(limited_renderer.init(scene_shader,sizeof(scene_shader),512),"limited backend init");
    check(limited_renderer.begin(false),"limited upload begin");
    NativeModel refused;
    expect(refused.upload(limited_renderer,bank)==Error::budget && refused.parts()==0 &&
        refused.metadata_bytes()==0 && limited_renderer.stats().live_linear_bytes==256,
        "model upload rolls back a partial resource set");
    ModelBank sprite_bank;
    check(ModelBank::open(native_residency_fixture,sizeof(native_residency_fixture),sprite_bank),"two-image native bank");
    NativeModel sprites;std::array<bool,ModelBank::max_images> required{};required[0]=true;
    check(sprites.upload_images(limited_renderer,sprite_bank,required),"initial sprite image");
    const auto first_image=sprites.image(0);const auto sprite_bytes=limited_renderer.stats().live_linear_bytes;
    required[1]=true;
    expect(sprites.upload_images(limited_renderer,sprite_bank,required)==Error::budget&&
        sprites.image(0)==first_image&&!sprites.image(1)&&limited_renderer.stats().live_linear_bytes==sprite_bytes,
        "failed extension preserves resident image and budget");
    sprites.reset();expect(limited_renderer.stats().live_linear_bytes==256,"sprite extension rollback releases metadata/resources");
    check(limited_renderer.end(),"limited upload end");
    check(limited_renderer.shutdown(),"limited shutdown");
    check(renderer.init(scene_shader,sizeof(scene_shader),1024*1024,true),"renderer init");
    if (!okay || !check(renderer.begin(false),"initial upload begin")) {
        if(log_file){std::fputs("FAIL\n",log_file);std::fclose(log_file);} C3D_Fini();gfxExit();return 1;
    }
    const Mesh white=quad(255,255,255,255),red=quad(255,0,0,255),blue=quad(0,0,255,255),green=quad(0,255,0,128);
    std::array<std::uint8_t,16*8*4> image{},tiled{};
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x){
        auto* p=image.data()+(y*16+x)*4;
        p[0]=y<4?(x<8?255:0):(x<8?0:255);
        p[1]=x<8?0:255; p[2]=y<4?0:(x<8?255:0); p[3]=255;
    }
    check(cook_rgba8(image.data(),image.size(),16,8,tiled.data(),tiled.size()),"cook texture");
    Texture texture;
    check(renderer.create_texture(16,8,tiled.data(),tiled.size(),texture),"texture upload");
    NativeModel resident_model,ephemeral_model;
    check(resident_model.upload(renderer,bank),"resident model upload");
    required[1]=false;check(sprites.upload_images(renderer,sprite_bank,required),"sprite partial residency");
    const auto resident_image=sprites.image(0);required[1]=true;
    check(sprites.upload_images(renderer,sprite_bank,required),"sprite incremental extension");
    expect(sprites.image(0)==resident_image&&bool(sprites.image(1)),"extension retains original handle");
    const auto extended_bytes=renderer.stats().live_linear_bytes;
    check(sprites.upload_images(renderer,sprite_bank,required),"sprite resident reuse");
    expect(renderer.stats().live_linear_bytes==extended_bytes&&sprites.image(0)==resident_image,"reuse allocates nothing");
    expect(sprites.upload_images(renderer,bank,required)==Error::invalid_data&&sprites.image(0)==resident_image,
        "incompatible bank cannot replace live images");
    NativeModel moved_sprites(std::move(sprites));
    check(moved_sprites.upload_images(renderer,sprite_bank,required),"moved sprite source identity");
    moved_sprites.reset();
    // Failed creates must preserve both a live handle and current byte budget.
    Mesh unchanged=white; const auto bytes=renderer.stats().live_linear_bytes;
    expect(renderer.create_mesh({},unchanged)==Error::invalid_data && unchanged==white &&
        renderer.stats().live_linear_bytes==bytes,"invalid create transaction");
    Texture retained_texture=texture;
    expect(renderer.create_texture(1024,1024,tiled.data(),1024*1024*4,retained_texture)==Error::budget &&
        retained_texture==texture && renderer.stats().live_linear_bytes==bytes,"budget failure transaction");
    const Mesh retired=quad(255,255,255,255);
    check(renderer.destroy(retired),"release upload resource");
    check(ephemeral_model.upload(renderer,bank),"ephemeral model upload");
    const auto ephemeral_bytes=renderer.stats().live_linear_bytes-bytes;
    bank={};std::memset(bank_bytes.data(),0,bank_bytes.size());
    check(renderer.end(),"initial upload end");
    Material opaque;opaque.cull=Cull::none;
    Material textured=opaque;textured.texture=texture;
    Material alpha=opaque;alpha.blend=Blend::alpha;alpha.depth_write=false;
    unsigned frames=0;
    for (;okay && frames<1200 && aptMainLoop();++frames) {
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        const bool stereo=frames>=600;gfxSet3D(stereo);
        if(!check(renderer.begin(),"frame begin"))break;
        for(unsigned eye=0;eye<(stereo?2u:1u);++eye){
            check(renderer.pass(targets[eye],true,0x182028ff),"pass");
            const float offset=stereo?(eye?3.f:-3.f):0.f;
            Material animated=textured;
            if(frames==420){animated.uv={{-1,0,1,0,-1,1}};animated.tint=0x80ff80ff;}
            if(frames==480 || frames==540){
                animated.uv={{3,0,-1,0,3,-1}};
                animated.wrap_s=animated.wrap_t=frames==480?Wrap::repeat:Wrap::mirror;
            }
            check(renderer.draw(white,animated,rectangle(20+offset,20,160,80,-.5f)),"texture draw");
            if(frames==480 || frames==540){
                // Change wrapping on the SAME texture before other materials.
                // The later white pane covers this sacrificial draw; the
                // first pane must still retain its earlier sampler state.
                check(renderer.draw(white,textured,rectangle(220,130,160,80,-.5f)),"shared texture sampler change");
            }
            // Near red is intentionally submitted before farther blue.
            check(renderer.draw(red,opaque,rectangle(220+offset,20,160,80,-.2f)),"near draw");
            check(renderer.draw(blue,opaque,rectangle(220+offset,20,160,80,-.8f)),"far draw");
            check(renderer.draw(blue,opaque,rectangle(20+offset,130,160,80,-.8f)),"alpha background");
            check(renderer.draw(green,alpha,rectangle(20+offset,130,160,80,-.2f)),"alpha foreground");
            const Matrix m=rectangle(220+offset,130,160,80,-.5f);
            check(renderer.draw(white,opaque,m,0,3),"first indexed range");
            const auto matrices=renderer.stats().matrix_uploads,materials=renderer.stats().material_binds;
            check(renderer.draw(white,opaque,m,3,3),"second indexed range");
            expect(renderer.stats().matrix_uploads==matrices && renderer.stats().material_binds==materials,
                "adjacent matrix/material reuse");
            check(resident_model.draw(&m,1),"resident model instance");
            if(frames==0){
                check(ephemeral_model.draw(&m,1),"ephemeral model instance");
                const auto before=renderer.stats().live_linear_bytes;
                ephemeral_model.reset();
                expect(renderer.stats().live_linear_bytes==before && ephemeral_model.metadata_bytes()==0,
                    "logical model release keeps in-flight GPU resources");
            }
            const auto submitted=renderer.stats().draws;
            expect(renderer.draw(retired,opaque,m)==Error::stale_handle && renderer.stats().draws==submitted,
                "stale handle rejected before draw");
            expect(renderer.destroy(white)==Error::invalid_phase,"draw phase protects GPU resource");
        }
        if(frames==300){
            // Later clear of the SAME target must retire the preceding draws
            // in queue order. An immediate clear before queued geometry would
            // leave the seven earlier fixtures visible in this capture.
            check(renderer.pass(targets[0],true,0x305070ff),"ordered second clear");
            check(renderer.draw(red,opaque,rectangle(80,70,100,60,-.2f)),"post-clear draw");
        }
        check(renderer.end(),"frame end");
        const Statistics s=renderer.stats();
        expect(s.mesh_upload_bytes==0 && s.texture_upload_bytes==0 && s.resource_creates==0,
            "resident frame has zero resource uploads");
        expect(s.live_linear_bytes==bytes+(frames==0?ephemeral_bytes:0),"resident bytes stable after GPU retirement");
        if(frames==1)expect(s.resource_destroys==2,"retired mesh and texture reclaimed at the next fence");
        if(frames%120==0 && log_file){
            std::fprintf(log_file,"frame=%u eyes=%u draws=%llu meshBinds=%llu materialBinds=%llu matrices=%llu bytes=%u waitTicks=%llu submitTicks=%llu previousQueueMs=%.6f\n",
                frames,stereo?2:1,s.draws,s.mesh_binds,s.material_binds,s.matrix_uploads,unsigned(s.live_linear_bytes),s.wait_ticks,s.submit_ticks,double(s.previous_gpu_queue_ms));
            std::fflush(log_file);
        }
        if(frames==120)capture(targets[0],readback,"mono");
        if(frames==300)capture(targets[0],readback,"clear-order");
        if(frames==420)capture(targets[0],readback,"uv-tint");
        if(frames==480)capture(targets[0],readback,"repeat");
        if(frames==540)capture(targets[0],readback,"mirror");
        if(frames==720){capture(targets[0],readback,"stereo-left");capture(targets[1],readback,"stereo-right");}
    }
    expect(frames==1200,"all laboratory frames completed");
    resident_model.reset();
    check(renderer.shutdown(),"renderer shutdown");
    expect(renderer.stats().live_linear_bytes==0,"renderer releases all owned bytes");
    if(readback)linearFree(readback);
    for(auto* t:targets)if(t)C3D_RenderTargetDelete(t);
    C3D_Fini();
    if(log_file){std::fprintf(log_file,"completed_frames=%u\n%s\n",frames,okay?"PASS":"FAIL");std::fclose(log_file);}
    gfxExit();return okay?0:1;
}
