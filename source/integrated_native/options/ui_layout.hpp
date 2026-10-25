#pragma once
// Pixel-perfect 2D interface over the native 3D upper screen. The DS 2D
// layers (boxes, text, counters) used to be stretched with the 256x192 DS
// screen onto the native panel (1.5625x1.25 in Wide). Instead, each element
// (a connected group of 2D pixels and sprites) is drawn 1:1 and placed like
// sprite_layout places sprite groups; every other panel pixel samples a DS
// pixel that holds no 2D at all, so the 3D shows there unchanged.
// The compositor draws its usual passes over these tiles instead of one
// full-panel quad. Large 2D backgrounds keep the stretched layout.
// Portable: host tests use it.
#include "sprite_layout.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace ui_layout {
constexpr int ds_width = 256, ds_height = 192;
constexpr int cell = 4, columns = ds_width / cell, rows = ds_height / cell;  // 64x48 cells
constexpr unsigned max_components = 24, max_tiles = 128;

struct Tile {
    int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // panel pixels, half-open
    int16_t sx = 0, sy = 0;                  // DS pixel at (x0,y0); background: the empty DS pixel
    bool content = false;
};
struct Layout {
    std::array<Tile, max_tiles> tiles{};
    unsigned count = 0, components = 0;
    int empty_x = -1, empty_y = -1;  // DS pixel with no 2D (background tiles sample it)
    bool valid() const { return count != 0; }
};

// Occupied 4x4 cells of the DS screen: one 64-bit row mask per cell row.
struct Cells {
    std::array<uint64_t, rows> bits{};
    void mark(int x0, int y0, int x1, int y1) {
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > ds_width) x1 = ds_width;
        if (y1 > ds_height) y1 = ds_height;
        if (x0 >= x1 || y0 >= y1) return;
        const int c0 = x0 / cell, c1 = (x1 - 1) / cell;
        const uint64_t mask = (c1 - c0 == 63 ? ~uint64_t(0) : ((uint64_t(1) << (c1 - c0 + 1)) - 1)) << c0;
        for (int r = y0 / cell; r <= (y1 - 1) / cell; ++r) bits[r] |= mask;
    }
    bool at(int c, int r) const { return (bits[r] >> c) & 1; }
};

// 2D pixels of a BG-only layer plane: any layer code other than the
// backdrop (5). Four bytes of one cell row per 32-bit load.
inline void mark_layers(Cells& cells, const uint8_t* front_layer) {
    for (int r = 0; r < rows; ++r) {
        uint64_t row = 0;
        for (int y = r * cell; y < r * cell + cell; ++y) {
            const uint8_t* line = front_layer + y * ds_width;
            for (int c = 0; c < columns; ++c) {
                uint32_t word;
                std::memcpy(&word, line + c * cell, 4);
                if (word != 0x05050505u) row |= uint64_t(1) << c;
            }
        }
        cells.bits[r] |= row;
    }
}

