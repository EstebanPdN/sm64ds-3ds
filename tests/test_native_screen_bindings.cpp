// Real CPU renderer texture/cache code with GPU allocation/flush observation
// seams. No PICA rendering, emulation, game execution or FPS measurement.
#include "sm64ds/game_renderer.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace {
struct Allocation {std::uint8_t* storage;std::size_t bytes;};
std::unordered_map<void*,Allocation> allocations;
unsigned created=0,deleted=0,flushed=0;
bool fail_next=false;
constexpr std::size_t guard=32;
void check_guard(const Allocation& a) {
    for(std::size_t i=0;i<guard;++i) {
        assert(a.storage[i]==0xa5);
        assert(a.storage[guard+a.bytes+i]==0x5a);
    }
}
}
extern "C" bool C3D_TexInitWithParams(C3D_Tex* tex,C3D_TexCube*,C3D_TexInitParams p) {
    assert(p.format==GPU_RGBA8 && p.type==GPU_TEX_2D && p.maxLevel==0);
    if(fail_next) {fail_next=false;return false;}
    const std::size_t size=std::size_t(p.width)*p.height*4;
    auto* storage=static_cast<std::uint8_t*>(std::malloc(size+2*guard));assert(storage);
    std::memset(storage,0xa5,guard);std::memset(storage+guard,0xcc,size);
    std::memset(storage+guard+size,0x5a,guard);
    tex->data=storage+guard;tex->width=p.width;tex->height=p.height;
    tex->size=size;tex->fmt=p.format;
    assert(allocations.emplace(tex->data,Allocation{storage,size}).second);
    ++created;return true;
}
extern "C" void C3D_TexFlush(C3D_Tex* tex) {
    check_guard(allocations.at(tex->data));++flushed;
}
extern "C" void C3D_TexDelete(C3D_Tex* tex) {
    const auto found=allocations.find(tex->data);assert(found!=allocations.end());
    check_guard(found->second);std::free(found->second.storage);
    allocations.erase(found);tex->data=nullptr;++deleted;
}
namespace sm64ds {
struct GameRendererTextureProbe {
    static void begin(GameRenderer& renderer) {
        renderer.stats={};for(auto& entry:renderer.textures_)entry.second.used=false;
    }
    static const std::uint8_t* bind(GameRenderer& renderer,const Native2DLayer& layer,
                                    unsigned index,std::uint64_t revision,std::string& error) {
        auto* texture=renderer.screen_texture(layer,index,revision,error);
        return texture?static_cast<const std::uint8_t*>(texture->gpu.data):nullptr;
    }
    static void trim(GameRenderer& renderer) {renderer.trim_textures();}
    static bool empty(const GameRenderer& renderer) {
        if(!renderer.textures_.empty())return false;
        for(const auto& binding:renderer.screen_bindings_)if(binding.texture)return false;
        return true;
    }
};
}
using Probe=sm64ds::GameRendererTextureProbe;
static std::uint64_t compared=0;
static void compare_image(const std::uint8_t* actual,const sm64ds::Native2DLayer& layer) {
    assert(actual);
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
        unsigned within=0;
        for(unsigned bit=0;bit<3;++bit) {
            within|=((x>>bit)&1)<<(2*bit);within|=((y>>bit)&1)<<(2*bit+1);
        }
        const unsigned offset=(((y/8)*32+x/8)*64+within)*4;
        const auto pixel=layer.pixels[y*256+x];
        assert(actual[offset]==std::uint8_t(pixel>>24));
        assert(actual[offset+1]==std::uint8_t(pixel));
        assert(actual[offset+2]==std::uint8_t(pixel>>8));
        assert(actual[offset+3]==std::uint8_t(pixel>>16));compared+=4;
    }
}
int main() {
    constexpr std::uint64_t layer_bytes=256*256*4;
    auto frame=std::make_unique<sm64ds::Native2DFrame>();
    sm64ds::Native2DLayer* layers[]={&frame->underlay,&frame->overlay,&frame->bottom};
    for(unsigned layer=0;layer<3;++layer)for(unsigned i=0;i<256*256;++i)
        layers[layer]->pixels[i]=0xff000000u|((i*7193u+layer*101u)&0xffffffu);
    sm64ds::GameRenderer renderer;std::string error;
    auto draw=[&](std::uint64_t revision,std::uint64_t expected_hash,unsigned hits,unsigned uploads) {
        Probe::begin(renderer);
        for(unsigned i=0;i<3;++i)compare_image(Probe::bind(renderer,*layers[i],i,revision,error),*layers[i]);
        Probe::trim(renderer);assert(error.empty());
        assert(renderer.stats.texture_hash_bytes==expected_hash);
        assert(renderer.stats.screen_cache_hits==hits);
        assert(renderer.stats.texture_uploads==uploads);
    };
    draw(1,3*layer_bytes,0,3);
    draw(1,0,3,0); // Actual unchanged bindings avoid all three full CRCs.
    layers[1]->pixels[37]^=0x00123456u;
    layers[1]->pixels[255*256+255]=0x8013579bu; // Padding is also part of the texture.
    draw(2,3*layer_bytes,0,1);
    draw(2,0,3,0);
    draw(3,3*layer_bytes,0,0); // Input revision changed, pixels did not.
    layers[0]->pixels[7]^=0x00abcdefu;
    draw(0,3*layer_bytes,0,1); // Token-zero callers detect direct mutations.
    draw(0,3*layer_bytes,0,0);
    draw(4,3*layer_bytes,0,0);
    draw(4,0,3,0);
    auto other=std::make_unique<sm64ds::Native2DFrame>(*frame);
    layers[0]=&other->underlay;layers[1]=&other->overlay;layers[2]=&other->bottom;
    draw(4,3*layer_bytes,0,3); // Same token with a different owner must miss.
    Probe::begin(renderer);Probe::trim(renderer);
    assert(Probe::empty(renderer) && allocations.empty());
    draw(4,3*layer_bytes,0,3); // Restore after eviction, with no dangling binding.
    layers[0]->pixels[87]^=0x00012345u;
    Probe::begin(renderer);fail_next=true;
    assert(!Probe::bind(renderer,*layers[0],0,5,error));assert(!error.empty());
    error.clear();
    compare_image(Probe::bind(renderer,*layers[0],0,5,error),*layers[0]);
    assert(error.empty() && renderer.stats.screen_cache_hits==0);
    // Drop every resource through the same eviction routine used by draw.
    Probe::begin(renderer);Probe::trim(renderer);
    assert(Probe::empty(renderer) && allocations.empty());assert(created==deleted);
    std::printf("{\"status\":\"passed\",\"complete_RGBA_bytes_compared\":%llu,"
        "\"textures_created\":%u,\"textures_deleted\":%u,\"flushes_observed\":%u,"
        "\"unchanged_screen_CRC_bytes_saved\":786432,\"GPU_executed\":false}\n",
        static_cast<unsigned long long>(compared),created,deleted,flushed);
}
