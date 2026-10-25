#include "sm64ds/game_draw.hpp"
#include <cstddef>
namespace {
sm64ds::DrawWord words[sm64ds::draw_capacity];
uint32_t count;
bool valid=true;
void append(uint32_t port,uint32_t value) {
    if(!valid)return;
    if(count==sm64ds::draw_capacity) {valid=false;return;}
    words[count++]={port,value};
}
}
extern "C" {
void sm64ds_draw_reset() {count=0;valid=true;}
void sm64ds_draw_port(uint32_t command,uint32_t value) {
    if(command<0x10 || command>0x72) {valid=false;return;}
    append(command,value);
}
void sm64ds_draw_fifo(uint32_t value) {append(0,value);}
void sm64ds_draw_list(const void* bytes,uint32_t size) {
    if(!valid)return;
    if((size&3u) || (!bytes && size) || size/4>sm64ds::draw_capacity-count ||
       reinterpret_cast<std::uintptr_t>(bytes)>UINTPTR_MAX-size) {valid=false;return;}
    const auto* data=static_cast<const uint8_t*>(bytes);
    for(uint32_t offset=0;offset<size;offset+=4)
        append(0,uint32_t(data[offset])|uint32_t(data[offset+1])<<8|
                 uint32_t(data[offset+2])<<16|uint32_t(data[offset+3])<<24);
}
const sm64ds::DrawWord* sm64ds_draw_entries() {return words;}
uint32_t sm64ds_draw_count() {return count;}
bool sm64ds_draw_ok() {return valid;}
}
