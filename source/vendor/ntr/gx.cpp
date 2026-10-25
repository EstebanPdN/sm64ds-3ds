// DS geometry engine: command decode, matrix stacks, vertex assembly, raster.
//
// Command encoding and parameter counts from GBATEK. Geometry transforms use
// float; normal-based texture generation retains the original fixed-point
// matrix and the rounding required by the Nintendo NITRO programming manual.

#include "ntr/gx.h"

#include "ntr/texture.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace ntr {
namespace {

constexpr float FX12 = 1.0f / 4096.0f;

// --- matrices ---------------------------------------------------------------
// DS matrices are row-vector convention: v' = v * M, translation in row 3.
struct Mat {
    float m[16];
    static Mat identity() {
        Mat r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
};

// Texture generation consumes signed 32-bit matrix values, including low bits
// which a float cannot preserve. Keep this alongside the existing float matrix
// used by TEXGEN1, rather than reconstructing integer values from floats.
struct FixedMat {
    int32_t m[16];
    static FixedMat identity() {
        FixedMat r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 4096;
        return r;
    }
};

int32_t signed_word(uint32_t word) {
    int32_t result;
    std::memcpy(&result, &word, sizeof result);
    return result;
}

int32_t signed_component(uint32_t word, unsigned bits) {
    const uint32_t mask = (uint32_t(1) << bits) - 1;
    const uint32_t value = word & mask;
    return value & (uint32_t(1) << (bits - 1))
        ? int32_t(value) - int32_t(uint32_t(1) << bits) : int32_t(value);
}

FixedMat fixed_matrix(const uint32_t* words, unsigned count) {
    FixedMat result = FixedMat::identity();
    const unsigned rows = count == 9 ? 3 : 4;
    const unsigned columns = count == 16 ? 4 : 3;
    for (unsigned r = 0; r < rows; ++r)
        for (unsigned c = 0; c < columns; ++c)
            result.m[r * 4 + c] = signed_word(words[r * columns + c]);
    return result;
}

FixedMat mul(const FixedMat& a, const FixedMat& b) {
    FixedMat result{};
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c) {
            // Accumulate before dropping 12 fractional bits. Unsigned modular
            // addition avoids C++ signed overflow and preserves all 32 output
            // bits even when the mathematical sum exceeds int64_t.
            uint64_t sum = 0;
            for (unsigned k = 0; k < 4; ++k)
                sum += uint64_t(int64_t(a.m[r * 4 + k]) * b.m[k * 4 + c]);
            result.m[r * 4 + c] = signed_word(uint32_t(sum >> 12));
        }
    return result;
}

float normal_texcoord(int32_t raw, int32_t nx, int32_t ny, int32_t nz,
                      const FixedMat& matrix, unsigned column) {
    // NITRO V1.44 pp. 213-214: output and raw S/T are in 1/16-texel
    // units. NORMAL has 9 fractional bits and matrix entries have 12.
    // The untranslated sum therefore loses 21 bits, not 17. The raw
    // NORMAL, before the directional transform, is the texture input.
    const int64_t sum = int64_t(nx) * matrix.m[column]
        + int64_t(ny) * matrix.m[4 + column]
        + int64_t(nz) * matrix.m[8 + column];
    constexpr int64_t denominator = int64_t(1) << 21;
    const int64_t generated = sum >= 0 ? sum / denominator
        : -((-sum + denominator - 1) / denominator);
    // The result is signed 1.11.4; preserve 16-bit wrapping, not saturation.
    return signed_component(uint32_t(int64_t(raw) + generated), 16) / 16.0f;
}

Mat mul(const Mat &a, const Mat &b) {
    Mat r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

struct Vec4 { float x, y, z, w; };

Vec4 mul(const Vec4 &v, const Mat &m) {
    return {v.x * m.m[0] + v.y * m.m[4] + v.z * m.m[8] + v.w * m.m[12],
            v.x * m.m[1] + v.y * m.m[5] + v.z * m.m[9] + v.w * m.m[13],
            v.x * m.m[2] + v.y * m.m[6] + v.z * m.m[10] + v.w * m.m[14],
            v.x * m.m[3] + v.y * m.m[7] + v.z * m.m[11] + v.w * m.m[15]};
}

// --- engine state -----------------------------------------------------------
enum MtxMode { MTX_PROJ = 0, MTX_POS = 1, MTX_POSVEC = 2, MTX_TEX = 3 };

struct State {
    int mode = MTX_POS;
    Mat proj = Mat::identity();
    Mat pos = Mat::identity();
    Mat vec = Mat::identity();
    Mat tex = Mat::identity();
    FixedMat tex_fixed = FixedMat::identity();
    Mat tex_saved = Mat::identity();
    FixedMat tex_fixed_saved = FixedMat::identity();
    bool tex_stored = false;

    Mat proj_stack[2];
    Mat pos_stack[32];
    Mat vec_stack[32];
    int proj_sp = 0, pos_sp = 0;

    uint32_t color = 0xFFFFFFFFu;
    uint8_t alpha = 31, wrap = 0;
    float u = 0, v = 0;                 // current TEXCOORD, in texels
    float raw_u = 0, raw_v = 0;
    int32_t raw_s = 0, raw_t = 0;
    uint32_t texture_image=0, texture_palette=0;
    bool texture_dirty=false;
    const uint32_t *tex_rgba = nullptr; // bound texture (Mat tex above is the
    int tw = 0, th = 0;                 // texture *matrix* -- different thing)
    int prim = -1;                 // BEGIN_VTXS type, -1 when not inside a primitive
    int16_t vx = 0, vy = 0, vz = 0;
    std::vector<GxVertex> strip;   // vertices accumulated in the current primitive
    int strip_parity = 0;

    int vp_x = 0, vp_y = 0, vp_w = SCREEN_W, vp_h = SCREEN_H;

    // Lighting state. diffuse/ambient/specular/emission are 0..1 per channel.
    float diffuse[3] = {1, 1, 1}, ambient[3] = {0, 0, 0};
    float emission[3] = {0, 0, 0};
    struct Light { float dx, dy, dz; float r, g, b; } lights[4] = {};
    uint32_t light_mask = 0;

    std::vector<GxTriangle> tris;
};

State g;
int g_store_count;
bool g_unsupported_vram=false;
GxStreamFailure g_failure=GxStreamFailure::none;
uint8_t g_failing_command=0;
uint32_t g_failed_image=0,g_failed_palette=0;
GxTextureResolver g_texture_resolver=nullptr;
void* g_texture_context=nullptr;

void fail_stream(GxStreamFailure failure,uint8_t command) {
    if(!g_unsupported_vram) {
        g_failure=failure;g_failing_command=command;
        g_failed_image=g.texture_image;g_failed_palette=g.texture_palette;
    }
    g_unsupported_vram=true;
}

void resolve_texture() {
    if(!g.texture_dirty)return;
    g.texture_dirty=false;
    const unsigned format=(g.texture_image>>26)&7;
    if(!format) {g.tex_rgba=nullptr;g.tw=g.th=0;return;}
    if(!g_texture_resolver || !g_texture_resolver(g_texture_context,g.texture_image,
            g.texture_palette,g.tex_rgba,g.tw,g.th) || !g.tex_rgba || g.tw<=0 || g.th<=0) {
        fail_stream(GxStreamFailure::texture_resolution,0x2a);g.tex_rgba=nullptr;g.tw=g.th=0;
    }
}

uint32_t bgr555_to_argb(uint16_t c) {
    const uint32_t r = c & 0x1F, gg = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((gg << 3 | gg >> 2) << 8)
           | (b << 3 | b >> 2);
}

Mat &current_pos() { return g.pos; }

// Project a model-space vertex all the way to screen space.
GxVertex project(int16_t x, int16_t y, int16_t z) {
    const Vec4 v{x * FX12, y * FX12, z * FX12, 1.0f};
    const Vec4 view = mul(v, current_pos());
    const Vec4 c = mul(view, g.proj);

    GxVertex out{};
    out.vx = view.x; out.vy = view.y; out.vz = view.z;
    out.w = c.w;
    const float iw = (std::fabs(c.w) > 1e-6f) ? 1.0f / c.w : 0.0f;
    // DS screen y runs bottom-up; the framebuffer is top-down.
    out.x = (c.x * iw + 1.0f) * 0.5f * g.vp_w + g.vp_x;
    out.y = (1.0f - (c.y * iw + 1.0f) * 0.5f) * g.vp_h + g.vp_y;
    out.z = (c.z * iw + 1.0f) * 0.5f;
    out.u = g.u;
    out.v = g.v;
    out.color = g.color;
    return out;
}

void emit_tri(const GxVertex &a, const GxVertex &b, const GxVertex &c) {
    GxTriangle t;
    t.v[0] = a; t.v[1] = b; t.v[2] = c;
    t.tex = g.tex_rgba; t.tw = g.tw; t.th = g.th;
    t.alpha = g.alpha; t.wrap = g.wrap;
    std::memcpy(t.projection, g.proj.m, sizeof t.projection);
    t.native_projection = true;
    g.tris.push_back(t);
}

// Assemble according to the active BEGIN_VTXS primitive type.
void push_vertex(const GxVertex &v) {
    g.strip.push_back(v);
    const size_t n = g.strip.size();
    switch (g.prim) {
        case 0:                                        // separate triangles
            if (n == 3) { emit_tri(g.strip[0], g.strip[1], g.strip[2]); g.strip.clear(); }
            break;
        case 1:                                        // separate quads
            if (n == 4) {
                emit_tri(g.strip[0], g.strip[1], g.strip[2]);
                emit_tri(g.strip[0], g.strip[2], g.strip[3]);
                g.strip.clear();
            }
            break;
        case 2:                                        // triangle strip
            if (n >= 3) {
                const GxVertex &p0 = g.strip[n - 3], &p1 = g.strip[n - 2], &p2 = g.strip[n - 1];
                if ((n - 3) & 1) emit_tri(p1, p0, p2);  // alternate winding
                else emit_tri(p0, p1, p2);
            }
            break;
        case 3:                                        // quad strip
            if (n >= 4 && (n % 2) == 0) {
                const GxVertex &p0 = g.strip[n - 4], &p1 = g.strip[n - 3];
                const GxVertex &p2 = g.strip[n - 2], &p3 = g.strip[n - 1];
                emit_tri(p0, p1, p3);
                emit_tri(p0, p3, p2);
            }
            break;
        default: break;
    }
}

void vertex(int16_t x, int16_t y, int16_t z) {
    g.vx = x; g.vy = y; g.vz = z;
    if (g.prim >= 0) {resolve_texture();push_vertex(project(x, y, z));}
}

// --- command execution ------------------------------------------------------
void load_mtx(Mat &dst, const uint32_t *p, int n) {
    Mat m = Mat::identity();
    if (n == 16) {
        for (int i = 0; i < 16; ++i) m.m[i] = static_cast<int32_t>(p[i]) * FX12;
    } else {  // 4x3: three rows of 3 plus a translation row
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 3; ++c) m.m[r * 4 + c] = static_cast<int32_t>(p[r * 3 + c]) * FX12;
        m.m[3] = m.m[7] = m.m[11] = 0.0f;
        m.m[15] = 1.0f;
    }
    dst = m;
}

Mat mat_3x3(const uint32_t *p) {
    Mat m = Mat::identity();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) m.m[r * 4 + c] = static_cast<int32_t>(p[r * 3 + c]) * FX12;
    return m;
}

