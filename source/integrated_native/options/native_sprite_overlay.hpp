#pragma once
// Included by viewer_3ds.cpp after enhance_draw (viewer shader, V3dsVert).
// Pixel-perfect DS 2D in gameplay, backgrounds unchanged:
//  * sprite_overlay, lower screen: the DS image keeps its full-screen
//    stretch (map, backgrounds) while sprites that lie in front of every
//    background pixel leave it and are drawn here at 1x, grouped and placed
//    by sprite_layout. Anything else (windows, alpha OBJ, blended OBJ,
//    sprites behind a background) stays in the DS image as before.
//  * ui_overlay, upper screen: 2D elements composed 1:1 over the 3D.
#include "sprite_layout.hpp"
#include "native_obj_plan.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace sprite_overlay {
constexpr int lcd_width = 320, lcd_height = 240;
constexpr unsigned slots = 48;

struct Slot {
    C3D_Tex gpu{};
    ntr::HostVector<uint8_t> encoded, palette;
    unsigned width = 0, height = 0;
    uint64_t used = 0;
    bool ready = false;
};
static Slot g_slots[slots];
static uint64_t g_frame = 0;
static V3dsVert* g_vertices = nullptr;
static std::array<bool, 128> g_ignored{};
static std::array<Slot*, 128> g_texture{};
static sprite_layout::Result g_layout;
static const ds_gpu::ObjPlan* g_plan = nullptr;
static uint16_t g_master = 0, g_blend = 0, g_brightness = 0;
static bool g_active = false;

static void release(Slot& slot) {
    if (slot.ready) C3D_TexDelete(&slot.gpu);
    slot.gpu = {}; slot.ready = false; slot.encoded.clear(); slot.palette.clear();
}
static void shutdown() {
    for (auto& slot : g_slots) release(slot);
    if (g_vertices) linearFree(g_vertices);
    g_vertices = nullptr; g_active = false; g_plan = nullptr;
}
static void reset() { g_active = false; g_plan = nullptr; }

// Decoded sprite texture, cached by its exact tile and palette bytes. Called
// inside a begun frame: slots not used in this frame are free to replace.
static Slot* texture(const ds_gpu::ObjSnapshot& s) {
    Slot* replace = nullptr;
    for (auto& t : g_slots) {
        if (t.ready && t.width == s.width && t.height == s.height &&
            t.encoded.size() == s.encoded.size() && t.palette.size() == s.palette_bytes &&
            !std::memcmp(t.encoded.data(), s.encoded.data(), s.encoded.size()) &&
            !std::memcmp(t.palette.data(), s.palette.data(), s.palette_bytes)) {
            t.used = g_frame; return &t;
        }
        if (!t.ready) { if (!replace || replace->ready) replace = &t; }
        else if (t.used != g_frame && (!replace || (replace->ready && t.used < replace->used))) replace = &t;
    }
    if (!replace) return nullptr;
    release(*replace);
    Slot& t = *replace;
    if (!C3D_TexInit(&t.gpu, s.width, s.height, GPU_RGBA8)) { t.gpu = {}; return nullptr; }
    C3D_TexSetFilter(&t.gpu, GPU_NEAREST, GPU_NEAREST);
    // Rotated affine sprites sample outside their source: transparent border.
    C3D_TexSetWrap(&t.gpu, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER); t.gpu.border = 0;
    ntr::HostVector<uint32_t> pixels;
    ds_gpu::decode_obj_texture(s, pixels);
    auto* gpu = static_cast<uint32_t*>(t.gpu.data);
    for (unsigned y = 0; y < s.height; ++y) for (unsigned x = 0; x < s.width; ++x) {
        const unsigned py = s.height - 1 - y, xx = x & 7, yy = py & 7;
        const unsigned morton = (xx & 1) | ((yy & 1) << 1) | ((xx & 2) << 1) | ((yy & 2) << 2) | ((xx & 4) << 2) | ((yy & 4) << 3);
        const uint32_t c = pixels[y * s.width + x];
        gpu[((py / 8) * (s.width / 8) + x / 8) * 64 + morton] = (c << 8) | (c >> 24);
    }
    GSPGPU_FlushDataCache(t.gpu.data, t.gpu.size);
    t.encoded = s.encoded;
    t.palette.assign(s.palette.begin(), s.palette.begin() + s.palette_bytes);
    t.width = s.width; t.height = s.height; t.used = g_frame; t.ready = true;
    return &t;
}

