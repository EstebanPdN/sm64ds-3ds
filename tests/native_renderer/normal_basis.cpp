#include "game_model_pose.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
using namespace sm64ds::render;
namespace game=sm64ds::render::game;
extern "C" void MulMat3x3Mat3x3(const int*,const int*,int*);
static int normalized(int v,std::uint64_t sq){
    if(!sq)return 0;
    auto length=std::uint64_t(std::sqrt(double(sq*4)));
    while(length*length>sq*4)--length;
    while((length+1)*(length+1)<=sq*4)++length;
    const auto factor=std::int64_t(((std::uint64_t(1)<<56)/sq)*length);
    return int((factor*v+(std::int64_t(1)<<44))>>45);
}
int main(){
    MaterialBinding material{0,8,8,ColourSource::emission,false,true};
    PartBinding parts[]={{0,0,false,1},{0,0,false,1}};
    ModelBindings binding{205,{},2,1,nullptr,0,&material,1,parts,2};
    std::array<game::Affine,2> bones{};std::array<unsigned char,104> records{};
    std::array<NormalBasis,2> result{};game::Affine view{};
    std::uint32_t seed=1;
    for(unsigned trial=0;trial<10000;++trial){
        for(unsigned k=0;k<9;++k){seed=seed*1664525u+1013904223u;bones[1].value[k]=int(seed%33554433)-16777216;
            seed=seed*1664525u+1013904223u;view.value[k]=int(seed%65537)-32768;}
        const bool normalize=trial%2;records[52+24]=normalize;
        game::Affine expected=bones[1];
        if(normalize)for(unsigned column=0;column<3;++column){
            std::uint64_t sq=0;for(unsigned i=0;i<3;++i){const auto v=expected.value[column*3+i];sq+=std::uint64_t(std::int64_t(v)*v);}
            for(unsigned i=0;i<3;++i)expected.value[column*3+i]=normalized(expected.value[column*3+i],sq);
        }
        NormalBasis combined;MulMat3x3Mat3x3(expected.value,view.value,combined.value);
        result[0].value[0]=123;
        assert(game::build_normal_bases(binding,bones.data(),records.data(),2,&view,result.data(),2)==Error::none);
        assert(result[0].value[0]==123&&!std::memcmp(result[1].value,combined.value,sizeof(combined.value)));
    }
    // BUILD131: the normalisation runs on the exact VFP divide/square root.
    // Small and extreme columns reach its declined-divide fallback (|v|^2 < 8)
    // and its correction steps; the reference above is independent of both.
    {
        const int edges[]={0,1,-1,2,-2,3,7,-7,63,64,-64,4095,4096,-4096,65535,-65536,0xffffff,-0xffffff,0x1000000,-0x1000000};
        const unsigned count=sizeof edges/sizeof edges[0];unsigned long checked=0;
        records[52+24]=1;view=game::unit_affine();
        for(unsigned a=0;a<count;++a)for(unsigned b=0;b<count;++b)for(unsigned c=0;c<count;++c){
            bones[1]={};
            for(unsigned column=0;column<3;++column){bones[1].value[column*3]=edges[(a+column)%count];
                bones[1].value[column*3+1]=edges[(b+2*column)%count];bones[1].value[column*3+2]=edges[(c+5*column)%count];}
            game::Affine expected=bones[1];
            for(unsigned column=0;column<3;++column){
                std::uint64_t sq=0;for(unsigned i=0;i<3;++i){const auto v=expected.value[column*3+i];sq+=std::uint64_t(std::int64_t(v)*v);}
                for(unsigned i=0;i<3;++i)expected.value[column*3+i]=normalized(expected.value[column*3+i],sq);
            }
            NormalBasis combined;MulMat3x3Mat3x3(expected.value,view.value,combined.value);
            assert(game::build_normal_bases(binding,bones.data(),records.data(),2,&view,result.data(),2)==Error::none);
            assert(!std::memcmp(result[1].value,combined.value,sizeof(combined.value)));++checked;
        }
        for(unsigned trial=0;trial<200000;++trial){
            const int range=trial%4==0?7:trial%4==1?300:trial%4==2?70000:16777216;
            for(unsigned k=0;k<9;++k){seed=seed*1664525u+1013904223u;bones[1].value[k]=int((seed>>4)%unsigned(2*range+1))-range;}
            game::Affine expected=bones[1];
            for(unsigned column=0;column<3;++column){
                std::uint64_t sq=0;for(unsigned i=0;i<3;++i){const auto v=expected.value[column*3+i];sq+=std::uint64_t(std::int64_t(v)*v);}
                for(unsigned i=0;i<3;++i)expected.value[column*3+i]=normalized(expected.value[column*3+i],sq);
            }
            NormalBasis combined;MulMat3x3Mat3x3(expected.value,view.value,combined.value);
            assert(game::build_normal_bases(binding,bones.data(),records.data(),2,&view,result.data(),2)==Error::none);
            assert(!std::memcmp(result[1].value,combined.value,sizeof(combined.value)));++checked;
        }
        assert(checked==count*count*count+200000);
    }
    bones[1]={};records[52+24]=1;
    assert(game::build_normal_bases(binding,bones.data(),records.data(),2,nullptr,result.data(),2)==Error::none);
    for(int v:result[1].value)assert(v==0);
    bones[1].value[0]=0x1000001;
    assert(game::build_normal_bases(binding,bones.data(),records.data(),2,nullptr,result.data(),2)==Error::unsupported);
    parts[0].billboard=true;
    assert(game::build_normal_bases(binding,bones.data(),records.data(),2,nullptr,result.data(),2)==Error::unsupported);
    parts[0].billboard=false;parts[0].normal_bone=2;
    assert(game::build_normal_bases(binding,bones.data(),records.data(),2,nullptr,result.data(),2)==Error::invalid_data);
    material.normal_uv=false;
    assert(game::build_normal_bases(binding,nullptr,nullptr,0,nullptr,nullptr,0)==Error::none);
    material.colour=ColourSource::lighting;material.normal_uv=true;
    parts[0]={0,0,false,1,1};parts[1]=parts[0];parts[1].billboard=true;
    bones[1]=game::unit_affine();bones[1].value[0]=8192;bones[1].value[4]=12288;
    view=game::unit_affine();view.value[0]=view.value[4]=0;view.value[1]=4096;view.value[3]=-4096;
    LiveMaterial live;live.lighting.mask=1;
    std::array<NormalBasis,2> ordinary{},billboard{};
    const std::int32_t scale[3]={8192,12288,4096};
    assert(game::build_lighting_bases(binding,bones.data(),records.data(),2,&view,scale,&live,ordinary.data(),billboard.data(),2)==Error::none);
    assert(ordinary[1].value[1]==4096&&ordinary[1].value[3]==-4096&&ordinary[1].value[8]==4096);
    assert(billboard[1].value[0]==0&&billboard[1].value[1]==4096&&billboard[1].value[3]==-4096);
    // A basis already prepared for a visible ordinary texture-normal part is
    // exactly reusable for lighting, independent of position scale.
    parts[1].billboard=false;
    assert(game::build_normal_bases(binding,bones.data(),records.data(),2,&view,result.data(),2,&live)==Error::none);
    auto expected=result[1];
    assert(game::build_lighting_bases(binding,bones.data(),records.data(),2,&view,scale,&live,ordinary.data(),nullptr,2,result.data())==Error::none);
    assert(!std::memcmp(ordinary[1].value,expected.value,sizeof(expected)));
    live.visible=false;
    assert(game::build_lighting_bases(binding,nullptr,nullptr,0,nullptr,nullptr,&live,nullptr,nullptr,0)==Error::none);
    assert(game::build_normal_bases(binding,nullptr,nullptr,0,nullptr,nullptr,0,&live)==Error::none);
}