void exec(uint8_t cmd, const uint32_t *p, int np) {
    (void)np;
    switch (cmd) {
        case 0x00: break;                                        // NOP
        case 0x10: g.mode = p[0] & 3; break;                     // MTX_MODE
        case 0x11:                                               // MTX_PUSH
            if (g.mode == MTX_TEX) fail_stream(GxStreamFailure::matrix_operation, cmd);
            else if (g.mode == MTX_PROJ) { if (g.proj_sp < 1) g.proj_stack[g.proj_sp++] = g.proj; }
            else if (g.pos_sp < 31) { g.pos_stack[g.pos_sp] = g.pos; g.vec_stack[g.pos_sp] = g.vec; ++g.pos_sp; }
            break;
        case 0x12: {                                             // MTX_POP
            int n = static_cast<int32_t>(p[0] << 26) >> 26;      // signed 6-bit
            if (g.mode == MTX_TEX) fail_stream(GxStreamFailure::matrix_operation, cmd);
            else if (g.mode == MTX_PROJ) { if (g.proj_sp > 0) g.proj = g.proj_stack[--g.proj_sp]; }
            else { g.pos_sp -= n; if (g.pos_sp < 0) g.pos_sp = 0;
                   if (g.pos_sp < 31) { g.pos = g.pos_stack[g.pos_sp]; g.vec = g.vec_stack[g.pos_sp]; } }
            break;
        }
        case 0x13: {                                             // MTX_STORE
            ++g_store_count;
            const int i = p[0] & 31;
            if (g.mode == MTX_TEX) {
                // The original menu uses texture STORE/RESTORE 0. Keep the
                // texture slot separate from the position/vector bone slots.
                // Other indices and texture PUSH/POP remain explicit rejects.
                if (i != 0) fail_stream(GxStreamFailure::matrix_operation, cmd);
                else { g.tex_saved = g.tex; g.tex_fixed_saved = g.tex_fixed; g.tex_stored = true; }
            } else if (g.mode == MTX_PROJ) g.proj_stack[0] = g.proj;
            else { g.pos_stack[i] = g.pos; g.vec_stack[i] = g.vec; }
            break;
        }
        case 0x14: {                                             // MTX_RESTORE
            const int i = p[0] & 31;
            if (g.mode == MTX_TEX) {
                if (i != 0 || !g.tex_stored) fail_stream(GxStreamFailure::matrix_operation, cmd);
                else { g.tex = g.tex_saved; g.tex_fixed = g.tex_fixed_saved; }
            } else if (g.mode == MTX_PROJ) g.proj = g.proj_stack[0];
            else { g.pos = g.pos_stack[i]; g.vec = g.vec_stack[i]; }
            break;
        }
        case 0x15:                                               // MTX_IDENTITY
            if (g.mode == MTX_PROJ) g.proj = Mat::identity();
            else if (g.mode == MTX_TEX) { g.tex = Mat::identity(); g.tex_fixed = FixedMat::identity(); }
            else { g.pos = Mat::identity(); if (g.mode == MTX_POSVEC) g.vec = Mat::identity(); }
            break;
        case 0x16: case 0x17: {                                  // MTX_LOAD_4x4 / 4x3
            Mat m; load_mtx(m, p, cmd == 0x16 ? 16 : 12);
            if (g.mode == MTX_PROJ) g.proj = m;
            else if (g.mode == MTX_TEX) { g.tex = m; g.tex_fixed = fixed_matrix(p, cmd == 0x16 ? 16 : 12); }
            else { g.pos = m; if (g.mode == MTX_POSVEC) g.vec = m; }
            break;
        }
        case 0x18: case 0x19: case 0x1A: {                       // MTX_MULT_4x4 / 4x3 / 3x3
            Mat m;
            if (cmd == 0x1A) m = mat_3x3(p);
            else load_mtx(m, p, cmd == 0x18 ? 16 : 12);
            if (g.mode == MTX_PROJ) g.proj = mul(m, g.proj);
            else if (g.mode == MTX_TEX) {
                g.tex = mul(m, g.tex);
                g.tex_fixed = mul(fixed_matrix(p, cmd == 0x18 ? 16 : (cmd == 0x19 ? 12 : 9)), g.tex_fixed);
            }
            else { g.pos = mul(m, g.pos); if (g.mode == MTX_POSVEC) g.vec = mul(m, g.vec); }
            break;
        }
        case 0x1B: {                                             // MTX_SCALE
            Mat m = Mat::identity();
            m.m[0] = static_cast<int32_t>(p[0]) * FX12;
            m.m[5] = static_cast<int32_t>(p[1]) * FX12;
            m.m[10] = static_cast<int32_t>(p[2]) * FX12;
            if (g.mode == MTX_PROJ) g.proj = mul(m, g.proj);
            else if (g.mode == MTX_TEX) {
                g.tex = mul(m, g.tex);
                FixedMat fixed = FixedMat::identity();
                fixed.m[0] = signed_word(p[0]); fixed.m[5] = signed_word(p[1]); fixed.m[10] = signed_word(p[2]);
                g.tex_fixed = mul(fixed, g.tex_fixed);
            }
            else g.pos = mul(m, g.pos);
            break;
        }
        case 0x1C: {                                             // MTX_TRANS
            Mat m = Mat::identity();
            m.m[12] = static_cast<int32_t>(p[0]) * FX12;
            m.m[13] = static_cast<int32_t>(p[1]) * FX12;
            m.m[14] = static_cast<int32_t>(p[2]) * FX12;
            if (g.mode == MTX_PROJ) g.proj = mul(m, g.proj);
            else if (g.mode == MTX_TEX) {
                g.tex = mul(m, g.tex);
                FixedMat fixed = FixedMat::identity();
                fixed.m[12] = signed_word(p[0]); fixed.m[13] = signed_word(p[1]); fixed.m[14] = signed_word(p[2]);
                g.tex_fixed = mul(fixed, g.tex_fixed);
            }
            else { g.pos = mul(m, g.pos); if (g.mode == MTX_POSVEC) g.vec = mul(m, g.vec); }
            break;
        }
        case 0x20: g.color = bgr555_to_argb(static_cast<uint16_t>(p[0] & 0x7FFF)); break;
        case 0x21: {                                             // NORMAL
            // 3 x 10-bit signed, 1.9 fixed point.
            const int32_t normal_x = signed_component(p[0], 10);
            const int32_t normal_y = signed_component(p[0] >> 10, 10);
            const int32_t normal_z = signed_component(p[0] >> 20, 10);
            if ((g.texture_image >> 30) == 2) {
                g.u = normal_texcoord(g.raw_s, normal_x, normal_y, normal_z, g.tex_fixed, 0);
                g.v = normal_texcoord(g.raw_t, normal_x, normal_y, normal_z, g.tex_fixed, 1);
            }
            float nx = normal_x / 512.0f;
            float ny = normal_y / 512.0f;
            float nz = normal_z / 512.0f;

            // Normals are transformed by the directional matrix, not position.
            const Mat &v = g.vec;
            const float tx = nx * v.m[0] + ny * v.m[4] + nz * v.m[8];
            const float ty = nx * v.m[1] + ny * v.m[5] + nz * v.m[9];
            const float tz = nx * v.m[2] + ny * v.m[6] + nz * v.m[10];
            const float len = std::sqrt(tx * tx + ty * ty + tz * tz);
            nx = len > 1e-6f ? tx / len : 0;
            ny = len > 1e-6f ? ty / len : 0;
            nz = len > 1e-6f ? tz / len : 1;

            float c[3] = {g.emission[0], g.emission[1], g.emission[2]};
            for (int i = 0; i < 4; ++i) {
                if (!((g.light_mask >> i) & 1)) continue;
                const State::Light &L = g.lights[i];
                // GBATEK: diffuse level is max(0, -dot(light_vector, normal)).
                float d = -(L.dx * nx + L.dy * ny + L.dz * nz);
                if (d < 0) d = 0;
                const float lc[3] = {L.r, L.g, L.b};
                for (int k = 0; k < 3; ++k)
                    c[k] += g.diffuse[k] * lc[k] * d + g.ambient[k] * lc[k];
            }
            auto ch = [](float f) {
                const int i = static_cast<int>(f * 255.0f + 0.5f);
                return static_cast<uint32_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
            };
            g.color = 0xFF000000u | (ch(c[0]) << 16) | (ch(c[1]) << 8) | ch(c[2]);
            break;
        }
        case 0x22:                                               // TEXCOORD (1.4 fx)
            g.raw_s = signed_component(p[0], 16);
            g.raw_t = signed_component(p[0] >> 16, 16);
            g.raw_u = g.raw_s / 16.0f;
            g.raw_v = g.raw_t / 16.0f;
            // Mode 1 uses (S,T,1/16,1/16), including BOTH final rows.
            // This is used by the original player's animated eye material.
            if((g.texture_image>>30)==1) {
                g.u=g.raw_u*g.tex.m[0]+g.raw_v*g.tex.m[4]+(g.tex.m[8]+g.tex.m[12])/16.0f;
                g.v=g.raw_u*g.tex.m[1]+g.raw_v*g.tex.m[5]+(g.tex.m[9]+g.tex.m[13])/16.0f;
            } else {g.u=g.raw_u;g.v=g.raw_v;}
            break;
        case 0x23:                                               // VTX_16
            vertex(static_cast<int16_t>(p[0] & 0xFFFF), static_cast<int16_t>(p[0] >> 16),
                   static_cast<int16_t>(p[1] & 0xFFFF));
            break;
        case 0x24: {                                             // VTX_10  (s4.6 -> 4.12)
            auto s10 = [](uint32_t v) { return static_cast<int16_t>((static_cast<int32_t>(v << 22) >> 22) * 64); };
            vertex(s10(p[0] & 0x3FF), s10((p[0] >> 10) & 0x3FF), s10((p[0] >> 20) & 0x3FF));
            break;
        }
        case 0x25: vertex(static_cast<int16_t>(p[0] & 0xFFFF), static_cast<int16_t>(p[0] >> 16), g.vz); break;
        case 0x26: vertex(static_cast<int16_t>(p[0] & 0xFFFF), g.vy, static_cast<int16_t>(p[0] >> 16)); break;
        case 0x27: vertex(g.vx, static_cast<int16_t>(p[0] & 0xFFFF), static_cast<int16_t>(p[0] >> 16)); break;
        case 0x28: {                                             // VTX_DIFF
            auto d10 = [](uint32_t v) { return static_cast<int32_t>(v << 22) >> 22; };
            vertex(static_cast<int16_t>(g.vx + d10(p[0] & 0x3FF)),
                   static_cast<int16_t>(g.vy + d10((p[0] >> 10) & 0x3FF)),
                   static_cast<int16_t>(g.vz + d10((p[0] >> 20) & 0x3FF)));
            break;
        }
        case 0x29:                                               // POLYGON_ATTR
            g.alpha = (p[0] >> 16) & 31;
            g.light_mask = p[0] & 15;
            break;
        case 0x2A: gx_teximage_param(p[0]); break;               // TEXIMAGE_PARAM
        case 0x2B: gx_pltt_base(p[0]); break;                    // PLTT_BASE
        case 0x30: {                                             // DIF_AMB
            auto unpack = [](uint32_t v, float *o) {
                o[0] = (v & 0x1F) / 31.0f;
                o[1] = ((v >> 5) & 0x1F) / 31.0f;
                o[2] = ((v >> 10) & 0x1F) / 31.0f;
            };
            unpack(p[0] & 0x7FFF, g.diffuse);
            unpack((p[0] >> 16) & 0x7FFF, g.ambient);
            // Bit 15 sets the vertex colour to the diffuse colour immediately,
            // which is what an unlit polygon then draws with.
            if ((p[0] >> 15) & 1) {
                auto ch = [](float f) { return static_cast<uint32_t>(f * 255.0f + 0.5f); };
                g.color = 0xFF000000u | (ch(g.diffuse[0]) << 16)
                          | (ch(g.diffuse[1]) << 8) | ch(g.diffuse[2]);
            }
            break;
        }
        case 0x31: {                                             // SPE_EMI
            g.emission[0] = ((p[0] >> 16) & 0x1F) / 31.0f;
            g.emission[1] = ((p[0] >> 21) & 0x1F) / 31.0f;
            g.emission[2] = ((p[0] >> 26) & 0x1F) / 31.0f;
            break;
        }
        case 0x32: {                                             // LIGHT_VECTOR
            const int i = (p[0] >> 30) & 3;
            auto n10 = [](uint32_t v) {
                return (static_cast<int32_t>(v << 22) >> 22) / 512.0f;
            };
            g.lights[i].dx = n10(p[0] & 0x3FF);
            g.lights[i].dy = n10((p[0] >> 10) & 0x3FF);
            g.lights[i].dz = n10((p[0] >> 20) & 0x3FF);
            break;
        }
        case 0x33: {                                             // LIGHT_COLOR
            const int i = (p[0] >> 30) & 3;
            g.lights[i].r = (p[0] & 0x1F) / 31.0f;
            g.lights[i].g = ((p[0] >> 5) & 0x1F) / 31.0f;
            g.lights[i].b = ((p[0] >> 10) & 0x1F) / 31.0f;
            break;
        }
        case 0x34: break;                                        // SHININESS
        case 0x40:                                               // BEGIN_VTXS
            g.prim = p[0] & 3;
            g.strip.clear();
            g.strip_parity = 0;
            break;
        case 0x41: g.prim = -1; g.strip.clear(); break;          // END_VTXS
        case 0x50: break;                                        // SWAP_BUFFERS
        case 0x60:                                               // VIEWPORT
            g.vp_x = p[0] & 0xFF;
            g.vp_y = (p[0] >> 8) & 0xFF;
            g.vp_w = ((p[0] >> 16) & 0xFF) - g.vp_x + 1;
            g.vp_h = ((p[0] >> 24) & 0xFF) - g.vp_y + 1;
            break;
        default: fail_stream(GxStreamFailure::unknown_command,cmd);break;
    }
}

