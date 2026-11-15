#include "dialogue_text.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <random>
using namespace sm64ds::render;
namespace {
constexpr unsigned width=256,rows=6,height=rows*16;
using Bitmap=std::array<std::uint8_t,width*height>;
bool ink(unsigned code,unsigned x,unsigned y){return ((code*1103515245u+x*2654435761u+y*12345u)>>(x%9))&1;}
void paint(Bitmap& bitmap,const DialogueCharacter* text,unsigned count,unsigned x,unsigned row){
    for(unsigned i=0;i<count;++i){
        for(unsigned y=0;y<16;++y)for(unsigned p=0;p<8&&x+p<width;++p)
            if(row*16+y<height&&ink(text[i].code,p,y))bitmap[(row*16+y)*width+x+p]|=text[i].white?15:13;
        x=(x+text[i].advance)&255;
    }
}
void compare(const DialogueText& text,const Bitmap& expected){
    Bitmap actual{};
    for(unsigned colour=0;colour<2;++colour)for(std::size_t i=0;i<text.size();++i){const auto& g=text.glyphs()[i];
        if(g.white!=bool(colour))continue;
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<8;++x){const int dx=g.x+int(x),dy=g.y+int(y);
            if(dx>=0&&dx<int(width)&&dy>=0&&dy<int(height)&&ink(g.code,x,y))actual[dy*width+dx]=g.white?15:13;
        }
    }
    assert(actual==expected);
}
}
int main(){
    DialogueText text;Bitmap bitmap{};std::mt19937 rng(0x4449414c);
    std::array<DialogueCharacter,24> incoming;
    auto stage=[&]{for(auto& c:incoming)c={std::uint8_t(rng()),std::uint8_t(2+rng()%7),bool(rng()&1)};};
    for(unsigned row=0;row<rows;++row){stage();const unsigned x=rng()%8;
        assert(text.line(incoming.data(),incoming.size(),std::uint8_t(x),row)==Error::none);
        paint(bitmap,incoming.data(),unsigned(incoming.size()),x,row);compare(text,bitmap);
    }
    // Independent raster algorithm: physically move scanlines and inject four
    // source rows. Retained geometry instead translates glyphs and clips them.
    for(unsigned line=0;line<100;++line){stage();const auto x=std::uint8_t(rng()%8);
        for(unsigned phase=0;phase<16;phase+=4){
            std::memmove(bitmap.data(),bitmap.data()+4*width,(height-4)*width);
            std::memset(bitmap.data()+(height-4)*width,0,4*width);
            unsigned cursor=x;
            for(const auto& c:incoming){
                for(unsigned y=0;y<4;++y)for(unsigned p=0;p<8&&cursor+p<width;++p)
                    if(ink(c.code,p,phase+y))bitmap[(height-4+y)*width+cursor+p]|=c.white?15:13;
                cursor=(cursor+c.advance)&255;
            }
            assert(text.scroll(incoming.data(),incoming.size(),x,rows,phase)==Error::none);compare(text,bitmap);
        }
    }
    const auto count=text.size();std::array<DialogueGlyph,DialogueText::capacity> saved;
    std::memcpy(saved.data(),text.glyphs(),count*sizeof(DialogueGlyph));
    assert(text.scroll(incoming.data(),incoming.size(),0,rows,2)==Error::invalid_data);
    assert(text.line(incoming.data(),DialogueText::capacity,0,0)==Error::capacity);
    assert(text.size()==count&&std::memcmp(saved.data(),text.glyphs(),count*sizeof(DialogueGlyph))==0);
    // Publication owns the geometry and preserves colour/clip independently of
    // subsequent parser changes. One atlas batch contains both ink selectors.
    std::array<Occurrence,1> occurrences;std::array<PreparedPart,1> parts;
    std::array<Vertex,DialogueText::capacity*4> arena;std::array<ImageLayer,1> images;
    SceneFrame frame(occurrences.data(),1,parts.data(),1,nullptr,0,nullptr,0,{arena.data(),arena.size(),nullptr,0},images.data(),1);
    assert(frame.begin(1,1)==Error::none);
    assert(text.capture(frame,Screen::top,50,40,{50,40,350,160},rows,width)==Error::none);
    assert(frame.seal(true,0)==Error::none);text.reset();SceneView view;
    assert(frame.acquire(view)==Error::none&&view.image_layers==1&&view.image_layer[0].bank_id==6000);
    assert(view.image_layer[0].vertices>0&&view.quad_states==0&&view.image_layer[0].clip.left==50);
    assert(frame.release()==Error::none);
}
