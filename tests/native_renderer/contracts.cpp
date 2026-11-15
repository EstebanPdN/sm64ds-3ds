#include "contracts.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <vector>
using namespace sm64ds::render;

static void resources() {
    Slots<int, MeshTag, 2> pool, other;
    const auto a = pool.available(); assert(a);
    assert(!pool.get(a)); assert(!pool.release(a));
    assert(*pool.commit(a, 41) == 41);
    assert(!pool.commit(a, 99)); assert(!other.get(a)); assert(!other.commit(a, 2));
    const auto b = pool.available(); assert(b && a != b);
    assert(*pool.commit(b, 42) == 42); assert(!pool.available());
    assert(pool.release(a)); assert(!pool.get(a)); assert(!pool.release(a));
    for (unsigned i = 0; i < 100000; ++i) {
        const auto next = pool.available(); assert(next && next != a);
        assert(pool.commit(next, int(i))); assert(!pool.get(a));
        assert(pool.release(next)); assert(!pool.get(next));
    }
    unsigned live = 0;
    pool.each([&](Mesh h, int& value) { ++live; assert(value == 42); assert(pool.release(h)); });
    assert(live == 1 && !pool.get(b));
}
static void lifetime() {
    FrameState state;
    assert(!state.pass() && !state.end() && !state.can_mutate() && !state.can_draw());
    for (unsigned i = 0; i < 1000; ++i) {
        assert(state.begin() && state.can_mutate());
        assert(!state.begin() && !state.can_draw());
        assert(state.pass() && state.can_draw() && !state.can_mutate());
        assert(state.pass() && !state.can_mutate());
        assert(state.end() && !state.can_draw() && !state.can_mutate());
    }
}
static void mesh_validation() {
    std::array<Vertex,3> vertices{{{0,0,0,0,0,255,255,255,255},
        {1,0,0,1,0,255,255,255,255}, {0,1,0,0,1,255,255,255,255}}};
    std::array<std::uint16_t,3> indices{{0,1,2}};
    MeshView view{vertices.data(),vertices.size(),indices.data(),indices.size()};
    assert(validate(view) == Error::none && mesh_bytes(view) == 256);
    indices[2] = 3; assert(validate(view) == Error::invalid_data); indices[2] = 2;
    vertices[1].u = std::numeric_limits<float>::infinity();
    assert(validate(view) == Error::invalid_data); vertices[1].u = 1;
    view.index_count = 2; assert(validate(view) == Error::invalid_data);
    view.index_count = 0; assert(validate(view) == Error::invalid_data);
    view.index_count = std::numeric_limits<std::size_t>::max(); assert(validate(view) == Error::invalid_data);
    view.index_count = 3; view.vertex_count = 65537; assert(validate(view) == Error::invalid_data);
    view.vertex_count = 3; view.vertices = nullptr; assert(validate(view) == Error::invalid_data);
    auto matrix = identity(); assert(finite(matrix));
    matrix.row[3][0] = std::numeric_limits<float>::quiet_NaN(); assert(!finite(matrix));
    Material m, same; assert(valid(m) && m == same);
    same.brightness=-16;assert(valid(same)&&!(m==same));same.brightness=16;assert(valid(same));same.brightness=17;assert(!valid(same));same=m;
    same.depth_write = false; assert(!(m == same));
    same=m;same.sample_alpha=false;assert(valid(same)&&!(m==same));
    m.blend=Blend::premultiplied;assert(valid(m));
    m.blend = static_cast<Blend>(99); assert(!valid(m));
    m=Material{};same=m;same.wrap_s=Wrap::mirror;assert(valid(same) && !(m==same));
    m.wrap_t=static_cast<Wrap>(3);assert(!valid(m));
    m=Material{};m.uv[2]=std::numeric_limits<float>::quiet_NaN();assert(!valid(m));
}
static void prepared_mesh_bytes() {
    std::array<std::uint8_t,3*sizeof(Vertex)+1> data{};
    std::array<std::uint8_t,7> indices{{0,0,0,1,0,2,0}};
    MeshBytes view{data.data()+1,data.size()-1,indices.data()+1,indices.size()-1};
    assert(validate(view)==Error::none);
    // IEEE754 +infinity in an unaligned UV component is invalid. No typed
    // pointer dereference is allowed by this loader's byte-span contract.
    data[1+12+2]=0x80;data[1+12+3]=0x7f;
    assert(validate(view)==Error::invalid_data);
    data[1+12+2]=data[1+12+3]=0;
    indices[5]=3;assert(validate(view)==Error::invalid_data);indices[5]=2;
    --view.vertex_bytes;assert(validate(view)==Error::invalid_data);++view.vertex_bytes;
    --view.index_bytes;assert(validate(view)==Error::invalid_data);
}
static void typed_triangle_stream() {
    std::array<NormalVertex,6> vertices{};const std::array<std::uint16_t,6> indices{{0,1,2,3,4,5}};
    const MeshBytes bytes{reinterpret_cast<const std::uint8_t*>(vertices.data()),sizeof(vertices),
        reinterpret_cast<const std::uint8_t*>(indices.data()),sizeof(indices),true};
    std::uint32_t random=0x51aa321u;
    for(unsigned n=0;n<4096;++n){
        for(auto& v:vertices){random=random*1664525u+1013904223u;
            const float u=float(int(random&65535)-32768)/65536;
            v={{u,u*2,u*.5f,u,-u,12,34,56,78},std::int16_t(int(random&1023)-512),-512,511,2};}
        if(n%8==1)vertices[n%6].base.x=std::numeric_limits<float>::infinity();
        if(n%8==2)vertices[n%6].base.u=std::numeric_limits<float>::quiet_NaN();
        if(n%8==3)vertices[n%6].x=512;
        if(n%8==4)vertices[n%6].y=-513;
        if(n%8==5)vertices[n%6].uv_source=-1;
        if(n%8==6)vertices[n%6].uv_source=3;
        UvDomain typed,reference;const auto expected=validate(bytes,&reference);
        assert(validate(vertices.data(),vertices.size(),&typed)==expected);
        if(expected==Error::none)for(unsigned axis=0;axis<2;++axis)
            assert(typed.low[axis]==reference.low[axis]&&typed.high[axis]==reference.high[axis]&&
                typed.fractional_bits[axis]==reference.fractional_bits[axis]);
    }
    assert(validate(static_cast<const NormalVertex*>(nullptr),6)==Error::invalid_data);
    assert(validate(vertices.data(),0)==Error::invalid_data);
    assert(validate(vertices.data(),5)==Error::invalid_data);
    assert(validate(vertices.data(),65538)==Error::invalid_data);
}
static void uv_significand_precision() {
    // Frozen shift-loop reference, independent of the new CTZ lowering. Cover
    // every finite exponent/sign and every possible nonzero trailing-bit count,
    // including subnormal values and both zero representations.
    for(unsigned exponent=0;exponent<255;++exponent)for(unsigned sign=0;sign<2;++sign)
        for(unsigned trailing=0;trailing<24;++trailing){
            const unsigned mantissa=(0x7fffffu<<trailing)&0x7fffffu;
            const std::uint32_t bits=(sign<<31)|(exponent<<23)|mantissa;
            float value;std::memcpy(&value,&bits,4);UvDomain domain;domain.include(value,-value);
            unsigned significand=mantissa|(exponent?0x800000u:0u),zeros=0;
            if(significand)while(!(significand&1)){++zeros;significand>>=1;}
            const int needed=significand?(exponent?150-int(exponent):149)-int(zeros):0;
            for(unsigned axis=0;axis<2;++axis)assert(domain.fractional_bits[axis]==(needed>0?needed:0));
            assert(domain.low[0]==(value<0?value:0)&&domain.high[0]==(value>0?value:0));
        }
}
static void texture_layout(unsigned w, unsigned h) {
    const auto bytes = std::size_t(w)*h*4;
    std::vector<std::uint8_t> input(bytes), output(bytes+32, 0xca);
    for (std::size_t i = 0; i < bytes; ++i) input[i] = std::uint8_t(i*17+i/71);
    assert(cook_rgba8(input.data(), bytes, w, h, output.data()+16, bytes) == Error::none);
    // Independently walk GPU tiles and de-interleave each Morton address.
    for (unsigned tile_y = 0; tile_y < h/8; ++tile_y)
        for (unsigned tile_x = 0; tile_x < w/8; ++tile_x)
            for (unsigned m = 0; m < 64; ++m) {
                unsigned x = 0, y = 0;
                for (unsigned bit = 0; bit < 3; ++bit) {
                    x |= ((m>>(bit*2))&1)<<bit;
                    y |= ((m>>(bit*2+1))&1)<<bit;
                }
                const auto src = ((h-1-(tile_y*8+y))*w+tile_x*8+x)*4;
                const auto dst = ((tile_y*(w/8)+tile_x)*64+m)*4+16;
                for (unsigned c = 0; c < 4; ++c) assert(output[dst+c] == input[src+3-c]);
            }
    for (unsigned i = 0; i < 16; ++i) assert(output[i] == 0xca && output[bytes+16+i] == 0xca);
    assert(cook_rgba8(input.data(), bytes-1, w,h,output.data()+16,bytes) == Error::invalid_data);
    assert(cook_rgba8(input.data(), bytes, w,h,input.data(),bytes) == Error::invalid_data);
    assert(cook_rgba8(input.data(), bytes, w,h,input.data()+1,bytes) == Error::invalid_data);
}
int main() {
    resources(); lifetime(); mesh_validation(); prepared_mesh_bytes();typed_triangle_stream();uv_significand_precision();
    // Differential classification across all exponents, both signs and
    // mantissa boundaries, in every matrix and typed-vertex component.
    for(unsigned exponent=0;exponent<256;++exponent)for(unsigned sign=0;sign<2;++sign)
        for(auto mantissa:{0u,1u,0x3fffffu,0x7fffffu}){
            const std::uint32_t bits=(sign<<31)|(exponent<<23)|mantissa;
            float value;std::memcpy(&value,&bits,4);const bool expected=std::isfinite(value);
            for(unsigned component=0;component<16;++component){auto m=identity();m.row[component/4][component%4]=value;
                assert(finite(m)==expected);}
            for(unsigned component=0;component<5;++component){NormalVertex vertices[3]{};auto& v=vertices[1].base;
                float* fields[]={&v.x,&v.y,&v.z,&v.u,&v.v};*fields[component]=value;
                assert(validate(vertices,3)==(expected?Error::none:Error::invalid_data));}
        }

    texture_layout(8,8); texture_layout(16,8); texture_layout(8,32); texture_layout(128,64);
    assert(!texture_dimensions(0,8) && !texture_dimensions(9,8) && !texture_dimensions(2048,8));
    std::puts("native renderer: resource lifetime, ownership, validation and texture layout passed");
}
