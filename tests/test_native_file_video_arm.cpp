// ARM-only CPU fixture. Production native_card is included unchanged so this
// translation unit can provide an explicit already-open FSFile/vector seam.
// FS_ReadFile, LoadCompressedFileAt, native_video and LZ16 execute real code.
#include <new>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../source/game_port/native_card.cpp"

extern "C" {
// The Python observer supplies guarded allocations at these two boundaries.
volatile std::uintptr_t test_file_video_allocation_return{};
volatile unsigned test_file_video_free_observer{};
__attribute__((noipa)) void* test_file_video_allocate(unsigned,unsigned) {
    return reinterpret_cast<void*>(test_file_video_allocation_return);
}
__attribute__((noipa)) void test_file_video_free(void*) {
    test_file_video_free_observer=test_file_video_free_observer+1;
}
__attribute__((noipa,noreturn)) void test_file_video_unexpected(const char*) { __builtin_trap(); }

ArchiveRecord data_0208ecf4[13]{};
void* data_020a0ea0{};
std::uint8_t data_020a8074[0x60]{};
std::int8_t data_0208eb54=-1;
std::uint8_t data_0208edf8[4]={'L','Z','7','7'};
char data_0208eb9c[]="original exact-read diagnostic";
std::uint16_t* data_0209d3b8{};
unsigned test_file_video_platform_failures{};
unsigned test_file_video_open_calls{};
unsigned test_file_video_cache_observations{};
unsigned test_file_video_missing_open{};
unsigned test_file_video_current_id{};
const std::uint8_t* test_file_video_current_payload{};
unsigned test_file_video_current_size{};

void test_file_video_initialize() {
    // Normal CRT constructors are outside this isolated CPU executable. These
    // are the same std::list constructors for production's private containers.
    new(&files) std::list<File>();new(&mounts) std::list<Mount>();
    test_file_video_platform_failures=0;test_file_video_open_calls=0;
    test_file_video_cache_observations=0;test_file_video_missing_open=0;
}
void test_file_video_set_payload(unsigned file_id,const std::uint8_t* payload,unsigned size) {
    test_file_video_current_id=file_id;test_file_video_current_payload=payload;test_file_video_current_size=size;
}
int test_file_video_open_vector(void* object,unsigned id) {
    ++test_file_video_open_calls;
    if(!object || id!=test_file_video_current_id || !test_file_video_current_payload) {
        ++test_file_video_missing_open;return 0;
    }
    auto old=find_file(object);if(old!=files.end())close_file(old);
    FS_InitFile(object);
    std::vector<std::uint8_t> payload(test_file_video_current_payload,
        test_file_video_current_payload+test_file_video_current_size);
    files.push_back({object,nullptr,std::move(payload)});
    // Matches the already-open native_card FSFile fields. Neither FAT lookup,
    // libc FILE/TLS, nor filesystem success is being fabricated by this seam.
    word(object,8,0x12345678);word(object,0xc,(word(object,0xc)|0x10)&~0x21u);
    word(object,0x10,6);word(object,0x14,0);
    word(object,0x20,0);word(object,0x24,test_file_video_current_size);word(object,0x28,0);
    word(object,0x2c,0x12345678);word(object,0x30,id);return 1;
}
// This is the declared open-file boundary used by original loader callers.
// It creates a real native_card list node and vector; reads remain untouched.
int func_020185c0(void* object,unsigned id) {
    if(!test_file_video_open_vector(object,id))test_file_video_unexpected("fixture has no such already-open file");
    return 1;
}
void* _ZN6Memory8AllocateEji(unsigned size,int alignment) {
    const auto positive=alignment<0?unsigned(-alignment):unsigned(alignment);
    return test_file_video_allocate(size,positive?positive:4);
}
void* _ZN6Memory8AllocateEj(unsigned size) {return test_file_video_allocate(size,32);}
void _ZN6Memory10DeallocateEPv(void* pointer) {test_file_video_free(pointer);}
void _ZN4Heap11_DeallocateEPv(void*,void*) {test_file_video_unexpected("NARC mount deallocation reached");}
void _ZN4CP1527FlushAndInvalidateDataCacheEjj(unsigned,unsigned) {++test_file_video_cache_observations;}
void _ZN4CP1514FlushDataCacheEjj(unsigned,unsigned) {++test_file_video_cache_observations;}
void func_02018e68(void*) {test_file_video_unexpected("original exact-read diagnostic reached");}
void func_0205d4a0(void*,void*,int) {test_file_video_unexpected("original exact-read recovery reached");}
void Crash() {test_file_video_unexpected("original Crash reached");}
int func_0204ede8(void*,unsigned) {test_file_video_unexpected("resident NARC lookup reached");}
int func_0204eda4(void*,const char*,unsigned) {test_file_video_unexpected("NARC FAT lookup reached");}
[[noreturn]] void sm64ds_native_missing(const char* message) {test_file_video_unexpected(message);}
void sm64ds_draw_fifo(std::uint32_t) {test_file_video_unexpected("GX FIFO write reached");}
void sm64ds_draw_port(std::uint32_t,std::uint32_t) {test_file_video_unexpected("GX port write reached");}
bool sm64ds_draw_ok() {test_file_video_unexpected("GX query reached");}
}

void* operator new(std::size_t bytes) {
    void* result=test_file_video_allocate(unsigned(bytes),16);
    if(!result)test_file_video_unexpected("fixture C++ allocation failed");
    return result;
}
void operator delete(void* pointer) noexcept {test_file_video_free(pointer);}
void operator delete(void* pointer,std::size_t) noexcept {test_file_video_free(pointer);}

namespace sm64ds {
void native_platform_fail(const char*) {++test_file_video_platform_failures;}
void native_platform_irq_set_master(bool) {test_file_video_unexpected("IRQ service reached");}
bool native_video_gx_status(std::uint32_t&) {test_file_video_unexpected("GX status reached");}
GameFiles* bound_native_game_files() {return nullptr;}
int AssetPack::find(const std::string&) const {test_file_video_unexpected("pack lookup outside opened-file seam");}
bool AssetPack::read(std::size_t,std::vector<std::uint8_t>&,std::string&) {
    test_file_video_unexpected("pack read outside opened-file seam");
}
bool parse_narc(const std::vector<std::uint8_t>&,std::vector<NarcMember>&,std::string&) {
    test_file_video_unexpected("NARC parser outside opened-file seam");
}
}
extern "C" {
void* _ZN4Heap9_AllocateEji(void*,unsigned,int) {test_file_video_unexpected("NARC heap allocation reached");}
int func_0204ee40(void*,const char*,void*) {test_file_video_unexpected("NARC registration reached");}
int func_0204ee10(void*) {test_file_video_unexpected("NARC unregistration reached");}
}
