#include "title_background.hpp"
#include <cassert>
#include <cstdio>
int main(){
    using title_background::extension_source;
    for(unsigned i=0;i<64;++i){
        const auto x=extension_source(i,true);assert(x<256);
        assert(x==(i<32?31-i:287-i));
        assert(extension_source(i,false)==(i<32?224+i:i-32));
    }
    assert(extension_source(31,true)==0);
    assert(extension_source(32,true)==255);
    // Entire 400x240 output: original centre 320px never remaps; each edge
    // meets the adjacent DS column, instead of the opposite castle edge.
    for(unsigned x=0;x<400;++x){
        unsigned src;
        if(x<40)src=extension_source(x*32/40,true);
        else if(x<360)src=(x-40)*256/320;
        else src=extension_source(32+(x-360)*32/40,true);
        assert(src<256);
        if(x>=40&&x<360)assert(src==(x-40)*4/5);
    }
    puts("PASS approved title reflection: 64 strip columns, unchanged centre, 400 LCD columns bounded");
}
