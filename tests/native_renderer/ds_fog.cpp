#include "ds_fog.hpp"
#include <cassert>
#include <cstdio>
using namespace sm64ds::render;
namespace {
// Bob-omb Battlefield's registers, from a BUILD131 level dump (ds_video.bin).
DsFog bobomb(){
    DsFog fog;fog.enabled=true;fog.shift=6;fog.offset=0;fog.colour=0x001f5294;
    const unsigned char t[32]={0x00,0x02,0x04,0x06,0x08,0x0a,0x0c,0x0e,0x0f,0x11,0x13,0x15,0x17,0x19,0x1b,0x1c,
        0x1e,0x20,0x22,0x24,0x26,0x28,0x2a,0x2b,0x2d,0x2f,0x31,0x33,0x35,0x37,0x38,0x3a};
    for(unsigned i=0;i<32;++i)fog.table[i]=t[i];
    return fog;
}
}
int main(){
    const auto fog=bobomb();
    // Table boundaries: shift 6 gives a step of 0x400>>6 = 16 units of 15-bit
    // depth, 0x2000 of 24-bit depth. Entry n holds at boundary n+1.
    assert(ds_fog_density(fog,0)==0);
    for(unsigned n=0;n<32;++n)assert(ds_fog_density(fog,(n+1)*0x2000u)==fog.table[n]);
    assert(ds_fog_density(fog,0x2000u+0x1000u)==1);      // halfway between 0 and 2
    assert(ds_fog_density(fog,33*0x2000u)==0x3a&&ds_fog_density(fog,0xffffffu)==0x3a);
    unsigned last=0;for(std::uint32_t z=0;z<0x50000u;z+=0x155u){const auto d=ds_fog_density(fog,z);assert(d>=last);last=d;}
    // The offset moves the start; 127 counts as full; a large shift wraps in 32 bits.
    DsFog moved=fog;moved.offset=0x100;
    assert(ds_fog_density(moved,0x100u*0x200u)==0&&ds_fog_density(moved,0x100u*0x200u+2*0x2000u)==fog.table[1]);
    DsFog full=fog;full.table.fill(127);assert(ds_fog_density(full,0x40000u)==128);
    DsFog wrap=fog;wrap.shift=10;
    assert(ds_fog_density(wrap,0x2000u*16)==0x3a);   // 16 times the step: past the table
    // Projection: clip z = a*w + b. The game's projection reaches the native
    // target as z = -1/16 for every w (near plane at w = 1/16, no far plane).
    Matrix projection{};projection.row[0][0]=1;projection.row[1][1]=1;projection.row[2][3]=-0.0625f;projection.row[3][2]=-1;
    const auto depth=fog_depth(projection);
    assert(depth.perspective&&depth.a==0&&depth.b==-0.0625f);
    assert(fog_depth24(depth,1)==256&&fog_depth24(depth,0.0625f/8)==8*4096&&fog_depth24(depth,0)==0xffffffu);
    Matrix finite_far=projection;finite_far.row[2][2]=0.001f; // z = -0.001*w - 1/16: far plane at w = 62.5/1.001
    const auto bounded=fog_depth(finite_far);
    assert(bounded.perspective&&bounded.a==-0.001f&&fog_depth24(bounded,0.0005f)==0xffffffu);
    Matrix flat{};flat.row[3][3]=1;assert(!fog_depth(flat).perspective&&!fog_drawable(fog,fog_depth(flat)));
    DsFog off=fog;off.enabled=false;DsFog alpha=fog;alpha.alpha_only=true;
    assert(fog_drawable(fog,depth)&&!fog_drawable(off,depth)&&!fog_drawable(alpha,depth));
    assert(fog_colour(fog)==0xa5a5a5u&&same(fog,bobomb())&&!same(fog,moved));
    // The PICA table: exact where the DS has no fog, bounded error elsewhere.
    float table[256],worst=0;build_fog_lut(fog,depth,table,&worst);
    for(unsigned i=0;i<128;++i){assert(table[i]>=0&&table[i]<=1&&table[i]+table[128+i]>=0&&table[i]+table[128+i]<=1.0001f);}
    for(unsigned i=4;i<128;++i)assert(table[i]>0.99f&&table[128+i]<0.01f);  // nearer than w = 2
    assert(table[0]<0.75f&&table[0]+table[128]>0.9f);   // the far interval carries the ramp
    assert(worst<0.13f);
    std::printf("DS fog: density formula, W-buffer depth mapping and PICA table (largest error %.1f of 128 with Bob-omb's registers)\n",double(worst)*128);
}
