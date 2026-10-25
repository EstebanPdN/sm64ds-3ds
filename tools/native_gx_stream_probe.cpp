// Host-only decoder/resolver inspection. Inputs are preserved DrawWord and
// GameGeometry snapshots; no renderer, DS firmware or replacement game loop.
#include "ntr/gx.h"
#include "sm64ds/game_draw.hpp"
#include "sm64ds/game_geometry.hpp"
#include "sm64ds/game_textures.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open snapshot");
    file.seekg(0,std::ios::end);
    const auto end=file.tellg();
    if(end<0)throw std::runtime_error("Cannot size snapshot");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    file.seekg(0,std::ios::beg);
    if(!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))
        throw std::runtime_error("Cannot read complete snapshot");
    return bytes;
}
std::uint32_t little32(const std::uint8_t* p) {
    return std::uint32_t(p[0])|std::uint32_t(p[1])<<8|
           std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;
}
const char* failure_name(ntr::GxStreamFailure failure) {
    switch(failure) {
        case ntr::GxStreamFailure::none:return "none";
        case ntr::GxStreamFailure::unknown_command:return "command";
        case ntr::GxStreamFailure::texture_generation:return "texture_generation";
        case ntr::GxStreamFailure::texture_resolution:return "texture_resolution";
        case ntr::GxStreamFailure::matrix_operation:return "matrix_operation";
    }
    return "invalid_diagnostic";
}
void print_diagnostic(const ntr::GxStreamDiagnostics& d) {
    std::printf("{\"failure\":\"%s\",\"failing_command\":%u,"
        "\"texture_image\":\"0x%08x\",\"texture_palette\":\"0x%08x\","
        "\"fifo_command\":%u,\"fifo_parameters\":%u,\"fifo_needed\":%u,"
        "\"fifo_commands_remaining\":%u,\"port_command\":%u,"
        "\"port_parameters\":%u,\"port_needed\":%u}",failure_name(d.failure),
        unsigned(d.failing_command),unsigned(d.texture_image),unsigned(d.texture_palette),
        unsigned(d.fifo_command),unsigned(d.fifo_parameters),unsigned(d.fifo_needed),
        unsigned(d.fifo_commands_remaining),unsigned(d.port_command),
        unsigned(d.port_parameters),unsigned(d.port_needed));
}
struct TextureQuery {
    std::size_t word;
    std::uint32_t image,palette,first_pixel=0;
    bool accepted=false;
    int width=0,height=0;
    const char* rejection="none";
};
struct Resolver {
    sm64ds::GameGeometry& geometry;
    sm64ds::GameTextures real;
    bool fixture;
    std::vector<std::uint32_t> tokens;
    std::vector<TextureQuery> queries;
    std::size_t word=0;
    Resolver(sm64ds::GameGeometry& input,bool protocol_only):geometry(input),real(input),
        fixture(protocol_only),tokens(protocol_only?1024*1024:0,0xa5a5a5a5) {}
    const char* rejection_reason(std::uint32_t image,std::uint32_t palette) const {
        const auto& g=geometry;
        const unsigned format=(image>>26)&7;
        if(g.texture_transfer || g.palette_transfer)return "upload still active";
        if(g.texture_capacity>sizeof g.textures || g.palette_capacity>sizeof g.palettes)
            return "capacity exceeds owned storage";
        if(palette>0x1fff)return "palette register exceeds 13 bits";
        const unsigned bits[]={0,8,2,4,8,2,8,16};
        const auto count=std::uint32_t(8<<((image>>20)&7))*(8<<((image>>23)&7));
        const auto offset=(image&0xffff)*8;
        const auto bytes=count*bits[format]/8;
        if(offset>g.texture_capacity || bytes>g.texture_capacity-offset)
            return "texel range exceeds live texture capacity";
        if(format==5) {
            const auto slot=offset/0x20000,within=offset%0x20000;
            if((slot!=0 && slot!=2) || bytes>0x20000-within)
                return "compressed texel range is outside slot 0 or 2";
            const auto index=0x20000+(slot==2?0x10000:0)+within/2;
            if(index>g.texture_capacity || bytes/2>g.texture_capacity-index)
                return "compressed index range exceeds live texture capacity";
        }
        if(format!=7) {
            const unsigned colours[]={0,32,4,16,256,0,8,0};
            const auto base=palette*(format==2?8u:16u);
            if(base>g.palette_capacity || colours[format]*2>g.palette_capacity-base)
                return "palette range exceeds live palette capacity";
            if(format==5)return "compressed palette index or texture decode rejected";
        }
        return "texture decoder rejected binding";
    }
    static bool resolve(void* context,std::uint32_t image,std::uint32_t palette,
                        const std::uint32_t*& pixels,int& width,int& height) {
        auto& self=*static_cast<Resolver*>(context);
        TextureQuery q{self.word,image,palette};
        if(self.fixture) {
            // Token storage is explicitly synthetic and never rasterized. It
            // separates a command-framing fault from actual uploaded bytes.
            width=8<<((image>>20)&7);height=8<<((image>>23)&7);
            pixels=self.tokens.data();q.accepted=true;
        } else {
            q.accepted=self.real.resolve(image,palette,pixels,width,height);
            if(!q.accepted)q.rejection=self.rejection_reason(image,palette);
        }
        if(q.accepted) {
            q.width=width;q.height=height;
            if(pixels)q.first_pixel=pixels[0];
        }
        self.queries.push_back(q);return q.accepted;
    }
};
}

