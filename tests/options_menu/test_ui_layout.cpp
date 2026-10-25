// Pixel-perfect upper 2D interface: elements, placement and panel tiling.
#include "ui_layout.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace ui_layout;

// Every panel pixel belongs to exactly one tile.
static void check_cover(const Layout& layout, int width, int height) {
    std::vector<int> hits(size_t(width * height), 0);
    for (unsigned i = 0; i < layout.count; ++i) {
        const Tile& t = layout.tiles[i];
        assert(t.x0 >= 0 && t.y0 >= 0 && t.x1 <= width && t.y1 <= height && t.x0 < t.x1 && t.y0 < t.y1);
        for (int y = t.y0; y < t.y1; ++y) for (int x = t.x0; x < t.x1; ++x) ++hits[size_t(y * width + x)];
    }
    for (int hit : hits) assert(hit == 1);
}

int main() {
    // Castle pause (DS 256x192, measured OAM/BG): cream box with the star
    // counter inside it, "TOUCH TO SELECT" line with its two arrow sprites.
    {
        Cells cells;
        cells.mark(40, 32, 216, 112);    // BG2 box + BG3 text
        cells.mark(104, 88, 144, 104);   // OBJ star counter (inside the box)
        cells.mark(24, 164, 232, 180);   // BG1 "TOUCH TO SELECT"
        cells.mark(4, 162, 20, 186);     // OBJ left arrow
        cells.mark(236, 162, 252, 186);  // OBJ right arrow
        const Layout wide = arrange(cells, 400, 240);
        assert(wide.valid() && wide.components == 2);
        check_cover(wide, 400, 240);
        unsigned content = 0;
        for (unsigned i = 0; i < wide.count; ++i) {
            const Tile& t = wide.tiles[i];
            if (!t.content) { assert(t.sx == wide.empty_x && t.sy == wide.empty_y); continue; }
            ++content;
            assert(t.x0 + t.x1 == 400);  // both elements centred horizontally
            if (t.sy == 32) assert(t.x1 - t.x0 == 176 && t.y1 - t.y0 == 80);  // box at 1x
            else assert(t.sx == 4 && t.x1 - t.x0 == 248);                    // line + arrows at 1x
        }
        assert(content == 2);
        // The empty pixel holds no 2D.
        assert(!cells.at(wide.empty_x / cell, wide.empty_y / cell));
        const Layout standard = arrange(cells, 320, 240);
        assert(standard.valid());
        check_cover(standard, 320, 240);
    }
    // HUD counters at the corners stay in the corners of the wide panel.
    {
        Cells cells;
        cells.mark(2, 2, 34, 18);
        cells.mark(222, 2, 254, 18);
        const Layout layout = arrange(cells, 400, 240);
        assert(layout.components == 2);
        check_cover(layout, 400, 240);
        for (unsigned i = 0; i < layout.count; ++i) {
            const Tile& t = layout.tiles[i];
            if (!t.content) continue;
            assert(t.y0 <= 3);
            assert(t.sx < 128 ? t.x0 <= 3 : t.x1 >= 397);
        }
    }
    // Nothing drawn, or a full-screen 2D background: keep the stretch.
    {
        Cells empty;
        assert(!arrange(empty, 400, 240).valid());
        Cells background;
        background.mark(0, 0, 256, 192);
        assert(!arrange(background, 400, 240).valid());
        Cells wide_sky;
        wide_sky.mark(0, 0, 256, 120);
        assert(!arrange(wide_sky, 400, 240).valid());
    }
    // Layer plane: only non-backdrop codes count.
    {
        std::vector<uint8_t> plane(256 * 192, 5);
        plane[100 * 256 + 50] = 3;
        Cells cells;
        mark_layers(cells, plane.data());
        unsigned set = 0;
        for (int r = 0; r < rows; ++r) for (int c = 0; c < columns; ++c) set += cells.at(c, r);
        assert(set == 1 && cells.at(50 / cell, 100 / cell));
    }
    std::puts("PASS ui layout: 2D elements at 1x, centred/corner placement, exact panel tiling, stretch fallback");
}
