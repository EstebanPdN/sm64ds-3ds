#include "citro3d.h"
#include "native_uniforms.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
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
std::vector<u32> emitted[2];unsigned destination=0,updates=0;
u32 random_word(){static u32 v=0x9247135;v^=v<<13;v^=v>>17;v^=v<<5;return v;}
void same(){
    assert(!std::memcmp(C3D_FVUnif,oracle_C3D_FVUnif,sizeof(C3D_FVUnif)));
    assert(!std::memcmp(C3D_IVUnif,oracle_C3D_IVUnif,sizeof(C3D_IVUnif)));
    assert(!std::memcmp(C3D_BoolUnifs,oracle_C3D_BoolUnifs,sizeof(C3D_BoolUnifs)));
    assert(!std::memcmp(C3D_FVUnifDirty,oracle_C3D_FVUnifDirty,sizeof(C3D_FVUnifDirty)));
    assert(!std::memcmp(C3D_IVUnifDirty,oracle_C3D_IVUnifDirty,sizeof(C3D_IVUnifDirty)));
    assert(!std::memcmp(C3D_BoolUnifsDirty,oracle_C3D_BoolUnifsDirty,sizeof(C3D_BoolUnifsDirty)));
}
void draw(GPU_SHADER_TYPE stage){
    emitted[0].clear();emitted[1].clear();destination=0;C3D_UpdateUniforms(stage);
    destination=1;oracle_C3D_UpdateUniforms(stage);assert(emitted[0]==emitted[1]);same();++updates;
}
}
extern "C" void uniform_test_write(unsigned kind,unsigned reg,const u32* values,unsigned count){
    auto& out=emitted[destination];out.push_back(kind);out.push_back(reg);out.push_back(count);
    out.insert(out.end(),values,values+count);
}
int main(int argc,char** argv){
    assert(argc==2);sm64ds_native_uniform_ranges(std::atoi(argv[1])!=0);
    // Unique payloads prove byte order, run start/end and stage offsets.
    for(unsigned e=0;e<2;++e)for(unsigned i=0;i<96;++i){
        u32 words[4]={random_word(),random_word(),random_word(),random_word()};
        std::memcpy(&C3D_FVUnif[e][i],words,sizeof(words));
        std::memcpy(&oracle_C3D_FVUnif[e][i],words,sizeof(words));
    }
    // Every16-bit pattern, crossing every kind of4/16-byte search boundary.
    for(unsigned origin:{0u,7u,15u,31u,63u,80u})for(unsigned mask=0;mask<65536;++mask){
        for(unsigned i=0;i<96;++i)C3D_FVUnifDirty[0][i]=oracle_C3D_FVUnifDirty[0][i]=false;
        for(unsigned b=0;b<16;++b)C3D_FVUnifDirty[0][origin+b]=oracle_C3D_FVUnifDirty[0][origin+b]=bool(mask&(1u<<b));
        draw(GPU_VERTEX_SHADER);
    }
    DVLE_s dvle[2]={{DVLE_VERTEX_SHADER},{DVLE_GEOMETRY_SHADER}};
    std::array<float24Uniform_s,12> constants[2];shaderInstance_s shader[2]{};
    for(unsigned e=0;e<2;++e){shader[e].dvle=&dvle[e];shader[e].float24Uniforms=constants[e].data();}
    for(unsigned step=0;step<30000;++step){
        const auto stage=GPU_SHADER_TYPE(random_word()&1u);const auto op=random_word()%6;
        if(op<=1){
            for(unsigned i=0;i<96;++i)if((random_word()&7u)==0){
                C3D_FVUnifDirty[stage][i]=oracle_C3D_FVUnifDirty[stage][i]=true;
                u32 words[4]={random_word(),random_word(),random_word(),random_word()};
                std::memcpy(&C3D_FVUnif[stage][i],words,sizeof(words));
                std::memcpy(&oracle_C3D_FVUnif[stage][i],words,sizeof(words));
            }
            for(unsigned i=0;i<4;++i)if(random_word()&1u){
                C3D_IVUnif[stage][i]=oracle_C3D_IVUnif[stage][i]=random_word();
                C3D_IVUnifDirty[stage][i]=oracle_C3D_IVUnifDirty[stage][i]=true;
            }
            C3D_BoolUnifs[stage]=oracle_C3D_BoolUnifs[stage]=u16(random_word());
            C3D_BoolUnifsDirty[stage]=oracle_C3D_BoolUnifsDirty[stage]=true;
        }else if(op==2){C3Di_DirtyUniforms(stage);oracle_C3Di_DirtyUniforms(stage);}
        else if(op==3){C3Di_ClearShaderUniforms(stage);oracle_C3Di_ClearShaderUniforms(stage);}
        else if(op==4){
            auto& s=shader[stage];s.boolUniforms=u16(random_word());s.boolUniformMask=u16(random_word());
            s.intUniformMask=uint8_t(random_word()&15u);for(auto& v:s.intUniforms)v=random_word();
            s.numFloat24Uniforms=uint8_t(random_word()%13);
            for(auto& c:constants[stage]){c.id=random_word()%96;for(auto& v:c.data)v=random_word();}
            C3Di_LoadShaderUniforms(&s);oracle_C3Di_LoadShaderUniforms(&s);
        }
        same();draw(stage);
        if(step%7==0){draw(GPU_SHADER_TYPE(unsigned(stage)^1u));draw(stage);}
    }
    // Restore previously dirty floats after constants and after clearing GSH.
    for(auto stage:{GPU_VERTEX_SHADER,GPU_GEOMETRY_SHADER}){
        C3Di_DirtyUniforms(stage);oracle_C3Di_DirtyUniforms(stage);same();draw(stage);draw(stage);
    }
    std::printf("PASS SDK1.7.1 differential: %u updates, mode=%s\n",updates,argv[1]);
}
