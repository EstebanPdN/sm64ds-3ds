#include "sm64ds/game_renderer.hpp"
#include "sm64ds/game_textures.hpp"
#include "sm64ds/assets.hpp"
#include "scene_shader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace sm64ds {
namespace {
constexpr u32 transfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
unsigned morton(unsigned x,unsigned y) {
    return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
}
}
bool GameRenderer::init(std::string& error) {
    auto fail=[&](const char* message) {error=message;exit();return false;};
    if(initialized_)return fail("GPU del juego ya inicializada");
    if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))return fail("No se pudo iniciar PICA");
    initialized_=true;
    for(unsigned i=0;i<2;++i) {
        targets_[i]=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        if(!targets_[i])return fail("No hay memoria para las vistas del juego");
        C3D_RenderTargetSetOutput(targets_[i],GFX_TOP,i?GFX_RIGHT:GFX_LEFT,transfer);
    }
    shader_=DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<uint8_t*>(scene_shader)),sizeof(scene_shader));
    if(!shader_)return fail("Shader del juego no valido");
    shaderProgramInit(&program_);program_ready_=true;
    if(R_FAILED(shaderProgramSetVsh(&program_,&shader_->DVLE[0])))return fail("No se pudo enlazar el shader");
    projection_location_=shaderInstanceGetUniformLocation(program_.vertexShader,"projection");
    view_location_=shaderInstanceGetUniformLocation(program_.vertexShader,"modelView");
    return true;
}
GameRenderer::Vertex GameRenderer::triangle_vertex(const ntr::GxTriangle& triangle,
                                                   const ntr::GxVertex& vertex) {
    // GameTextures decodes original row zero at the top. Upload preserves that
    // row in Morton storage, whose PICA sampler addresses V from the bottom.
    // Keep unbounded coordinates for the material's repeat/mirror modes; the
    // screen and capture quads already use their own V=1..0.25 convention.
    return {vertex.vx,vertex.vy,vertex.vz,
        ((vertex.color>>16)&255)|(vertex.color&0xff00)|((vertex.color&255)<<16)|
            (uint32_t(triangle.alpha*255/31)<<24),
        triangle.tex?vertex.u/triangle.tw:0,
        triangle.tex?1.0f-vertex.v/triangle.th:0};
}
GameRenderer::Texture* GameRenderer::texture(const ntr::GxTriangle& triangle,std::string& error,
                                            const GameTextures* decoded_textures) {
    static const auto white=[] {std::array<uint32_t,64> p;p.fill(0xffffffffu);return p;}();
    const auto* pixels=triangle.tex ? triangle.tex : white.data();
    const int width=triangle.tex ? triangle.tw : 8,height=triangle.tex ? triangle.th : 8;
    if(width<8 || width>1024 || height<8 || height>1024 || (width&(width-1)) || (height&(height-1))) {
        error="Dimensiones de textura no validas para PICA";return nullptr;
    }
    // Hash the complete image once per distinct texture per frame. Pointer
    // reuse after a scene/palette change must never reuse stale GPU texels.
    for(auto& item:textures_)if(item.second.used && std::get<0>(item.first)==reinterpret_cast<uintptr_t>(pixels) &&
        std::get<2>(item.first)==width && std::get<3>(item.first)==height)return &item.second;
    const auto revision=decoded_textures?decoded_textures->revision(pixels,width,height):0;
    if(revision)for(auto& item:textures_)if(item.second.decoded_revision==revision &&
        std::get<0>(item.first)==reinterpret_cast<uintptr_t>(pixels) &&
        std::get<2>(item.first)==width && std::get<3>(item.first)==height) {
        item.second.used=true;++stats.decoded_texture_cache_hits;return &item.second;
    }
    const auto bytes=std::size_t(width)*height*4;
    const auto hash=crc32(pixels,bytes);stats.texture_hash_bytes+=bytes;
    const TextureKey key{reinterpret_cast<uintptr_t>(pixels),hash,width,height};
    auto found=textures_.find(key);
    if(found!=textures_.end()) {
        found->second.used=true;found->second.decoded_revision=revision;return &found->second;
    }
    auto inserted=textures_.try_emplace(key);auto& texture=inserted.first->second;
    if(!C3D_TexInit(&texture.gpu,width,height,GPU_RGBA8)) {
        textures_.erase(inserted.first);error="No hay memoria para texturas del juego";return nullptr;
    }
    auto* destination=static_cast<uint8_t*>(texture.gpu.data);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const auto pixel=pixels[y*width+x];const unsigned alpha=pixel>>24;
        texture.translucent|=alpha!=0 && alpha!=255;
        const unsigned offset=((y/8)*(width/8)+x/8)*64+morton(x&7,y&7);
        destination[offset*4]=uint8_t(alpha);destination[offset*4+1]=uint8_t(pixel);
        destination[offset*4+2]=uint8_t(pixel>>8);destination[offset*4+3]=uint8_t(pixel>>16);
    }
    C3D_TexSetFilter(&texture.gpu,GPU_NEAREST,GPU_NEAREST);C3D_TexFlush(&texture.gpu);
    texture.used=true;texture.decoded_revision=revision;++stats.texture_uploads;return &texture;
}
GameRenderer::Texture* GameRenderer::screen_texture(const Native2DLayer& layer,unsigned index,
                                                   std::uint64_t revision,std::string& error) {
    auto& cached=screen_bindings_[index];
    if(revision && cached.texture && cached.pixels==layer.pixels && cached.revision==revision) {
        cached.texture->used=true;++stats.screen_cache_hits;return cached.texture;
    }
    ntr::GxTriangle binding{};binding.tex=layer.pixels;binding.tw=binding.th=256;
    auto* result=texture(binding,error);
    // A changed revision still uses the ordinary complete-image CRC route;
    // screen bytes that remain equal do not cause redundant GPU uploads.
    cached={revision && result?layer.pixels:nullptr,revision,result};
    return result;
}
bool GameRenderer::draw(const ntr::GxTriangle* triangles,std::size_t count,
                        const GameProjection& p,float slider,std::string& error,const Native2DFrame* screens,
                        const Native2DFrame* capture_layers,std::uint8_t* capture_rgba,
                        std::uint64_t screen_revision,const GameTextures* decoded_textures) {
    if(bool(capture_layers)!=bool(capture_rgba)) {error="Captura GPU sin capas o destino RGBA8";return false;}
    if(!initialized_ || (!triangles && count) || count>65536 ||
       !std::isfinite(p.vertical_fov_degrees) || p.vertical_fov_degrees<=0 || p.vertical_fov_degrees>=179 ||
       !std::isfinite(p.near_plane) || !std::isfinite(p.far_plane) || p.near_plane<=0 || p.far_plane<=p.near_plane ||
       !std::isfinite(p.convergence) || p.convergence<=0 || !std::isfinite(p.eye_separation) ||
       p.eye_separation<0 || !std::isfinite(slider)) {error="Frame nativo no valido";return false;}
    if(p.native_matrix && (!std::isfinite(p.source_aspect) || p.source_aspect<=0 ||
       !game_projection_finite(p.matrix))) {
        error="Proyeccion original no valida";return false;
    }
    slider=std::clamp(slider,0.0f,1.0f);
    if(screens && !targets_[2]) {
        targets_[2]=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        if(!targets_[2]) {error="No hay memoria para la pantalla inferior";return false;}
        C3D_RenderTargetSetOutput(targets_[2],GFX_BOTTOM,GFX_LEFT,transfer);
    }
    if(capture_layers && !targets_[3]) {
        // A dedicated original-resolution target uses the same sideways
        // projection as the panels. It has no display output and contains
        // engine A before display-mode selection or master brightness.
        targets_[3]=C3D_RenderTargetCreate(192,256,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        capture_readback_=static_cast<std::uint8_t*>(linearAlloc(256*192*4));
        if(!targets_[3] || !capture_readback_) {
            if(targets_[3])C3D_RenderTargetDelete(targets_[3]);targets_[3]=nullptr;
            if(capture_readback_)linearFree(capture_readback_);capture_readback_=nullptr;
            error="No hay memoria para la captura original";return false;
        }
    }
    const bool stereo=slider>0.001f;
    if(stereo!=stereo_) {gfxSet3D(stereo);stereo_=stereo;}
    // Synchronize BEFORE modifying vertices/textures read by the previous GPU
    // submission. A CPU update ahead of FrameBegin would race the GPU.
    if(!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) {error="No se pudo comenzar el frame";return false;}
    auto fail=[&](const char* message) {error=message;C3D_FrameEnd(0);return false;};
    stats={};stats.triangles=uint32_t(count);stats.eyes=stereo?2:1;
    batches_.clear();for(auto& item:textures_)item.second.used=false;
    const std::size_t needed=count*3+(screens?18:0)+(capture_layers?12:0);
    if(needed>vertex_capacity_) {
        auto* buffer=static_cast<Vertex*>(linearAlloc(needed*sizeof(Vertex)));
        if(!buffer)return fail("No hay memoria para vertices del juego");
        if(vertices_)linearFree(vertices_);vertices_=buffer;vertex_capacity_=needed;
    }
    unsigned index=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& t=triangles[i];
        if(t.alpha>31 || t.wrap>15)return fail("Material del juego no valido");
        auto* tex=texture(t,error,decoded_textures);
        if(!tex) {C3D_FrameEnd(0);return false;}
        const bool translucent=t.alpha<31 || tex->translucent;
        GameBatchKey key;
        if(!game_batch_key(t,p,tex,translucent,key))return fail("Proyeccion del triangulo no valida");
        if(batches_.empty() || batches_.back().key!=key)batches_.push_back({int(index),0,tex,key});
        for(const auto& v:t.v) {
            if(!std::isfinite(v.vx) || !std::isfinite(v.vy) || !std::isfinite(v.vz) ||
               !std::isfinite(v.u) || !std::isfinite(v.v))return fail("Vertice del juego no finito");
            vertices_[index++]=triangle_vertex(t,v);
            ++batches_.back().count;
        }
    }
    if(screens || capture_layers) {
        const Native2DLayer* layers[]={screens?&screens->underlay:nullptr,screens?&screens->overlay:nullptr,
            screens?&screens->bottom:nullptr,capture_layers?&capture_layers->underlay:nullptr,
            capture_layers?&capture_layers->overlay:nullptr};
        for(unsigned layer=0;layer<5;++layer) {
            if(!layers[layer])continue;
            if(layer<3)screen_textures_[layer]=screen_texture(*layers[layer],layer,screen_revision,error);
            else {
                ntr::GxTriangle binding{};binding.tex=layers[layer]->pixels;binding.tw=binding.th=256;
                screen_textures_[layer]=texture(binding,error);
            }
            if(!screen_textures_[layer]) {C3D_FrameEnd(0);return false;}
            screen_vertices_[layer]=int(index);
            // DS 256x192 image occupies a 256x256 texture. Uniform 1.25x
            // scaling fills 320x240, with 40px margins on the upper panel.
            // PICA's V origin is the bottom of the uploaded image. The first
            // source row is V=1; V=0 would sample the 64 padding rows first
            // and display the original menu upside down and clipped.
            const Vertex quad[]={
                {-1,1,0,0xffffffff,0,1},{1,1,0,0xffffffff,1,1},{-1,-1,0,0xffffffff,0,.25f},
                {1,1,0,0xffffffff,1,1},{1,-1,0,0xffffffff,1,.25f},{-1,-1,0,0xffffffff,0,.25f}};
            std::copy(std::begin(quad),std::end(quad),vertices_+index);index+=6;
        }
    }
    trim_textures();
    stats.textures=uint32_t(textures_.size());stats.batches=uint32_t(batches_.size());
    if(index)GSPGPU_FlushDataCache(vertices_,index*sizeof(Vertex));
    C3D_BindProgram(&program_);
    auto* attributes=C3D_GetAttrInfo();AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3);AttrInfo_AddLoader(attributes,1,GPU_UNSIGNED_BYTE,4);
    AttrInfo_AddLoader(attributes,2,GPU_FLOAT,2);
    auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    if(index)BufInfo_Add(buffers,vertices_,sizeof(Vertex),3,0x210);
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(true,GPU_GREATER,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
    eye(0,p,stereo?-slider*p.eye_separation*0.5f:0,screens);
    if(stereo)eye(1,p,slider*p.eye_separation*0.5f,screens);
    if(screens)eye(2,p,0,screens);
    if(capture_layers)eye(3,p,0,capture_layers);
    C3D_FrameEnd(0);
    if(capture_layers) {
        C3D_FrameSync();
        C3D_SyncDisplayTransfer(static_cast<u32*>(targets_[3]->frameBuf.colorBuf),GX_BUFFER_DIM(192,256),
            reinterpret_cast<u32*>(capture_readback_),GX_BUFFER_DIM(192,256),
            GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|
            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
        if(R_FAILED(GSPGPU_InvalidateDataCache(capture_readback_,256*192*4))) {
            error="No se pudo leer la captura GPU original";return false;
        }
        // 3DS linear display-transfer storage is sideways and bottom-to-top
        // within each column; RGBA8 GPU memory is byte-ordered A,B,G,R.
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x) {
            const auto* src=capture_readback_+(x*192+191-y)*4;
            auto* dst=capture_rgba+(y*256+x)*4;
            dst[0]=src[3];dst[1]=src[2];dst[2]=src[1];dst[3]=src[0];
        }
    }
    return true;
}
void GameRenderer::trim_textures() {
    for(auto it=textures_.begin();it!=textures_.end();) {
        if(!it->second.used) {
            // Screens may disappear for a draw. Clear their cached bindings
            // before erasing map nodes so a later draw cannot retain a freed
            // Texture pointer, even if its caller revision is unchanged.
            for(auto& binding:screen_bindings_)if(binding.texture==&it->second)binding={};
            C3D_TexDelete(&it->second.gpu);it=textures_.erase(it);
        }
        else ++it;
    }
}
void GameRenderer::screen_quad(int layer,bool bottom) {
    C3D_Mtx flat,view;Mtx_Zeros(&flat);Mtx_Identity(&view);
    flat.r[0].y=1;flat.r[1].x=bottom?-1:-0.8f;flat.r[2].w=-0.5f;flat.r[3].w=1;
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projection_location_,&flat);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,view_location_,&view);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_TexSetWrap(&screen_textures_[layer]->gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    C3D_TexBind(0,&screen_textures_[layer]->gpu);
    C3D_DrawArrays(GPU_TRIANGLES,screen_vertices_[layer],6);
}
void GameRenderer::eye(int eye_index,const GameProjection& p,float separation,const Native2DFrame* screens) {
    C3D_RenderTargetClear(targets_[eye_index],C3D_CLEAR_ALL,0xdee5ebff,0);C3D_FrameDrawOn(targets_[eye_index]);
    const bool bottom=eye_index==2,capture=eye_index==3;
    const bool main_engine=capture || !screens || (bottom!=screens->main_on_top);
    if(screens) {
        screen_quad(capture?3:main_engine?0:2,bottom||capture);
        if(!main_engine)return;
        if(!screens->has_3d) {screen_quad(capture?4:1,bottom||capture);return;}
    }
    C3D_Mtx projection,view;
    if(!p.native_matrix) {
        Mtx_PerspStereoTilt(&projection,C3D_AngleFromDegrees(p.vertical_fov_degrees),capture?256.0f/192.0f:(bottom?320.0f:400.0f)/240.0f,
                           p.near_plane,p.far_plane,separation,p.convergence,false);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projection_location_,&projection);
    }
    Mtx_Identity(&view);
    // GX already supplied view-space vertices. Citro3D's stereo projection
    // includes its own eye offset; another translation would apply it twice.
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,view_location_,&view);
    for(unsigned translucent=0;translucent<2;++translucent) {
        C3D_DepthTest(true,GPU_GREATER,translucent?GPU_WRITE_COLOR:GPU_WRITE_ALL);
        for(auto& batch:batches_)if(batch.key.translucent==bool(translucent)) {
            if(p.native_matrix) {
                const auto rows=game_projection_for_target(batch.key.matrix,p,bottom,capture,separation);
                for(unsigned row=0;row<4;++row)projection.r[row]=FVec4_New(
                    rows[row*4],rows[row*4+1],rows[row*4+2],rows[row*4+3]);
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projection_location_,&projection);
            }
            auto wrap=[](bool repeat,bool mirror) {return !repeat?GPU_CLAMP_TO_EDGE:mirror?GPU_MIRRORED_REPEAT:GPU_REPEAT;};
            C3D_TexSetWrap(&batch.texture->gpu,wrap(batch.key.wrap&1,batch.key.wrap&4),wrap(batch.key.wrap&2,batch.key.wrap&8));
            C3D_TexBind(0,&batch.texture->gpu);C3D_DrawArrays(GPU_TRIANGLES,batch.first,batch.count);
        }
    }
    if(screens)screen_quad(capture?4:1,bottom||capture);
}
void GameRenderer::exit() {
    if(initialized_)C3D_FrameSync();
    if(vertices_)linearFree(vertices_);vertices_=nullptr;vertex_capacity_=0;
    if(capture_readback_)linearFree(capture_readback_);capture_readback_=nullptr;
    for(auto& item:textures_)C3D_TexDelete(&item.second.gpu);
    textures_.clear();batches_.clear();
    for(auto& binding:screen_bindings_)binding={};
    for(auto*& texture:screen_textures_)texture=nullptr;
    if(program_ready_)shaderProgramFree(&program_);program_ready_=false;
    if(shader_)DVLB_Free(shader_);shader_=nullptr;
    for(auto*& target:targets_) {if(target)C3D_RenderTargetDelete(target);target=nullptr;}
    if(initialized_)C3D_Fini();initialized_=false;
}
}
