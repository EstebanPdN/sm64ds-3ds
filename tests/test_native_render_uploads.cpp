// Real SDK upload providers and GX decoder checked against temporary local
// fixtures produced by original ROM execution. No source ROM bytes are kept.
#include "sm64ds/game_draw.hpp"
#include "sm64ds/native_video.hpp"
#include "ntr/gx.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
extern "C" {
void func_020555a4(const void*);
void func_0205532c(const std::int32_t*,const std::int32_t*);
void func_0204af3c(void*);
void func_0204c24c(int,int);
void func_0204c194(int,int);
}
namespace {
void require(bool okay,const char* message) {
    if(!okay) {std::fprintf(stderr,"render upload check: %s\n",message);std::exit(1);}
}
std::uint32_t word(const std::vector<char>& bytes,std::size_t& offset) {
    require(offset+4<=bytes.size(),"truncated oracle fixture");
    std::uint32_t value;std::memcpy(&value,bytes.data()+offset,4);offset+=4;return value;
}
void consume(const std::vector<sm64ds::DrawWord>& words) {
    ntr::gx_reset();
    for(const auto& entry:words)
        if(entry.port)ntr::gx_write_port(0x04000400+entry.port*4,entry.value);
        else ntr::gx_write_fifo(entry.value);
    require(ntr::gx_asset_stream_valid(),"real GX decoder rejected complete ROM/native stream");
}
unsigned geometry_check(const std::int32_t* matrix) {
    ntr::gx_write_port(0x04000500,0);
    const std::int16_t vertices[][3]={{0,0,0},{4096,0,0},{0,4096,-4096}};
    for(const auto& vertex:vertices) {
        ntr::gx_write_port(0x0400048c,std::uint16_t(vertex[0])|(std::uint32_t(std::uint16_t(vertex[1]))<<16));
        ntr::gx_write_port(0x0400048c,std::uint16_t(vertex[2]));
    }
    ntr::gx_write_port(0x04000504,0);
    std::size_t count=0;const auto* triangles=ntr::gx_polygons(count);
    require(count==1&&ntr::gx_asset_stream_valid(),"FIFO matrix did not produce triangle");
    for(unsigned vertex=0;vertex<3;++vertex)for(unsigned axis=0;axis<3;++axis) {
        float expected=matrix[9+axis]/4096.0f;
        for(unsigned coordinate=0;coordinate<3;++coordinate)
            expected+=(vertices[vertex][coordinate]/4096.0f)*(matrix[coordinate*3+axis]/4096.0f);
        const auto& actual=triangles[0].v[vertex];
        require(std::fabs((axis==0?actual.vx:axis==1?actual.vy:actual.vz)-expected)<=0.002f,
                "FIFO matrix parameter order changed");
    }
    return 1;
}
}
extern "C" [[noreturn]] void sm64ds_native_missing(const char* message) {
    std::fprintf(stderr,"unexpected native failure: %s\n",message);std::abort();
}
int main(int argc,char** argv) {
    static_assert(std::endian::native==std::endian::little);
    require(argc==2,"argument: temporary ROM oracle fixture");
    std::ifstream input(argv[1],std::ios::binary);require(bool(input),"cannot open oracle fixture");
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    require(bytes.size()>=12&&!std::memcmp(bytes.data(),"RUPL1\0\0\0",8),"invalid oracle fixture");
    std::size_t offset=8;const auto cases=word(bytes,offset);
    unsigned toon_checks=0,matrix_checks=0,material_checks=0,quad_checks=0,sprite_checks=0,triangles=0;
    for(unsigned test=0;test<cases;++test) {
        const auto kind=word(bytes,offset),length=word(bytes,offset),count=word(bytes,offset);
        require(offset+length+std::size_t(count)*8<=bytes.size(),"oracle record outside fixture");
        alignas(16) std::array<char,128> source;source.fill(char(0xa5));
        require(length<=source.size()-32,"source fixture too large");
        std::memcpy(source.data()+16,bytes.data()+offset,length);offset+=length;
        const auto before=source;
        std::vector<sm64ds::DrawWord> expected;
        for(unsigned i=0;i<count;++i) {
            const auto port=word(bytes,offset),value=word(bytes,offset);expected.push_back({port,value});
        }
        sm64ds_draw_reset();sm64ds::native_video_reset();
        if(kind==0) {
            require(length==64&&count==0,"TOON extent");
            auto* registers=static_cast<unsigned char*>(sm64ds::native_video_pointer(0x04000000,0x2000));
            std::memset(registers,0x5a,0x2000);
            func_020555a4(source.data()+16);
            for(unsigned i=0;i<0x2000;++i)
                require(registers[i]==(i>=0x380&&i<0x3c0?static_cast<unsigned char>(source[16+i-0x380]):0x5a),"TOON direction or register canary");
            ++toon_checks;
        } else if(kind==1) {
            require(length==48&&count==13,"FIFO matrix extent");
            auto* matrix=reinterpret_cast<std::int32_t*>(source.data()+16);
            func_0205532c(matrix,matrix+9);++matrix_checks;
        } else if(kind==2) {
            require(length==20&&count==8,"material extent");
            func_0204af3c(source.data()+16);++material_checks;
        } else if(kind==3) {
            require(length==12&&count==10,"quad extent");
            const auto* args=reinterpret_cast<const int*>(source.data()+16);
            if(args[0])func_0204c194(args[1],args[2]);else func_0204c24c(args[1],args[2]);
            ++quad_checks;
        } else {require(kind==4&&length==0,"unknown fixture kind");++sprite_checks;}
        require(source==before,"source/canaries changed by render submission");
        require(sm64ds::native_video_ok()&&sm64ds_draw_ok(),"provider hid invalid video/stream state");
        if(kind!=4) {
            require(sm64ds_draw_count()==count,"partial provider stream");
            for(unsigned i=0;i<count;++i) {
                const auto actual=sm64ds_draw_entries()[i];
                require(actual.port==expected[i].port&&actual.value==expected[i].value,"provider word differs from ROM");
            }
        }
        consume(expected);
        if(kind==1)triangles+=geometry_check(reinterpret_cast<const std::int32_t*>(source.data()+16));
    }
    require(offset==bytes.size(),"trailing oracle bytes");
    sm64ds_draw_reset();
    for(unsigned i=0;i<sm64ds::draw_capacity-3;++i)sm64ds_draw_fifo(0);
    const std::int32_t matrix[12]={4096,0,0,0,4096,0,0,0,4096,1,2,3};
    func_0205532c(matrix,matrix+9);
    require(!sm64ds_draw_ok()&&sm64ds_draw_count()==sm64ds::draw_capacity,"matrix overflow hidden");
    std::printf("{\"status\":\"passed\",\"TOON_uploads\":%u,\"FIFO_matrices\":%u,\"material_streams\":%u,\"quads\":%u,\"full_sprite_streams\":%u,\"GX_triangle_checks\":%u,\"stream_overflow\":\"explicit failure\"}\n",
                toon_checks,matrix_checks,material_checks,quad_checks,sprite_checks,triangles);
}
