#include "sm64ds/game_draw.hpp"
// These are synchronous geometry submissions. No DS DMA/IRQ is required to
// append a display list to the native command buffer. All other game code,
// including its material selection and bone transforms, remains the caller.
extern "C" {
void Geometry_MatrixMultiply3x3(const uint32_t* matrix) {
    sm64ds_draw_fifo(0x1a);
    for(unsigned i=0;i<9;++i)sm64ds_draw_fifo(matrix[i]);
}
void func_020553a4(const uint32_t* matrix) {
    sm64ds_draw_fifo(0x17);
    for(unsigned i=0;i<12;++i)sm64ds_draw_fifo(matrix[i]);
}
void func_02055388(const uint32_t* matrix) {
    sm64ds_draw_fifo(0x19);
    for(unsigned i=0;i<12;++i)sm64ds_draw_fifo(matrix[i]);
}
void func_02055998(const uint32_t* matrix) {
    sm64ds_draw_port(0x10,3);
    for(unsigned i=0;i<16;++i)sm64ds_draw_port(0x16,i<12 ? matrix[i]<<4 : matrix[i]);
}
void func_01ffde98(uint32_t,const void* source,uint32_t size) {
    sm64ds_draw_list(source,size);
}
}
