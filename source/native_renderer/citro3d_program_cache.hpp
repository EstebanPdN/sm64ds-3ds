#pragma once
#include <citro3d.h>
#include "indexed_draw_packet.hpp"
namespace sm64ds::render {
// ARM Citro3D 1.7.1 compatibility seam. Its public API has no program detach;
// BindProgram compares the previous program even after Fini/Init. Validate the
// installed cache location after binding before using this narrow ABI seam.
bool citro3d_program_cached(const shaderProgram_s*) noexcept;
void citro3d_forget_program(const shaderProgram_s*) noexcept;
// Caller owns a validated native unsigned16 triangle range and SDK GPU frame.
// deltas: optional single-register writes (pica_state.hpp, an even word
// count) placed after the SDK context update and before the draw.
void citro3d_submit_indexed_packet(const IndexedDrawPacket&,const std::uint32_t* deltas=nullptr,unsigned delta_words=0) noexcept;

// BUILD134 cost cuts. SDK 1.7.1 internal.h C3D_Effect with the installed
// ABI's one-byte enums (libcitro3d.a: Tag_ABI_enum_size small; its setters
// store culling at context+0xe8, the alpha test at +0xf0, blending at +0x104).
struct Citro3DEffect {
    std::uint32_t fragOpMode,fragOpShadow,zScale,zOffset;
    std::uint8_t cullMode;bool zBuffer,earlyDepth;std::uint8_t earlyDepthFunc;
    std::uint32_t earlyDepthRef,alphaTest,stencilMode,stencilOp,depthTest,blendClr,alphaBlend;
    std::uint8_t clrLogicOp;
};
// The SDK's CPU copies of GPU state, for writes that must not mark a whole
// block for re-emission. A value written here must also reach its register
// in the same draw; the SDK then stays coherent when it re-sends the block
// (first draw of a pass, applet restore).
struct Citro3DShadow {
    enum Dirty:std::uint32_t {buffer_table=1u<<3,effect_block=1u<<4,texture1=1u<<24,texture2=1u<<25};
    C3D_BufInfo* buffers=nullptr;
    Citro3DEffect* effect=nullptr;
    C3D_Tex* const* texture=nullptr; // three units
    const C3D_TexEnv* stage=nullptr; // six combiner stages
    explicit operator bool() const noexcept {return buffers!=nullptr;}
};
// Needs an initialised C3D context. Checks the layout against the public
// getters and each effect field against its public setter; the effect values
// are restored. It leaves the attribute, buffer, effect, unit 1 and stage
// 0/5 blocks marked for re-emission (unchanged values). probe: any valid 2D
// texture. False: this is not the SDK the seam was written for; no shadow.
bool citro3d_shadow(Citro3DShadow&,C3D_Tex* probe) noexcept;
std::uint32_t citro3d_dirty() noexcept; // the SDK's pending re-emission flags
}
