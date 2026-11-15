#pragma once
namespace canvas_crop_fixture {
struct Case {int x,y,extra_x,extra_y;};
inline constexpr Case cases[]={{0,0,0,0},{0,-192,0,0},{7,5,0,0},{-7,-5,0,0},
    {-20,25,263,-257},{0,-32,0,0},{0,0,-3,13},{0,0,128,128},
    {2,2,0,0},{-2,-2,0,0}};
inline constexpr unsigned count=sizeof(cases)/sizeof(cases[0]);
}