// Call inside the frame, before the engine's OBJ layer is drawn. Returns the
// OAM indices to leave out of that layer, or null when nothing moves.
static const bool* prepare(const ds_gpu::ObjPlan* plan, const ntr::PpuPreparedCandidates& c, bool enabled) {
    reset();
    if (!enabled || !plan || !plan->count || plan->count > 128 || plan->fallback != ds_gpu::ObjFallback::None) return nullptr;
    const auto& s = c.state;
    if (s.blank || s.has_windows || s.window_cut || s.has_obj_alpha || s.obj_first_blend || s.semi_back || s.bg_first_blend) return nullptr;
    if (((s.blend >> 6) & 3) == 1 && (s.blend & 16)) return nullptr;  // OBJ alpha-blended over the screen
    const uint8_t* priority = c.gpu_affine ? nullptr : c.front_priority();
    if (!c.gpu_affine && !priority) return nullptr;
    ++g_frame;
    sprite_layout::Box boxes[128];
    bool eligible[128];
    unsigned rank[128];
    for (unsigned i = 0; i < plan->count; ++i) {
        const unsigned n = plan->order[i];
        if (n >= 128) return nullptr;
        const auto& o = plan->sprites[n];
        const int w = int(o.affine ? o.bound_width : o.width), h = int(o.affine ? o.bound_height : o.height);
        boxes[i] = {o.x, o.y, o.x + w, o.y + h};
        rank[i] = i;
        bool ok = o.x < 256 && o.y < 192 && o.x + w > 0 && o.y + h > 0 && !(o.affine && o.clip_y_second0 < o.clip_y_second1);
        for (int y = std::max(0, o.y); ok && y < std::min(192, o.y + h); ++y)
            for (int x = std::max(0, o.x); x < std::min(256, o.x + w); ++x) {
                const unsigned offset = unsigned(y * 256 + x);
                if ((priority ? priority[offset] : c.gpu_affine->priority) < o.priority) { ok = false; break; }
                if (c.gpu_affine && c.gpu_affine->overlay) {
                    const unsigned alpha = c.gpu_affine->overlay[offset] >> 24;
                    if (alpha && alpha < 32 * (o.priority + 2)) { ok = false; break; }
                }
            }
        g_texture[i] = ok ? texture(o) : nullptr;
        eligible[i] = g_texture[i] != nullptr;
    }
    g_layout = sprite_layout::arrange(boxes, eligible, rank, plan->count, 256, 192, lcd_width, lcd_height);
    if (!g_layout.groups) return nullptr;
    g_ignored.fill(false);
    for (unsigned i = 0; i < plan->count; ++i) if (g_layout.moved[i]) g_ignored[plan->order[i]] = true;
    g_plan = plan; g_master = s.master; g_blend = s.blend; g_brightness = s.brightness; g_active = true;
    return g_ignored.data();
}

// DS brightness (BLDCNT effect on OBJ, then master) as combiner lerps.
static void brightness_stages() {
    auto lerp = [](unsigned stage, unsigned factor, bool up) {
        if (!factor) return;
        auto* env = C3D_GetTexEnv(stage);
        C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_CONSTANT);
        C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
        C3D_TexEnvColor(env, ((factor * 255 + 8) / 16) << 24 | (up ? 0x00ffffffu : 0u));
    };
    const unsigned mode = (g_blend >> 6) & 3, master = g_master >> 14;
    if ((mode == 2 || mode == 3) && (g_blend & 16)) lerp(1, std::min(g_brightness & 31u, 16u), mode == 2);
    if (master == 1 || master == 2) lerp(2, std::min(g_master & 31u, 16u), master == 1);
}

