#include "sm64ds/game_geometry.hpp"
#include "sm64ds/game_textures.hpp"
#include "ntr/gx.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
static std::vector<uint8_t> read(const fs::path& path,std::size_t limit) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input || input.tellg()<0 || uint64_t(input.tellg())>limit)throw std::runtime_error("Invalid input: "+path.string());
    std::vector<uint8_t> bytes(static_cast<std::size_t>(input.tellg()));
    input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!input)throw std::runtime_error("Truncated input");
    return bytes;
}
static uint32_t word(const uint8_t* p) {
    return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
}
int main(int argc,char** argv) try {
    if(argc!=2) {std::fprintf(stderr,"usage: game_draw_probe FRAME_DIRECTORY\n");return 2;}
    fs::path folder=argv[1];
    const auto stream=read(folder/"commands.bin",65536*8);
    const auto texture=read(folder/"textures.bin",sm64ds::GameGeometry::max_texture_bytes);
    const auto palette=read(folder/"palettes.bin",sm64ds::GameGeometry::max_palette_bytes);
    if(stream.empty() || stream.size()%8 || texture.size()!=512*1024 || palette.size()!=96*1024)
        throw std::runtime_error("Unexpected captured buffers");
    auto& geometry=sm64ds::game_geometry();
    // Same bank selection as the original Stage resource initialization.
    if(!geometry.set_texture_banks(3) || !geometry.set_palette_banks(0x30))return 1;
    std::memcpy(geometry.textures,texture.data(),texture.size());
    std::memcpy(geometry.palettes,palette.data(),palette.size());
    sm64ds::GameTextures textures(geometry);
    textures.begin_frame();ntr::gx_reset();
    ntr::gx_set_texture_resolver(sm64ds::GameTextures::resolver,&textures);
    // Diagnostic scene light only: Stage/camera lighting is not initialized.
    ntr::gx_set_light(0,0.2f,-0.8f,-0.5f,0x7fff);ntr::gx_enable_lights(1);
    for(std::size_t i=0;i<stream.size();i+=8) {
        const auto port=word(stream.data()+i),value=word(stream.data()+i+4);
        if(port==0)ntr::gx_write_fifo(value);
        else if(port>=0x10 && port<=0x72)ntr::gx_write_port(0x04000400+port*4,value);
        else throw std::runtime_error("Invalid command tag");
    }
    std::size_t count=0;
    const auto* triangles=ntr::gx_polygons(count);
    if(!ntr::gx_asset_stream_valid() || !count || count>65536)throw std::runtime_error("Invalid or empty decoded frame");
    std::map<const uint32_t*,unsigned> ids;
    std::array<float,3> low={INFINITY,INFINITY,INFINITY},high={-INFINITY,-INFINITY,-INFINITY};
    std::ofstream mesh(folder/"geometry.json");
    mesh.precision(9);mesh<<"{\"scope\":\"Host decode of original ARM11 Player::Render; not a 3DS frame\",\"triangles\":[";
    for(std::size_t i=0;i<count;++i) {
        const auto& t=triangles[i];
        unsigned id=0;
        if(t.tex) {
            if(t.tw<=0 || t.th<=0)throw std::runtime_error("Invalid texture dimensions");
            auto result=ids.emplace(t.tex,unsigned(ids.size()+1));id=result.first->second;
            if(result.second) {
                std::ofstream out(folder/("texture-"+std::to_string(id)+".rgba"),std::ios::binary);
                uint32_t dimensions[]={uint32_t(t.tw),uint32_t(t.th)};
                out.write(reinterpret_cast<const char*>(dimensions),sizeof dimensions);
                out.write(reinterpret_cast<const char*>(t.tex),t.tw*t.th*4);
            }
        }
        if(i)mesh<<',';
        mesh<<"{\"texture\":"<<id<<",\"width\":"<<t.tw<<",\"height\":"<<t.th<<",\"alpha\":"<<int(t.alpha)<<",\"wrap\":"<<int(t.wrap)<<",\"vertices\":[";
        for(unsigned j=0;j<3;++j) {
            const auto& v=t.v[j];const float xyz[]={v.vx,v.vy,v.vz};
            for(unsigned axis=0;axis<3;++axis) {
                if(!std::isfinite(xyz[axis]))throw std::runtime_error("Nonfinite position");
                low[axis]=std::min(low[axis],xyz[axis]);high[axis]=std::max(high[axis],xyz[axis]);
            }
            if(!std::isfinite(v.u) || !std::isfinite(v.v))throw std::runtime_error("Nonfinite texture coordinates");
            if(j)mesh<<',';
            mesh<<'['<<v.vx<<','<<v.vy<<','<<v.vz<<','<<v.u<<','<<v.v<<','<<v.color<<']';
        }
        mesh<<"]}";
    }
    mesh<<"]}\n";
    ntr::gx_set_texture_resolver(nullptr,nullptr);
    std::printf("{\"triangles\":%zu,\"textures\":%zu,\"bounds\":[[%g,%g,%g],[%g,%g,%g]],\"valid\":true}\n",
                count,ids.size(),low[0],low[1],low[2],high[0],high[1],high[2]);
    return 0;
} catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
