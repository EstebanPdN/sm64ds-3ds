// BUILD134 register deltas against the unmodified Citro3D 1.7.1 effect.c and
// buffers.c. Two worlds receive the same draws. The reference world is BUILD
// 133: every change goes through the SDK setter, which marks its whole block
// and re-sends it with the next draw. The other world is the cut: a pass
// states its blocks through the SDK, then each draw writes the SDK's copy
// and only the registers of the fields that changed. Both command streams are
// encoded as libctru encodes them and replayed over a PICA register file;
// before every draw the two register files and the two SDK copies must be
// identical, also after the SDK declares the GPU state lost.
#include "internal.h"
#include "pica_state.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace sm64ds::render;
extern "C" {
void oracle_C3D_DepthMap(bool,float,float);
void oracle_C3D_CullFace(GPU_CULLMODE);
void oracle_C3D_StencilTest(bool,GPU_TESTFUNC,int,int,int);
void oracle_C3D_StencilOp(GPU_STENCILOP,GPU_STENCILOP,GPU_STENCILOP);
void oracle_C3D_EarlyDepthTest(bool,GPU_EARLYDEPTHFUNC,u32);
void oracle_C3D_DepthTest(bool,GPU_TESTFUNC,GPU_WRITEMASK);
void oracle_C3D_AlphaTest(bool,GPU_TESTFUNC,int);
void oracle_C3D_AlphaBlend(GPU_BLENDEQUATION,GPU_BLENDEQUATION,GPU_BLENDFACTOR,GPU_BLENDFACTOR,GPU_BLENDFACTOR,GPU_BLENDFACTOR);
void oracle_C3D_FragOpMode(GPU_FRAGOPMODE);
void oracle_C3D_FragOpShadow(float,float);
void oracle_C3D_BlendingColor(u32);
void oracle_C3Di_EffectBind(C3D_Effect*);
void oracle_BufInfo_Init(C3D_BufInfo*);
int oracle_BufInfo_Add(C3D_BufInfo*,const void*,ptrdiff_t,int,u64);
void oracle_C3D_SetBufInfo(C3D_BufInfo*);
void oracle_C3Di_BufInfoBind(C3D_BufInfo*);
}
namespace {
struct World {
    C3D_Context context{};
    std::array<u32,0x400> reg{};
    std::vector<u32> list;
    std::size_t words=0;
};
World reference,cut;World* current=&reference;
u32 random_word(){static u32 v=0x2f6e2b1;v^=v<<13;v^=v>>17;v^=v<<5;return v;}
// PICA command list: parameter, header (register, byte mask, extra parameter
// count, consecutive registers), extra parameters, padded to eight bytes.
void run(World& world){
    auto& list=world.list;assert(list.size()%2==0);
    const auto apply=[&](u32 reg,u32 value,u32 mask){
        assert(reg<world.reg.size());u32 bytes=0;
        for(unsigned b=0;b<4;++b)if(mask&(1u<<b))bytes|=0xffu<<(8*b);
        world.reg[reg]=(world.reg[reg]&~bytes)|(value&bytes);
    };
    for(std::size_t i=0;i<list.size();){
        const u32 parameter=list[i],header=list[i+1];
        const u32 reg=header&0xffff,mask=(header>>16)&15,extra=(header>>20)&0x7ff;const bool consecutive=header>>31;
        apply(reg,parameter,mask);
        for(u32 k=0;k<extra;++k)apply(consecutive?reg+1+k:reg,list[i+2+k],mask);
        i+=2+extra;if(i&1)++i;
    }
    world.words+=list.size();list.clear();
}
// The SDK context update, for the two blocks under test (base.c order).
void update(World& world){
    current=&world;auto& c=world.context;
    if(c.flags&C3DiF_BufInfo){c.flags&=~u32(C3DiF_BufInfo);oracle_C3Di_BufInfoBind(&c.bufInfo);}
    if(c.flags&C3DiF_Effect){c.flags&=~u32(C3DiF_Effect);oracle_C3Di_EffectBind(&c.effect);}
}
struct Stencil {bool enable;GPU_TESTFUNC function;int reference;GPU_STENCILOP depth_fail,pass;};
}
extern "C" {
C3D_Context* C3Di_GetContext(void){return &current->context;}
u32 osConvertVirtToPhys(const void* address){return u32(reinterpret_cast<std::uintptr_t>(address));}
u32 f32tof16(float value){u32 bits;std::memcpy(&bits,&value,4);return (bits>>7)&0xffff;}
u32 f32tof24(float value){u32 bits;std::memcpy(&bits,&value,4);return (bits>>3)&0xffffff;}
// libctru gpu.c GPUCMD_AddInternal.
void state_test_add(u32 header,const u32* values,u32 count){
    auto& list=current->list;assert(count&&count<=0x100);--count;header|=(count&0xff)<<20;
    list.push_back(values[0]);list.push_back(header);
    for(u32 k=0;k<count;++k)list.push_back(values[1+k]);
    if(count&1)list.push_back(0);
}
}
int main(){
    reference.context.flags=cut.context.flags=C3DiF_Active;
    // The block sizes the cut is measured against.
    {World probe;probe.context.flags=C3DiF_Active;current=&probe;
        oracle_C3Di_EffectBind(&probe.context.effect);assert(probe.list.size()==34);probe.list.clear();
        oracle_C3Di_BufInfoBind(&probe.context.bufInfo);assert(probe.list.size()==40);}
    for(unsigned i=0;i<reference.reg.size();++i)reference.reg[i]=cut.reg[i]=random_word();
    // Meshes as the backend creates them: buffer 0 over the SDK's fixed base.
    // The last two are not, and must take the complete table.
    std::array<C3D_BufInfo,34> mesh;
    for(unsigned i=0;i<mesh.size();++i){
        oracle_BufInfo_Init(&mesh[i]);
        const auto address=reinterpret_cast<const void*>(std::uintptr_t(0x18000000u+(random_word()&0x03ffff80u)));
        const unsigned attributes=3+i%3;
        assert(oracle_BufInfo_Add(&mesh[i],address,attributes==3?24:attributes==4?32:36,int(attributes),attributes==3?0x210:attributes==4?0x3210:0x43210)==0);
    }
    assert(oracle_BufInfo_Add(&mesh[32],reinterpret_cast<const void*>(std::uintptr_t(0x18100000u)),24,3,0x210)==1);
    mesh[33].base_paddr=0x18000008u;
    bool resident=false;pica::Deltas deltas;
    unsigned draws=0,passes=0,restores=0,complete_tables=0,largest=0;
    const auto both=[&](auto&& call){current=&reference;call();current=&cut;call();};
    for(unsigned step=0;step<400000;++step){
        // No draw precedes the first pass (FrameState): the registers hold
        // another owner's values until a pass states the blocks.
        const auto roll=step?random_word()%64:0;
        if(roll==0){
            // Citro3DBackend::pass: the SDK setters mark the effect block.
            ++passes;resident=false;
            both([]{oracle_C3D_FragOpMode(GPU_FRAGOPMODE_GL);oracle_C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);
                oracle_C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);oracle_C3D_DepthMap(true,-1.0f,0.0f);
                oracle_C3D_AlphaTest(true,GPU_GREATER,0);oracle_C3D_EarlyDepthTest(false,GPU_EARLYDEPTH_GREATER,0);});
            continue;
        }
        if(roll==1){
            // Applet restore: the registers are lost, the SDK marks its blocks.
            ++restores;
            for(unsigned i=0;i<reference.reg.size();++i)reference.reg[i]=cut.reg[i]=random_word();
            reference.context.flags|=C3DiF_BufInfo|C3DiF_Effect;cut.context.flags|=C3DiF_BufInfo|C3DiF_Effect;
            continue;
        }
        if(roll==2){
            // Another owner's state through the SDK between images.
            const auto colour=random_word();const auto scale=float(random_word()%1000)/1000;
            both([&]{oracle_C3D_BlendingColor(colour);oracle_C3D_FragOpShadow(scale,1-scale);});
            continue;
        }
        // One draw. The material block decides once whether the SDK already
        // has the effect block pending.
        deltas.count=0;
        pica::Deltas* const delta=cut.context.flags&C3DiF_Effect?nullptr:&deltas;
        auto& effect=cut.context.effect;const auto changes=random_word();
        if(changes&1){
            const auto& binding=mesh[random_word()%mesh.size()];
            current=&reference;oracle_C3D_SetBufInfo(const_cast<C3D_BufInfo*>(&binding));
            auto& shadow=cut.context.bufInfo;
            if(resident&&binding.bufCount==1&&shadow.bufCount==1&&binding.base_paddr==shadow.base_paddr){
                shadow.buffers[0]=binding.buffers[0];
                deltas.buffer0(binding.buffers[0].offset,binding.buffers[0].flags[0],binding.buffers[0].flags[1]);
            }else{current=&cut;oracle_C3D_SetBufInfo(const_cast<C3D_BufInfo*>(&binding));resident=binding.bufCount==1;++complete_tables;}
        }
        if(changes&2){
            const auto mode=GPU_CULLMODE(random_word()%3);
            current=&reference;oracle_C3D_CullFace(mode);pica::cull(effect,mode,delta);
        }
        if(changes&4){
            const bool enable=random_word()&1;const auto function=GPU_TESTFUNC(random_word()&7);
            const auto mask=GPU_WRITEMASK(random_word()&31);
            current=&reference;oracle_C3D_DepthTest(enable,function,mask);pica::depth_test(effect,enable,function,mask,delta);
        }
        if(changes&8){
            const Stencil choice[]={{false,GPU_ALWAYS,0,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP},
                {true,GPU_ALWAYS,int(random_word()&255),GPU_STENCIL_REPLACE,GPU_STENCIL_KEEP},
                {true,GPU_EQUAL,int(random_word()&255),GPU_STENCIL_ZERO,GPU_STENCIL_ZERO},
                {bool(random_word()&1),GPU_TESTFUNC(random_word()&7),int(random_word()&255),GPU_STENCILOP(random_word()&7),GPU_STENCILOP(random_word()&7)}};
            const auto& s=choice[random_word()&3];
            current=&reference;oracle_C3D_StencilTest(s.enable,s.function,s.reference,0xff,0xff);
            oracle_C3D_StencilOp(GPU_STENCIL_KEEP,s.depth_fail,s.pass);
            pica::stencil(effect,s.enable,s.function,unsigned(s.reference),0xff,0xff,GPU_STENCIL_KEEP,s.depth_fail,s.pass,delta);
        }
        if(changes&16){
            const int cutoff=int(random_word()&255);const auto function=random_word()&1?GPU_GREATER:GPU_TESTFUNC(random_word()&7);
            current=&reference;oracle_C3D_AlphaTest(true,function,cutoff);pica::alpha_test(effect,true,function,unsigned(cutoff),delta);
        }
        if(changes&32){
            const auto factor=[]{return GPU_BLENDFACTOR(random_word()%15);};
            const auto equation=[]{return GPU_BLENDEQUATION(random_word()%5);};
            const auto ce=equation(),ae=equation();const auto sc=factor(),dc=factor(),sa=factor(),da=factor();
            current=&reference;oracle_C3D_AlphaBlend(ce,ae,sc,dc,sa,da);pica::alpha_blend(effect,ce,ae,sc,dc,sa,da,delta);
        }
        assert(deltas.count<=deltas.words.size()&&deltas.count%2==0);
        // Submission: the SDK update, then this draw's deltas, then the draw.
        update(reference);update(cut);
        cut.list.insert(cut.list.end(),deltas.words.begin(),deltas.words.begin()+deltas.count);
        if(deltas.count>largest)largest=deltas.count;
        run(reference);run(cut);++draws;
        assert(reference.reg==cut.reg);
        assert(reference.context.flags==cut.context.flags);
        assert(!std::memcmp(&reference.context.bufInfo,&cut.context.bufInfo,sizeof(C3D_BufInfo)));
        const auto& a=reference.context.effect;const auto& b=cut.context.effect;
        assert(a.fragOpMode==b.fragOpMode&&a.fragOpShadow==b.fragOpShadow&&a.zScale==b.zScale&&a.zOffset==b.zOffset&&
            a.cullMode==b.cullMode&&a.zBuffer==b.zBuffer&&a.earlyDepth==b.earlyDepth&&a.earlyDepthFunc==b.earlyDepthFunc&&
            a.earlyDepthRef==b.earlyDepthRef&&a.alphaTest==b.alphaTest&&a.stencilMode==b.stencilMode&&a.stencilOp==b.stencilOp&&
            a.depthTest==b.depthTest&&a.blendClr==b.blendClr&&a.alphaBlend==b.alphaBlend&&a.clrLogicOp==b.clrLogicOp);
    }
    assert(draws>300000&&passes>1000&&restores>1000&&complete_tables>1000&&largest>=18&&cut.words<reference.words);
    std::printf("PASS %u draws, %u passes, %u restores: identical PICA registers and SDK copies before every draw; "
        "command words %zu -> %zu (largest delta list %u words)\n",draws,passes,restores,reference.words,cut.words,largest);
}
