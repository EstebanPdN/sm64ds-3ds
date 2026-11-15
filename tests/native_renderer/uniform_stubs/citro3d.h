#pragma once
// Host-only public SDK types used by uniforms.c. No renderer is emulated.
#include <stdint.h>
#include <stdbool.h>
#define C3D_FVUNIF_COUNT 96
#define C3D_IVUNIF_COUNT 4
#define BIT(n) (1u<<(n))
#define GPUREG_GSH_BOOLUNIFORM 0x280
#define GPUREG_VSH_BOOLUNIFORM 0x2b0
#define GPUREG_VSH_INTUNIFORM_I0 0x2b1
#define GPUREG_VSH_FLOATUNIFORM_CONFIG 0x2c0
#define GPUREG_VSH_FLOATUNIFORM_DATA 0x2c1
typedef uint32_t u32;
typedef uint16_t u16;
typedef u32 C3D_IVec;
typedef struct {float w,z,y,x;} C3D_FVec;
typedef enum {GPU_VERTEX_SHADER,GPU_GEOMETRY_SHADER} GPU_SHADER_TYPE;
typedef enum {GPU_TRIANGLES=0,GPU_GEOMETRY_PRIM=0x300} GPU_Primitive_t;
#define GPUREG_INDEXBUFFER_CONFIG 0x227
#define GPUREG_NUMVERTICES 0x228
#define GPUREG_GEOSTAGE_CONFIG 0x229
#define GPUREG_VERTEX_OFFSET 0x22a
#define GPUREG_DRAWELEMENTS 0x22f
#define GPUREG_VTX_FUNC 0x231
#define GPUREG_START_DRAW_FUNC0 0x245
#define GPUREG_GEOSTAGE_CONFIG2 0x253
#define GPUREG_PRIMITIVE_CONFIG 0x25e
#define GPUREG_RESTART_PRIMITIVE 0x25f
typedef struct {u32 id,data[3];} float24Uniform_s;
typedef enum {DVLE_VERTEX_SHADER,DVLE_GEOMETRY_SHADER} DVLE_type;
typedef struct {DVLE_type type;} DVLE_s;
typedef struct {
    DVLE_s* dvle;u16 boolUniforms,boolUniformMask;u32 intUniforms[4];
    float24Uniform_s* float24Uniforms;uint8_t intUniformMask,numFloat24Uniforms;
} shaderInstance_s;
#ifdef __cplusplus
extern "C" {
#endif
extern C3D_FVec C3D_FVUnif[2][96];
extern C3D_IVec C3D_IVUnif[2][4];
extern u16 C3D_BoolUnifs[2];
extern bool C3D_FVUnifDirty[2][96],C3D_IVUnifDirty[2][4],C3D_BoolUnifsDirty[2];
void C3D_UpdateUniforms(GPU_SHADER_TYPE);
void C3Di_DirtyUniforms(GPU_SHADER_TYPE);
void C3Di_LoadShaderUniforms(shaderInstance_s*);
void C3Di_ClearShaderUniforms(GPU_SHADER_TYPE);
void uniform_test_write(unsigned kind,unsigned reg,const u32* values,unsigned count);
void uniform_test_masked_write(unsigned mask,unsigned reg,u32 value);
#ifdef __cplusplus
}
#endif
#define GPUCMD_AddWrite(reg,value) do {u32 v_=(value);uniform_test_write(0,(reg),&v_,1);} while(0)
#define GPUCMD_AddWrites(reg,values,count) uniform_test_write(1,(reg),(values),(count))
#define GPUCMD_AddIncrementalWrites(reg,values,count) uniform_test_write(2,(reg),(values),(count))
#define GPUCMD_AddMaskedWrite(reg,mask,value) uniform_test_masked_write((mask),(reg),(value))
