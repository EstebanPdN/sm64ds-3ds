#pragma once
#include "ntr/gx.h"
#include "sm64ds/game_projection.hpp"
#include "sm64ds/native_2d.hpp"
#include <citro3d.h>
#include <array>
#include <map>
#include <string>
#include <tuple>
#include <vector>
namespace sm64ds {
class GameTextures;
// Consumes the original renderer's view-space geometry. It neither advances
// simulation nor invents a player/world transform. One pose serves both eyes.
class GameRenderer {
public:
    bool init(std::string& error);
    bool draw(const ntr::GxTriangle* triangles,std::size_t count,
              const GameProjection& projection,float slider,std::string& error,
              const Native2DFrame* screens=nullptr,
              const Native2DFrame* capture_layers=nullptr,std::uint8_t* capture_rgba=nullptr,
              std::uint64_t screen_revision=0,const GameTextures* decoded_textures=nullptr);
    void exit();
    struct Stats {
        uint32_t triangles=0,batches=0,textures=0,texture_uploads=0,eyes=0;
        uint32_t screen_cache_hits=0;
        uint32_t decoded_texture_cache_hits=0;
        std::uint64_t texture_hash_bytes=0;
    } stats;
private:
    friend struct GameRendererTextureProbe;
    struct Vertex {float x,y,z;uint32_t color;float u,v;};
    static_assert(sizeof(Vertex)==24);
    static Vertex triangle_vertex(const ntr::GxTriangle&,const ntr::GxVertex&);
    struct Texture {C3D_Tex gpu{};bool used=false,translucent=false;std::uint64_t decoded_revision=0;};
    using TextureKey=std::tuple<uintptr_t,uint32_t,int,int>;
    struct Batch {int first,count;Texture* texture;GameBatchKey key;};
    bool initialized_=false,program_ready_=false,stereo_=false;
    C3D_RenderTarget* targets_[4]{};
    std::uint8_t* capture_readback_=nullptr;
    DVLB_s* shader_=nullptr;
    shaderProgram_s program_{};
    int projection_location_=0,view_location_=0;
    Vertex* vertices_=nullptr;
    std::size_t vertex_capacity_=0;
    std::map<TextureKey,Texture> textures_;
    std::vector<Batch> batches_;
    Texture* screen_textures_[5]{};
    int screen_vertices_[5]{};
    // A nonzero caller revision certifies unchanged screen bytes at this
    // source address. Zero retains full-image CRC behavior. The caller must
    // never reuse a revision for different bytes during this renderer's life.
    struct ScreenBinding {
        const std::uint32_t* pixels=nullptr;
        std::uint64_t revision=0;
        Texture* texture=nullptr;
    };
    ScreenBinding screen_bindings_[3]{};
    Texture* texture(const ntr::GxTriangle&,std::string& error,const GameTextures* decoded_textures=nullptr);
    Texture* screen_texture(const Native2DLayer&,unsigned,std::uint64_t,std::string& error);
    void trim_textures();
    void eye(int eye,const GameProjection&,float separation,const Native2DFrame*);
    void screen_quad(int layer,bool bottom);
};
}
