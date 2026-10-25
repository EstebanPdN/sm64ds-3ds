#pragma once
// Pixel-perfect sprites over a stretched DS screen. Backgrounds keep the
// stretched full-screen layout; sprites are drawn at 1x instead of 1.25x.
// Touching sprites form one group (a counter's icon, cross and digits, a
// multi-piece portrait) that moves as a unit, so pieces never drift apart.
// Each group keeps its share of the free space on both axes: a group at a
// screen edge stays at that edge, a centred group stays centred, and a map
// icon keeps its place on the stretched map to within three pixels.
// Portable: host tests use it.
#include <array>
#include <cstdint>

namespace sprite_layout {
constexpr unsigned max_sprites = 128;

struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // DS pixels, half-open
    constexpr int width() const { return x1 - x0; }
    constexpr int height() const { return y1 - y0; }
};

// Start of a 1x span [x0, x0+size) of a ds-pixel axis on an lcd-pixel axis.
constexpr int place(int x0, int size, int ds, int lcd) {
    if (size >= ds) return x0 + (lcd - ds) / 2;
    if (x0 <= 0) return x0;                           // leading edge
    if (x0 + size >= ds) return x0 + (lcd - ds);      // trailing edge
    const int free_ds = ds - size, free_lcd = lcd - size;
    return (2 * x0 * free_lcd + free_ds) / (2 * free_ds);
}

// Boxes closer than `gap` pixels on both axes belong to the same group.
constexpr bool near(const Box& a, const Box& b, int gap) {
    return a.x0 < b.x1 + gap && b.x0 < a.x1 + gap && a.y0 < b.y1 + gap && b.y0 < a.y1 + gap;
}
constexpr bool overlap(const Box& a, const Box& b) { return near(a, b, 0); }

struct Result {
    std::array<int16_t, max_sprites> dx{}, dy{};  // DS -> LCD offset of each moved sprite
    std::array<bool, max_sprites> moved{};
    unsigned groups = 0;
};

// boxes[i]: sprite i's DS bounds. eligible[i]: in front of every background
// pixel it covers, so it may be drawn after the stretched screen. rank[i]:
// DS draw precedence (higher is in front). A group moves only when every
// member is eligible and no sprite that stays in the DS image lies in front
// of one of its members and overlaps it.
inline Result arrange(const Box* boxes, const bool* eligible, const unsigned* rank, unsigned count,
                      int ds_width, int ds_height, int lcd_width, int lcd_height, int gap = 2) {
    Result result;
    if (count > max_sprites) return result;
    std::array<uint8_t, max_sprites> group{};
    for (unsigned i = 0; i < count; ++i) group[i] = uint8_t(i);
    auto root = [&](unsigned i) { while (group[i] != i) i = group[i] = group[group[i]]; return i; };
    for (unsigned i = 0; i < count; ++i)
        for (unsigned j = i + 1; j < count; ++j)
            if (near(boxes[i], boxes[j], gap)) {
                const unsigned a = root(i), b = root(j);
                if (a != b) group[a > b ? a : b] = uint8_t(a < b ? a : b);
            }
    std::array<bool, max_sprites> movable{};
    for (unsigned i = 0; i < count; ++i) movable[root(i)] = true;
    for (unsigned i = 0; i < count; ++i) if (!eligible[i]) movable[root(i)] = false;
    // A stationary sprite in front of a moving one would be drawn behind it.
    for (bool changed = true; changed;) {
        changed = false;
        for (unsigned i = 0; i < count; ++i) {
            if (!movable[root(i)]) continue;
            for (unsigned j = 0; j < count; ++j)
                if (!movable[root(j)] && rank[j] > rank[i] && overlap(boxes[i], boxes[j])) {
                    movable[root(i)] = false; changed = true; break;
                }
        }
    }
    std::array<Box, max_sprites> bounds{};
    std::array<bool, max_sprites> seen{};
    for (unsigned i = 0; i < count; ++i) {
        const unsigned r = root(i);
        if (!movable[r]) continue;
        Box& b = bounds[r];
        if (!seen[r]) { b = boxes[i]; seen[r] = true; ++result.groups; continue; }
        if (boxes[i].x0 < b.x0) b.x0 = boxes[i].x0;
        if (boxes[i].y0 < b.y0) b.y0 = boxes[i].y0;
        if (boxes[i].x1 > b.x1) b.x1 = boxes[i].x1;
        if (boxes[i].y1 > b.y1) b.y1 = boxes[i].y1;
    }
    for (unsigned i = 0; i < count; ++i) {
        const unsigned r = root(i);
        if (!movable[r]) continue;
        const Box& b = bounds[r];
        result.moved[i] = true;
        result.dx[i] = int16_t(place(b.x0, b.width(), ds_width, lcd_width) - b.x0);
        result.dy[i] = int16_t(place(b.y0, b.height(), ds_height, lcd_height) - b.y0);
    }
    return result;
}

static_assert(place(0, 16, 256, 320) == 0 && place(240, 16, 256, 320) == 304, "edges stay edges");
static_assert(place(120, 16, 256, 320) == 152, "centred stays centred");
static_assert(place(-4, 16, 256, 320) == -4 && place(250, 16, 256, 320) == 314, "partly off-screen");
}
