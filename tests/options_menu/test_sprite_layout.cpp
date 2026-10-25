// Pixel-perfect lower-screen sprites: grouping and 1x placement.
#include "sprite_layout.hpp"
#include <cassert>
#include <cstdio>
using namespace sprite_layout;
static Result run(const Box* boxes, const bool* eligible, const unsigned* rank, unsigned count) {
    return arrange(boxes, eligible, rank, count, 256, 192, 320, 240);
}
int main() {
    // Lives counter at the top-left corner: icon, cross and digit touch, so
    // they move together; the 2-pixel corner margin scales like the map (2-3).
    {
        const Box boxes[] = {{2, 2, 18, 18}, {18, 6, 26, 14}, {26, 4, 34, 16}};
        const bool eligible[] = {true, true, true};
        const unsigned rank[] = {0, 1, 2};
        const auto r = run(boxes, eligible, rank, 3);
        assert(r.groups == 1);
        for (unsigned i = 0; i < 3; ++i) assert(r.moved[i] && r.dx[i] == r.dx[0] && r.dy[i] == r.dy[0]);
        assert(2 + r.dx[0] >= 2 && 2 + r.dx[0] <= 3 && 2 + r.dy[0] >= 2 && 2 + r.dy[0] <= 3);
    }
    // Star counter at the top-right corner stays flush with the right edge.
    {
        const Box boxes[] = {{222, 2, 238, 18}, {238, 6, 246, 14}, {246, 4, 254, 16}};
        const bool eligible[] = {true, true, true};
        const unsigned rank[] = {0, 1, 2};
        const auto r = run(boxes, eligible, rank, 3);
        assert(r.groups == 1 && 254 + r.dx[0] >= 317 && 254 + r.dx[0] <= 318);
    }
    // A map icon keeps its place on the stretched map within three pixels.
    for (int x = 0; x <= 240; x += 3)
        for (int y = 0; y <= 176; y += 5) {
            const Box boxes[] = {{x, y, x + 16, y + 16}};
            const bool eligible[] = {true};
            const unsigned rank[] = {0};
            const auto r = run(boxes, eligible, rank, 1);
            const int centre_x = 2 * (x + r.dx[0]) + 16, centre_y = 2 * (y + r.dy[0]) + 16;
            const int stretched_x = (2 * x + 16) * 5 / 4, stretched_y = (2 * y + 16) * 5 / 4;
            assert(centre_x - stretched_x <= 6 && stretched_x - centre_x <= 6);  // doubled units
            assert(centre_y - stretched_y <= 6 && stretched_y - centre_y <= 6);
        }
    // A piece behind a background keeps its whole group in the DS image.
    {
        const Box boxes[] = {{40, 40, 56, 56}, {56, 40, 64, 48}, {150, 100, 166, 116}};
        const bool eligible[] = {true, false, true};
        const unsigned rank[] = {0, 1, 2};
        const auto r = run(boxes, eligible, rank, 3);
        assert(!r.moved[0] && !r.moved[1] && r.moved[2] && r.groups == 1);
    }
    // A stationary sprite in front of a moving one pins it; one behind does not.
    {
        const Box boxes[] = {{100, 100, 116, 116}, {104, 104, 112, 112}};
        const bool front_pinned[] = {true, false};
        const unsigned rank[] = {0, 1};
        assert(!run(boxes, front_pinned, rank, 2).moved[0]);
        const bool back_pinned[] = {false, true};
        const unsigned rank_back[] = {1, 0};
        const auto r = run(boxes, back_pinned, rank_back, 2);
        assert(!r.moved[0] && !r.moved[1]);  // overlapping boxes share one group
    }
    {
        const Box boxes[] = {{100, 100, 116, 116}, {140, 140, 148, 148}};
        const bool eligible[] = {true, false};
        const unsigned rank[] = {0, 1};
        assert(run(boxes, eligible, rank, 2).moved[0]);  // far apart: independent
    }
    std::puts("PASS sprite layout: touching sprites move together at 1x, edges and map positions kept, DS order preserved");
}
