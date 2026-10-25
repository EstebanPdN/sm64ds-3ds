#pragma once
#include <cstdint>
namespace sm64ds {
// A FIFO word has port=0. Other entries identify a GX command (0x10..0x72),
// not a memory address. The platform consumes the same ordered native stream.
struct DrawWord { uint32_t port, value; };
constexpr uint32_t draw_capacity=65536;
}
extern "C" {
void sm64ds_draw_reset();
void sm64ds_draw_port(uint32_t command,uint32_t value);
void sm64ds_draw_fifo(uint32_t value);
void sm64ds_draw_list(const void* bytes,uint32_t size);
const sm64ds::DrawWord* sm64ds_draw_entries();
uint32_t sm64ds_draw_count();
bool sm64ds_draw_ok();
}