// GBATEK parameter counts, indexed by command byte.
int param_count(uint8_t cmd) {
    switch (cmd) {
        case 0x00: case 0x11: case 0x15: case 0x41: return 0;
        case 0x16: case 0x18: return 16;
        case 0x17: case 0x19: return 12;
        case 0x1A: return 9;
        case 0x1B: case 0x1C: case 0x70: return 3;
        case 0x23: case 0x71: return 2;
        case 0x34: return 32;
        default: return 1;
    }
}

// --- packed FIFO state machine ---------------------------------------------
uint8_t g_queue[4];
int g_queued = 0, g_qpos = 0;
uint32_t g_params[32];
int g_have = 0;

void feed(uint32_t word) {
    if (g_queued == 0) {                       // expecting a command word
        for (int i = 0; i < 4; ++i) g_queue[i] = static_cast<uint8_t>(word >> (i * 8));
        g_queued = 4;
        g_qpos = 0;
        g_have = 0;
        // Commands taking no parameters execute immediately.
        while (g_qpos < g_queued && param_count(g_queue[g_qpos]) == 0) {
            exec(g_queue[g_qpos], nullptr, 0);
            ++g_qpos;
        }
        if (g_qpos >= g_queued) g_queued = 0;
        return;
    }
    const uint8_t cmd = g_queue[g_qpos];
    g_params[g_have++] = word;
    if (g_have >= param_count(cmd)) {
        exec(cmd, g_params, g_have);
        g_have = 0;
        ++g_qpos;
        while (g_qpos < g_queued && param_count(g_queue[g_qpos]) == 0) {
            exec(g_queue[g_qpos], nullptr, 0);
            ++g_qpos;
        }
        if (g_qpos >= g_queued) g_queued = 0;
    }
}