namespace detail {
struct Box { int x0, y0, x1, y1; };
inline bool overlap(const Box& a, const Box& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
inline Box join(const Box& a, const Box& b) {
    return {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}
// Merges boxes until none overlap (after `shift` places them on the panel).
template <class Shift> inline void merge(Box* boxes, unsigned& count, Shift shift) {
    for (bool changed = true; changed;) {
        changed = false;
        for (unsigned i = 0; i < count && !changed; ++i)
            for (unsigned j = i + 1; j < count; ++j)
                if (overlap(boxes[i], boxes[j]) || overlap(shift(boxes[i]), shift(boxes[j]))) {
                    boxes[i] = join(boxes[i], boxes[j]);
                    boxes[j] = boxes[--count];
                    changed = true;
                    break;
                }
    }
}
}

// Elements: occupied cells connected within three cells across (a text
// line with ordinary word spacing) and one cell down. Returns an empty
// layout when nothing is drawn, the 2D is a large background, or the
// layout would need too many tiles: the caller then keeps the stretch.
inline Layout arrange(const Cells& cells, int panel_width, int panel_height) {
    using detail::Box;
    Layout out;
    // One caller thread (the presentation worker): static scratch, no stack.
    static std::array<uint16_t, rows * columns> parent;
    static std::array<int16_t, rows * columns> slot;
    unsigned occupied = 0;
    for (unsigned i = 0; i < parent.size(); ++i) parent[i] = uint16_t(i);
    auto root = [&](unsigned i) { while (parent[i] != i) i = parent[i] = parent[parent[i]]; return i; };
    auto unite = [&](unsigned a, unsigned b) { a = root(a); b = root(b); if (a != b) parent[a > b ? a : b] = uint16_t(a < b ? a : b); };
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c) {
            if (!cells.at(c, r)) continue;
            ++occupied;
            const unsigned here = unsigned(r * columns + c);
            for (int d = 1; d <= 3 && c + d < columns; ++d) if (cells.at(c + d, r)) unite(here, here + d);
            if (r + 1 < rows)
                for (int d = -3; d <= 3; ++d)
                    if (c + d >= 0 && c + d < columns && cells.at(c + d, r + 1)) unite(here, unsigned((r + 1) * columns + c + d));
        }
    if (!occupied || occupied * 2 > unsigned(rows * columns)) return out;
    Box boxes[128];
    unsigned count = 0;
    slot.fill(-1);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c) {
            if (!cells.at(c, r)) continue;
            const unsigned k = root(unsigned(r * columns + c));
            const Box b{c * cell, r * cell, c * cell + cell, r * cell + cell};
            if (slot[k] < 0) { if (count == sizeof boxes / sizeof *boxes) return out; slot[k] = int16_t(count); boxes[count++] = b; }
            else boxes[slot[k]] = detail::join(boxes[slot[k]], b);
        }
    auto place = [&](const Box& b) {
        const int x = sprite_layout::place(b.x0, b.x1 - b.x0, ds_width, panel_width);
        const int y = sprite_layout::place(b.y0, b.y1 - b.y0, ds_height, panel_height);
        return Box{x, y, x + b.x1 - b.x0, y + b.y1 - b.y0};
    };
    detail::merge(boxes, count, place);
    if (count > max_components) return out;
    for (unsigned i = 0; i < count; ++i)
        if ((boxes[i].x1 - boxes[i].x0) * 10 >= ds_width * 9 && (boxes[i].y1 - boxes[i].y0) * 2 >= ds_height) return out;
    // An empty cell outside every element: background tiles sample it.
    for (int r = 0; r < rows && out.empty_x < 0; ++r)
        for (int c = 0; c < columns; ++c) {
            if (cells.at(c, r)) continue;
            const Box b{c * cell, r * cell, c * cell + cell, r * cell + cell};
            bool inside = false;
            for (unsigned i = 0; i < count && !inside; ++i) inside = detail::overlap(b, boxes[i]);
            if (!inside) { out.empty_x = b.x0 + cell / 2; out.empty_y = b.y0 + cell / 2; break; }
        }
    if (out.empty_x < 0) return out;
    // Content tiles, clipped to the panel.
    Box placed[max_components];
    for (unsigned i = 0; i < count; ++i) {
        Box p = place(boxes[i]);
        int sx = boxes[i].x0, sy = boxes[i].y0;
        if (p.x0 < 0) { sx -= p.x0; p.x0 = 0; }
        if (p.y0 < 0) { sy -= p.y0; p.y0 = 0; }
        if (p.x1 > panel_width) p.x1 = panel_width;
        if (p.y1 > panel_height) p.y1 = panel_height;
        placed[i] = p;
        if (p.x0 >= p.x1 || p.y0 >= p.y1) { placed[i] = {0, 0, 0, 0}; continue; }
        out.tiles[out.count++] = {int16_t(p.x0), int16_t(p.y0), int16_t(p.x1), int16_t(p.y1), int16_t(sx), int16_t(sy), true};
    }
    // Background: the panel minus the content tiles, in horizontal bands.
    int cuts[2 * max_components + 2];
    unsigned ncuts = 0;
    cuts[ncuts++] = 0; cuts[ncuts++] = panel_height;
    for (unsigned i = 0; i < count; ++i) if (placed[i].x0 < placed[i].x1) { cuts[ncuts++] = placed[i].y0; cuts[ncuts++] = placed[i].y1; }
    for (unsigned i = 1; i < ncuts; ++i) for (unsigned j = i; j > 0 && cuts[j - 1] > cuts[j]; --j) { const int t = cuts[j]; cuts[j] = cuts[j - 1]; cuts[j - 1] = t; }
    for (unsigned b = 0; b + 1 < ncuts; ++b) {
        const int ya = cuts[b], yb = cuts[b + 1];
        if (ya >= yb) continue;
        int spans[max_components][2];
        unsigned nspans = 0;
        for (unsigned i = 0; i < count; ++i)
            if (placed[i].x0 < placed[i].x1 && placed[i].y0 <= ya && placed[i].y1 >= yb) { spans[nspans][0] = placed[i].x0; spans[nspans][1] = placed[i].x1; ++nspans; }
        for (unsigned i = 1; i < nspans; ++i)
            for (unsigned j = i; j > 0 && spans[j - 1][0] > spans[j][0]; --j) {
                const int a = spans[j][0], e = spans[j][1];
                spans[j][0] = spans[j - 1][0]; spans[j][1] = spans[j - 1][1]; spans[j - 1][0] = a; spans[j - 1][1] = e;
            }
        int x = 0;
        for (unsigned i = 0; i <= nspans; ++i) {
            const int end = i < nspans ? spans[i][0] : panel_width;
            if (end > x) {
                if (out.count == max_tiles) { out.count = 0; return out; }
                out.tiles[out.count++] = {int16_t(x), int16_t(ya), int16_t(end), int16_t(yb), int16_t(out.empty_x), int16_t(out.empty_y), false};
            }
            if (i < nspans && spans[i][1] > x) x = spans[i][1];
        }
    }
    out.components = count;
    return out;
}
}
