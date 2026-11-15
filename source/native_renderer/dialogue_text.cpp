#include "dialogue_text.hpp"
#include <algorithm>
namespace sm64ds::render {
void DialogueText::append(const DialogueCharacter* text,std::size_t count,std::uint8_t x,int y) noexcept {
    for(std::size_t i=0;i<count;++i){
        glyph_[used_++]={std::int16_t(x),std::int16_t(y),text[i].code,text[i].white};
        x=std::uint8_t(x+text[i].advance);
    }
}
Error DialogueText::line(const DialogueCharacter* text,std::size_t count,std::uint8_t x,unsigned row) noexcept {
    if((count&&!text)||row>255)return Error::invalid_data;
    if(count>capacity-used_)return Error::capacity;
    append(text,count,x,int(row)*16);return Error::none;
}
Error DialogueText::scroll(const DialogueCharacter* text,std::size_t count,std::uint8_t x,unsigned rows,unsigned phase) noexcept {
    if(!rows||rows>255||phase>12||phase%4||(count&&!text))return Error::invalid_data;
    std::size_t retained=0;
    for(std::size_t i=0;i<used_;++i)retained+=glyph_[i].y+16>4;
    if(!phase&&count>capacity-retained)return Error::capacity;
    const auto before=used_;used_=0;
    // Compact in place. A glyph partly above the page remains clipped, so its
    // remaining ink survives all four steps of the original sixteen-pixel scroll.
    for(std::size_t i=0;i<before;++i)if(glyph_[i].y+16>4){auto next=glyph_[i];next.y-=4;glyph_[used_++]=next;}
    if(!phase)append(text,count,x,int(rows)*16-4);
    return Error::none;
}
Error DialogueText::capture(SceneFrame& frame,Screen screen,float x,float y,const ScreenClip& clip,unsigned rows,unsigned width) const noexcept {
    if(!rows||!width||width>256||!valid(clip,screen_width(screen)))return Error::invalid_data;
    auto output=frame.quad_workspace();std::size_t vertices=0;
    // Original nibble masks are15(white) and13(black). OR painting makes white
    // ink dominate overlaps, independently of character/commit order.
    for(unsigned ink=0;ink<2;++ink)for(std::size_t i=0;i<used_;++i){const auto& g=glyph_[i];
        if(g.white!=bool(ink)||g.x>=int(width)||g.y>=int(rows*16)||g.y+16<=0)continue;
        if(output.capacity-vertices<4)return Error::capacity;
        const float left=float(g.x),top=float(g.y),u=float(g.code%32)*8/256,v=float(g.code/32)*16/128;
        const std::uint8_t c=g.white?255:0;
        output.data[vertices++]={left,top,0,u,v,c,c,c,255};
        output.data[vertices++]={left+8,top,0,u+8.f/256,v,c,c,c,255};
        output.data[vertices++]={left+8,top+16,0,u+8.f/256,v+16.f/128,c,c,c,255};
        output.data[vertices++]={left,top+16,0,u,v+16.f/128,c,c,c,255};
    }
    if(!vertices)return Error::none;
    ImageLayer layer;layer.bank_id=6000;layer.screen=screen;layer.order=4095;
    // A source edge at 2 mod4 lands on an LCD pixel centre at 5/4 scale.
    // PICA's rotated top-left fill rule otherwise excludes that glyph's first
    // column or includes the preceding glyph's last column. Move the quad one
    // raster subpixel toward the upper left, keeping exact texel boundaries
    // inside the intended glyph. The shift plus UV bias is < .2 source pixel,
    // the smallest nonzero distance from a sample to the next texel boundary.
    layer.rectangle={x-1.f/16,y-1.f/16,1.25f,1.25f};layer.clip=clip;layer.material.blend=Blend::alpha;
    layer.material.uv={1,0,1.f/16384,0,1,1.f/8192};
    return frame.append_image_quads(layer,output.data,vertices);
}
}
