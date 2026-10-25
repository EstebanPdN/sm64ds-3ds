// Observe production CPU triangle vertices and uploads from copied ready RAM.
// Allocation/flush/delete are host seams. No PICA draw or live game executes.
#include "sm64ds/game_renderer.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr std::size_t guard=32;
struct Allocation {std::uint8_t* storage;std::size_t size;};
std::unordered_map<void*,Allocation> allocations;
unsigned created=0,deleted=0,flushed=0;
void check(const Allocation& a) {
    for(std::size_t i=0;i<guard;++i) {
        assert(a.storage[i]==0xa5 && a.storage[guard+a.size+i]==0x5a);
    }
}
struct VertexOutput {float x,y,z;std::uint32_t color;float u,v;};
static_assert(sizeof(VertexOutput)==24);
struct CapturedTexture {std::uint32_t pointer;int width,height;std::vector<std::uint32_t> pixels;};
template<class T> T read_at(const std::vector<std::uint8_t>& bytes,std::size_t at) {
    assert(at<=bytes.size() && sizeof(T)<=bytes.size()-at);
    T value;std::memcpy(&value,bytes.data()+at,sizeof value);return value;
}
template<class T> T consume(const std::vector<std::uint8_t>& bytes,std::size_t& at) {
    const auto value=read_at<T>(bytes,at);at+=sizeof(T);return value;
}
FILE* output(const std::string& folder,const char* name) {
    FILE* result=std::fopen((folder+"/"+name).c_str(),"wb");assert(result);return result;
}
template<class T> void write(FILE* file,const T& value) {
    assert(std::fwrite(&value,sizeof value,1,file)==1);
}
void dump(const std::string& folder,const char* name,const void* bytes,std::size_t size) {
    FILE* file=output(folder,name);assert(std::fwrite(bytes,1,size,file)==size);assert(!std::fclose(file));
}
}

extern "C" bool C3D_TexInitWithParams(C3D_Tex* texture,C3D_TexCube*,C3D_TexInitParams p) {
    assert(p.format==GPU_RGBA8 && p.type==GPU_TEX_2D && p.maxLevel==0);
    const std::size_t size=std::size_t(p.width)*p.height*4;
    auto* storage=static_cast<std::uint8_t*>(std::malloc(size+2*guard));assert(storage);
    std::memset(storage,0xa5,guard);std::memset(storage+guard,0xcc,size);
    std::memset(storage+guard+size,0x5a,guard);
    texture->data=storage+guard;texture->width=p.width;texture->height=p.height;
    texture->size=size;texture->fmt=p.format;
    assert(allocations.emplace(texture->data,Allocation{storage,size}).second);
    ++created;return true;
}
extern "C" void C3D_TexFlush(C3D_Tex* texture) {check(allocations.at(texture->data));++flushed;}
extern "C" void C3D_TexDelete(C3D_Tex* texture) {
    const auto found=allocations.find(texture->data);assert(found!=allocations.end());
    check(found->second);std::free(found->second.storage);allocations.erase(found);
    texture->data=nullptr;++deleted;
}

namespace sm64ds {
struct GameRendererTextureProbe {
    static VertexOutput vertex(const ntr::GxTriangle& triangle,const ntr::GxVertex& vertex) {
        const auto actual=GameRenderer::triangle_vertex(triangle,vertex);
        VertexOutput out;static_assert(sizeof out==sizeof actual);std::memcpy(&out,&actual,sizeof out);
        return out;
    }
    static const std::uint8_t* bind(GameRenderer& renderer,const ntr::GxTriangle& triangle,
                                   std::string& error) {
        auto* result=renderer.texture(triangle,error);
        return result?static_cast<const std::uint8_t*>(result->gpu.data):nullptr;
    }
    static const std::uint8_t* screen(GameRenderer& renderer,const Native2DLayer& layer,
                                     unsigned index,std::string& error) {
        auto* result=renderer.screen_texture(layer,index,0,error);
        return result?static_cast<const std::uint8_t*>(result->gpu.data):nullptr;
    }
    static void clear(GameRenderer& renderer) {
        for(auto& entry:renderer.textures_)entry.second.used=false;
        renderer.trim_textures();assert(renderer.textures_.empty());
    }
};
}