// --- direct port writes -----------------------------------------------------
uint8_t g_port_cmd = 0;
uint32_t g_port_params[32];
int g_port_have = 0;

}  // namespace

void gx_stream_note(uint32_t w);
void gx_write_fifo(uint32_t word) { gx_stream_note(word); feed(word); }

void gx_set_matrix_slot(int slot, const float m[16]) {
    if (slot < 0 || slot >= 32) return;
    Mat mm;
    for (int i = 0; i < 16; ++i) mm.m[i] = m[i];
    g.pos_stack[slot] = mm;
    g.vec_stack[slot] = mm;
}

void gx_set_material(uint32_t dif_amb, uint32_t spe_emi) {
    exec(0x30, &dif_amb, 1);
    exec(0x31, &spe_emi, 1);
}

void gx_set_light(int index, float dx, float dy, float dz, uint32_t bgr555) {
    if (index < 0 || index > 3) return;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) { dx /= len; dy /= len; dz /= len; }
    g.lights[index] = {dx, dy, dz,
                       (bgr555 & 0x1F) / 31.0f, ((bgr555 >> 5) & 0x1F) / 31.0f,
                       ((bgr555 >> 10) & 0x1F) / 31.0f};
}

void gx_enable_lights(uint32_t mask) { g.light_mask = mask & 0xF; }
void gx_set_texture_wrap(uint8_t wrap) { g.wrap = wrap; }

