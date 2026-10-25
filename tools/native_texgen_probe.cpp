// Batch host probe for original GX command packets. It reports emitted vertex
// UV/position values, not private matrix getters or a rendered game frame.
#include "ntr/gx.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
std::uint32_t read32(std::ifstream& input) {
    unsigned char bytes[4];
    if(!input.read(reinterpret_cast<char*>(bytes),4))
        throw std::runtime_error("Truncated command fixture");
    return std::uint32_t(bytes[0])|std::uint32_t(bytes[1])<<8|
        std::uint32_t(bytes[2])<<16|std::uint32_t(bytes[3])<<24;
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Expected batch command fixture");
        std::ifstream input(argv[1],std::ios::binary);
        if(!input)throw std::runtime_error("Cannot open command fixture");
        const auto frames=read32(input);
        if(frames>4096)throw std::runtime_error("Too many command fixtures");
        std::printf("[");
        for(std::uint32_t frame=0;frame<frames;++frame) {
            const auto words=read32(input);
            if(words>65536)throw std::runtime_error("Command fixture too large");
            ntr::gx_set_texture_resolver(nullptr,nullptr);ntr::gx_reset();
            const auto initial_hash=ntr::gx_state_hash();
            for(std::uint32_t word=0;word<words;++word) {
                const auto port=read32(input),value=read32(input);
                if(port) {
                    if(port<0x10 || port>0x72)throw std::runtime_error("Invalid command port");
                    ntr::gx_write_port(0x04000400+port*4,value);
                } else ntr::gx_write_fifo(value);
            }
            ntr::GxStackState stack{};const bool stack_available=ntr::gx_get_stack_state(stack);
            const auto diagnostic=ntr::gx_get_stream_diagnostics();std::size_t count=0;
            const auto* triangles=ntr::gx_polygons(count);
            std::printf("%s{\"valid\":%s,\"failure\":%u,\"command\":%u,"
                "\"fifo_pending\":%u,\"port_pending\":%u,\"stack_available\":%s,"
                "\"mode\":%u,\"position_depth\":%d,\"projection_depth\":%d,"
                "\"initial_position_hash\":%u,\"position_hash\":%u,\"vertices\":[",
                frame?",":"",ntr::gx_asset_stream_valid()?"true":"false",unsigned(diagnostic.failure),
                unsigned(diagnostic.failing_command),unsigned(diagnostic.fifo_commands_remaining),
                unsigned(diagnostic.port_parameters),stack_available?"true":"false",stack.mode,
                stack.position,stack.projection,unsigned(initial_hash),unsigned(ntr::gx_state_hash()));
            bool comma=false;
            for(std::size_t i=0;i<count;++i)for(const auto& v:triangles[i].v) {
                std::printf("%s[%.9g,%.9g,%.9g,%.9g,%.9g]",comma?",":"",
                    double(v.u),double(v.v),double(v.vx),double(v.vy),double(v.vz));comma=true;
            }
            std::printf("]}");
        }
        char extra;
        if(input.read(&extra,1))throw std::runtime_error("Trailing command fixture bytes");
        std::printf("]\n");return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"Texture-generation probe: %s\n",error.what());return 2;
    }
}
