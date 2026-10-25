#include "sm64ds/native_video.hpp"
#include <cstdint>

extern "C" {
void* func_0201817c(std::uint16_t file_id);
std::uint32_t func_02018ac4(void* buffer);
void DecompressLZ16(void* source, void* destination);
void Deallocate(void* allocation);
extern std::uint8_t data_0208edf8[];
int func_0206e28c(const std::uint8_t* first, const std::uint8_t* second,
                std::uint32_t bytes);

// Original four-byte LZ77 marker check. LoadFileAt's raw-read caller keeps
// its DS destination in r0 even after FS_ReadFile has copied the payload into
// native video storage. Translate that one read boundary before running the
// original byte comparison; normal allocation pointers are unchanged.
int func_02018568(std::uintptr_t address) {
    auto* source=reinterpret_cast<const std::uint8_t*>(address);
    if(address>=0x04000000 && address<0x08000000) {
        source=static_cast<const std::uint8_t*>(
            sm64ds::native_video_pointer(static_cast<std::uint32_t>(address),4));
        if(!source)return 0;
    }
    return func_0206e28c(source,data_0208edf8,4)==0;
}

// HUD's direct decompression calls retain DS OBJ addresses, while Minimap
// normally receives a native pointer from G2S. Keep this adapter local to the
// reviewed display callers; the general-purpose LZ16 primitive is unchanged.
void sm64ds_decompress_lz16_video(void* source, void* target) {
    const auto address=reinterpret_cast<std::uintptr_t>(target);
    if(address>=0x04000000 && address<0x08000000) {
        const auto bytes=func_02018ac4(source);
        target=sm64ds::native_video_pointer(static_cast<std::uint32_t>(address),bytes);
        if(!target)return;
    }
    DecompressLZ16(source,target);
}

// Pinned original LoadCompressedFileAt, retaining its loader, size, LZ16
// decompressor and ownership. Stage's literal sub-OBJ destination is a DS
// video alias; resolve the declared output span before the original writes.
std::uint32_t LoadCompressedFileAt(std::uint16_t file_id, void* target) {
    void* source=func_0201817c(file_id);
    if(!source)return 0;
    const auto size=func_02018ac4(source);
    const auto address=reinterpret_cast<std::uintptr_t>(target);
    if(address>=0x04000000 && address<0x08000000) {
        target=sm64ds::native_video_pointer(static_cast<std::uint32_t>(address),size);
        if(!target) {Deallocate(source);return 0;}
    }
    DecompressLZ16(source,target);
    Deallocate(source);
    return size;
}
}
