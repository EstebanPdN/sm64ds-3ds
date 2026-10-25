// Shared lower-screen drawing helpers: diagnostics panel and menu background.
#include "enhance_overlay.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace enhance;
int main(){
    std::array<uint32_t,320*240+2> image;
    image.fill(0x12345678);
    render_menu_background(image.data()+1);
    assert(image.front()==0x12345678 && image.back()==0x12345678);
    for(size_t i=1;i<=320*240;++i)assert((image[i]>>24)==0xff); // opaque pattern
    image.fill(0x12345678);
    render_diagnostics(image.data()+1,{59.7,16.75,32*1024*1024,4*1024*1024,true});
    assert(image.front()==0x12345678 && image.back()==0x12345678);
    FILE* f=std::fopen("diagnostics-preview.ppm","wb");assert(f);
    std::fprintf(f,"P6\n320 240\n255\n");
    for(size_t i=1;i<=320*240;++i){const uint32_t c=image[i];unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};std::fwrite(rgb,1,3,f);}
    std::fclose(f);
    std::array<uint32_t,64*16> text{};
    assert(draw_game_text(text.data(),64,16,-8,-4,"WIDE TEXT CLIPS",false)>0);
    assert(draw_game_text(nullptr,64,16,0,0,"X")==0);
    std::puts("PASS lower-screen helpers: menu background, diagnostics panel and clipped game text");
}