void gx_bind_texture(const uint32_t *rgba, int width, int height) {
    g.texture_dirty=false;
    g.tex_rgba = rgba;
    g.tw = width;
    g.th = height;
}

// --- VRAM-sourced texturing: the game path ----------------------------------
// The game binds textures by writing TEXIMAGE_PARAM / PLTT_BASE around each
// display list; the texel data was already uploaded through GX::LoadTex into
// the mapped texture-slot window and palettes through GX::LoadTexPltt. On
// hardware the slot addresses come from bank assignment; the port's HAL
// points the game's upload-base globals at these same windows, so a slot
// offset here is a plain host address.
// Defer resolution until geometry consumes the binding. The original material
// writes PLTT_BASE before TEXIMAGE_PARAM; decoding both immediately would bind
// the new palette to the previous material's image as an intermediate state.
void gx_set_texture_resolver(GxTextureResolver resolver,void* context) {
    g_texture_resolver=resolver;g_texture_context=context;
    g.texture_dirty=true;
}
void gx_teximage_param(uint32_t image) {
    g.texture_image=image;g.texture_dirty=true;g.wrap=(image>>16)&15;
    // Vertex-source TEXGEN3 still needs its own verified execution path.
    if((image>>30)==3)fail_stream(GxStreamFailure::texture_generation,0x2a);
}
void gx_pltt_base(uint32_t palette) {g.texture_palette=palette;g.texture_dirty=true;}
bool gx_asset_stream_valid() {
    return !g_unsupported_vram && g_queued==0 && g_have==0 && g_port_have==0;
}
GxStreamDiagnostics gx_get_stream_diagnostics() {
    const uint8_t fifo=g_queued?g_queue[g_qpos]:0;
    return {g_failure,g_failing_command,
        g_unsupported_vram?g_failed_image:g.texture_image,
        g_unsupported_vram?g_failed_palette:g.texture_palette,
        fifo,uint8_t(g_have),uint8_t(g_queued?param_count(fifo):0),
        uint8_t(g_queued?g_queued-g_qpos:0),g_port_cmd,uint8_t(g_port_have),
        uint8_t(g_port_have?param_count(g_port_cmd):0)};
}

