// BUILD134 uniform residency against the unmodified Citro3D 1.7.1 uniforms.c.
// The SDK sends a program's constant table and the boolean word on every
// bind. With residency on, native_uniforms.cpp leaves both out while the GPU
// still holds them. Both emissions are replayed over a model of the PICA
// uniform registers (index/format, the data FIFO, integers, booleans); after
// every update the two models and the two sets of SDK dirty flags must be
// identical, also after the SDK declares the GPU state lost.
#include "citro3d.h"
#include "native_uniforms.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" {
extern C3D_FVec oracle_C3D_FVUnif[2][96];
extern C3D_IVec oracle_C3D_IVUnif[2][4];
extern u16 oracle_C3D_BoolUnifs[2];
extern bool oracle_C3D_FVUnifDirty[2][96],oracle_C3D_IVUnifDirty[2][4],oracle_C3D_BoolUnifsDirty[2];
void oracle_C3D_UpdateUniforms(GPU_SHADER_TYPE);
void oracle_C3Di_DirtyUniforms(GPU_SHADER_TYPE);
void oracle_C3Di_LoadShaderUniforms(shaderInstance_s*);
void oracle_C3Di_ClearShaderUniforms(GPU_SHADER_TYPE);
}
namespace {
// One shader stage's uniform registers (3dbrew GPU/Internal_Registers):
// boolean word, four integers, float index + format, eight data registers
// feeding one FIFO that stores a row per 4 (float32) or 3 (float24) words.
struct Stage {
    u32 boolean=0;std::array<u32,4> integer{};
    std::array<std::array<u32,4>,96> row{};std::array<unsigned char,96> format{};
    u32 index=0;bool float32=false;std::array<u32,4> fifo{};unsigned held=0;
    // The upload index (register 0x2C0 and the pointer behind it) is left
    // where the last upload ended. A skipped table leaves it elsewhere; every
    // upload sets it first, and no draw reads it.
    bool operator==(const Stage& b) const {
        return boolean==b.boolean&&integer==b.integer&&row==b.row&&format==b.format&&!held&&!b.held;
    }
};
struct Gpu {
    std::array<Stage,2> stage{};std::size_t words=0;
    void write(u32 reg,u32 value){
        const bool geometry=reg<GPUREG_VSH_BOOLUNIFORM;auto& s=stage[geometry];
        const u32 local=reg-(geometry?GPUREG_GSH_BOOLUNIFORM:GPUREG_VSH_BOOLUNIFORM);
        if(local==0)s.boolean=value;
        else if(local<=4)s.integer[local-1]=value;
        else if(local==0x10){s.index=value&0xff;s.float32=value>>31;s.held=0;}
        else{
            assert(local>=0x11&&local<=0x18&&s.index<96);
            s.fifo[s.held++]=value;
            if(s.held==(s.float32?4u:3u)){
                s.row[s.index]={s.fifo[0],s.fifo[1],s.fifo[2],s.float32?s.fifo[3]:0u};s.format[s.index]=s.float32;
                ++s.index;s.held=0;
            }
        }
    }
};
Gpu gpu[2];unsigned destination=0;
u32 random_word(){static u32 v=0x51f15e7;v^=v<<13;v^=v>>17;v^=v<<5;return v;}
void same_flags(){
    assert(!std::memcmp(C3D_FVUnif,oracle_C3D_FVUnif,sizeof(C3D_FVUnif)));
    assert(!std::memcmp(C3D_IVUnif,oracle_C3D_IVUnif,sizeof(C3D_IVUnif)));
    assert(!std::memcmp(C3D_BoolUnifs,oracle_C3D_BoolUnifs,sizeof(C3D_BoolUnifs)));
    assert(!std::memcmp(C3D_FVUnifDirty,oracle_C3D_FVUnifDirty,sizeof(C3D_FVUnifDirty)));
    assert(!std::memcmp(C3D_IVUnifDirty,oracle_C3D_IVUnifDirty,sizeof(C3D_IVUnifDirty)));
    assert(!std::memcmp(C3D_BoolUnifsDirty,oracle_C3D_BoolUnifsDirty,sizeof(C3D_BoolUnifsDirty)));
}
unsigned updates=0;
void draw(GPU_SHADER_TYPE stage){
    destination=0;oracle_C3D_UpdateUniforms(stage);destination=1;C3D_UpdateUniforms(stage);
    same_flags();assert(gpu[0].stage==gpu[1].stage);++updates;
}
}
// kind 0: one write, 1: the same register repeatedly, 2: consecutive registers.
extern "C" void uniform_test_write(unsigned kind,unsigned reg,const u32* values,unsigned count){
    auto& g=gpu[destination];g.words+=(count+2)&~1u; // parameter, header, extras, padding
    for(unsigned k=0;k<count;++k)g.write(kind==2?reg+k:reg,values[k]);
}
int main(){
    sm64ds_native_uniform_ranges(true);sm64ds_native_uniform_residency(true);
    DVLE_s dvle[2]={{DVLE_VERTEX_SHADER},{DVLE_GEOMETRY_SHADER}};
    // Per stage: the renderer's case (two programs with equal tables, a third
    // with other constants above the dynamic rows), a table over rows the
    // draws also write, one too long to remember, and an empty one.
    std::array<std::array<float24Uniform_s,20>,6> table[2];shaderInstance_s shader[2][6]{};
    const unsigned counts[6]={12,12,3,5,20,0};
    for(unsigned e=0;e<2;++e)for(unsigned t=0;t<6;++t){
        auto& s=shader[e][t];s.dvle=&dvle[e];s.float24Uniforms=table[e][t].data();s.numFloat24Uniforms=uint8_t(counts[t]);
        for(unsigned i=0;i<counts[t];++i){auto& c=table[e][t][i];
            c.id=t<2?79+i:t==2?82+i*4:t==3?i*7:70+i;for(auto& v:c.data)v=random_word()&0xffffff;}
    }
    for(unsigned e=0;e<2;++e)table[e][1]=table[e][0];
    for(unsigned e=0;e<2;++e)for(unsigned i=0;i<96;++i){
        u32 words[4]={random_word(),random_word(),random_word(),random_word()};
        std::memcpy(&C3D_FVUnif[e][i],words,sizeof(words));std::memcpy(&oracle_C3D_FVUnif[e][i],words,sizeof(words));
    }
    unsigned binds=0,losses=0,forgets=0;
    for(unsigned step=0;step<300000;++step){
        const auto stage=GPU_SHADER_TYPE(random_word()&1u);const auto op=random_word()%32;
        if(op<12){
            // A draw's dynamic uniforms: mostly the rows below the constants.
            const unsigned limit=random_word()%8?79:96;
            for(unsigned n=random_word()%6;n--;){const unsigned i=random_word()%limit;
                C3D_FVUnifDirty[stage][i]=oracle_C3D_FVUnifDirty[stage][i]=true;
                u32 words[4]={random_word(),random_word(),random_word(),random_word()};
                std::memcpy(&C3D_FVUnif[stage][i],words,sizeof(words));std::memcpy(&oracle_C3D_FVUnif[stage][i],words,sizeof(words));
            }
            if(random_word()&1){const unsigned i=random_word()&3;
                C3D_IVUnif[stage][i]=oracle_C3D_IVUnif[stage][i]=random_word();
                C3D_IVUnifDirty[stage][i]=oracle_C3D_IVUnifDirty[stage][i]=true;}
            if(random_word()&1){
                // C3D_BoolUnifSet: often the value it already has.
                const u16 value=random_word()&3?C3D_BoolUnifs[stage]:u16(C3D_BoolUnifs[stage]^(1u<<(random_word()%15)));
                C3D_BoolUnifs[stage]=oracle_C3D_BoolUnifs[stage]=value;
                C3D_BoolUnifsDirty[stage]=oracle_C3D_BoolUnifsDirty[stage]=true;}
        }else if(op<28){
            // C3D_BindProgram: the bound program again, or a switch.
            auto& s=shader[stage][random_word()%4?random_word()%2:random_word()%6];
            if(random_word()%16==0){s.boolUniforms=u16(random_word());s.boolUniformMask=u16(random_word());
                s.intUniformMask=uint8_t(random_word()&15u);for(auto& v:s.intUniforms)v=random_word();}
            else{s.boolUniformMask=0;s.intUniformMask=0;}
            C3Di_LoadShaderUniforms(&s);oracle_C3Di_LoadShaderUniforms(&s);++binds;
        }else if(op==28){
            // Applet restore: the registers are lost and the SDK says so.
            ++losses;
            for(unsigned g=0;g<2;++g){auto& s=gpu[g].stage[stage];s=Stage{};}
            const auto garbage=random_word();
            for(unsigned g=0;g<2;++g){auto& s=gpu[g].stage[stage];s.boolean=garbage;
                for(unsigned i=0;i<96;++i)s.row[i]={garbage+i,garbage^i,garbage*i,garbage-i};}
            C3Di_DirtyUniforms(stage);oracle_C3Di_DirtyUniforms(stage);
        }else if(op==29){C3Di_ClearShaderUniforms(stage);oracle_C3Di_ClearShaderUniforms(stage);}
        else if(op==30){sm64ds_native_uniform_forget();++forgets;} // start of an image
        else{
            // A table rewritten in place between binds must be sent again.
            auto& c=table[stage][random_word()%4][random_word()%3];c.data[random_word()%3]=random_word()&0xffffff;
            if(random_word()&1)table[stage][1]=table[stage][0];
        }
        same_flags();draw(stage);
        if(step%5==0){draw(GPU_SHADER_TYPE(unsigned(stage)^1u));draw(stage);}
    }
    assert(binds>100000&&losses>5000&&forgets>5000&&gpu[1].words<gpu[0].words);
    // Off: the SDK's emission, word for word (also covered by sdk_uniforms).
    sm64ds_native_uniform_residency(false);
    for(unsigned step=0;step<20000;++step){
        const auto stage=GPU_SHADER_TYPE(random_word()&1u);auto& s=shader[stage][random_word()%6];
        C3Di_LoadShaderUniforms(&s);oracle_C3Di_LoadShaderUniforms(&s);
        const auto before0=gpu[0].words,before1=gpu[1].words;draw(stage);
        assert(gpu[0].words-before0==gpu[1].words-before1);
    }
    std::printf("PASS %u updates, %u binds, %u losses: identical uniform registers and SDK flags; command words %zu -> %zu\n",
        updates,binds,losses,gpu[0].words,gpu[1].words);
}
