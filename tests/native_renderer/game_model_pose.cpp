#include "game_model_pose.hpp"
#include <cassert>
#include <cmath>
using namespace sm64ds::render;
using namespace sm64ds::render::game;
extern "C" void MulMat4x3Mat4x3(const int*,const int*,int*);
int main() {
    std::uint32_t seed=1;
    for(unsigned trial=0;trial<10000;++trial){
        Affine local{},parent{},expected{};
        for(unsigned i=0;i<12;++i){seed=seed*1664525u+1013904223u;local.value[i]=int(seed%65537)-32768;
            seed=seed*1664525u+1013904223u;parent.value[i]=int(seed%65537)-32768;}
        MulMat4x3Mat4x3(local.value,parent.value,expected.value);
        const auto actual=compose(local,parent);
        for(unsigned i=0;i<12;++i)assert(actual.value[i]==expected.value[i]);
    }
    const PartBinding parts[]={{0,0,false},{0,0,true}};
    const ModelBindings bindings{261,{},1,1,nullptr,0,nullptr,0,parts,2};
    Affine bone=unit_affine(),view=unit_affine();
    bone.value[0]=bone.value[4]=bone.value[8]=3482;
    bone.value[9]=4096*3;view.value[10]=4096*7;
    Matrix ordinary{},billboard{};
    assert(build_poses(bindings,&bone,1,&view,nullptr,identity(),&ordinary,&billboard,1)==Error::none);
    assert(ordinary.row[0][0]==3482.f/4096 && ordinary.row[0][3]==3 && ordinary.row[1][3]==7);
    assert(billboard.row[0][0]==1 && billboard.row[1][1]==1 && billboard.row[2][2]==1);
    assert(billboard.row[0][3]==3 && billboard.row[1][3]==7);
    const std::int32_t scale[3]={8192,12288,4096};
    assert(build_poses(bindings,&bone,1,&view,scale,identity(),&ordinary,&billboard,1)==Error::none);
    assert(ordinary.row[0][3]==6 && billboard.row[0][3]==6);
    assert(billboard.row[0][0]==3 && billboard.row[1][1]==3);
    bone.value[3]=12288;bone.value[4]=16384;
    assert(build_poses(bindings,&bone,1,&view,scale,identity(),&ordinary,&billboard,1)==Error::none);
    assert(billboard.row[0][0]==9831.f/4096&&billboard.row[1][0]==-4916.f/4096);
    assert(billboard.row[0][1]==4916.f/4096&&billboard.row[1][1]==9831.f/4096);
    bone=unit_affine();bone.value[3]=0;bone.value[4]=0;bone.value[0]=3;bone.value[1]=4;
    assert(build_poses(bindings,&bone,1,nullptr,nullptr,identity(),&ordinary,&billboard,1)==Error::none);
    assert(billboard.row[0][0]==2458.f/4096 && billboard.row[1][0]==3277.f/4096);
    bone.value[0]=-3;bone.value[1]=-4;
    assert(build_poses(bindings,&bone,1,nullptr,nullptr,identity(),&ordinary,&billboard,1)==Error::none);
    assert(billboard.row[0][0]==-2458.f/4096 && billboard.row[1][0]==-3277.f/4096);
    bone.value[0]=bone.value[1]=0;
    assert(build_poses(bindings,&bone,1,nullptr,nullptr,identity(),&ordinary,&billboard,1)==Error::none);
    assert(billboard.row[0][0]==0 && billboard.row[1][0]==0);
    assert(build_poses(bindings,&bone,0,nullptr,nullptr,identity(),&ordinary,&billboard,1)==Error::invalid_data);
    // Independent original-normalization equation, with a floating sqrt seed
    // corrected in integer space, versus the native integer-root algorithm.
    for(unsigned trial=0;trial<10000;++trial){
        seed=seed*1664525u+1013904223u;const int x=int(seed%33554433)-16777216;
        seed=seed*1664525u+1013904223u;const int y=int(seed%33554433)-16777216;
        const std::uint64_t sq=std::uint64_t(std::int64_t(x)*x)+std::uint64_t(std::int64_t(y)*y);
        if(!sq)continue;
        auto length=std::uint64_t(std::sqrt(double(sq*4)));
        while(length*length>sq*4)--length;
        while((length+1)*(length+1)<=sq*4)++length;
        const auto factor=std::int64_t(((std::uint64_t(1)<<56)/sq)*length);
        const int nx=int((factor*x+(std::int64_t(1)<<44))>>45);
        const int ny=int((factor*y+(std::int64_t(1)<<44))>>45);
        bone=unit_affine();bone.value[3]=bone.value[4]=0;bone.value[0]=x;bone.value[1]=y;
        assert(build_poses(bindings,&bone,1,nullptr,nullptr,identity(),&ordinary,&billboard,1)==Error::none);
        assert(billboard.row[0][0]==float(nx)/4096 && billboard.row[1][0]==float(ny)/4096);
    }
}