int main(int argc,char** argv) {
    try {
        if(argc!=6)throw std::runtime_error("Expected GX, resolver mode, metadata, textures, palettes");
        const bool fixture=std::string(argv[2])=="fixture";
        if(!fixture && std::string(argv[2])!="real")throw std::runtime_error("Unknown resolver mode");
        const auto stream=read_file(argv[1]),metadata=read_file(argv[3]);
        const auto texels=read_file(argv[4]),palettes=read_file(argv[5]);
        if(stream.size()%8 || stream.size()/8>sm64ds::draw_capacity || metadata.size()!=44 ||
           texels.size()!=sm64ds::GameGeometry::max_texture_bytes ||
           palettes.size()!=sm64ds::GameGeometry::max_palette_bytes)
            throw std::runtime_error("Invalid or truncated snapshot size");
        auto owned=std::make_unique<sm64ds::GameGeometry>();auto& g=*owned;
        std::memcpy(g.textures,texels.data(),texels.size());
        std::memcpy(g.palettes,palettes.data(),palettes.size());
        g.texture_capacity=little32(metadata.data());g.palette_capacity=little32(metadata.data()+4);
        g.texture_generation=little32(metadata.data()+8);g.palette_generation=little32(metadata.data()+12);
        g.texture_uploads=little32(metadata.data()+16);g.palette_uploads=little32(metadata.data()+20);
        g.texture_bytes=little32(metadata.data()+24);g.palette_bytes=little32(metadata.data()+28);
        g.texture_transfer=little32(metadata.data()+32)!=0;
        g.palette_transfer=little32(metadata.data()+36)!=0;
        g.highlight_shading=little32(metadata.data()+40)!=0;
        Resolver resolver(g,fixture);
        ntr::gx_reset();ntr::gx_set_texture_resolver(Resolver::resolve,&resolver);
        std::array<std::size_t,256> port_entries{};std::size_t fifo_entries=0;
        std::size_t first_failure=SIZE_MAX;ntr::GxStreamDiagnostics first{};
        for(std::size_t i=0;i<stream.size()/8;++i) {
            const auto port=little32(stream.data()+i*8),value=little32(stream.data()+i*8+4);
            resolver.word=i;
            if(port) {
                if(port<0x10 || port>0x72)throw std::runtime_error("DrawWord has invalid port");
                ++port_entries[port];ntr::gx_write_port(0x04000400+port*4,value);
            } else {++fifo_entries;ntr::gx_write_fifo(value);}
            const auto d=ntr::gx_get_stream_diagnostics();
            if(first_failure==SIZE_MAX && d.failure!=ntr::GxStreamFailure::none) {
                first_failure=i;first=d;
            }
        }
        const auto d=ntr::gx_get_stream_diagnostics();std::size_t triangles=0;
        const auto* polygons=ntr::gx_polygons(triangles);
        std::printf("{\"stream_valid\":%s,\"entries\":%zu,\"triangles\":%zu,"
            "\"fifo_entries\":%zu,\"resolver\":\"%s\",\"first_failure_word\":",
            ntr::gx_asset_stream_valid()?"true":"false",stream.size()/8,triangles,
            fifo_entries,fixture?"synthetic protocol fixture":"actual uploaded bytes");
        if(first_failure==SIZE_MAX)std::printf("null");else std::printf("%zu",first_failure);
        std::printf(",\"first_failure\":");print_diagnostic(first);
        std::printf(",\"final_diagnostic\":");print_diagnostic(d);
        std::printf(",\"failure_kinds\":[");bool comma=false;
        auto kind=[&](const char* name) {std::printf("%s\"%s\"",comma?",":"",name);comma=true;};
        if(d.failure!=ntr::GxStreamFailure::none)kind(failure_name(d.failure));
        if(d.fifo_commands_remaining)kind("fifo_pending");
        if(d.port_parameters)kind("port_pending");
        std::printf("],\"direct_port_entries\":{");comma=false;
        for(unsigned i=0;i<port_entries.size();++i)if(port_entries[i]) {
            std::printf("%s\"0x%02x\":%zu",comma?",":"",i,port_entries[i]);comma=true;
        }
        std::printf("},\"texture_queries\":[");comma=false;
        for(const auto& q:resolver.queries) {
            const auto format=(q.image>>26)&7;
            const unsigned bits[]={0,8,2,4,8,2,8,16};
            const unsigned w=8<<((q.image>>20)&7),h=8<<((q.image>>23)&7);
            std::printf("%s{\"word\":%zu,\"image\":\"0x%08x\",\"palette\":\"0x%08x\","
                "\"format\":%u,\"texgen\":%u,\"declared_width\":%u,\"declared_height\":%u,"
                "\"texel_offset\":%u,\"texel_bytes\":%u,\"palette_offset\":%u,"
                "\"accepted\":%s,\"resolved_width\":%d,\"resolved_height\":%d,"
                "\"first_pixel_argb\":\"0x%08x\",\"rejection\":\"%s\"}",comma?",":"",
                q.word,unsigned(q.image),unsigned(q.palette),unsigned(format),unsigned(q.image>>30),w,h,
                unsigned((q.image&0xffff)*8),w*h*bits[format]/8,unsigned(q.palette*(format==2?8:16)),
                q.accepted?"true":"false",q.width,q.height,unsigned(q.first_pixel),q.rejection);comma=true;
        }
        std::printf("],\"triangles_uv\":[");
        for(std::size_t i=0;i<triangles;++i) {
            std::printf("%s[",i?",":"");
            for(unsigned j=0;j<3;++j)
                std::printf("%s[%.9g,%.9g]",j?",":"",double(polygons[i].v[j].u),
                            double(polygons[i].v[j].v));
            std::printf("]");
        }
        ntr::gx_set_texture_resolver(nullptr,nullptr);ntr::gx_reset();
        std::printf("],\"reset_valid\":%s,\"reset_diagnostic\":",
                    ntr::gx_asset_stream_valid()?"true":"false");
        print_diagnostic(ntr::gx_get_stream_diagnostics());std::printf("}\n");return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"GX snapshot probe: %s\n",error.what());return 2;
    }
}
