#include "internal.h"
#include "indexed_draw_packet.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace sm64ds::render;
namespace {
C3D_Context context{};u32 physical=0;unsigned updates=0;
std::vector<u32> commands;
u32 random_word(){static u32 v=0x17213bcf;v^=v<<13;v^=v>>17;v^=v<<5;return v;}
}
extern "C" {
void oracle_C3D_DrawElements(GPU_Primitive_t,int,int,const void*);
C3D_Context* C3Di_GetContext(void){return &context;}
void C3Di_UpdateContext(void){++updates;}
u32 osConvertVirtToPhys(const void*){return physical;}
void uniform_test_masked_write(unsigned mask,unsigned reg,u32 value){
    commands.push_back(value);commands.push_back((mask<<16)|(reg&0x3ff));
}
void uniform_test_write(unsigned kind,unsigned reg,const u32* values,unsigned count){
    assert(kind==0&&count==1);uniform_test_masked_write(15,reg,values[0]);
}
}
int main(){
    IndexedDrawPacket packet;
    for(unsigned test=0;test<100000;++test){
        // Native allocations have indices after their vertex buffer, within
        // the same bounded linear region. Exercise first-index offsets too.
        context.bufInfo.base_paddr=0x20000000;physical=context.bufInfo.base_paddr+(random_word()&0x1ffffe);
        const auto base_configuration=(physical-context.bufInfo.base_paddr)|0x80000000u;
        const auto first=(random_word()%1000)*3;physical+=first*2;
        const auto count=(random_word()%20000+1)*3;
        context.flags=random_word()&~u32(C3DiF_DrawUsed);const auto before=context.flags;const auto calls=updates;
        commands.clear();oracle_C3D_DrawElements(GPU_TRIANGLES,count,1,nullptr);
        assert(updates==calls+1&&context.flags==(before|C3DiF_DrawUsed));
        packet.range(base_configuration+first*2,count);
        assert(commands.size()==packet.words.size());
        for(unsigned i=0;i<commands.size();++i)assert(commands[i]==packet.words[i]);
    }
    // The SDK guard is handled by our resource/range boundary, not duplicated
    // per draw. The packet is never used for an index before its buffer base.
    physical=context.bufInfo.base_paddr-2;commands.clear();const auto calls=updates;
    oracle_C3D_DrawElements(GPU_TRIANGLES,3,1,nullptr);assert(commands.empty()&&updates==calls);
    std::puts("PASS 100000 native packets equal all30 SDK GPU words; range offsets/context ordering checked");
}
