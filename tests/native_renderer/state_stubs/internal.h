#pragma once
// Host-only declarations consumed by the unmodified Citro3D 1.7.1 effect.c and
// buffers.c oracles (tests/native_renderer/pica_state.cpp). No renderer is
// emulated: the oracles' register writes are encoded as libctru encodes them
// and replayed over a register file.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define BIT(n) (1U<<(n))
// libctru 3ds/gpu/registers.h
#define GPUREG_FACECULLING_CONFIG 0x0040
#define GPUREG_DEPTHMAP_SCALE 0x004D
#define GPUREG_DEPTHMAP_OFFSET 0x004E
#define GPUREG_EARLYDEPTH_FUNC 0x0061
#define GPUREG_EARLYDEPTH_TEST1 0x0062
#define GPUREG_EARLYDEPTH_DATA 0x006A
#define GPUREG_DEPTHMAP_ENABLE 0x006D
#define GPUREG_COLOR_OPERATION 0x0100
#define GPUREG_BLEND_FUNC 0x0101
#define GPUREG_LOGIC_OP 0x0102
#define GPUREG_BLEND_COLOR 0x0103
#define GPUREG_FRAGOP_ALPHA_TEST 0x0104
#define GPUREG_STENCIL_TEST 0x0105
#define GPUREG_STENCIL_OP 0x0106
#define GPUREG_DEPTH_COLOR_MASK 0x0107
#define GPUREG_EARLYDEPTH_TEST2 0x0118
#define GPUREG_GAS_DELTAZ_DEPTH 0x0126
#define GPUREG_FRAGOP_SHADOW 0x0130
#define GPUREG_ATTRIBBUFFERS_LOC 0x0200
#define GPUREG_ATTRIBBUFFER0_OFFSET 0x0203
// libctru 3ds/gpu/enums.h
typedef enum {GPU_CULL_NONE=0,GPU_CULL_FRONT_CCW=1,GPU_CULL_BACK_CCW=2} GPU_CULLMODE;
typedef enum {GPU_NEVER=0,GPU_ALWAYS=1,GPU_EQUAL=2,GPU_NOTEQUAL=3,GPU_LESS=4,GPU_LEQUAL=5,GPU_GREATER=6,GPU_GEQUAL=7} GPU_TESTFUNC;
typedef enum {GPU_EARLYDEPTH_GEQUAL=0,GPU_EARLYDEPTH_GREATER=1,GPU_EARLYDEPTH_LEQUAL=2,GPU_EARLYDEPTH_LESS=3} GPU_EARLYDEPTHFUNC;
typedef enum {GPU_GAS_NEVER=0,GPU_GAS_ALWAYS=1,GPU_GAS_GREATER=2,GPU_GAS_LESS=3} GPU_GASDEPTHFUNC;
#define GPU_MAKEGASDEPTHFUNC(n) (GPU_GASDEPTHFUNC)((0xAF02>>((int)(n)<<1))&3)
typedef enum {GPU_STENCIL_KEEP=0,GPU_STENCIL_ZERO=1,GPU_STENCIL_REPLACE=2,GPU_STENCIL_INCR=3,GPU_STENCIL_DECR=4,
              GPU_STENCIL_INVERT=5,GPU_STENCIL_INCR_WRAP=6,GPU_STENCIL_DECR_WRAP=7} GPU_STENCILOP;
typedef enum {GPU_WRITE_RED=0x01,GPU_WRITE_GREEN=0x02,GPU_WRITE_BLUE=0x04,GPU_WRITE_ALPHA=0x08,GPU_WRITE_DEPTH=0x10,
              GPU_WRITE_COLOR=0x0F,GPU_WRITE_ALL=0x1F} GPU_WRITEMASK;
typedef enum {GPU_BLEND_ADD=0,GPU_BLEND_SUBTRACT=1,GPU_BLEND_REVERSE_SUBTRACT=2,GPU_BLEND_MIN=3,GPU_BLEND_MAX=4} GPU_BLENDEQUATION;
typedef enum {GPU_ZERO=0,GPU_ONE=1,GPU_SRC_COLOR=2,GPU_ONE_MINUS_SRC_COLOR=3,GPU_DST_COLOR=4,GPU_ONE_MINUS_DST_COLOR=5,
              GPU_SRC_ALPHA=6,GPU_ONE_MINUS_SRC_ALPHA=7,GPU_DST_ALPHA=8,GPU_ONE_MINUS_DST_ALPHA=9,GPU_CONSTANT_COLOR=10,
              GPU_ONE_MINUS_CONSTANT_COLOR=11,GPU_CONSTANT_ALPHA=12,GPU_ONE_MINUS_CONSTANT_ALPHA=13,GPU_SRC_ALPHA_SATURATE=14} GPU_BLENDFACTOR;
typedef enum {GPU_LOGICOP_CLEAR=0,GPU_LOGICOP_COPY=3} GPU_LOGICOP;
typedef enum {GPU_FRAGOPMODE_GL=0,GPU_FRAGOPMODE_GAS_ACC=1,GPU_FRAGOPMODE_SHADOW=3} GPU_FRAGOPMODE;
// Citro3D 1.7.1 internal.h / c3d/buffers.h, the members these files use.
typedef struct
{
	u32 fragOpMode;
	u32 fragOpShadow;
	u32 zScale, zOffset;
	GPU_CULLMODE cullMode;
	bool zBuffer, earlyDepth;
	GPU_EARLYDEPTHFUNC earlyDepthFunc;
	u32 earlyDepthRef;

	u32 alphaTest;
	u32 stencilMode, stencilOp;
	u32 depthTest;

	u32 blendClr;
	u32 alphaBlend;
	GPU_LOGICOP clrLogicOp;
} C3D_Effect;
typedef struct
{
	u32 offset;
	u32 flags[2];
} C3D_BufCfg;
typedef struct
{
	u32 base_paddr;
	int bufCount;
	C3D_BufCfg buffers[12];
} C3D_BufInfo;
typedef struct {u32 flags;C3D_BufInfo bufInfo;C3D_Effect effect;} C3D_Context;
enum {C3DiF_Active=BIT(0),C3DiF_BufInfo=BIT(3),C3DiF_Effect=BIT(4)};
#ifdef __cplusplus
extern "C" {
#endif
C3D_Context* C3Di_GetContext(void);
u32 osConvertVirtToPhys(const void*);
u32 f32tof16(float);
u32 f32tof24(float);
// libctru GPUCMD_Add: header without its parameter count.
void state_test_add(u32 header,const u32* values,u32 count);
#ifdef __cplusplus
}
#endif
// libctru 3ds/gpu/gpu.h
#define GPUCMD_HEADER(incremental, mask, reg) (((u32)(incremental)<<31)|(((u32)(mask)&0xF)<<16)|((u32)(reg)&0x3FF))
#define GPUCMD_AddMaskedWrite(reg, mask, val) do {u32 v_=(u32)(val);state_test_add(GPUCMD_HEADER(0,(mask),(reg)),&v_,1);} while(0)
#define GPUCMD_AddWrite(reg, val) GPUCMD_AddMaskedWrite((reg), 0xF, (val))
#define GPUCMD_AddIncrementalWrites(reg, vals, num) state_test_add(GPUCMD_HEADER(1,0xF,(reg)),(vals),(num))
