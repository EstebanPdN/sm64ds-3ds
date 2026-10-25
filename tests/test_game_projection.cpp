#include "sm64ds/game_projection.hpp"
#include "sm64ds/game_geometry.hpp"
#include "sm64ds/game_textures.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Matrix=sm64ds::GameProjectionMatrix;
// Numeric matrices observed by the read-only GX audit of the preserved first
// scene-1 frame. These are test parameters; no ROM or texture bytes are stored.
constexpr Matrix original={7.393310546875f,0,0,0,0,9.857177734375f,0,0,
    0,0,-1.176513671875f,-1.170654296875f,0,0,-2.34716796875f,0};
constexpr Matrix final={2.811279296875f,0,0,0,0,3.748046875f,0,0,
    0,0,-1.005126953125f,-1,0,0,-2.005126953125f,0};
unsigned checks=0,negative_inputs=0,clip_checks=0;
void require(bool value,const char* message) {
    ++checks;if(!value)throw std::runtime_error(message);
}
void near(double actual,double expected,const char* message) {
    require(std::fabs(actual-expected)<=2e-6*std::max(1.0,std::fabs(expected)),message);
}
std::array<double,4> ds_clip(const ntr::GxVertex& v,const Matrix& matrix) {
    std::array<double,4> clip{};
    for(unsigned c=0;c<4;++c)clip[c]=double(v.vx)*matrix[c]+double(v.vy)*matrix[c+4]+
        double(v.vz)*matrix[c+8]+matrix[c+12];
    return clip;
}
std::array<double,4> pica_clip(const ntr::GxVertex& v,const Matrix& rows) {
    std::array<double,4> clip{};
    for(unsigned r=0;r<4;++r)clip[r]=double(v.vx)*rows[r*4]+double(v.vy)*rows[r*4+1]+
        double(v.vz)*rows[r*4+2]+rows[r*4+3];
    return clip;
}
void target_clip(const ntr::GxVertex& vertex,const Matrix& matrix,
                 const sm64ds::GameProjection& settings,bool bottom,bool capture,float separation) {
    const auto ds=ds_clip(vertex,matrix);
    const auto result=pica_clip(vertex,sm64ds::game_projection_for_target(matrix,settings,bottom,capture,separation));
    const float horizontal=capture?1.0f:settings.source_aspect/((bottom?320.0f:400.0f)/240.0f);
    double stereo=0;
    if(std::fabs(matrix[11])>0.00001f && std::fabs(matrix[15])<0.00001f)
        stereo=separation*.5-matrix[11]*(separation/(2*settings.convergence))*matrix[0]*horizontal*vertex.vz;
    near(result[0],ds[1],"PICA sideways X changed original clip Y");
    near(result[1],-ds[0]*horizontal+stereo,"PICA sideways Y changed original clip X/aspect/stereo");
    near(result[2],(ds[2]-ds[3])*.5,"PICA depth conversion changed original clip depth");
    near(result[3],ds[3],"PICA clip W changed");
    ++clip_checks;
}
ntr::GxTriangle triangle(const Matrix& matrix) {
    ntr::GxTriangle t{};t.alpha=31;t.native_projection=true;
    std::copy(matrix.begin(),matrix.end(),t.projection);
    return t;
}
sm64ds::GameBatchKey key(const ntr::GxTriangle& t,const sm64ds::GameProjection& p,
                        const void* texture=nullptr,bool translucent=false) {
    sm64ds::GameBatchKey k;require(sm64ds::game_batch_key(t,p,texture,translucent,k),"valid batch key rejected");return k;
}
unsigned batch_count(const std::vector<ntr::GxTriangle>& triangles,const sm64ds::GameProjection& settings) {
    unsigned count=0;sm64ds::GameBatchKey previous;
    for(const auto& t:triangles) {
        const auto next=key(t,settings,t.tex,t.alpha<31);
        if(!count || previous!=next)++count;
        previous=next;
    }
    return count;
}
void fixtures() {
    sm64ds::GameProjection settings;settings.native_matrix=true;settings.matrix=final;
    auto a=triangle(original),b=triangle(final);
    const auto ka=key(a,settings),kb=key(b,settings);
    require(ka.matrix==original && kb.matrix==final && ka!=kb,"two original matrices merged into a batch");
    require(ka==key(a,settings),"equal projection/material did not merge");
    int texture_a=0,texture_b=0;
    require(key(a,settings,&texture_a)!=key(a,settings,&texture_b),"different textures merged");
    require(key(a,settings,nullptr,false)!=key(a,settings,nullptr,true),"opaque/translucent batches merged");
    for(unsigned wrap=1;wrap<16;++wrap) {
        b=a;b.wrap=wrap;require(ka!=key(b,settings),"different wraps merged");
    }
    b=a;b.projection[0]=std::nextafter(b.projection[0],std::numeric_limits<float>::infinity());
    require(ka!=key(b,settings),"unequal finite projections merged by tolerance");
    std::vector<ntr::GxTriangle> recorded(78);
    for(unsigned i=0;i<recorded.size();++i)recorded[i]=triangle(i<6?original:final);
    require(batch_count(recorded,settings)==2,"6+72 triangle projection sequence did not split into two batches");
    auto old_control=recorded;
    for(auto& t:old_control)t.native_projection=false;
    require(batch_count(old_control,settings)==1,"old final-matrix control did not merge the known bad batch");

    // The failure control is independent of the conversion helper: applying
    // the final DS matrix moves the observed vertex into the clip volume.
    ntr::GxVertex observed{};observed.vx=7.90771484375f;observed.vy=-7.907470703125f;observed.vz=-49.937744140625f;
    const auto right=ds_clip(observed,ka.matrix),wrong=ds_clip(observed,settings.matrix);
    require(right[1]<-right[3] && wrong[1]>-wrong[3],"old final-matrix control did not change clipping");
    require(std::fabs(right[1]/right[3]-wrong[1]/wrong[3])*96>71,"old control did not reproduce the measured displacement");
    for(const auto& matrix:{original,final})for(bool bottom:{false,true})for(bool capture:{false,true})
        for(float separation:{-.5f,0.0f,.5f})target_clip(observed,matrix,settings,bottom,capture,separation);

    // Existing manual capture fixtures remain zero-initialized and use the
    // frame's matrix. Copies of a batch do not borrow the caller's memory.
    ntr::GxTriangle manual{};settings.matrix={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    require(!manual.native_projection,"manual fixture default changed");
    const auto fallback=key(manual,settings);
    require(fallback.matrix==settings.matrix,"manual fixture lost global-matrix fallback");
    settings.matrix[0]=9;require(fallback.matrix[0]==1,"batch borrowed global matrix storage");
    a.projection[0]=99;require(ka.matrix==original,"batch borrowed triangle matrix storage");
    observed.vx=.5f;observed.vy=-.5f;observed.vz=0;
    settings.matrix=fallback.matrix;
    target_clip(observed,fallback.matrix,settings,false,true,0);
    const auto top=sm64ds::game_projection_for_target(fallback.matrix,settings,false,false,0);
    const auto bottom=sm64ds::game_projection_for_target(fallback.matrix,settings,true,false,0);
    const auto capture=sm64ds::game_projection_for_target(fallback.matrix,settings,false,true,0);
    near(top[4],-.8f,"upper-panel aspect scale changed");
    near(bottom[4],-1,"lower-panel aspect scale changed");
    near(capture[4],-1,"capture resolution scale changed");
    // Original horizontal and vertical viewport pixels receive equal output
    // magnification: 320x240 image within the 400x240 panel, or 256x192 capture.
    near(std::fabs(top[4])*400/256.0,top[1]*240/192.0,"upper-panel output stretches an axis");
    near(std::fabs(bottom[4])*320/256.0,bottom[1]*240/192.0,"lower-panel output stretches an axis");
    near(std::fabs(capture[4])*256/256.0,capture[1]*192/192.0,"capture output stretches an axis");

    const auto sentinel=key(manual,settings);
    for(unsigned component=0;component<16;++component)for(float invalid:{
        std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()}) {
        a=triangle(original);a.projection[component]=invalid;
        auto untouched=sentinel;
        require(!sm64ds::game_batch_key(a,settings,nullptr,false,untouched),"nonfinite triangle projection accepted");
        require(untouched==sentinel,"failed key selection changed its destination");
        auto invalid_settings=settings;invalid_settings.matrix[component]=invalid;
        require(!sm64ds::game_batch_key(manual,invalid_settings,nullptr,false,untouched),"nonfinite fallback projection accepted");
        ++negative_inputs;
    }
    settings.native_matrix=false;
    settings.matrix.fill(std::numeric_limits<float>::quiet_NaN());
    a=triangle(original);b=triangle(final);b.projection[0]=std::numeric_limits<float>::quiet_NaN();
    require(key(a,settings)==key(b,settings),"conventional projection used original per-triangle matrices");
    require(batch_count(recorded,settings)==1,"conventional path introduced projection batch splits");
}
std::vector<std::uint8_t> read(const char* path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("cannot open preserved snapshot");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::uint32_t word(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|
           (std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
}
struct SnapshotResult {unsigned triangles=0,projection0=0,projection1=0,batches=0,old_batches=0,old_bad_triangles=0;};
SnapshotResult snapshot(char** paths) {
    const auto gx=read(paths[0]),metadata=read(paths[1]),pixels=read(paths[2]),palettes=read(paths[3]);
    require(gx.size()==1466*8 && metadata.size()==44 && pixels.size()==524288 && palettes.size()==98304,"snapshot sizes changed");
    auto owned=std::make_unique<sm64ds::GameGeometry>();auto& geometry=*owned;
    std::memcpy(geometry.textures,pixels.data(),pixels.size());std::memcpy(geometry.palettes,palettes.data(),palettes.size());
    std::uint32_t* fields[]={&geometry.texture_capacity,&geometry.palette_capacity,&geometry.texture_generation,
        &geometry.palette_generation,&geometry.texture_uploads,&geometry.palette_uploads,&geometry.texture_bytes,&geometry.palette_bytes};
    for(unsigned i=0;i<8;++i)*fields[i]=word(metadata.data()+i*4);
    geometry.texture_transfer=word(metadata.data()+32)!=0;geometry.palette_transfer=word(metadata.data()+36)!=0;
    geometry.highlight_shading=word(metadata.data()+40)!=0;
    sm64ds::GameTextures textures(geometry);ntr::gx_reset();
    ntr::gx_set_texture_resolver(sm64ds::GameTextures::resolver,&textures);
    for(unsigned i=0;i<gx.size()/8;++i) {
        const auto port=word(gx.data()+i*8),value=word(gx.data()+i*8+4);
        if(port)ntr::gx_write_port(0x04000400+port*4,value);else ntr::gx_write_fifo(value);
    }
    require(ntr::gx_asset_stream_valid(),"real snapshot decoder rejected command/texture state");
    std::size_t count=0;const auto* decoded=ntr::gx_polygons(count);
    require(count==78,"real snapshot triangle count changed");
    std::vector<ntr::GxTriangle> triangles(decoded,decoded+count);
    sm64ds::GameProjection settings;settings.native_matrix=true;ntr::gx_get_projection(settings.matrix.data());
    require(settings.matrix==final,"real snapshot final projection changed");
    SnapshotResult result;result.triangles=count;
    for(const auto& t:triangles) {
        require(t.native_projection,"real triangle lacks original projection flag");
        const auto selected=key(t,settings,t.tex,t.alpha<31);
        if(selected.matrix==original)++result.projection0;
        else if(selected.matrix==final)++result.projection1;
        else require(false,"real triangle contains an unexpected projection");
        bool old_bad=false;
        for(const auto& vertex:t.v) {
            // Uniform conversion is tested independently for every real
            // vertex, both panels, capture and both stereo eye offsets.
            for(unsigned target=0;target<3;++target)for(float separation:{-.5f,0.0f,.5f})
                target_clip(vertex,selected.matrix,settings,target==1,target==2,separation);
            const auto correct=ds_clip(vertex,selected.matrix),wrong=ds_clip(vertex,settings.matrix);
            for(unsigned c=0;c<4;++c)old_bad|=std::fabs(correct[c]-wrong[c])>1e-5;
        }
        result.old_bad_triangles+=old_bad;
    }
    require(result.projection0==6 && result.projection1==72,"real projection groups differ from independent audit");
    require(result.old_bad_triangles==6,"old final-matrix control did not fail for six real triangles");
    result.batches=batch_count(triangles,settings);
    auto old_control=triangles;for(auto& t:old_control)t.native_projection=false;
    result.old_batches=batch_count(old_control,settings);
    // Material changes may already force separate batches; comparing their
    // count alone cannot prove correctness. The real clip control above can.
    require(result.batches>=result.old_batches,"retaining projection merged material batches");
    ntr::gx_set_texture_resolver(nullptr,nullptr);
    return result;
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=1 && argc!=5)throw std::runtime_error("expected optional GX, metadata, textures and palettes snapshots");
        fixtures();SnapshotResult actual;if(argc==5)actual=snapshot(argv+1);
        std::printf("{\"status\":\"passed\",\"checks\":%u,\"nonfinite_cases\":%u,\"clip_checks\":%u,"
            "\"snapshot_triangles\":%u,\"projection0_triangles\":%u,\"projection1_triangles\":%u,"
            "\"snapshot_batches\":%u,\"old_control_batches\":%u,\"old_control_bad_triangles\":%u}\n",
            checks,negative_inputs,clip_checks,actual.triangles,actual.projection0,actual.projection1,
            actual.batches,actual.old_batches,actual.old_bad_triangles);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"game projection test: %s\n",error.what());return 1;
    }
}
