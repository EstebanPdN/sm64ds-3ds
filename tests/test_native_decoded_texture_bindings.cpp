// Real decoder and CPU GPU-texture cache against complete acquired C12 texels.
// Only GPU allocations/flush/deletion are observed; no PICA/FPS is claimed.
#include "sm64ds/game_renderer.hpp"
#include "sm64ds/game_textures.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <unordered_map>

namespace {
struct Allocation {std::uint8_t* storage;std::size_t bytes;};
std::unordered_map<void*,Allocation> allocations;
unsigned created=0,deleted=0,flushed=0;
bool fail_next=false;
constexpr std::size_t guard=32;
std::uint64_t compared=0;
void check_guard(const Allocation& a) {
    for(std::size_t i=0;i<guard;++i)assert(a.storage[i]==0xa5 && a.storage[guard+a.bytes+i]==0x5a);
}
}
extern "C" bool C3D_TexInitWithParams(C3D_Tex* tex,C3D_TexCube*,C3D_TexInitParams p) {
    assert(p.format==GPU_RGBA8 && p.type==GPU_TEX_2D && p.maxLevel==0);
    if(fail_next) {fail_next=false;return false;}
    const auto bytes=std::size_t(p.width)*p.height*4;
    auto* memory=static_cast<std::uint8_t*>(std::malloc(bytes+2*guard));assert(memory);
    std::memset(memory,0xa5,guard);std::memset(memory+guard,0xcc,bytes);
    std::memset(memory+guard+bytes,0x5a,guard);
    tex->data=memory+guard;tex->width=p.width;tex->height=p.height;tex->size=bytes;tex->fmt=p.format;
    assert(allocations.emplace(tex->data,Allocation{memory,bytes}).second);
    ++created;return true;
}
extern "C" void C3D_TexFlush(C3D_Tex* tex) {check_guard(allocations.at(tex->data));++flushed;}
extern "C" void C3D_TexDelete(C3D_Tex* tex) {
    auto it=allocations.find(tex->data);assert(it!=allocations.end());check_guard(it->second);
    std::free(it->second.storage);allocations.erase(it);tex->data=nullptr;++deleted;
}
namespace sm64ds {
struct GameRendererTextureProbe {
    static void begin(GameRenderer& r) {r.stats={};for(auto& row:r.textures_)row.second.used=false;}
    static const std::uint8_t* bind(GameRenderer& r,const uint32_t* pixels,int w,int h,
                                   const GameTextures* owner,std::string& error) {
        ntr::GxTriangle t{};t.tex=pixels;t.tw=w;t.th=h;t.alpha=31;
        auto* texture=r.texture(t,error,owner);
        return texture?static_cast<const std::uint8_t*>(texture->gpu.data):nullptr;
    }
    static void trim(GameRenderer& r) {r.trim_textures();}
};
}
using Probe=sm64ds::GameRendererTextureProbe;
static void compare(const std::uint8_t* upload,const uint32_t* pixels,unsigned w,unsigned h) {
    assert(upload);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        unsigned interleaved=0;
        for(unsigned bit=0;bit<3;++bit)interleaved|=((x>>bit)&1)<<(2*bit)|((y>>bit)&1)<<(2*bit+1);
        auto at=(((y/8)*(w/8)+x/8)*64+interleaved)*4;auto p=pixels[y*w+x];
        assert(upload[at]==uint8_t(p>>24) && upload[at+1]==uint8_t(p) &&
               upload[at+2]==uint8_t(p>>8) && upload[at+3]==uint8_t(p>>16));compared+=4;
    }
}
static unsigned word(FILE* file) {uint32_t v;assert(std::fread(&v,4,1,file)==1);return v;}
int main(int argc,char** argv) {
    assert(argc==2);auto* file=std::fopen(argv[1],"rb");assert(file);
    char magic[8];assert(std::fread(magic,1,8,file)==8 && !std::memcmp(magic,"TEXRAM01",8));
    unsigned count=word(file),tc=word(file),pc=word(file);assert(count==53 && tc==262144 && pc==81920);
    std::vector<uint8_t> textures(tc),palettes(pc);
    assert(std::fread(textures.data(),1,tc,file)==tc && std::fread(palettes.data(),1,pc,file)==pc);
    struct Binding {uint32_t image,palette;int w,h;std::vector<uint32_t> expected;const uint32_t* pixels=nullptr;uint64_t revision=0;};
    std::vector<Binding> bindings;
    std::uint64_t pixel_bytes=0;
    for(unsigned i=0;i<count;++i) {
        Binding b;b.image=word(file);b.palette=word(file);b.w=word(file);b.h=word(file);
        auto bytes=word(file);assert(bytes==unsigned(b.w)*b.h*4);b.expected.resize(bytes/4);
        assert(std::fread(b.expected.data(),1,bytes,file)==bytes);pixel_bytes+=bytes;bindings.push_back(std::move(b));
    }
    assert(std::fgetc(file)==EOF);std::fclose(file);
    auto geometry=std::make_unique<sm64ds::GameGeometry>();auto& g=*geometry;
    assert(g.set_texture_banks(3) && g.set_palette_banks(0x30));
    assert(g.begin(false) && g.upload(false,textures.data(),0,tc) && g.end(false));
    assert(g.begin(true) && g.upload(true,palettes.data(),0,pc) && g.end(true));
    sm64ds::GameTextures decoder(g);std::set<uint64_t> revisions;
    for(auto& b:bindings) {
        int w,h;assert(decoder.resolve(b.image,b.palette,b.pixels,w,h) && w==b.w && h==b.h);
        assert(!std::memcmp(b.pixels,b.expected.data(),b.expected.size()*4));
        b.revision=decoder.revision(b.pixels,w,h);assert(b.revision && revisions.insert(b.revision).second);
        assert(decoder.revision(b.pixels,w*2,h)==0);
    }
    sm64ds::GameRenderer renderer;std::string error;
    auto draw=[&](uint64_t crc,unsigned hits,int uploads) {
        Probe::begin(renderer);
        for(const auto& b:bindings)compare(Probe::bind(renderer,b.pixels,b.w,b.h,&decoder,error),b.expected.data(),b.w,b.h);
        Probe::trim(renderer);assert(error.empty());
        assert(renderer.stats.texture_hash_bytes==crc && renderer.stats.decoded_texture_cache_hits==hits);
        if(uploads>=0)assert(renderer.stats.texture_uploads==unsigned(uploads));
    };
    draw(pixel_bytes,0,count);draw(0,count,0);
    decoder.begin_frame();draw(0,count,0);
    // A version is a lifetime identity, not a byte hash: equal uploaded bytes
    // produce new versions after generation change. They get a fresh CRC.
    assert(g.begin(false) && g.upload(false,textures.data(),0,tc) && g.end(false));
    decoder.begin_frame();
    for(auto& b:bindings) {
        int w,h;assert(decoder.resolve(b.image,b.palette,b.pixels,w,h));
        auto revision=decoder.revision(b.pixels,w,h);assert(revision>b.revision);b.revision=revision;
        assert(!std::memcmp(b.pixels,b.expected.data(),b.expected.size()*4));
    }
    draw(pixel_bytes,0,-1);
    // GPU allocations depend on legitimate allocator address reuse; exact
    // output and complete CRC traffic above are the invariant, not pointer luck.
    draw(0,count,0);
    // Explicit unmanaged input never inherits a decoded version certificate.
    uint32_t foreign[64];std::fill(std::begin(foreign),std::end(foreign),0xff123456u);
    assert(decoder.revision(foreign,8,8)==0);
    Probe::begin(renderer);compare(Probe::bind(renderer,foreign,8,8,&decoder,error),foreign,8,8);
    assert(renderer.stats.texture_hash_bytes==256 && renderer.stats.decoded_texture_cache_hits==0);
    foreign[19]^=0x00123456u;
    Probe::begin(renderer);compare(Probe::bind(renderer,foreign,8,8,&decoder,error),foreign,8,8);
    assert(renderer.stats.texture_hash_bytes==256 && renderer.stats.texture_uploads==1);
    // Real palette update while a frame still owns its old decoded version.
    uint8_t indices[32]{};uint8_t red[]={0x1f,0,0,0},blue[]={0,0x7c,0,0};
    assert(g.begin(false) && g.upload(false,indices,tc-32,32) && g.end(false));
    assert(g.begin(true) && g.upload(true,red,0,4) && g.end(true));
    const unsigned image=(3u<<26)|((tc-32)/8);const uint32_t *old,*newer;int w,h;
    assert(decoder.resolve(image,0,old,w,h) && w==8 && h==8);auto old_revision=decoder.revision(old,w,h);
    for(unsigned i=0;i<64;++i)assert(old[i]==0xffff0000u);
    assert(g.begin(true) && g.upload(true,blue,0,4) && g.end(true));
    assert(decoder.resolve(image,0,newer,w,h) && newer!=old && decoder.revision(newer,w,h)>old_revision);
    assert(decoder.revision(old,w,h)==old_revision);
    for(unsigned i=0;i<64;++i)assert(newer[i]==0xff0000ffu && old[i]==0xffff0000u);
    Probe::begin(renderer);compare(Probe::bind(renderer,old,8,8,&decoder,error),old,8,8);
    compare(Probe::bind(renderer,newer,8,8,&decoder,error),newer,8,8);
    assert(renderer.stats.texture_hash_bytes==512 && renderer.stats.texture_uploads==2);
    decoder.begin_frame();Probe::begin(renderer);
    compare(Probe::bind(renderer,newer,8,8,&decoder,error),newer,8,8);
    assert(renderer.stats.texture_hash_bytes==0 && renderer.stats.decoded_texture_cache_hits==1);
    // A separate decoder has a separate version even for identical pixels.
    sm64ds::GameTextures other(g);const uint32_t* independent;
    assert(other.resolve(image,0,independent,w,h) && other.revision(independent,w,h)>decoder.revision(newer,w,h));
    Probe::begin(renderer);fail_next=true;
    assert(!Probe::bind(renderer,independent,8,8,&other,error) && !error.empty());error.clear();
    compare(Probe::bind(renderer,independent,8,8,&other,error),independent,8,8);
    assert(error.empty() && renderer.stats.texture_hash_bytes==512 && renderer.stats.texture_uploads==1);
    Probe::begin(renderer);Probe::trim(renderer);assert(allocations.empty());
    Probe::begin(renderer);compare(Probe::bind(renderer,independent,8,8,&other,error),independent,8,8);
    assert(renderer.stats.texture_hash_bytes==256 && renderer.stats.texture_uploads==1);
    Probe::begin(renderer);Probe::trim(renderer);assert(allocations.empty() && created==deleted);
    std::printf("{\"status\":\"passed\",\"captured_bindings\":%u,\"captured_pixel_bytes\":%llu,"
        "\"complete_RGBA_upload_bytes_compared\":%llu,\"CRC_bytes_saved_per_unchanged_fixture_frame\":%llu,"
        "\"allocation_guards_checked\":true,\"GPU_executed\":false}\n",count,
        (unsigned long long)pixel_bytes,(unsigned long long)compared,(unsigned long long)pixel_bytes);
}
