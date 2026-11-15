/* Altered Citro3D v1.7.1 uniforms.c: native ARM11 block-range emission.
 * Original: https://github.com/devkitPro/citro3d/blob/v1.7.1/source/uniforms.c
 * The SDK lifecycle, constants, int/bool and ever-dirty rules are retained.
 * Full original notice follows.
 *
 * Copyright (C) 2014-2018 fincs
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 *    must not claim that you wrote the original software. If you use
 *    this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and
 *    must not be misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source
 *    distribution.
 */
#include <citro3d.h>
#include <cstring>
#include "native_uniforms.hpp"

namespace {
bool block_ranges=true;
// BUILD134: what the GPU holds from this file's last emission, per stage.
bool residency=false;
struct Resident {
    static constexpr int capacity=16;
    bool constants=false,booleans=false;
    u16 boolean_word=0;
    int count=0,low=0,high=-1; // constant rows span [low,high]
    float24Uniform_s row[capacity];
} resident[2];
}
void sm64ds_native_uniform_ranges(bool enabled) noexcept {block_ranges=enabled;}
void sm64ds_native_uniform_forget() noexcept {for(auto& r:resident)r.constants=r.booleans=false;}
void sm64ds_native_uniform_residency(bool enabled) noexcept {residency=enabled;sm64ds_native_uniform_forget();}

extern "C" {

C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
C3D_IVec C3D_IVUnif[2][C3D_IVUNIF_COUNT];
u16      C3D_BoolUnifs[2];

alignas(4) bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT];
bool C3D_IVUnifDirty[2][C3D_IVUNIF_COUNT];
bool C3D_BoolUnifsDirty[2];

static struct
{
	bool dirty;
	int count;
	float24Uniform_s* data;
} C3Di_ShaderFVecData[2];

alignas(4) static bool C3Di_FVUnifEverDirty[2][C3D_FVUNIF_COUNT];
static bool C3Di_IVUnifEverDirty[2][C3D_IVUNIF_COUNT];


} // extern C

template<bool Blocks>
static void emit_float_ranges(GPU_SHADER_TYPE type,int offset)
{
    int i=0;
	while (i < C3D_FVUNIF_COUNT)
	{
        if constexpr (Blocks)
        {
            // Byte flags are SDK-owned public state. Aligned memcpy reads their
            // representation without aliasing bool as an integer object.
            auto* dirty=C3D_FVUnifDirty[type];
            while(i<C3D_FVUNIF_COUNT && (i&3) && !dirty[i])++i;
            if(!(i&3)){
                while(i+16<=C3D_FVUNIF_COUNT){
                    u32 words[4];std::memcpy(words,dirty+i,sizeof(words));
                    if(words[0]|words[1]|words[2]|words[3])break;
                    i+=16;
                }
                while(i+4<=C3D_FVUNIF_COUNT){
                    u32 word;std::memcpy(&word,dirty+i,sizeof(word));
                    if(word)break;
                    i+=4;
                }
            }
            while(i<C3D_FVUNIF_COUNT && !dirty[i])++i;
            if(i==C3D_FVUNIF_COUNT)break;
        }
        else if (!C3D_FVUnifDirty[type][i]){++i;continue;}

        // Preserve precisely the SDK's consecutive runs and their ordering.
        int j=i;
        if constexpr(Blocks){
            auto* dirty=C3D_FVUnifDirty[type];
            while(j<C3D_FVUNIF_COUNT && (j&3) && dirty[j])++j;
            if(!(j&3))while(j+4<=C3D_FVUNIF_COUNT){
                u32 word;std::memcpy(&word,dirty+j,sizeof(word));
                if(word!=0x01010101u)break;
                j+=4;
            }
        }
        while(j<C3D_FVUNIF_COUNT && C3D_FVUnifDirty[type][j])++j;

		// A dynamic value replaces a constant the GPU held: send the table again.
		if(resident[type].constants&&i<=resident[type].high&&j>resident[type].low)resident[type].constants=false;

		// Upload the uniforms
		GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG+offset, 0x80000000|i);
		GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA+offset, (u32*)&C3D_FVUnif[type][i], (j-i)*4);

		// Clear the dirty flag
        if constexpr(Blocks){
            std::memset(C3D_FVUnifDirty[type]+i,0,std::size_t(j-i));
            std::memset(C3Di_FVUnifEverDirty[type]+i,1,std::size_t(j-i));
        }else for(int k=i;k<j;++k){
            C3D_FVUnifDirty[type][k]=false;C3Di_FVUnifEverDirty[type][k]=true;
        }

		// Advance
		i = j;
	}

}

