// DS geometry engine.
//
// Unlike the 2D engines, 3D is not readable register state -- it is a *stream*.
// Writing a geometry port or pushing a word into the FIFO at 0x4000400 executes a
// command, and the result exists only as the polygons it emitted. A host layer
// that only mirrors registers renders nothing here, which is why this is the one
// part of the port that cannot be done by mapping memory.
//
// SM64DS drives it both ways, so both are implemented:
//
//     *(volatile u32 *)0x4000400 = 0x1a;      // packed command word
//     Copy36Bytes(m, (int *)0x4000400);       // ...then its 9 parameters
//
//     DMAStartTransfer(ch, src, 0x4000400, 0x84400000 | (len >> 2));   // display list
//
// See notes/assessment.md section 2a and docs/mmio-inventory.md.

#ifndef NTR_GX_H
#define NTR_GX_H

#include <stdint.h>
#include <cstddef>

#include "ntr/ppu.h"

namespace ntr {

// A vertex after the modelview+projection transform, in screen space.
struct GxVertex {
    float x, y, z, w;      // x/y in pixels, z in [0,1] after divide
    float u, v;            // texel coordinates (DS TEXCOORD is 1.4 fixed point)
    uint32_t color;        // 0xAARRGGBB
    float vx, vy, vz;      // pre-projection position for native stereo GPU rendering
};

// The bound texture travels with the triangle. Material state is set by the
// game's C code around each display list, not inside it, so a triangle's
// texture is whatever was bound when it was assembled.
struct GxTriangle {
    GxVertex v[3];
    const uint32_t *tex;   // decoded RGBA, or null for untextured
    int tw, th;
    uint8_t alpha, wrap;
    // The original projection active for this triangle. Native presentation
    // must retain it when later display lists change the projection matrix.
    float projection[16]{};
    bool native_projection=false;
};

// Feed the engine. `gx_write_port` takes a geometry-port address in
// 0x4000440..0x40005FF; `gx_write_fifo` takes a word pushed at 0x4000400.
void gx_write_port(uint32_t addr, uint32_t value);
void gx_write_fifo(uint32_t word);

// Reset the command stream, matrix stacks and polygon list.
void gx_reset();

// Retire the preceding frame's polygons while retaining the game's matrix,
// material, light and texture-register state. Call before decoding the next
// complete frame; a partial or invalid command stream cannot be retired.
bool gx_begin_frame();
// Current original DS row-vector projection matrix, before 3DS adaptation.
void gx_get_projection(float matrix[16]);
// Snapshot the fully consumed decoder. Pending game submissions are separate
// from this state and must be parsed before answering SDK stack queries.
struct GxStackState { unsigned mode; int projection, position; };
bool gx_get_stack_state(GxStackState& state);
// -1 means an unknown command; values otherwise include hardware query commands
// whose rendering implementation may still be unsupported.
int gx_command_parameter_count(uint8_t command);

// Bind a decoded texture for subsequent geometry. Pass null for untextured.
void gx_bind_texture(const uint32_t *rgba, int width, int height);
// The caller owns the returned pixels through the end of the frame. No DS
// address is dereferenced by this decoder. A failed bind invalidates the frame.
using GxTextureResolver=bool (*)(void* context,uint32_t image,uint32_t palette,
                                const uint32_t*& pixels,int& width,int& height);
void gx_set_texture_resolver(GxTextureResolver resolver,void* context);

// The game path: TEXIMAGE_PARAM / PLTT_BASE writes (ports 0x2A/0x2B) bind by
// decoding lazily out of the mapped VRAM texture/palette slot windows.
void gx_teximage_param(uint32_t value);
void gx_pltt_base(uint32_t value);

// Diagnostic stream hash: every word entering the engine since the last
// reset, so smokes can separate "different stream" from "ignored stream".
uint32_t gx_stream_hash_reset();
uint32_t gx_state_hash();  // pos matrix + first slots
int gx_store_count_reset();

// Preload a position-matrix stack slot. Display lists open with MTX_RESTORE
// against slots the *scene* filled in beforehand, so a model drawn on its own
// has to have them set up first. Row-major, row-vector convention.
void gx_set_matrix_slot(int slot, const float m[16]);

// Lighting. The DS computes a vertex's colour when it sees NORMAL, out of the
// current material and the four lights -- so lighting is not a rasteriser stage
// here, it happens during command decode, exactly as on hardware.
//
// Display lists carry NORMAL but never DIF_AMB or LIGHT_*: the material and the
// scene set those. Bind them before submitting geometry.
void gx_set_material(uint32_t dif_amb, uint32_t spe_emi);
void gx_set_light(int index, float dx, float dy, float dz, uint32_t bgr555);
void gx_enable_lights(uint32_t mask);
void gx_set_texture_wrap(uint8_t wrap);           // bits 0..3

// Polygons emitted since the last gx_reset / SWAP_BUFFERS.
const GxTriangle *gx_polygons(size_t &count);

// Rasterise them into fb with a depth buffer. Does not clear fb -- the 3D layer
// composites over whatever the 2D engine already drew.
bool gx_asset_stream_valid();

// Read-only evidence for a rejected original stream. Partial packets are
// normal between writes; only a non-none failure identifies a decoder reject.
enum class GxStreamFailure : uint8_t {
    none, unknown_command, texture_generation, texture_resolution, matrix_operation
};
struct GxStreamDiagnostics {
    GxStreamFailure failure;
    uint8_t failing_command;
    uint32_t texture_image, texture_palette;
    uint8_t fifo_command, fifo_parameters, fifo_needed, fifo_commands_remaining;
    uint8_t port_command, port_parameters, port_needed;
};
GxStreamDiagnostics gx_get_stream_diagnostics();

}  // namespace ntr

#endif  // NTR_GX_H
