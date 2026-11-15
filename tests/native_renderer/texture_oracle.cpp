// Host-only differential oracle. Links the unchanged pinned texture decoder,
// never the new renderer or a game executable. No game resources are embedded.
#include "ntr/texture.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
static std::uint32_t read32(const std::uint8_t* p){
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
int main(int argc,char** argv){
    assert(argc==3);
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    assert(data.size()>=36 && data.size()<=16*1024*1024 && !std::memcmp(data.data(),"NTEXOR01",8));
    const auto* p=data.data();
    const auto main=read32(p+24),index=read32(p+28),palette=read32(p+32);
    assert(std::uint64_t(main)+index+palette+36==data.size());
    ntr::TextureDesc d;
    d.format=read32(p+8);d.width=read32(p+12);d.height=read32(p+16);d.color0_transparent=read32(p+20)!=0;
    assert(d.width>0 && d.height>0 && d.width<=1024 && d.height<=1024);
    d.data=p+36;d.data_len=main;d.index=p+36+main;d.index_len=index;
    d.pal=p+36+main+index;d.pal_len=palette;
    std::vector<std::uint32_t> decoded;
    assert(ntr::texture_decode(d,decoded));
    std::vector<std::uint8_t> rgba;rgba.reserve(decoded.size()*4);
    for(auto colour:decoded){
        rgba.push_back(colour>>16);rgba.push_back(colour>>8);rgba.push_back(colour);rgba.push_back(colour>>24);
    }
    std::ofstream output(argv[2],std::ios::binary);
    output.write(reinterpret_cast<const char*>(rgba.data()),rgba.size());assert(output.good());
}
