#pragma once
// BUILD134 exact single-register PICA writes (cost cut, see perf_switch.hpp).
// Citro3D 1.7.1 keeps a CPU shadow of the effect block and of the vertex
// buffer table and re-sends the whole block when any field changes: 34
// command words for one culling change, 40 for the address of the next mesh.
// These helpers put the same value in the SDK's shadow and write only the
// registers that C3Di_EffectBind / C3Di_BufInfoBind derive from that field.
// Every register ends with the value the full block would have written; an
// SDK re-send of the block (first draw of a pass, applet restore) repeats it.
// Portable: the 3DS seam (citro3d_program_cache.cpp) checks the register
// numbers and the shadow layout against the installed SDK; the host test
// (tests/native_renderer/pica_state.cpp) replays both command streams over a
// register file against the unmodified upstream effect.c / buffers.c.
#include <array>
#include <cstdint>
namespace sm64ds::render::pica {
enum Register : std::uint32_t {
    reg_culling=0x040,reg_colour_operation=0x100,reg_blend_function=0x101,reg_alpha_test=0x104,
    reg_stencil_test=0x105,reg_stencil_operation=0x106,reg_depth_colour_mask=0x107,reg_gas_depth=0x126,
    reg_buffer0_offset=0x203
};
constexpr std::uint32_t header(std::uint32_t reg,unsigned mask=15,unsigned parameters=1,bool consecutive=false) noexcept {
    return (consecutive?0x80000000u:0u)|(std::uint32_t(parameters-1)<<20)|(std::uint32_t(mask&15u)<<16)|(reg&0x3ffu);
}
// libctru GPU_MAKEGASDEPTHFUNC for a GPU_TESTFUNC (checked at the ARM seam).
constexpr std::uint32_t gas_depth_function(unsigned test) noexcept {return (0xAF02u>>(test<<1))&3u;}
// The writes of one draw, submitted after the SDK context update and
// immediately before the indexed draw packet. Always an even word count.
struct Deltas {
    // buffer 4 + culling 2 + depth 4 + stencil 4 + alpha 2 + blend 2 + colour operation 2
    std::array<std::uint32_t,24> words;
    unsigned count=0;
    void write(Register reg,std::uint32_t value,unsigned mask=15) noexcept {
        words[count]=value;words[count+1]=header(reg,mask);count+=2;
    }
    // Registers 0x203..0x205 of vertex buffer 0: offset from the shared base,
    // attribute permutation, permutation high bits | stride | attribute count.
    void buffer0(std::uint32_t offset,std::uint32_t low,std::uint32_t high) noexcept {
        words[count]=offset;words[count+1]=header(reg_buffer0_offset,15,3,true);words[count+2]=low;words[count+3]=high;count+=4;
    }
};
// Each function mirrors one setter of SDK effect.c on its C3D_Effect (or the
// layout-checked mirror of it). out==nullptr: only the shadow changes, for a
// draw whose SDK update re-sends the whole block anyway.
template<class Effect> void cull(Effect& e,unsigned mode,Deltas* out) noexcept {
    e.cullMode=static_cast<decltype(e.cullMode)>(mode);
    if(out)out->write(reg_culling,mode&3u);
}
template<class Effect> void depth_test(Effect& e,bool enable,unsigned function,unsigned writemask,Deltas* out) noexcept {
    e.depthTest=(enable?1u:0u)|((function&7u)<<4)|(writemask<<8);
    if(out){
        out->write(reg_depth_colour_mask,e.depthTest);
        out->write(reg_gas_depth,gas_depth_function((e.depthTest>>4)&7u)<<24,8);
    }
}
template<class Effect> void alpha_test(Effect& e,bool enable,unsigned function,unsigned reference,Deltas* out) noexcept {
    e.alphaTest=(enable?1u:0u)|((function&7u)<<4)|(reference<<8);
    if(out)out->write(reg_alpha_test,e.alphaTest);
}
template<class Effect> void stencil(Effect& e,bool enable,unsigned function,unsigned reference,unsigned input_mask,unsigned write_mask,
                                    unsigned stencil_fail,unsigned depth_fail,unsigned pass,Deltas* out) noexcept {
    e.stencilMode=(enable?1u:0u)|((function&7u)<<4)|(write_mask<<8)|(reference<<16)|(input_mask<<24);
    e.stencilOp=stencil_fail|(depth_fail<<4)|(pass<<8);
    if(out){out->write(reg_stencil_test,e.stencilMode);out->write(reg_stencil_operation,e.stencilOp);}
}
template<class Effect> void alpha_blend(Effect& e,unsigned colour_equation,unsigned alpha_equation,unsigned source_colour,
                                        unsigned destination_colour,unsigned source_alpha,unsigned destination_alpha,Deltas* out) noexcept {
    e.alphaBlend=colour_equation|(alpha_equation<<8)|(source_colour<<16)|(destination_colour<<20)|(source_alpha<<24)|(destination_alpha<<28);
    // C3D_AlphaBlend also selects blending (not a logic operation) in the
    // fragment operation word. It changes on the first call only.
    const std::uint32_t operation=(e.fragOpMode&~0xFF00u)|0x0100u;
    const bool changed=operation!=e.fragOpMode;e.fragOpMode=operation;
    if(out){
        out->write(reg_blend_function,e.alphaBlend);
        if(changed)out->write(reg_colour_operation,operation,7);
    }
}
}
