#include "sm64ds/renderer.hpp"
#include "scene_shader.h"
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
namespace sm64ds {
namespace {
constexpr u32 TRANSFER=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
unsigned morton(unsigned x,unsigned y) {
    return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
}
bool upload(C3D_Tex& tex,const uint32_t* pixels,int w,int h) {
    if(!C3D_TexInit(&tex,w,h,GPU_RGBA8))return false;
    auto* dest=static_cast<uint8_t*>(tex.data);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        // PICA's RGBA8 storage is A,B,G,R in little-endian byte order.
        unsigned off=((y/8)*(w/8)+x/8)*64+morton(x&7,y&7);
        uint32_t c=pixels[y*w+x];
        dest[off*4]=uint8_t(c>>24);dest[off*4+1]=uint8_t(c);dest[off*4+2]=uint8_t(c>>8);dest[off*4+3]=uint8_t(c>>16);
    }
    C3D_TexSetFilter(&tex,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(&tex,GPU_REPEAT,GPU_REPEAT);
    C3D_TexFlush(&tex);return true;
}
}
bool Renderer::init(const ModelPreview& model,std::string& error) {
    auto fail=[&](const char* text){error=text;exit();return false;};
    if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))return fail("No se puede iniciar la GPU");
    ready_=true;
    for(int i=0;i<2;++i) {
        targets_[i]=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        if(!targets_[i])return fail("No hay memoria para las vistas 3D");
        C3D_RenderTargetSetOutput(targets_[i],GFX_TOP,i?GFX_RIGHT:GFX_LEFT,TRANSFER);
    }
    dvlb_=DVLB_ParseFile((u32*)scene_shader,sizeof(scene_shader));
    if(!dvlb_)return fail("Shader no valido");
    shaderProgramInit(&program_);program_ready_=true;
    if(R_FAILED(shaderProgramSetVsh(&program_,&dvlb_->DVLE[0])))return fail("No se puede cargar el shader");
    C3D_BindProgram(&program_);
    proj_location_=shaderInstanceGetUniformLocation(program_.vertexShader,"projection");
    view_location_=shaderInstanceGetUniformLocation(program_.vertexShader,"modelView");
    C3D_AttrInfo* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_UNSIGNED_BYTE,4);AttrInfo_AddLoader(attr,2,GPU_FLOAT,2);
    std::map<const uint32_t*,int> ids;
    std::map<const uint32_t*,bool> alpha_textures;
    textures_.reserve(model.model.textures.size()+1);
    uint32_t white[64];std::fill(white,white+64,0xffffffffu);
    textures_.emplace_back();if(!upload(textures_.back(),white,8,8)) {textures_.pop_back();return fail("No hay memoria para texturas");}
    ids[nullptr]=0;
    for(auto& texture:model.model.textures)if(!texture.rgba.empty()) {
        int index=int(textures_.size());textures_.emplace_back();
        if(!upload(textures_.back(),texture.rgba.data(),texture.width,texture.height)) {textures_.pop_back();return fail("No hay memoria para texturas");}
        ids[texture.rgba.data()]=index;
        alpha_textures[texture.rgba.data()]=(texture.format==1 || texture.format==6);
    }
    vertices_=static_cast<Vertex*>(linearAlloc(model.triangles.size()*3*sizeof(Vertex)));
    if(!vertices_)return fail("No hay memoria para geometria");
    int index=0;
    for(auto& t:model.triangles) {
        auto it=ids.find(t.tex);if(it==ids.end())return fail("Textura del modelo no encontrada");
        int texture=it->second;
        bool translucent=t.alpha<31 || alpha_textures[t.tex];
        if(batches_.empty() || batches_.back().texture!=texture || batches_.back().wrap!=t.wrap || batches_.back().translucent!=translucent)
            batches_.push_back({index,0,texture,t.wrap,translucent});
        for(auto& v:t.v) {
            vertices_[index++]={(v.vx-model.center[0])/model.radius,(v.vy-model.center[1])/model.radius,(v.vz-model.center[2])/model.radius,
                ((v.color>>16)&255)|(v.color&0xff00)|((v.color&255)<<16)|(uint32_t(t.alpha*255/31)<<24),
                t.tw?v.u/t.tw:0,t.th?v.v/t.th:0};
            ++batches_.back().count;
        }
    }
    GSPGPU_FlushDataCache(vertices_,index*sizeof(Vertex));
    auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);BufInfo_Add(buffers,vertices_,sizeof(Vertex),3,0x210);
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaTest(true,GPU_GREATER,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
    return true;
}
void Renderer::update(const InputFrame& f,float dt) {
    dt=std::min(dt,0.05f);
    yaw_+=f.camera.x*dt*1.7f;
    pitch_=std::clamp(pitch_+f.camera.y*dt*1.3f,-1.4f,1.4f);
    panx_+=f.movement.x*dt*distance_*0.5f;pany_+=f.movement.y*dt*distance_*0.5f;
    distance_=std::clamp(distance_+(float(bool(f.held&KEY_L))-float(bool(f.held&KEY_R)))*dt*2,0.25f,8.0f);
    if(f.pressed&KEY_A) {yaw_=0;pitch_=0.30f;distance_=2.7f;panx_=pany_=0;}
}
void Renderer::eye(int i,float iod) {
    C3D_RenderTargetClear(targets_[i],C3D_CLEAR_ALL,0x87bce5ff,0);C3D_FrameDrawOn(targets_[i]);
    C3D_Mtx projection,view;
    // Keep vertical FOV. 400/240 expands the horizontal view without scaling
    // an already rasterized 256x192 image. Both eyes use the same mesh/tick.
    Mtx_PerspStereoTilt(&projection,C3D_AngleFromDegrees(60),400.0f/240.0f,0.01f,100.0f,iod,distance_,false);
    float x=std::sin(yaw_)*std::cos(pitch_)*distance_,y=std::sin(pitch_)*distance_,z=std::cos(yaw_)*std::cos(pitch_)*distance_;
    float px=panx_*std::cos(yaw_),pz=-panx_*std::sin(yaw_);
    Mtx_LookAt(&view,FVec3_New(x+px,y+pany_,z+pz),FVec3_New(px,pany_,pz),FVec3_New(0,1,0),false);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,proj_location_,&projection);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,view_location_,&view);
    for(int transparent=0;transparent<2;++transparent) {
        C3D_DepthTest(true,GPU_GREATER,transparent?GPU_WRITE_COLOR:GPU_WRITE_ALL);
        for(auto& b:batches_)if(b.translucent==bool(transparent)) {
            auto wrap=[](bool repeat,bool mirror){return !repeat?GPU_CLAMP_TO_EDGE:mirror?GPU_MIRRORED_REPEAT:GPU_REPEAT;};
            C3D_TexSetWrap(&textures_[b.texture],wrap(b.wrap&1,b.wrap&4),wrap(b.wrap&2,b.wrap&8));
            C3D_TexBind(0,&textures_[b.texture]);C3D_DrawArrays(GPU_TRIANGLES,b.first,b.count);
        }
    }
}
void Renderer::draw(float slider) {
    bool stereo=slider>0.001f;
    if(stereo!=stereo_) {gfxSet3D(stereo);stereo_=stereo;}
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    eye(0,stereo?-slider*0.10f:0);
    if(stereo)eye(1,slider*0.10f);
    C3D_FrameEnd(0);
}
void Renderer::exit() {
    if(vertices_)linearFree(vertices_);
    vertices_=nullptr;
    for(auto& t:textures_)C3D_TexDelete(&t);
    textures_.clear();batches_.clear();
    if(program_ready_)shaderProgramFree(&program_);
    program_ready_=false;
    if(dvlb_)DVLB_Free(dvlb_);
    dvlb_=nullptr;
    for(auto*& t:targets_) {if(t)C3D_RenderTargetDelete(t);t=nullptr;}
    if(ready_)C3D_Fini();
    ready_=false;
}
}
