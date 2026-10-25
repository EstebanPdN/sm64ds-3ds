// Fresh ARM CPU unit contract. Production bodies are included unchanged to
// observe their private native mount/file ownership; this does not import RAM.
#include <new>
#include <cstdint>
#include <cstring>
#include "sm64ds/native_platform.hpp"
#include "../source/game_port/native_card.cpp"

extern "C" {
__attribute__((noipa)) void* test_archive_allocate(unsigned,unsigned) { return nullptr; }
__attribute__((noipa)) void test_archive_free(void*) {}
__attribute__((noipa,noreturn)) void test_archive_unexpected(const char*) { __builtin_trap(); }
ArchiveRecord data_0208ecf4[13]{};
void* data_020a0ea0{};
std::uint8_t data_020a8074[0x60]{};
void* data_020a8048{};
std::uint32_t data_020a804c[3]{};
std::uint8_t data_0209a048[4]={'r','o','m',0};
std::int8_t data_0208eb54=-1;
unsigned test_archive_heap_frees{};
unsigned test_archive_pack_reads{};
const char* test_archive_path{};
const std::uint8_t* test_archive_payload{};
unsigned test_archive_payload_size{};
alignas(sm64ds::AssetPack) unsigned char test_archive_pack[sizeof(sm64ds::AssetPack)];
alignas(sm64ds::GameFiles) unsigned char test_archive_service[sizeof(sm64ds::GameFiles)];
void test_archive_initialize() {
    new(&mounts) std::list<Mount>(); new(&files) std::list<File>();
    auto* pack=new(test_archive_pack) sm64ds::AssetPack();
    auto* service=new(test_archive_service) sm64ds::GameFiles(*pack);
    sm64ds::bind_native_game_files(service);
    sm64ds::native_platform_init(nullptr);
}
void test_archive_set_payload(const char* path,const std::uint8_t* data,unsigned size) {
    test_archive_path=path;test_archive_payload=data;test_archive_payload_size=size;
}
void test_archive_observe(unsigned* result,void* record) {
    auto* mount=find_mount(record);
    result[0]=mounts.size(); result[1]=files.size();
    result[2]=mount?mount->references:0;result[3]=mount?unsigned(mount->unmounted):0;
    result[4]=test_archive_heap_frees;result[5]=test_archive_pack_reads;
}
unsigned long long svcGetSystemTick() { return 0; } // Explicit unit clock input.
void* _ZN4Heap9_AllocateEji(void*,unsigned bytes,int alignment) {
    return test_archive_allocate(bytes,unsigned(alignment<0?-alignment:alignment));
}
void _ZN4Heap11_DeallocateEPv(void*,void* p) { ++test_archive_heap_frees;test_archive_free(p); }
int _ZN4cstd6strlenEPKc(const char* p) { return std::strlen(p); }
void func_0205c788(void*,int) { test_archive_unexpected("SDK active request cancellation"); }
[[noreturn]] void sm64ds_native_missing(const char* p) { test_archive_unexpected(p); }
void sm64ds_draw_fifo(std::uint32_t) { test_archive_unexpected("GX FIFO"); }
void sm64ds_draw_port(std::uint32_t,std::uint32_t) { test_archive_unexpected("GX port"); }
bool sm64ds_draw_ok() { test_archive_unexpected("GX query"); }
}
void* operator new(std::size_t n) {
    auto* p=test_archive_allocate(unsigned(n),16);
    if(!p)test_archive_unexpected("unit allocator exhausted");return p;
}
void operator delete(void* p) noexcept { test_archive_free(p); }
void operator delete(void* p,std::size_t) noexcept { test_archive_free(p); }
namespace sm64ds {
// Explicit raw pack transport seam: bytes/paths are verified from Europe.pak
// by the Python reader. The real GameFiles object and NARC parser are retained.
AssetPack::~AssetPack() {}
int AssetPack::find(const std::string& path) const {
    return test_archive_path && path==test_archive_path?0:-1;
}
bool AssetPack::read(std::size_t index,std::vector<std::uint8_t>& out,std::string& why) {
    if(index || !test_archive_payload){why="unit payload unavailable";return false;}
    ++test_archive_pack_reads;
    out.assign(test_archive_payload,test_archive_payload+test_archive_payload_size);
    why.clear();return true;
}
}
