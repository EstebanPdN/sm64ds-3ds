#include "citro3d_program_cache.hpp"
#include "pica_state.hpp"
#include <cstddef>
#include <cstring>

// Layout verified against the detected SDK and authoritative v1.7.1:
// https://github.com/devkitPro/citro3d/blob/v1.7.1/source/internal.h
// https://github.com/devkitPro/citro3d/blob/v1.7.1/source/base.c
// The program pointer and flags are read through the exported context's
// bytes. BUILD134 additionally hands out the SDK's own buffer table, effect
// block, unit pointers and combiner stages (citro3d_shadow), located by the
// public getters and probed through the public setters before any use.
// No GPU/SDK implementation is replaced.
extern "C" unsigned char __C3D_Context[];
extern "C" void C3Di_UpdateContext(void);
namespace sm64ds::render {
namespace {
struct ContextPrefix171 {
    gxCmdQueue_s queue;
    u32* commands;
    std::size_t command_words;
    float command_usage;
    u32 flags;
    shaderProgram_s* program;
    // BUILD134: through the combiner stages (internal.h C3D_Context).
    C3D_AttrInfo attributes;
    C3D_BufInfo buffers;
    Citro3DEffect effect;
    C3D_LightEnv* light_environment;
    u32 texture_configuration,texture_shadow;
    C3D_Tex* texture[3];
    C3D_TexEnv stage[6];
};
static_assert(sizeof(void*)==4&&offsetof(ContextPrefix171,program)==0x24,"Review Citro3D program-cache ABI for this SDK");
static_assert(offsetof(ContextPrefix171,flags)==0x20,"Review Citro3D draw-used flag location");
// Offsets read from the installed libcitro3d.a (C3D_GetAttrInfo adds 0x28,
// C3D_GetBufInfo 0x40, C3D_CullFace stores a byte at 0xe8, C3D_AlphaTest at
// 0xf0, C3D_StencilTest 0xf4, C3D_StencilOp 0xf8, C3D_DepthTest 0xfc,
// C3D_AlphaBlend 0xd8 and 0x104, C3D_TexBind 0x118+4n, C3D_GetTexEnv 0x124+20n).
static_assert(sizeof(GPU_CULLMODE)==1&&sizeof(GPU_EARLYDEPTHFUNC)==1&&sizeof(GPU_LOGICOP)==1&&sizeof(bool)==1,
    "Review the Citro3D effect mirror for this enum ABI");
static_assert(offsetof(ContextPrefix171,attributes)==0x28&&offsetof(ContextPrefix171,buffers)==0x40&&
    offsetof(ContextPrefix171,effect)==0xd8&&sizeof(Citro3DEffect)==52&&
    offsetof(ContextPrefix171,effect)+offsetof(Citro3DEffect,cullMode)==0xe8&&
    offsetof(ContextPrefix171,effect)+offsetof(Citro3DEffect,alphaTest)==0xf0&&
    offsetof(ContextPrefix171,effect)+offsetof(Citro3DEffect,stencilMode)==0xf4&&
    offsetof(ContextPrefix171,effect)+offsetof(Citro3DEffect,stencilOp)==0xf8&&
    offsetof(ContextPrefix171,effect)+offsetof(Citro3DEffect,depthTest)==0xfc&&
    offsetof(ContextPrefix171,effect)+offsetof(Citro3DEffect,alphaBlend)==0x104&&
    offsetof(ContextPrefix171,texture)==0x118&&offsetof(ContextPrefix171,stage)==0x124&&sizeof(C3D_TexEnv)==20&&
    sizeof(C3D_BufCfg)==12&&offsetof(C3D_BufInfo,buffers)==8,"Review the Citro3D shadow layout for this SDK");
// SDK1.7.1 internal.h dirty bits.
static_assert(Citro3DShadow::buffer_table==BIT(3)&&Citro3DShadow::effect_block==BIT(4)&&
    Citro3DShadow::texture1==BIT(23+1)&&Citro3DShadow::texture2==BIT(23+2),"Review Citro3D dirty flags");
static_assert(pica::reg_culling==GPUREG_FACECULLING_CONFIG&&pica::reg_colour_operation==GPUREG_COLOR_OPERATION&&
    pica::reg_blend_function==GPUREG_BLEND_FUNC&&pica::reg_alpha_test==GPUREG_FRAGOP_ALPHA_TEST&&
    pica::reg_stencil_test==GPUREG_STENCIL_TEST&&pica::reg_stencil_operation==GPUREG_STENCIL_OP&&
    pica::reg_depth_colour_mask==GPUREG_DEPTH_COLOR_MASK&&pica::reg_gas_depth==GPUREG_GAS_DELTAZ_DEPTH&&
    pica::reg_buffer0_offset==GPUREG_ATTRIBBUFFER0_OFFSET&&
    GPUREG_ATTRIBBUFFER0_CONFIG1==GPUREG_ATTRIBBUFFER0_OFFSET+1&&GPUREG_ATTRIBBUFFER0_CONFIG2==GPUREG_ATTRIBBUFFER0_OFFSET+2,
    "Review PICA state registers");
static_assert(pica::header(pica::reg_alpha_test)==GPUCMD_HEADER(0,0xF,GPUREG_FRAGOP_ALPHA_TEST)&&
    pica::header(pica::reg_gas_depth,8)==GPUCMD_HEADER(0,0x8,GPUREG_GAS_DELTAZ_DEPTH)&&
    pica::header(pica::reg_buffer0_offset,15,3,true)==(GPUCMD_HEADER(1,0xF,GPUREG_ATTRIBBUFFER0_OFFSET)|(2u<<20)),
    "Review PICA command headers");
static_assert(pica::gas_depth_function(GPU_NEVER)==unsigned(GPU_MAKEGASDEPTHFUNC(GPU_NEVER))&&
    pica::gas_depth_function(GPU_ALWAYS)==unsigned(GPU_MAKEGASDEPTHFUNC(GPU_ALWAYS))&&
    pica::gas_depth_function(GPU_EQUAL)==unsigned(GPU_MAKEGASDEPTHFUNC(GPU_EQUAL))&&
    pica::gas_depth_function(GPU_NOTEQUAL)==unsigned(GPU_MAKEGASDEPTHFUNC(GPU_NOTEQUAL))&&
    pica::gas_depth_function(GPU_LESS)==unsigned(GPU_MAKEGASDEPTHFUNC(GPU_LESS))&&
    pica::gas_depth_function(GPU_LEQUAL)==unsigned(GPU_MAKEGASDEPTHFUNC(GPU_LEQUAL))&&
    pica::gas_depth_function(GPU_GREATER)==unsigned(GPU_MAKEGASDEPTHFUNC(GPU_GREATER))&&
    pica::gas_depth_function(GPU_GEQUAL)==unsigned(GPU_MAKEGASDEPTHFUNC(GPU_GEQUAL)),"Review the gas depth function table");
static_assert(IndexedDrawPacket::index==GPUREG_INDEXBUFFER_CONFIG&&IndexedDrawPacket::vertices==GPUREG_NUMVERTICES&&
    IndexedDrawPacket::geometry==GPUREG_GEOSTAGE_CONFIG&&IndexedDrawPacket::offset==GPUREG_VERTEX_OFFSET&&
    IndexedDrawPacket::draw==GPUREG_DRAWELEMENTS&&IndexedDrawPacket::vertex_function==GPUREG_VTX_FUNC&&
    IndexedDrawPacket::start==GPUREG_START_DRAW_FUNC0&&IndexedDrawPacket::geometry2==GPUREG_GEOSTAGE_CONFIG2&&
    IndexedDrawPacket::primitive==GPUREG_PRIMITIVE_CONFIG&&IndexedDrawPacket::restart==GPUREG_RESTART_PRIMITIVE&&
    unsigned(GPU_GEOMETRY_PRIM)==0x300,"Review PICA indexed packet registers");
shaderProgram_s* cached() noexcept {
    shaderProgram_s* value;std::memcpy(&value,__C3D_Context+offsetof(ContextPrefix171,program),sizeof(value));return value;
}
}
bool citro3d_program_cached(const shaderProgram_s* program) noexcept {return cached()==program;}
void citro3d_forget_program(const shaderProgram_s* program) noexcept {
    if(cached()!=program)return; // another owner's program must remain intact
    shaderProgram_s* empty=nullptr;
    std::memcpy(__C3D_Context+offsetof(ContextPrefix171,program),&empty,sizeof(empty));
}
void citro3d_submit_indexed_packet(const IndexedDrawPacket& packet,const std::uint32_t* deltas,unsigned delta_words) noexcept {
    C3Di_UpdateContext();
    if(delta_words)GPUCMD_AddRawCommands(deltas,delta_words);
    GPUCMD_AddRawCommands(packet.words.data(),packet.words.size());
    // SDK1.7.1 internal.h: DrawUsed=BIT(1). The SDK consumes this when changing
    // framebuffer/context; it must observe our draw exactly like DrawElements.
    std::uint32_t flags;
    std::memcpy(&flags,__C3D_Context+offsetof(ContextPrefix171,flags),sizeof(flags));
    flags|=1u<<1;
    std::memcpy(__C3D_Context+offsetof(ContextPrefix171,flags),&flags,sizeof(flags));
}
std::uint32_t citro3d_dirty() noexcept {
    std::uint32_t flags;std::memcpy(&flags,__C3D_Context+offsetof(ContextPrefix171,flags),sizeof(flags));return flags;
}
bool citro3d_shadow(Citro3DShadow& output,C3D_Tex* probe) noexcept {
    output={};
    if(!probe)return false;
    // The public getters return the addresses of the SDK's own copies.
    auto* attributes=reinterpret_cast<unsigned char*>(C3D_GetAttrInfo());
    auto* buffers=reinterpret_cast<unsigned char*>(C3D_GetBufInfo());
    auto* first=reinterpret_cast<unsigned char*>(C3D_GetTexEnv(0));
    auto* last=reinterpret_cast<unsigned char*>(C3D_GetTexEnv(5));
    if(attributes!=__C3D_Context+offsetof(ContextPrefix171,attributes)||buffers!=__C3D_Context+offsetof(ContextPrefix171,buffers)||
       first!=__C3D_Context+offsetof(ContextPrefix171,stage)||last!=first+5*sizeof(C3D_TexEnv))return false;
    auto* effect=reinterpret_cast<Citro3DEffect*>(__C3D_Context+offsetof(ContextPrefix171,effect));
    auto** texture=reinterpret_cast<C3D_Tex**>(__C3D_Context+offsetof(ContextPrefix171,texture));
    // Every field the cuts write, through its public setter. The effect
    // region lies between two addresses confirmed above; restore its bytes.
    unsigned char saved[sizeof(Citro3DEffect)];std::memcpy(saved,effect,sizeof(saved));
    C3D_CullFace(GPU_CULL_FRONT_CCW);C3D_AlphaTest(true,GPU_GEQUAL,0x5a);
    C3D_StencilTest(true,GPU_EQUAL,0x21,0x43,0x65);C3D_StencilOp(GPU_STENCIL_ZERO,GPU_STENCIL_REPLACE,GPU_STENCIL_INVERT);
    C3D_DepthTest(true,GPU_LEQUAL,GPU_WRITE_GREEN);
    C3D_AlphaBlend(GPU_BLEND_SUBTRACT,GPU_BLEND_REVERSE_SUBTRACT,GPU_DST_COLOR,GPU_ONE_MINUS_DST_ALPHA,GPU_SRC_ALPHA,GPU_CONSTANT_COLOR);
    Citro3DEffect expected;std::memcpy(&expected,saved,sizeof(expected));
    pica::cull(expected,GPU_CULL_FRONT_CCW,nullptr);pica::alpha_test(expected,true,GPU_GEQUAL,0x5a,nullptr);
    pica::stencil(expected,true,GPU_EQUAL,0x21,0x43,0x65,GPU_STENCIL_ZERO,GPU_STENCIL_REPLACE,GPU_STENCIL_INVERT,nullptr);
    pica::depth_test(expected,true,GPU_LEQUAL,GPU_WRITE_GREEN,nullptr);
    pica::alpha_blend(expected,GPU_BLEND_SUBTRACT,GPU_BLEND_REVERSE_SUBTRACT,GPU_DST_COLOR,GPU_ONE_MINUS_DST_ALPHA,GPU_SRC_ALPHA,GPU_CONSTANT_COLOR,nullptr);
    const bool effect_matches=!std::memcmp(effect,&expected,sizeof(expected));
    std::memcpy(effect,saved,sizeof(saved));
    auto* previous=texture[1];C3D_TexBind(1,probe);
    const bool texture_matches=texture[1]==probe;
    if(texture_matches)texture[1]=previous;
    if(!effect_matches||!texture_matches)return false;
    output.buffers=reinterpret_cast<C3D_BufInfo*>(buffers);output.effect=effect;output.texture=texture;
    output.stage=reinterpret_cast<const C3D_TexEnv*>(first);
    return true;
}
}
