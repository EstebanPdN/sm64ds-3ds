#pragma once
#include <cstddef>
#include <cstdint>

// Views only. The ROM artwork owner outlives the loading session and releases
// its libc allocations before the game's heap/renderer/audio are started.
namespace loading_screen {
struct Image {
    const uint16_t* pixels=nullptr;
    unsigned width=0,height=0;
    // Pixels use A1R5G5B5. Bit 15 clear means transparent; opaque black is 8000.
    bool valid()const{return pixels&&width&&height&&width<=1024&&height<=1024;}
};
struct Font {
    Image image;
    const uint16_t* glyphs=nullptr; // Unicode Latin-1 0..255 -> cell; ffff = absent.
    const uint8_t* advances=nullptr; // Optional Latin-1 advance; defaults cell width.
    unsigned cell_width=0,cell_height=0;
    bool valid()const{return image.valid()&&glyphs&&cell_width&&cell_height&&
        cell_width<=image.width&&cell_height<=image.height;}
};
struct Artwork {
    Image sky,castle,pattern,logo,mario,yoshi,star,frame;
    Font title_font,body_font;
};
}
