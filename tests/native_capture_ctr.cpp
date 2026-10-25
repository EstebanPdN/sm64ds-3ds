#include <3ds.h>
#include "sm64ds/game_renderer.hpp"
#include <array>
#include <cstdio>
#include <memory>
#include <vector>

// Synthetic GPU fixture only: checks the capture tap's actual image, channel
// order, orientation, original resolution and 2D/3D layering. No game code.
extern "C" {u32 __stacksize__=128*1024;}
namespace {
constexpr unsigned width=256,height=192;
constexpr std::uint32_t geometry_color=0xff287cc1;
std::uint32_t pattern(unsigned x,unsigned y) {
    return 0xff000000u|((x*37+y*3)&255)<<16|((x*5+y*29)&255)<<8|((x*11+y*7)&255);
}
bool overlay(unsigned x,unsigned y) {
    return (x>=7 && x<35 && y>=13 && y<29) || (x>=100 && x<150 && y>=72 && y<84)
        || (x>=224 && x<252 && y>=161 && y<185);
}
void rgba(std::uint32_t pixel,std::uint8_t* destination) {
    destination[0]=pixel>>16;destination[1]=pixel>>8;destination[2]=pixel;destination[3]=pixel>>24;
}
bool write_image(const char* label,const char* suffix,const std::vector<std::uint8_t>& image) {
    char path[256];std::snprintf(path,sizeof path,"sdmc:/3ds/sm64ds/capture-check/%s-%s.rgba",label,suffix);
    FILE* file=std::fopen(path,"wb");if(!file)return false;
    const bool okay=std::fwrite(image.data(),1,image.size(),file)==image.size();
    return std::fclose(file)==0 && okay;
}
}
int main() {
    gfxInitDefault();osSetSpeedupEnable(false);
    FILE* log=std::fopen("sdmc:/3ds/sm64ds/capture-check/result.log","w");
    if(!log) {gfxExit();return 1;}
    std::fprintf(log,"scope=synthetic_gpu_capture_fixture gameplay_executed=0 width=256 height=192\n");std::fflush(log);
    auto layers=std::make_unique<sm64ds::Native2DFrame>();
    layers->main_on_top=true;layers->has_3d=false;
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
        // The padding rows have a distinct color: sampling the wrong crop
        // cannot accidentally match the expected 192-row image.
        layers->underlay.pixels[y*256+x]=y<height?pattern(x,y):0xffef26b8;
        layers->overlay.pixels[y*256+x]=y<height && overlay(x,y)?0xffd85327:0;
        layers->bottom.pixels[y*256+x]=0xff19354b;
    }
    sm64ds::GameRenderer renderer;sm64ds::GameProjection projection;
    projection.native_matrix=true;projection.matrix={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::string error;bool okay=renderer.init(error),all_passed=okay;
    std::vector<std::uint8_t> actual(width*height*4),expected(actual.size());
    std::array<ntr::GxTriangle,2> rectangle{};
    constexpr float positions[6][2]={{-.5f,.5f},{.5f,.5f},{-.5f,-.5f},
                                    {.5f,.5f},{.5f,-.5f},{-.5f,-.5f}};
    for(unsigned i=0;i<6;++i) {
        auto& triangle=rectangle[i/3];triangle.alpha=31;
        auto& vertex=triangle.v[i%3];vertex.vx=positions[i][0];vertex.vy=positions[i][1];vertex.vz=0;
        vertex.color=geometry_color&0xffffff;
    }
    for(unsigned fixture=0;okay && fixture<2;++fixture) {
        const char* label=fixture?"layers-and-geometry":"layers-only";
        layers->has_3d=fixture;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            auto color=pattern(x,y);
            if(fixture && x>=64 && x<192 && y>=48 && y<144)color=geometry_color;
            if(overlay(x,y))color=0xffd85327;
            rgba(color,expected.data()+(y*width+x)*4);
        }
        okay=renderer.draw(fixture?rectangle.data():nullptr,fixture?rectangle.size():0,
                           projection,0,error,layers.get(),layers.get(),actual.data());
        unsigned mismatched_pixels=0;
        if(okay)for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const unsigned index=(y*width+x)*4;
            bool same=true;for(unsigned channel=0;channel<4;++channel)same&=actual[index+channel]==expected[index+channel];
            if(!same) {
                if(mismatched_pixels<8)std::fprintf(log,
                    "mismatch fixture=%s x=%u y=%u actual=%02x%02x%02x%02x expected=%02x%02x%02x%02x\n",
                    label,x,y,actual[index],actual[index+1],actual[index+2],actual[index+3],
                    expected[index],expected[index+1],expected[index+2],expected[index+3]);
                ++mismatched_pixels;
            }
        }
        const bool images_written=okay && write_image(label,"actual",actual) && write_image(label,"expected",expected);
        std::fprintf(log,"fixture=%s gpu_draw=%d images_written=%d mismatched_pixels=%u triangles=%u\n",
                     label,okay,images_written,mismatched_pixels,renderer.stats.triangles);std::fflush(log);
        all_passed=all_passed && okay && images_written && mismatched_pixels==0;
    }
    all_passed=all_passed && okay;
    std::fprintf(log,"exit=%s error=%s\n",all_passed?"passed":"failed",error.c_str());std::fclose(log);
    renderer.exit();gfxExit();return all_passed?0:1;
}
