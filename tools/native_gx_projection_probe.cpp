// Host probe linked to a frozen production GX decoder. The optional taps are
// inserted only into a temporary copy; their output must match an intact replay.
#include "ntr/gx.h"
#include "sm64ds/game_geometry.hpp"
#include "sm64ds/game_textures.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
struct VertexTap {
    ntr::GxVertex vertex;
    std::array<float,16> matrix;
    std::array<std::int32_t,4> viewport;
    std::uint32_t word;
};
static_assert(sizeof(ntr::GxVertex)==40 && sizeof(VertexTap)==124);
std::vector<VertexTap> taps;
std::vector<std::uint32_t> strip;
std::vector<std::array<std::uint32_t,3>> triangles;
std::uint32_t current_word;
std::vector<std::uint8_t> read(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("cannot open snapshot");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
std::uint32_t word(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|
           (std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
}
void write(std::ofstream& out,const void* data,std::size_t size) {
    if(!out.write(static_cast<const char*>(data),size))throw std::runtime_error("cannot write probe evidence");
}
}

// Read-only observers called immediately after project(), and immediately
// before emit_tri(). Geometry, stack state and packets are never modified.
void sm64ds_projection_vertex_tap(const ntr::GxVertex& vertex,const float* matrix,
                                 int x,int y,int width,int height,std::size_t old_strip_size) {
    if(old_strip_size>strip.size())throw std::runtime_error("missing strip vertex tap");
    strip.resize(old_strip_size);
    VertexTap tap{vertex,{},{{x,y,width,height}},current_word};
    std::memcpy(tap.matrix.data(),matrix,64);
    strip.push_back(static_cast<std::uint32_t>(taps.size()));taps.push_back(tap);
}
void sm64ds_projection_triangle_tap(const ntr::GxVertex& a,const ntr::GxVertex& b,
    const ntr::GxVertex& c,const ntr::GxVertex* vertices,std::size_t count) {
    if(count!=strip.size())throw std::runtime_error("strip tap count differs from decoder");
    const std::array<std::size_t,3> indices={std::size_t(&a-vertices),std::size_t(&b-vertices),std::size_t(&c-vertices)};
    std::array<std::uint32_t,3> ids;
    for(unsigned i=0;i<3;++i) {
        if(indices[i]>=strip.size())throw std::runtime_error("triangle tap outside strip");
        ids[i]=strip[indices[i]];
    }
    triangles.push_back(ids);
}

int main(int argc,char** argv) {
    try {
        if(argc!=7)throw std::runtime_error("expected GX, metadata, textures, palettes, geometry output, tap output");
        const auto gx=read(argv[1]),metadata=read(argv[2]),texels=read(argv[3]),palettes=read(argv[4]);
        if(gx.size()%8 || metadata.size()!=44 || texels.size()!=524288 || palettes.size()!=98304)
            throw std::runtime_error("truncated snapshot");
        auto owned=std::make_unique<sm64ds::GameGeometry>();auto& geometry=*owned;
        std::memcpy(geometry.textures,texels.data(),texels.size());std::memcpy(geometry.palettes,palettes.data(),palettes.size());
        std::uint32_t* fields[]={&geometry.texture_capacity,&geometry.palette_capacity,&geometry.texture_generation,
            &geometry.palette_generation,&geometry.texture_uploads,&geometry.palette_uploads,
            &geometry.texture_bytes,&geometry.palette_bytes};
        for(unsigned i=0;i<8;++i)*fields[i]=word(metadata.data()+i*4);
        geometry.texture_transfer=word(metadata.data()+32)!=0;geometry.palette_transfer=word(metadata.data()+36)!=0;
        geometry.highlight_shading=word(metadata.data()+40)!=0;
        sm64ds::GameTextures textures(geometry);ntr::gx_reset();
        ntr::gx_set_texture_resolver(sm64ds::GameTextures::resolver,&textures);
        for(current_word=0;current_word<gx.size()/8;++current_word) {
            const auto port=word(gx.data()+current_word*8),value=word(gx.data()+current_word*8+4);
            if(port)ntr::gx_write_port(0x04000400+port*4,value);else ntr::gx_write_fifo(value);
        }
        std::size_t count=0;const auto* polys=ntr::gx_polygons(count);
        std::ofstream geometry_out(argv[5],std::ios::binary);
        const auto size=static_cast<std::uint32_t>(count);write(geometry_out,&size,4);
        for(std::size_t i=0;i<count;++i) {
            write(geometry_out,polys[i].v,sizeof polys[i].v);
            write(geometry_out,&polys[i].tw,4);write(geometry_out,&polys[i].th,4);
            write(geometry_out,&polys[i].alpha,1);write(geometry_out,&polys[i].wrap,1);
            write(geometry_out,polys[i].projection,sizeof polys[i].projection);
            const std::uint8_t native=polys[i].native_projection?1:0;write(geometry_out,&native,1);
        }
        std::array<float,16> final; ntr::gx_get_projection(final.data());
        std::ofstream tap_out(argv[6],std::ios::binary);write(tap_out,final.data(),64);
        const std::array<std::uint32_t,2> sizes={static_cast<std::uint32_t>(taps.size()),static_cast<std::uint32_t>(triangles.size())};
        write(tap_out,sizes.data(),8);
        if(!taps.empty())write(tap_out,taps.data(),taps.size()*sizeof(VertexTap));
        if(!triangles.empty())write(tap_out,triangles.data(),triangles.size()*12);
        const auto diagnostic=ntr::gx_get_stream_diagnostics();
        std::printf("{\"triangles\":%zu,\"vertices\":%zu,\"stream_valid\":%s,\"decoder_failure\":%u}\n",
            count,taps.size(),ntr::gx_asset_stream_valid()?"true":"false",unsigned(diagnostic.failure));
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"projection snapshot probe: %s\n",error.what());return 2;
    }
}
