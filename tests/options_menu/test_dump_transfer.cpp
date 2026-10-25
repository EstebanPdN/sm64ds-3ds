#include "dump_transfer.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>
int main(){
    alignas(32) std::array<uint32_t,native_diagnostics::transfer_bytes/4> staging{};
    constexpr uintptr_t vram=0x1f000000;constexpr std::size_t size=6*1024*1024;
    std::vector<unsigned char> reference(size),written;
    for(std::size_t i=0;i<size;++i)reference[i]=static_cast<unsigned char>((i*17+i/4096)&255);
    unsigned reads=0,writes=0;std::size_t progress=0;
    auto read=[&](uintptr_t source,void* target,std::size_t n){assert(target==staging.data());assert(source>=vram&&source+n<=vram+size);++reads;std::memcpy(target,reference.data()+source-vram,n);return true;};
    auto write=[&](const void* source,std::size_t n){assert(source==staging.data());++writes;const auto* b=static_cast<const unsigned char*>(source);written.insert(written.end(),b,b+n);return true;};
    auto update=[&](std::size_t done,std::size_t total){assert(total==size&&done>progress);progress=done;};
    assert(native_diagnostics::transfer_region(vram,size,staging.data(),sizeof staging,read,write,update));
    assert(written==reference&&reads==96&&writes==96&&progress==size);
    reads=writes=0;
    auto fail_write=[&](const void* source,std::size_t){assert(source==staging.data());return ++writes!=3;};
    assert(!native_diagnostics::transfer_region(vram,size,staging.data(),sizeof staging,read,fail_write,[](auto,auto){}));
    assert(reads==3&&writes==3);
    assert(!native_diagnostics::transfer_region(vram+1,size,staging.data(),sizeof staging,read,write,update));
    alignas(4) uint32_t source[]={0x12345678,0x87654321},target[2]{};
    assert(native_diagnostics::read_capture_words(reinterpret_cast<uintptr_t>(source),target,sizeof source));
    assert(std::memcmp(source,target,sizeof source)==0);
}
