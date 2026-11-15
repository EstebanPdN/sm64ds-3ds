#include "game_wave_grid.hpp"
#include <array>
#include <cassert>
using namespace sm64ds::render;
int main(){
    // Two rows reverse strip traversal and diagonal. Each row uses a half
    // texel-step stagger on its opposite side, including shared vertices.
    std::int32_t p[9][3]{};std::int16_t n[9][3]{};
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){p[y*3+x][0]=int(x)*262144;p[y*3+x][1]=int(y)*262144;n[y*3+x][2]=-4096;}
    game::WaveGrid grid;grid.positions=p;grid.normals=n;grid.columns=grid.rows=2;grid.image_width=grid.image_height=8;grid.du=grid.dv=8192;
    std::array<NormalVertex,24> out{};std::size_t count=99;
    assert(game::wave_grid(grid,out.data(),23,count)==Error::capacity&&count==99&&out[0].base.a==0);
    assert(game::wave_grid(grid,out.data(),out.size(),count)==Error::none&&count==24);
    assert(out[0].base.x==0&&out[0].base.y==0&&out[0].base.u==0);
    assert(out[1].base.x==0&&out[1].base.y==1&&out[1].base.u==.125f&&out[1].base.v==.25f);
    assert(out[2].base.x==1&&out[2].base.y==0&&out[2].base.u==.25f);
    assert(out[12].base.x==2&&out[12].base.y==1&&out[12].base.u==.625f);
    assert(out[13].base.x==2&&out[13].base.y==2&&out[13].base.u==.5f);
    assert(out[14].base.x==1&&out[14].base.y==1&&out[14].base.u==.375f);
    assert(out[0].z==-512&&out[0].uv_source==2&&out[0].base.a==255);
    // Negative fixed-point coordinates floor before signed16 narrowing; UVs
    // wrap BEFORE interpolation, rather than after GPU sampling.
    p[0][0]=-1;grid.u=8388608;grid.alpha=16;
    assert(game::wave_grid(grid,out.data(),out.size(),count)==Error::none);
    assert(out[0].base.x==-1.f/4096&&out[0].base.u==-256&&out[0].base.a==132);
    grid.u=0;grid.normal_u=4096;n[0][0]=-2049;
    assert(game::wave_grid(grid,out.data(),out.size(),count)==Error::none);
    assert(out[0].base.u==-9.f/128&&out[0].x==-257);
}
