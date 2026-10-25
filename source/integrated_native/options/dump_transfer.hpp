#pragma once
#include <cstddef>
#include <cstdint>
namespace native_diagnostics {
constexpr std::size_t transfer_bytes=65536;
// FS IPC must receive ordinary process RAM, never a mapped VRAM/device pointer.
// Source mappings are page aligned; use word loads for VRAM, not byte I/O.
inline bool read_capture_words(uintptr_t address,void* destination,std::size_t bytes){
    if((address&3u)||(reinterpret_cast<uintptr_t>(destination)&3u)||(bytes&3u))return false;
    auto* out=static_cast<uint32_t*>(destination);
    const auto* in=reinterpret_cast<const volatile uint32_t*>(address);
    for(std::size_t i=0;i<bytes/4;++i)out[i]=in[i];
    return true;
}
template<class Read,class Write,class Progress>
bool transfer_region(uintptr_t address,std::size_t bytes,void* staging,std::size_t capacity,
                     Read read,Write write,Progress progress){
    if(!staging||!capacity||capacity>transfer_bytes||(capacity&3u)||(address&3u)||(bytes&3u))return false;
    if(bytes>UINTPTR_MAX-address)return false;
    for(std::size_t offset=0;offset<bytes;){
        const auto n=bytes-offset<capacity?bytes-offset:capacity;
        if(!read(address+offset,staging,n)||!write(staging,n))return false;
        offset+=n;progress(offset,bytes);
    }
    return true;
}
}
