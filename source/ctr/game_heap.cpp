#include "sm64ds/game_heap.hpp"

#include "Heap.h"

#include <cstdint>
#include <cstdlib>
#include <malloc.h>

// The BSS generator supplies these original symbols, including the alias of
// Memory::defaultHeapPtr and data_020a0ea0. Never allocate a second copy.
extern "C" Heap* data_020a0e9c;
namespace Memory { extern Heap* defaultHeapPtr; }

// The heap-only object bundle renames the original game's delete symbols so
// they cannot capture STL allocations made by the native platform shell.
// GCC's deleting destructors may request sized delete; the DS allocator uses
// its own allocation header and does not need the redundant size argument.
extern "C" void sm64ds_game_operator_delete(void* ptr);
extern "C" void sm64ds_game_operator_delete_sized(void* ptr, std::size_t) {
    sm64ds_game_operator_delete(ptr);
}

namespace sm64ds {

bool initialize_game_root_heap_in_place(void* arena, std::size_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(arena);
    if (!arena || (address & 31u) || bytes < 0x64u ||
        bytes > UINT32_MAX || address > UINTPTR_MAX - bytes ||
        data_020a0e9c || Memory::defaultHeapPtr) {
        return false;
    }

    // Heap::SetupRootHeap normally obtains the arena from the DS OS. Its
    // downstream constructor and allocator are platform-independent and kept
    // intact, including their native GCC vtables and fail-fast allocation flag.
    Heap* root = Heap::CreateRootHeap(arena, static_cast<u32>(bytes));
    if (!root) return false;
    data_020a0e9c = root;
    Memory::defaultHeapPtr = root;
    return true;
}

bool initialize_game_root_heap(std::size_t bytes) {
    if (bytes < 0x64u || bytes > UINT32_MAX || data_020a0e9c ||
        Memory::defaultHeapPtr) return false;
    void* arena = memalign(32, bytes);
    if (!arena) return false;
    if (!initialize_game_root_heap_in_place(arena, bytes)) {
        std::free(arena);
        return false;
    }
    // The game uses this heap until process exit, just as the original root
    // arena does. Child game heaps still use Heap::CreateExpandingHeap.
    return true;
}

Heap* native_game_root_heap() { return data_020a0e9c; }

bool verify_game_root_heap() {
    Heap* root=native_game_root_heap();
    if(!root)return false;
    const auto before=root->VMemoryLeft();
    void* block=root->Allocate(4096,32);
    if(!block)return false;
    auto* bytes=static_cast<volatile unsigned char*>(block);
    bool valid=(reinterpret_cast<std::uintptr_t>(block)&31u)==0;
    for(unsigned i=0;i<4096;++i)bytes[i]=static_cast<unsigned char>(i^0xa5u);
    for(unsigned i=0;i<4096;++i)
        valid=valid && bytes[i]==static_cast<unsigned char>(i^0xa5u);
    root->Deallocate(block);
    return valid && root->VMemoryLeft()==before;
}

} // namespace sm64ds
