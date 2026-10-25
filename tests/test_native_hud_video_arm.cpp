// CPU-only display-resource fixture. Python supplies verified raw/NARC file
// buffers at LoadFile, guarded ownership, and declared scene/input objects.
// Display callers, SDK video providers, LZ16, arithmetic and metadata execute
// their production/original instructions. No FS/GPU/Stage lifecycle claim.
#include <cstdint>
#include <cstddef>
#include "sm64ds/native_video.hpp"
#include "sm64ds/game_draw.hpp"
#include "ntr/gx.h"

extern "C" {
__attribute__((noipa,noreturn)) void test_hud_unexpected(const char*) {__builtin_trap();}
__attribute__((noipa)) void* LoadFile(unsigned) {test_hud_unexpected("LoadFile observer missing");}
__attribute__((noipa)) void Deallocate(void*) {test_hud_unexpected("free observer missing");}
unsigned test_hud_language{},test_hud_failures{},test_hud_scene_query{1};
int GetOwnerLanguage() {return int(test_hud_language);}
int test_hud_scene_active(void*) {return int(test_hud_scene_query);}
std::uint8_t data_0209f2d8{},data_0209f2fc{},data_0209f250{},data_0209f2e8{},data_0209f25c{};
std::int8_t data_0209f2f8{},data_02092120{},data_02092110{-1};
std::uint8_t data_0209f220{},data_0209d454{},data_ov002_0211117c{},data_ov002_02111150{};
std::int8_t data_ov002_02111178{},data_ov002_02111184{},data_ov002_02111148{};
std::int16_t data_ov002_02111188{},data_02092144[4]{},data_0209d47c[4]{},data_0209d470[4]{};
std::uint16_t data_020a0f1c{},data_020a6088[13]{},data_02099fcc{},data_020a6084{};
std::uint32_t data_020a60a4{},data_020a60a8{},data_0209caa0[4]{},data_0209b454{};
std::int32_t data_0209fc48{},data_0209d4b0{},data_0208ee44{1};
std::uint8_t data_020a1040[0x60]{},data_0209cab4[0x20]{},SUBLEVEL_LEVEL_TABLE[0x40]{};
void* data_0209f394[4]{},*data_0209f340{},*data_0209f334{},*data_0209f348{},*data_0209f314{};
void* data_0209f318{},*data_0209f5bc{},*data_0209f40c[12]{},*data_0209f3e8[9]{},*data_0209f33c{},*data_0209f3a4[8]{};
std::uint8_t data_0209f350[4]{},data_0209f2c4{},data_0209f20c{},data_0209f294{},data_0209f204{};
std::uint8_t data_0209d660{},data_020a0e40{},data_0209f4ac[0x60]{},data_0209f4a8[0x60]{},data_0209f4a9[0x60]{},data_0209f4ae[0x60]{};
std::uint8_t data_ov002_0211114c{},data_ov002_02111144{},data_0209f37c[12]{};
std::int16_t data_02082214[0x2000]{};
std::int32_t data_ov002_0211116c[3]{};
std::uint8_t data_0209f3c4[0x24]{},data_ov002_0211064c[0x18]{},data_ov002_02110664[0x18]{};
int _ZN5Event6GetBitEj(unsigned) {test_hud_unexpected("event query outside castle fixture");}
[[noreturn]] void sm64ds_native_missing(const char* message) {test_hud_unexpected(message);}
void sm64ds_draw_fifo(std::uint32_t) {test_hud_unexpected("GX FIFO outside resource fixture");}
void sm64ds_draw_port(std::uint32_t,std::uint32_t) {test_hud_unexpected("GX port outside resource fixture");}
bool sm64ds_draw_ok() {test_hud_unexpected("GX status outside resource fixture");}
const sm64ds::DrawWord* sm64ds_draw_entries() {test_hud_unexpected("GX entries outside resource fixture");}
std::uint32_t sm64ds_draw_count() {test_hud_unexpected("GX count outside resource fixture");}
}
namespace sm64ds {
void native_platform_fail(const char*) {++test_hud_failures;}
void native_platform_irq_set_master(bool) {test_hud_unexpected("IRQ outside resource fixture");}
}
namespace ntr {
bool gx_get_stack_state(GxStackState&) {test_hud_unexpected("GX stack outside resource fixture");}
int gx_command_parameter_count(std::uint8_t) {test_hud_unexpected("GX command outside resource fixture");}
}