// Draws the moved sprites over the lower LCD target, back to front.
static void draw(C3D_RenderTarget* target) {
    if (!g_active || !g_plan || !target) { reset(); return; }
    const auto& plan = *g_plan;
    if (!g_vertices) g_vertices = static_cast<V3dsVert*>(linearAlloc(128 * 6 * sizeof(V3dsVert)));
    if (!g_vertices) { reset(); return; }
    unsigned count = 0;
    std::array<uint8_t, 128> drawn{};
    for (unsigned i = 0; i < plan.count; ++i) {
        if (!g_layout.moved[i] || !g_texture[i]) continue;
        const auto& s = plan.sprites[plan.order[i]];
        const int ox = s.x + g_layout.dx[i], oy = s.y + g_layout.dy[i];
        const unsigned by0 = s.affine ? s.clip_y0 : 0, by1 = s.affine ? s.clip_y1 : s.height;
        const int left = ox + int(s.affine ? s.clip_x0 : 0), right = ox + int(s.affine ? s.clip_x1 : s.width);
        const float x0 = left / 160.f - 1, x1 = right / 160.f - 1;
        const float y0 = 1 - (oy + int(by0)) / 120.f, y1 = 1 - (oy + int(by1)) / 120.f;
        float u0 = s.hflip ? 1 : 0, u1 = s.hflip ? 0 : 1, v0 = s.vflip ? 1 : 0, v1 = s.vflip ? 0 : 1;
        if (s.affine) {
            // Same pixel-centre mapping as the DS-layer OBJ renderer.
            auto uv = [](unsigned edge, unsigned bound, unsigned source, int coefficient) {
                return ((float(edge) - bound * .5f - .5f) * coefficient / 256.f + source * .5f + 1.f / 512.f) / source;
            };
            u0 = uv(s.clip_x0, s.bound_width, s.width, s.pa); u1 = uv(s.clip_x1, s.bound_width, s.width, s.pa);
            v0 = uv(by0, s.bound_height, s.height, s.pd); v1 = uv(by1, s.bound_height, s.height, s.pd);
        }
        V3dsVert q[4] = {{x0, y0, 0, 1, 1, 1, 1, 1, u0, v0}, {x1, y0, 0, 1, 1, 1, 1, 1, u1, v0},
                         {x1, y1, 0, 1, 1, 1, 1, 1, u1, v1}, {x0, y1, 0, 1, 1, 1, 1, 1, u0, v1}};
        if (s.affine && (s.pb || s.pc)) {
            auto corner = [&](V3dsVert& v, unsigned cx, unsigned cy) {
                const float dx = float(cx) - s.bound_width * .5f - .5f, dy = float(cy) - s.bound_height * .5f - .5f;
                v.u = ((s.pa * dx + s.pb * dy) / 256.f + s.width * .5f + 1.f / 512.f) / s.width;
                v.v = ((s.pc * dx + s.pd * dy) / 256.f + s.height * .5f + 1.f / 512.f) / s.height;
            };
            corner(q[0], s.clip_x0, by0); corner(q[1], s.clip_x1, by0); corner(q[2], s.clip_x1, by1); corner(q[3], s.clip_x0, by1);
        }
        const unsigned indices[] = {0, 1, 2, 0, 2, 3};
        for (unsigned k = 0; k < 6; ++k) g_vertices[count * 6 + k] = q[indices[k]];
        drawn[count++] = uint8_t(i);
    }
    if (count) {
        GSPGPU_FlushDataCache(g_vertices, count * 6 * sizeof(V3dsVert));
        shader_bind(false); C3D_SetAttrInfo(&g_attr); set_fix(1, 0, 1, false);
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
        C3D_CullFace(GPU_CULL_NONE); C3D_StencilTest(false, GPU_ALWAYS, 0, 0xff, 0xff);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR); C3D_AlphaTest(true, GPU_GREATER, 0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        for (unsigned stage = 0; stage < 6; ++stage) C3D_TexEnvInit(C3D_GetTexEnv(stage));
        auto* env = C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        brightness_stages();
        auto* buffers = C3D_GetBufInfo(); BufInfo_Init(buffers);
        BufInfo_Add(buffers, g_vertices, sizeof(V3dsVert), 3, 0x210);
        C3D_FrameDrawOn(target);
        for (unsigned k = 0; k < count; ++k) {
            C3D_TexBind(0, &g_texture[drawn[k]]->gpu);
            C3D_DrawArrays(GPU_TRIANGLES, k * 6, 6);
        }
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        for (unsigned stage = 1; stage < 3; ++stage) C3D_TexEnvInit(C3D_GetTexEnv(stage));
    }
    reset();
}
}

// Upper screen in gameplay: the engine's 2D elements (boxes, text, counters)
// are composed 1:1 at their ui_layout places over the native 3D, instead of
// stretched with the whole DS screen. Any composition the tiles do not
// cover exactly (windows, alpha OBJ, blends with a back plane, captures)
// keeps the stretched layout.
#include "ui_layout.hpp"
namespace ui_overlay {
static ui_layout::Cells g_layer_cells;
static const void* g_layer_identity = nullptr;
static uint64_t g_layer_generation = 0;

static bool apply(ds_gpu::Composite& compositor, const ntr::PpuPreparedCandidates& c, const ds_gpu::ObjPlan* objects,
                  unsigned width, unsigned height, bool enabled) {
    compositor.clear_layout();
    if (!enabled || width <= 256) return false;
    const auto& s = c.state;
    if (s.blank || s.has_windows || s.window_cut || s.has_obj_alpha || s.obj_first_blend || s.semi_back ||
        s.bg_first_blend || c.gpu_affine || !c.front_layer()) return false;
    if (objects && objects->fallback != ds_gpu::ObjFallback::None) return false;
    // BG coverage changes only with a new exact-snapshot generation.
    const void* identity = c.background_identity;
    if (!identity || !c.background_generation || identity != g_layer_identity || c.background_generation != g_layer_generation) {
        g_layer_cells = {};
        ui_layout::mark_layers(g_layer_cells, c.front_layer());
        g_layer_identity = identity; g_layer_generation = identity ? c.background_generation : 0;
    }
    ui_layout::Cells cells = g_layer_cells;
    if (objects)
        for (unsigned i = 0; i < objects->count; ++i) {
            const auto& o = objects->sprites[objects->order[i]];
            const int w = int(o.affine ? o.bound_width : o.width), h = int(o.affine ? o.bound_height : o.height);
            cells.mark(o.x, o.y, o.x + w, o.y + h);
        }
    const auto layout = ui_layout::arrange(cells, int(width), int(height));
    return layout.valid() && compositor.set_layout(layout, width, height);
}
}