// Diagnostic: a running hash of every word entering the engine, so a smoke
// can tell "the game emitted a different stream" from "the decode ignored
// it". Reset returns the previous value.
static uint32_t g_stream_hash = 2166136261u;
void gx_stream_note(uint32_t w) { g_stream_hash = (g_stream_hash ^ w) * 16777619u; }
uint32_t gx_stream_hash_reset()
{
    const uint32_t h = g_stream_hash;
    g_stream_hash = 2166136261u;
    return h;
}

int gx_store_count_reset() { int n = g_store_count; g_store_count = 0; return n; }

uint32_t gx_state_hash()
{
    uint32_t h = 2166136261u;
    const unsigned char *b = reinterpret_cast<const unsigned char *>(&g.pos);
    for (size_t i = 0; i < sizeof(Mat); ++i) h = (h ^ b[i]) * 16777619u;
    b = reinterpret_cast<const unsigned char *>(g.pos_stack);
    for (size_t i = 0; i < sizeof(Mat) * 4; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

void gx_write_port(uint32_t addr, uint32_t value) {
    gx_stream_note(addr ^ value);
    const uint8_t cmd = static_cast<uint8_t>((addr - 0x04000400u) >> 2);
    const int need = param_count(cmd);
    if (need <= 1) {
        exec(cmd, &value, 1);
        return;
    }
    // Multi-parameter ports are written repeatedly; accumulate until satisfied.
    if (cmd != g_port_cmd) { g_port_cmd = cmd; g_port_have = 0; }
    if (g_port_have < 32) g_port_params[g_port_have++] = value;
    if (g_port_have >= need) {
        exec(cmd, g_port_params, g_port_have);
        g_port_have = 0;
        g_port_cmd = 0;
    }
}

void gx_reset() {
    g = State{};
    // VRAM-decoded texture cache: uploads can be replaced between scenes
    // (the soak reuses slot offsets per model), so stale entries must go.
    g_unsupported_vram = false;
    g_failure=GxStreamFailure::none;g_failing_command=0;
    g_failed_image=g_failed_palette=0;
    // The stack slots must start as identity, not zero. Model display lists open
    // with MTX_RESTORE against a slot the *scene* filled in earlier; rendering a
    // model on its own would otherwise load an all-zero matrix and collapse every
    // vertex onto the origin -- geometry decodes fine and nothing draws.
    for (int i = 0; i < 32; ++i) {
        g.pos_stack[i] = Mat::identity();
        g.vec_stack[i] = Mat::identity();
    }
    g.proj_stack[0] = g.proj_stack[1] = Mat::identity();
    g_queued = g_qpos = g_have = 0;
    g_port_cmd = 0; g_port_have = 0;
}

const GxTriangle *gx_polygons(size_t &count) {
    count = g.tris.size();
    return g.tris.empty() ? nullptr : g.tris.data();
}

bool gx_begin_frame() {
    if(!gx_asset_stream_valid())return false;
    g.tris.clear();g.strip.clear();g.prim=-1;g.strip_parity=0;
    // The platform may retire an uploaded texture generation between frames.
    // Its register binding survives, but its decoded pixel pointer cannot.
    g.tex_rgba=nullptr;g.tw=g.th=0;g.texture_dirty=true;
    return true;
}

void gx_get_projection(float matrix[16]) {std::memcpy(matrix,g.proj.m,sizeof g.proj.m);}

bool gx_get_stack_state(GxStackState& state) {
    if(!gx_asset_stream_valid())return false;
    state={static_cast<unsigned>(g.mode),g.proj_sp,g.pos_sp};return true;
}
int gx_command_parameter_count(uint8_t command) {
    if(command==0 || (command>=0x10 && command<=0x1c) ||
       (command>=0x20 && command<=0x2b) || (command>=0x30 && command<=0x34) ||
       command==0x40 || command==0x41 || command==0x50 || command==0x60 ||
       (command>=0x70 && command<=0x72))return param_count(command);
    return -1;
}

} // namespace ntr