int main(int argc,char** argv) {
    static_assert(std::endian::native==std::endian::little);
    assert(argc==3);
    std::ifstream file(argv[1],std::ios::binary);assert(file);
    const std::vector<std::uint8_t> input{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    assert(input.size()>=16 && !std::memcmp(input.data(),"TEXUV01",8));
    std::size_t offset=8;
    const auto texture_count=consume<std::uint32_t>(input,offset),triangle_count=consume<std::uint32_t>(input,offset);
    assert(texture_count==24 && triangle_count==2274);
    std::vector<CapturedTexture> textures;textures.reserve(texture_count);
    std::uint64_t texture_pixels=0;
    for(unsigned i=0;i<texture_count;++i) {
        CapturedTexture texture;
        texture.pointer=consume<std::uint32_t>(input,offset);
        texture.width=consume<std::uint32_t>(input,offset);texture.height=consume<std::uint32_t>(input,offset);
        assert(texture.pointer && texture.width>=8 && texture.width<=1024 && texture.height>=8 && texture.height<=1024);
        assert(!(texture.width&(texture.width-1)) && !(texture.height&(texture.height-1)));
        const std::size_t count=std::size_t(texture.width)*texture.height;
        assert(offset<=input.size() && count*4<=input.size()-offset);
        texture.pixels.resize(count);std::memcpy(texture.pixels.data(),input.data()+offset,count*4);
        offset+=count*4;texture_pixels+=count;textures.push_back(std::move(texture));
    }
    using Probe=sm64ds::GameRendererTextureProbe;
    sm64ds::GameRenderer renderer;std::string error;const std::string folder=argv[2];
    for(const auto& texture:textures) {
        ntr::GxTriangle triangle{};triangle.tex=texture.pixels.data();triangle.tw=texture.width;triangle.th=texture.height;
        const auto* uploaded=Probe::bind(renderer,triangle,error);assert(uploaded && error.empty());
        char name[96];std::snprintf(name,sizeof name,"actual-texture-%08x-%dx%d.morton8",texture.pointer,texture.width,texture.height);
        dump(folder,name,uploaded,texture.pixels.size()*4);
    }
    FILE* vertex_file=output(folder,"actual-ready-vertices.bin");
    unsigned untextured=0;
    for(unsigned i=0;i<triangle_count;++i) {
        const auto at=offset+std::size_t(i)*204;
        assert(at<=input.size() && 204<=input.size()-at);
        ntr::GxTriangle triangle{};
        const auto pointer=read_at<std::uint32_t>(input,at+120);
        triangle.tw=read_at<std::int32_t>(input,at+124);triangle.th=read_at<std::int32_t>(input,at+128);
        triangle.alpha=read_at<std::uint8_t>(input,at+132);triangle.wrap=read_at<std::uint8_t>(input,at+133);
        if(pointer) {
            for(const auto& texture:textures)if(texture.pointer==pointer) {
                assert(texture.width==triangle.tw && texture.height==triangle.th);
                triangle.tex=texture.pixels.data();break;
            }
            assert(triangle.tex);
        } else ++untextured;
        std::memcpy(triangle.projection,input.data()+at+136,64);
        triangle.native_projection=read_at<std::uint8_t>(input,at+200)!=0;
        for(unsigned v=0;v<3;++v) {
            static_assert(sizeof(ntr::GxVertex)==40);
            std::memcpy(&triangle.v[v],input.data()+at+v*40,40);
            write(vertex_file,Probe::vertex(triangle,triangle.v[v]));
        }
    }
    assert(!std::fclose(vertex_file));offset+=std::size_t(triangle_count)*204;
    auto screens=std::make_unique<sm64ds::Native2DFrame>();
    sm64ds::Native2DLayer* layers[]={&screens->underlay,&screens->overlay,&screens->bottom};
    for(unsigned i=0;i<3;++i) {
        constexpr std::size_t layer_bytes=256*256*4;
        assert(offset<=input.size() && layer_bytes<=input.size()-offset);
        std::memcpy(layers[i]->pixels,input.data()+offset,layer_bytes);offset+=layer_bytes;
        const auto* uploaded=Probe::screen(renderer,*layers[i],i,error);assert(uploaded && error.empty());
        char name[48];std::snprintf(name,sizeof name,"actual-screen-%u.morton8",i);dump(folder,name,uploaded,layer_bytes);
    }
    assert(offset==input.size());
    FILE* centers=output(folder,"actual-centre-UV.bin");
    FILE* synthetic=output(folder,"actual-signed-vertices.bin");
    unsigned signed_cases=0;
    for(const auto& texture:textures) {
        ntr::GxTriangle triangle{};triangle.tex=texture.pixels.data();triangle.tw=texture.width;triangle.th=texture.height;triangle.alpha=31;
        ntr::GxVertex vertex{};vertex.vx=1.25f;vertex.vy=-2.5f;vertex.vz=-3.75f;vertex.color=0x80563412u;
        for(int y=0;y<texture.height;++y)for(int x=0;x<texture.width;++x) {
            vertex.u=x+.5f;vertex.v=y+.5f;
            const auto out=Probe::vertex(triangle,vertex);write(centers,out.u);write(centers,out.v);
        }
        const float width=float(texture.width),height=float(texture.height);
        const float us[]={-.5f,-1.5f,-width-.5f,-2*width+.5f,0,.5f,width-.5f,width+.5f,2*width+.5f,3*width-.5f};
        const float ts[]={-.5f,-1.5f,-height-.5f,-2*height+.5f,0,.5f,height-.5f,height+.5f,2*height+.5f,3*height-.5f};
        for(unsigned wrap=0;wrap<16;++wrap)for(float u:us)for(float t:ts) {
            triangle.wrap=wrap;vertex.u=u;vertex.v=t;
            write(synthetic,texture.pointer);write(synthetic,wrap);write(synthetic,u);write(synthetic,t);
            write(synthetic,Probe::vertex(triangle,vertex));++signed_cases;
        }
    }
    assert(!std::fclose(centers) && !std::fclose(synthetic));
    // Untextured coordinates must remain zero even with zero dimension fields.
    ntr::GxTriangle fallback{};fallback.alpha=7;
    ntr::GxVertex fallback_vertex{};fallback_vertex.u=-37.5f;fallback_vertex.v=900.5f;
    fallback_vertex.vx=1;fallback_vertex.vy=2;fallback_vertex.vz=3;fallback_vertex.color=0x80123456u;
    const auto fallback_out=Probe::vertex(fallback,fallback_vertex);
    assert(fallback_out.u==0 && fallback_out.v==0 && fallback_out.x==1 && fallback_out.y==2 && fallback_out.z==3);
    assert(fallback_out.color==0x39563412u);
    Probe::clear(renderer);assert(allocations.empty() && created==deleted);
    std::printf("{\"status\":\"passed\",\"captured_textures\":%u,\"captured_triangles\":%u,"
        "\"untextured_triangles\":%u,\"actual_centre_helper_calls\":%llu,\"signed_helper_calls\":%u,"
        "\"textures_created\":%u,\"textures_deleted\":%u,\"flushes_observed\":%u,\"GPU_executed\":false}\n",
        texture_count,triangle_count,untextured,static_cast<unsigned long long>(texture_pixels),signed_cases,created,deleted,flushed);
}