extern "C" {
void C3D_UpdateUniforms(GPU_SHADER_TYPE type)
{
    static_assert(sizeof(bool)==1 && C3D_FVUNIF_COUNT==96,"Review uniform flag layout for this SDK");
	int offset = type == GPU_GEOMETRY_SHADER ? (GPUREG_GSH_BOOLUNIFORM-GPUREG_VSH_BOOLUNIFORM) : 0;
	int i = 0;

	// Update FVec uniforms that come from shader constants
	if (C3Di_ShaderFVecData[type].dirty)
	{
		auto& held=resident[type];const int count=C3Di_ShaderFVecData[type].count;
		if(residency&&held.constants&&held.count==count&&
		   (!count||!std::memcmp(held.row,C3Di_ShaderFVecData[type].data,std::size_t(count)*sizeof(float24Uniform_s))))
		{
			// The GPU holds this table. Keep the SDK's side effect: a pending
			// dynamic write to a constant row is dropped, the constant wins.
			for(i=0;i<count;++i)C3D_FVUnifDirty[type][C3Di_ShaderFVecData[type].data[i].id]=false;
		}
		else
		{
		while (i < C3Di_ShaderFVecData[type].count)
		{
			float24Uniform_s* u = &C3Di_ShaderFVecData[type].data[i++];
			GPUCMD_AddIncrementalWrites(GPUREG_VSH_FLOATUNIFORM_CONFIG+offset, (u32*)u, 4);
			C3D_FVUnifDirty[type][u->id] = false;
		}
			held.constants=residency&&count<=Resident::capacity;
			if(held.constants){
				held.count=count;held.low=C3D_FVUNIF_COUNT;held.high=-1;
				if(count)std::memcpy(held.row,C3Di_ShaderFVecData[type].data,std::size_t(count)*sizeof(float24Uniform_s));
				for(i=0;i<count;++i){const int id=int(held.row[i].id);if(id<held.low)held.low=id;if(id>held.high)held.high=id;}
			}
		}
		C3Di_ShaderFVecData[type].dirty = false;
		i = 0;
	}

	// Mode is selected once, outside the hot scan; the scalar reference
	// does not pay a per-flag test for the block implementation.
	if(block_ranges)emit_float_ranges<true>(type,offset);
	else emit_float_ranges<false>(type,offset);

	// Update IVec uniforms
	for (i = 0; i < C3D_IVUNIF_COUNT; i ++)
	{
		if (!C3D_IVUnifDirty[type][i]) continue;

		GPUCMD_AddWrite(GPUREG_VSH_INTUNIFORM_I0+offset+i, C3D_IVUnif[type][i]);
		C3D_IVUnifDirty[type][i] = false;
		C3Di_IVUnifEverDirty[type][i] = false;
	}

	// Update bool uniforms
	if (C3D_BoolUnifsDirty[type])
	{
		auto& held=resident[type];
		if(!(residency&&held.booleans&&held.boolean_word==C3D_BoolUnifs[type]))
		{
			GPUCMD_AddWrite(GPUREG_VSH_BOOLUNIFORM+offset, 0x7FFF0000 | C3D_BoolUnifs[type]);
			held.booleans=true;held.boolean_word=C3D_BoolUnifs[type];
		}
		C3D_BoolUnifsDirty[type] = false;
	}
}

void C3Di_DirtyUniforms(GPU_SHADER_TYPE type)
{
	int i;
	// The SDK declares the GPU's registers lost (applet restore).
	resident[type].constants=resident[type].booleans=false;
	C3D_BoolUnifsDirty[type] = true;
	if (C3Di_ShaderFVecData[type].count)
		C3Di_ShaderFVecData[type].dirty = true;
	for (i = 0; i < C3D_FVUNIF_COUNT; i ++)
		C3D_FVUnifDirty[type][i] = C3D_FVUnifDirty[type][i] || C3Di_FVUnifEverDirty[type][i];
	for (i = 0; i < C3D_IVUNIF_COUNT; i ++)
		C3D_IVUnifDirty[type][i] = C3D_IVUnifDirty[type][i] || C3Di_IVUnifEverDirty[type][i];
}

void C3Di_LoadShaderUniforms(shaderInstance_s* si)
{
	GPU_SHADER_TYPE type = GPU_SHADER_TYPE(si->dvle->type);
	if (si->boolUniformMask)
	{
		C3D_BoolUnifs[type] &= ~si->boolUniformMask;
		C3D_BoolUnifs[type] |= si->boolUniforms;
	}

	if (type == GPU_GEOMETRY_SHADER)
		C3D_BoolUnifs[type] &= ~BIT(15);
	C3D_BoolUnifsDirty[type] = true;

	if (si->intUniformMask)
	{
		int i;
		for (i = 0; i < 4; i ++)
		{
			if (si->intUniformMask & BIT(i))
			{
				C3D_IVUnif[type][i] = si->intUniforms[i];
				C3D_IVUnifDirty[type][i] = true;
			}
		}
	}
	C3Di_ShaderFVecData[type].dirty = true;
	C3Di_ShaderFVecData[type].count = si->numFloat24Uniforms;
	C3Di_ShaderFVecData[type].data = si->float24Uniforms;
}

void C3Di_ClearShaderUniforms(GPU_SHADER_TYPE type)
{
	C3Di_ShaderFVecData[type].dirty = false;
	C3Di_ShaderFVecData[type].count = 0;
	C3Di_ShaderFVecData[type].data = NULL;
}

} // extern C
