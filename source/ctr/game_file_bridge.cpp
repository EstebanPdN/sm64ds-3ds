#include <cstdint>
#include <cstring>

namespace Memory { void* Allocate(unsigned int size,int alignment); }
extern "C" unsigned int data_0209d3bc;
// Platform I/O owns this span until the next request. Keeping I/O at one seam
// lets instruction-level tests supply ROM bytes without replacing the game's
// allocation, copy, SharedFilePtr ownership, or reference-count operations.
extern "C" bool sm64ds_game_file_span(uint16_t,const void**,uint32_t*);
extern "C" void sm64ds_game_file_allocation_failed();

extern "C" void* _ZN13SharedFilePtr4LoadEv(void* self) {
    static_assert(sizeof(void*)==4,"Original SharedFilePtr has 32-bit fields");
    if(!self) return nullptr;
    uint16_t id;
    std::memcpy(&id,self,2);data_0209d3bc=id;
    const void* bytes=nullptr;uint32_t size=0;
    if(!sm64ds_game_file_span(id,&bytes,&size) || !bytes || !size) return nullptr;
    void* result=Memory::Allocate(size,32);
    if(!result) {sm64ds_game_file_allocation_failed();return nullptr;}
    std::memcpy(result,bytes,size);
    std::memcpy(static_cast<uint8_t*>(self)+4,&result,4);
    return result;
}
