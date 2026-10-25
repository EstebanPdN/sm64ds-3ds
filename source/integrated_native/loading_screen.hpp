#pragma once
// Original software artwork, bitmap lettering and progress model. No game data,
// graphics services, threads, textures or heap-backed containers are needed.
#include "loading_art.hpp"
#include "loading_locale.hpp"
#include "loading_style.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace loading_screen {
struct Color { uint8_t r,g,b; };
enum class Format { Rgb8, Bgr8, Rgba8, Rgb565, Rgb5A1, Rgba4 };
struct Surface {
    unsigned char* pixels=nullptr;
    size_t bytes=0;
    unsigned width=0,height=0;
    Format format=Format::Rgb8;
    bool rotated=false;
    unsigned pixel_bytes() const {
        return format==Format::Rgb8 || format==Format::Bgr8 ? 3u : format==Format::Rgba8 ? 4u : 2u;
    }
    bool valid() const {
        return pixels && width && height && width<=2048 && height<=2048 &&
               uint64_t(width)*height*pixel_bytes()<=bytes;
    }
    void pixel(int x,int y,Color c) const {
        if (!valid() || x<0 || y<0 || unsigned(x)>=width || unsigned(y)>=height) return;
        const size_t index=rotated ? size_t(x)*height+(height-1-unsigned(y)) : size_t(y)*width+unsigned(x);
        auto* p=pixels+index*pixel_bytes();
        uint16_t packed=0;
        switch(format) {
        case Format::Rgb8: p[0]=c.r;p[1]=c.g;p[2]=c.b;return;
        case Format::Bgr8: p[0]=c.b;p[1]=c.g;p[2]=c.r;return;
        // GSP formats are stored from the least-significant component first.
        case Format::Rgba8: p[0]=255;p[1]=c.b;p[2]=c.g;p[3]=c.r;return;
        case Format::Rgb565: packed=uint16_t((c.r>>3)<<11 | (c.g>>2)<<5 | (c.b>>3));break;
        case Format::Rgb5A1: packed=uint16_t((c.r>>3)<<11 | (c.g>>3)<<6 | (c.b>>3)<<1 | 1u);break;
        case Format::Rgba4: packed=uint16_t((c.r>>4)<<12 | (c.g>>4)<<8 | (c.b>>4)<<4 | 15u);break;
        }
        p[0]=uint8_t(packed);p[1]=uint8_t(packed>>8);
    }
};
inline Color mix(Color a,Color b,unsigned step,unsigned total) {
    if (!total) return a;
    if (step>total) step=total;
    return {uint8_t((unsigned(a.r)*(total-step)+unsigned(b.r)*step)/total),
            uint8_t((unsigned(a.g)*(total-step)+unsigned(b.g)*step)/total),
            uint8_t((unsigned(a.b)*(total-step)+unsigned(b.b)*step)/total)};
}
inline void rect(const Surface& s,int x,int y,int w,int h,Color color) {
    if (!s.valid() || w<=0 || h<=0) return;
    const int left=x<0?0:x,top=y<0?0:y;
    const int64_t far_x=int64_t(x)+w,far_y=int64_t(y)+h;
    const int right=far_x>int64_t(s.width)?int(s.width):int(far_x);
    const int bottom=far_y>int64_t(s.height)?int(s.height):int(far_y);
    if(s.rotated&&s.format==Format::Bgr8){
        if(left>=right||top>=bottom)return;
        for(int cx=left;cx<right;++cx){
            auto* out=s.pixels+3*(size_t(cx)*s.height+(s.height-1-unsigned(top)));
            for(int cy=top;cy<bottom;++cy){out[0]=color.b;out[1]=color.g;out[2]=color.r;if(cy+1<bottom)out-=3;}
        }
        return;
    }
    for(int cy=top;cy<bottom;++cy) for(int cx=left;cx<right;++cx) s.pixel(cx,cy,color);
}
inline void gradient(const Surface& s,Color first,Color last) {
    if (!s.valid()) return;
    for(unsigned y=0;y<s.height;++y) rect(s,0,int(y),int(s.width),1,mix(first,last,y,s.height>1?s.height-1:1));
}
inline void circle(const Surface& s,int x,int y,int radius,Color color) {
    if (!s.valid() || radius<0 || radius>2048) return;
    for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx)
        if(int64_t(dx)*dx+int64_t(dy)*dy<=int64_t(radius)*radius) s.pixel(x+dx,y+dy,color);
}
inline void rounded(const Surface& s,int x,int y,int w,int h,int radius,Color color) {
    if(w<=0 || h<=0 || radius<0) return;
    radius=radius>w/2?w/2:radius;radius=radius>h/2?h/2:radius;
    rect(s,x+radius,y,w-2*radius,h,color);rect(s,x,y+radius,w,h-2*radius,color);
    for(int dy=0;dy<radius;++dy) for(int dx=0;dx<radius;++dx) {
        const int rx=radius-1-dx,ry=radius-1-dy;
        if(rx*rx+ry*ry<=radius*radius) {
            s.pixel(x+dx,y+dy,color);s.pixel(x+w-1-dx,y+dy,color);
            s.pixel(x+dx,y+h-1-dy,color);s.pixel(x+w-1-dx,y+h-1-dy,color);
        }
    }
}
struct Point {int x,y;};
inline int64_t edge(Point a,Point b,Point c) {
    return (int64_t(c.x)-a.x)*(int64_t(b.y)-a.y)-(int64_t(c.y)-a.y)*(int64_t(b.x)-a.x);
}
inline void triangle(const Surface& s,Point a,Point b,Point c,Color color) {
    if(!s.valid())return;
    int left=a.x<b.x?a.x:b.x;left=left<c.x?left:c.x;left=left<0?0:left;
    int right=a.x>b.x?a.x:b.x;right=right>c.x?right:c.x;right=right>=int(s.width)?int(s.width)-1:right;
    int top=a.y<b.y?a.y:b.y;top=top<c.y?top:c.y;top=top<0?0:top;
    int bottom=a.y>b.y?a.y:b.y;bottom=bottom>c.y?bottom:c.y;bottom=bottom>=int(s.height)?int(s.height)-1:bottom;
    const int64_t winding=edge(a,b,c);if(!winding)return;
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
        const Point p{x,y};const auto e1=edge(a,b,p),e2=edge(b,c,p),e3=edge(c,a,p);
        if(winding>0?(e1>=0&&e2>=0&&e3>=0):(e1<=0&&e2<=0&&e3<=0))s.pixel(x,y,color);
    }
}
inline void star(const Surface& s,int cx,int cy,int size,Color color) {
    // Ten original integer vertices; triangle fan has no raster/texture payload.
    constexpr Point vertices[]={{0,-20},{6,-6},{20,-6},{9,3},{13,19},{0,10},{-13,19},{-9,3},{-20,-6},{-6,-6}};
    for(unsigned i=0;i<10;++i)triangle(s,{cx,cy},{cx+vertices[i].x*size/20,cy+vertices[i].y*size/20},
        {cx+vertices[(i+1)%10].x*size/20,cy+vertices[(i+1)%10].y*size/20},color);
}

// Five columns, low seven bits per column; simple lettering authored here.
inline const unsigned char* glyph(char letter) {
    static constexpr unsigned char alphabet[][5]={
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
        {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x41,0x41,0x7f,0x41,0x41},
        {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
        {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
        {0x0f,0x30,0x40,0x30,0x0f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
        {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x62,0x51,0x49,0x49,0x46},
        {0x22,0x41,0x49,0x49,0x36},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
        {0x06,0x49,0x49,0x29,0x1e},
        {0,0,0,0,0},{0x00,0x60,0x60,0x00,0x00},{0x00,0x36,0x36,0x00,0x00},
        {0x08,0x08,0x08,0x08,0x08},{0x20,0x10,0x08,0x04,0x02},{0x63,0x13,0x08,0x64,0x63},
        {0x02,0x01,0x59,0x09,0x06},{0x08,0x08,0x3e,0x1c,0x08},{0,0,0x5f,0,0}
    };
    if(letter>='a'&&letter<='z')letter=char(letter-'a'+'A');
    if(letter>='A'&&letter<='Z')return alphabet[letter-'A'];
    if(letter>='0'&&letter<='9')return alphabet[26+letter-'0'];
    switch(letter){case ' ':return alphabet[36];case '.':return alphabet[37];case ':':return alphabet[38];
        case '-':return alphabet[39];case '/':return alphabet[40];case '%':return alphabet[41];
        case '>':return alphabet[43];case '!':return alphabet[44];default:return alphabet[42];}
}
inline unsigned text_width(const char* text,unsigned scale=1) {
    return text&&*text?unsigned(std::strlen(text))*6*scale-scale:0;
}
inline void text(const Surface& s,int x,int y,const char* value,Color color,unsigned scale=1) {
    if(!value || !scale || scale>8)return;
    for(;*value;++value,x+=int(6*scale)){
        const auto* columns=glyph(*value);
        for(unsigned col=0;col<5;++col)for(unsigned row=0;row<7;++row)
            if(columns[col]&(1u<<row))rect(s,x+int(col*scale),y+int(row*scale),int(scale),int(scale),color);
    }
}
inline void centered(const Surface& s,int y,const char* value,Color color,unsigned scale=1) {
    text(s,(int(s.width)-int(text_width(value,scale)))/2,y,value,color,scale);
}
inline void wrap(const Surface& s,int x,int y,const char* value,unsigned columns,unsigned max_lines,Color color,unsigned scale=1) {
    if(!value || !columns || columns>79)return;
    for(unsigned line=0;*value&&line<max_lines;++line){
        while(*value==' ')++value;
        size_t length=0,last_space=0;
        while(value[length]&&value[length]!='\n'&&length<columns){if(value[length]==' ')last_space=length;++length;}
        if(length==columns&&value[length]&&value[length]!=' '&&last_space)length=last_space;
        char part[80];std::memcpy(part,value,length);part[length]=0;
        text(s,x,y,part,color,scale);value+=length;if(*value=='\n')++value;y+=int(10*scale);
    }
}
inline void copy_text(char* target,size_t capacity,const char* value) {
    if(!capacity)return;
    if(!value)value="";
    size_t n=0;while(n+1<capacity&&value[n]){target[n]=value[n];++n;}
    if(value[n])while(n&&(static_cast<unsigned char>(value[n])&0xc0)==0x80)--n;
    target[n]=0;
}
inline char* decimal(char* target,unsigned value) {
    char reversed[10];unsigned length=0;
    do{reversed[length++]=char('0'+value%10);value/=10;}while(value);
    while(length)*target++=reversed[--length];
    *target=0;return target;
}
// Exact integer mapping even for a UINT64_MAX total, without 128-bit integers.
inline unsigned ratio(uint64_t done,uint64_t total,unsigned span) {
    if(!total||!span)return 0;
    if(done>=total)return span;
    const uint64_t q=total/span,r=total%span;
    unsigned lo=0,hi=span;
    while(lo<hi){const unsigned mid=(lo+hi+1)/2;
        const uint64_t threshold=q*mid+(r*mid+span-1)/span;
        if(done>=threshold)lo=mid;else hi=mid-1;
    }
    return lo;
}
struct State {
    char headline[80]{};
    char detail[192]{};
    unsigned percent=0,files_done=0,files_total=0;
    bool failed=false,preparing_files=false;
    unsigned phase_group=0;
    const Artwork* artwork=nullptr;
    Language language=Language::English;
    explicit State(Language selected=Language::English):language(normalize(selected)){
        copy_text(headline,sizeof(headline),translated(language,Label::Initial));
        copy_text(detail,sizeof(detail),translated(language,Label::Intro));
    }
    void advance(unsigned requested){requested=requested>100?100:requested;if(requested>percent)percent=requested;}
    void work(const char* phase,uint64_t done,uint64_t total,unsigned complete,unsigned count) {
        files_done=complete>count?count:complete;files_total=count;
        preparing_files=phase&&(std::strcmp(phase,"extract")==0||std::strcmp(phase,"write-package")==0);
        Label title,description;
        if(phase&&std::strcmp(phase,"verify-rom")==0){
            phase_group=1;title=Label::VerifyRom;description=Label::VerifyRomDetail;advance(ratio(done,total,10));
        }else if(phase&&std::strcmp(phase,"write-package")==0){
            phase_group=2;title=Label::Extract;description=Label::ExtractDetail;advance(10+ratio(done,total,65));
        }else if(phase&&std::strcmp(phase,"verify-package")==0){
            phase_group=7;title=Label::VerifyCache;description=Label::VerifyCacheDetail;advance(75+ratio(done,total,20));
        }else if(phase&&std::strcmp(phase,"finish")==0){
            phase_group=3;title=Label::Finish;description=Label::FinishDetail;advance(95);
        }else{
            phase_group=2;const bool extracting=phase&&std::strcmp(phase,"extract")==0;
            title=extracting?Label::Extract:Label::VerifyCache;description=extracting?Label::ExtractDetail:Label::VerifyCacheDetail;
            advance(10+ratio(done,total,85));
        }
        copy_text(headline,sizeof(headline),translated(language,title));copy_text(detail,sizeof(detail),translated(language,description));
    }
    void stage(Stage value){
        preparing_files=false;
        phase_group=value==Stage::Ready?5:4;
        copy_text(headline,sizeof(headline),translated(language,value==Stage::Ready?Label::Ready:Label::Audio));
        detail[0]=0;advance(value==Stage::Ready?100:97);
    }
    void error(Error value,const char* diagnostic=nullptr){
        failed=true;preparing_files=false;phase_group=6;copy_text(headline,sizeof(headline),error_title(language,value));
        copy_text(detail,sizeof(detail),error_help(language,value));
        if(diagnostic&&*diagnostic){const size_t used=std::strlen(detail);if(used+1<sizeof(detail)){
            detail[used]='\n';copy_text(detail+used+1,sizeof(detail)-used-1,diagnostic);}}
    }
    // Compatibility for caller-supplied diagnostic labels. Production setup uses
    // the typed stages and errors above so all instructions follow the console.
    void stage(const char* value,unsigned requested){preparing_files=false;copy_text(headline,sizeof(headline),value);detail[0]=0;advance(requested);}
    void error(const char* title,const char* message){failed=true;preparing_files=false;copy_text(headline,sizeof(headline),title);copy_text(detail,sizeof(detail),message);}
};

inline const char* progress_hint(const State& state) {
    if(state.failed||(state.phase_group>=4&&state.phase_group<=6))return "";
    if(state.percent>90)return translated(state.language,Label::AlmostThere);
    return state.preparing_files?translated(state.language,Label::LongWait):"";
}
struct ButtonRect {int x,y,width,height;};
inline constexpr ButtonRect cancel_button{112,204,96,28};
inline constexpr ButtonRect exit_button{92,204,136,28};
inline constexpr bool cancel_button_hit(int x,int y,ButtonRect button=cancel_button) {
    return x>=button.x&&x<button.x+button.width&&y>=button.y&&y<button.y+button.height;
}

struct CancelTouch {
    bool armed=false;
    bool update(bool down,bool held,bool visible,int x,int y,ButtonRect button=cancel_button) {
        if(!held)armed=true;
        if(!visible){armed=false;return false;}
        return armed&&down&&cancel_button_hit(x,y,button);
    }
};

// A validated view is used for the complete blit; the inner loop never calls
// Surface::pixel or performs per-pixel bounds/format discovery on the 3DS path.
inline Color image_color(uint16_t value) {
    const unsigned r=(value>>10)&31,g=(value>>5)&31,b=value&31;
    return {uint8_t((r<<3)|(r>>2)),uint8_t((g<<3)|(g>>2)),uint8_t((b<<3)|(b>>2))};
}
inline void unchecked_pixel(const Surface& s,int x,int y,uint16_t color) {
    const size_t index=s.rotated?size_t(x)*s.height+(s.height-1-unsigned(y)):size_t(y)*s.width+unsigned(x);
    auto* out=s.pixels+index*s.pixel_bytes();const Color c=image_color(color);
    uint16_t packed=0;
    switch(s.format){
    case Format::Rgb8:out[0]=c.r;out[1]=c.g;out[2]=c.b;return;
    case Format::Bgr8:out[0]=c.b;out[1]=c.g;out[2]=c.r;return;
    case Format::Rgba8:out[0]=255;out[1]=c.b;out[2]=c.g;out[3]=c.r;return;
    case Format::Rgb565:packed=uint16_t((c.r>>3)<<11|(c.g>>2)<<5|(c.b>>3));break;
    case Format::Rgb5A1:packed=uint16_t((c.r>>3)<<11|(c.g>>3)<<6|(c.b>>3)<<1|1u);break;
    case Format::Rgba4:packed=uint16_t((c.r>>4)<<12|(c.g>>4)<<8|(c.b>>4)<<4|15u);break;
    }
    out[0]=uint8_t(packed);out[1]=uint8_t(packed>>8);
}
inline void blit(const Surface& s,const Image& image,int dx,int dy,int dw,int dh,
                 unsigned sx=0,unsigned sy=0,unsigned sw=0,unsigned sh=0,uint16_t tint=0) {
    if(!s.valid()||!image.valid()||dw<=0||dh<=0)return;
    if(!sw)sw=image.width;
    if(!sh)sh=image.height;
    if(uint64_t(sx)+sw>image.width||uint64_t(sy)+sh>image.height)return;
    const int left=dx<0?0:dx,top=dy<0?0:dy;
    const int right=int64_t(dx)+dw>int64_t(s.width)?int(s.width):int(int64_t(dx)+dw);
    const int bottom=int64_t(dy)+dh>int64_t(s.height)?int(s.height):int(int64_t(dy)+dh);
    if(left>=right||top>=bottom)return;
    const uint64_t start_x=uint64_t(int64_t(left)-dx)*sw,start_y=uint64_t(int64_t(top)-dy)*sh;
    unsigned source_x=unsigned(start_x/unsigned(dw)),remainder_x=unsigned(start_x%unsigned(dw));
    const unsigned step_x=sw/unsigned(dw),fraction_x=sw%unsigned(dw);
    const unsigned first_y=unsigned(start_y/unsigned(dh)),first_remainder_y=unsigned(start_y%unsigned(dh));
    const unsigned step_y=sh/unsigned(dh),fraction_y=sh%unsigned(dh);
    const bool fast=s.rotated&&s.format==Format::Bgr8;
    for(int x=left;x<right;++x){
        unsigned source_y=first_y,remainder_y=first_remainder_y;
        unsigned char* out=fast?s.pixels+3*(size_t(x)*s.height+(s.height-1-unsigned(top))):nullptr;
        for(int y=top;y<bottom;++y){
            const uint16_t color=image.pixels[size_t(sy+source_y)*image.width+sx+source_x];
            if(color&0x8000u){
                if(fast){const Color c=image_color(tint?tint:color);out[0]=c.b;out[1]=c.g;out[2]=c.r;}
                else unchecked_pixel(s,x,y,tint?tint:color);
            }
            source_y+=step_y;remainder_y+=fraction_y;
            if(remainder_y>=unsigned(dh)){++source_y;remainder_y-=unsigned(dh);}
            if(fast&&y+1<bottom)out-=3;
        }
        source_x+=step_x;remainder_x+=fraction_x;
        if(remainder_x>=unsigned(dw)){++source_x;remainder_x-=unsigned(dw);}
    }
}
inline void tiled(const Surface& s,const Image& image) {
    if(!s.valid()||!image.valid())return;
    for(unsigned y=0;y<s.height;y+=image.height)for(unsigned x=0;x<s.width;x+=image.width)
        blit(s,image,int(x),int(y),int(image.width),int(image.height));
}
inline unsigned font_advance(const Font& font,unsigned char c) {
    if(!font.valid())return 6;
    return font.advances&&font.advances[c]?font.advances[c]:font.cell_width;
}
inline unsigned menu_width(const Font& font,const char* value) {
    if(!value)return 0;
    unsigned width=0;
    while(*value)width+=font_advance(font,next_latin1(value));
    return width;
}
inline void percent_glyph(const Surface& s,int x,int y,unsigned height) {
    const Image image{loading_style::percent_pixels,loading_style::percent_width,loading_style::percent_height};
    blit(s,image,x,y+int(height>image.height?(height-image.height)/2:0),int(image.width),int(image.height));
}
inline char fallback_base(uint8_t value,unsigned& accent) {
    accent=0;
    if(value<128)return char(value);
    unsigned upper=value>=0xe0&&value<=0xfe?value-32:value;
    if(upper>=0xc0&&upper<=0xc5){accent=upper-0xc0+1;return 'A';}
    if(upper==0xc7){accent=7;return 'C';}
    if(upper>=0xc8&&upper<=0xcb){accent=upper==0xcb?5:upper-0xc8+1;return 'E';}
    if(upper>=0xcc&&upper<=0xcf){accent=upper==0xcf?5:upper-0xcc+1;return 'I';}
    if(upper==0xd1){accent=4;return 'N';}
    if(upper>=0xd2&&upper<=0xd6){accent=upper-0xd2+1;return 'O';}
    if(upper>=0xd9&&upper<=0xdc){accent=upper==0xdc?5:upper-0xd9+1;return 'U';}
    if(upper==0xdd){accent=2;return 'Y';}
    if(value==0xff){accent=5;return 'Y';}
    if(value==0xdf)return 'B';
    return '?';
}
inline void fallback_text(const Surface& s,int x,int y,const char* value,Color color) {
    if(!value)return;
    static constexpr unsigned char quote[]={0,5,3,0,0},comma[]={0,0x50,0x30,0,0};
    static constexpr unsigned char sharp_s[]={0x7e,0x09,0x49,0x49,0x36};
    while(*value){
        const uint8_t code=next_latin1(value);unsigned accent=0;const char base=fallback_base(code,accent);
        const auto* columns=code==0xdf?sharp_s:base=='\''?quote:base==','?comma:glyph(base);
        for(unsigned col=0;col<5;++col)for(unsigned row=0;row<7;++row)
            if(columns[col]&(1u<<row))s.pixel(x+int(col),y+2+int(row),color);
        switch(accent){
        case 1:s.pixel(x+1,y,color);s.pixel(x+2,y+1,color);break;
        case 2:s.pixel(x+3,y,color);s.pixel(x+2,y+1,color);break;
        case 3:s.pixel(x+1,y+1,color);s.pixel(x+2,y,color);s.pixel(x+3,y+1,color);break;
        case 4:s.pixel(x,y+1,color);s.pixel(x+1,y,color);s.pixel(x+2,y+1,color);s.pixel(x+3,y,color);break;
        case 5:s.pixel(x+1,y,color);s.pixel(x+3,y,color);break;
        case 6:s.pixel(x+2,y,color);s.pixel(x+1,y+1,color);s.pixel(x+3,y+1,color);break;
        case 7:s.pixel(x+2,y+9,color);s.pixel(x+1,y+10,color);break;
        default:break;
        }
        x+=6;
    }
}
inline size_t line_bytes(const Font& font,const char* value,unsigned width,size_t capacity=79) {
    if(!capacity)return 0;
    if(capacity>79)capacity=79;
    const char* cursor=value;unsigned used=0;size_t length=0,last_space=0;
    while(*cursor&&*cursor!='\n'){
        const char* begin=cursor;const uint8_t code=next_latin1(cursor);const unsigned advance=font_advance(font,code);
        if(size_t(cursor-value)>capacity||used+advance>width)break;
        if(code==' ')last_space=size_t(begin-value);
        used+=advance;length=size_t(cursor-value);
    }
    if(!length&&*value&&*value!='\n'){cursor=value;next_latin1(cursor);const size_t first=size_t(cursor-value);length=first<=capacity?first:0;}
    if(value[length]&&value[length]!=' '&&value[length]!='\n'&&last_space)length=last_space;
    return length;
}
inline void menu_text(const Surface& s,const Font& font,int x,int y,const char* value,Color fallback={255,215,73},uint16_t tint=0) {
    if(!value)return;
    if(!font.valid()){fallback_text(s,x+1,y+1,value,{157,53,4});fallback_text(s,x,y,value,fallback);return;}
    const unsigned columns=font.image.width/font.cell_width,rows=font.image.height/font.cell_height;
    while(*value){
        const unsigned char c=next_latin1(value);
        uint16_t cell=font.glyphs[c];
        if(cell==0xffff&&c>='a'&&c<='z')cell=font.glyphs[c-'a'+'A'];
        if(cell!=0xffff&&cell<uint64_t(columns)*rows)
            blit(s,font.image,x,y,int(font.cell_width),int(font.cell_height),
                unsigned(cell%columns)*font.cell_width,unsigned(cell/columns)*font.cell_height,font.cell_width,font.cell_height,tint);
        else if(c=='%')percent_glyph(s,x,y,font.cell_height);
        x+=int(font_advance(font,c));
    }
}
inline void menu_center(const Surface& s,const Font& font,int y,const char* value,Color fallback={255,215,73}) {
    menu_text(s,font,(int(s.width)-int(menu_width(font,value)))/2,y,value,fallback);
}
inline void menu_shadow_center(const Surface& s,const Font& font,int y,const char* value,Color fallback={47,63,86}) {
    const int x=(int(s.width)-int(menu_width(font,value)))/2;
    if(font.valid())menu_text(s,font,x+1,y+1,value,fallback,0x8ccau);
    menu_text(s,font,x,y,value,fallback);
}
// Center visible glyph ink, excluding the original atlas cell's blank margins.
inline int text_ink_y(const Font& font,const char* value,int center_y) {
    if(!font.valid())return center_y-5;
    unsigned top=font.cell_height,bottom=0;
    const unsigned columns=font.image.width/font.cell_width;
    while(value&&*value){const unsigned code=next_latin1(value);const unsigned cell=font.glyphs[code];
        if(cell==0xffff)continue;
        const unsigned ox=(cell%columns)*font.cell_width,oy=(cell/columns)*font.cell_height;
        if(oy+font.cell_height>font.image.height)continue;
        for(unsigned row=0;row<font.cell_height;++row)for(unsigned col=0;col<font.cell_width;++col)
            if(font.image.pixels[size_t(oy+row)*font.image.width+ox+col]&0x8000u){
                if(row<top)top=row;
                if(row+1>bottom)bottom=row+1;}
    }
    return bottom?center_y-int((top+bottom)/2):center_y-int(font.cell_height/2);
}
inline void ink_center(const Surface& s,const Font& font,int center_y,const char* value,Color color={47,63,86}) {
    menu_shadow_center(s,font,text_ink_y(font,value,center_y),value,color);
}
struct PhaseLine {
    uint8_t codes[80]{};unsigned origins[80]{};
    unsigned count=0,natural_width=0,width=0,height=0;
};
inline PhaseLine phase_line(const Font& font,const char* text,unsigned available) {
    PhaseLine line;char bounded[80];copy_text(bounded,sizeof(bounded),text);
    const char* cursor=bounded;
    while(*cursor&&line.count<80){const unsigned code=next_latin1(cursor);
        line.codes[line.count]=uint8_t(code);line.origins[line.count++]=line.natural_width;
        line.natural_width+=font_advance(font,uint8_t(code));}
    line.width=line.natural_width<available?line.natural_width:available;
    const unsigned natural_height=font.valid()?font.cell_height:11;
    line.height=line.natural_width?unsigned(uint64_t(natural_height)*line.width/line.natural_width):0;
    return line;
}
inline void phase_text(const Surface& s,const Font& font,int y,const char* value,unsigned width) {
    const PhaseLine line=phase_line(font,value,width);
    if(!line.width||!line.height||!s.valid())return;
    if(line.width==line.natural_width){char bounded[80];copy_text(bounded,sizeof(bounded),value);ink_center(s,font,y+16,bounded,{83,40,0});return;}
    const unsigned native_height=font.valid()?font.cell_height:11;
    const unsigned columns=font.valid()?font.image.width/font.cell_width:0;
    const int left=(int(s.width)-int(line.width))/2;
    // Scale the virtual text strip uniformly, without allocating an intermediate
    // image. Origins retain native advances; overlapping transparent cells keep
    // the previous glyph's ink just as the unscaled painter does.
    const int top=y+16-int(line.height)/2;
    unsigned glyph_index=0;
    for(unsigned dx=0;dx<line.width;++dx){
        const unsigned sx=unsigned(uint64_t(dx)*line.natural_width/line.width);
        while(glyph_index+1<line.count&&line.origins[glyph_index+1]<=sx)++glyph_index;
        for(unsigned dy=0;dy<line.height;++dy){
            const unsigned sy=unsigned(uint64_t(dy)*native_height/line.height);
            bool opaque=false;uint16_t pixel=0;
            const unsigned first=glyph_index?glyph_index-1:glyph_index;
            for(unsigned index=first;index<=glyph_index;++index){
                const unsigned col=sx-line.origins[index];const uint8_t code=line.codes[index];
                if(font.valid()){
                    const unsigned cell=font.glyphs[code];
                    if(cell==0xffff||col>=font.cell_width)continue;
                    const unsigned ox=(cell%columns)*font.cell_width,oy=(cell/columns)*font.cell_height;
                    if(oy+sy>=font.image.height)continue;
                    const uint16_t sample=font.image.pixels[size_t(oy+sy)*font.image.width+ox+col];
                    if(sample&0x8000u){opaque=true;pixel=sample;}
                }else if(col<5&&sy>=2&&sy<9){unsigned accent=0;const char base=fallback_base(code,accent);
                    if(glyph(base)[col]&(1u<<(sy-2))){opaque=true;pixel=0xaaa0u;}}
            }
            if(opaque)s.pixel(left+int(dx),top+int(dy),image_color(pixel));
        }
    }
}
inline void menu_wrap(const Surface& s,const Font& font,int x,int y,const char* value,unsigned width,unsigned lines,Color fallback={69,39,9}) {
    if(!value)return;
    for(unsigned line=0;*value&&line<lines;++line){
        while(*value==' ')++value;
        const size_t length=line_bytes(font,value,width);
        char part[80];std::memcpy(part,value,length);part[length]=0;
        if(font.valid())menu_text(s,font,x+1,y+1,part,fallback,0x8ccau);
        menu_text(s,font,x,y,part,fallback);value+=length;if(*value=='\n')++value;
        y+=int(font.valid()?font.cell_height+2:12);
    }
}
inline void cartridge(const Surface& s,const Image& frame,int x,int y,int width,int height,bool fill=true) {
    if(frame.valid()&&frame.width>=3&&frame.height>=3){
        unsigned left=frame.width,top=frame.height,right=0,bottom=0;
        for(unsigned row=0;row<frame.height;++row)for(unsigned col=0;col<frame.width;++col)
            if(frame.pixels[size_t(row)*frame.width+col]&0x8000u){
                if(col<left)left=col;
                if(row<top)top=row;
                if(col+1>right)right=col+1;
                if(row+1>bottom)bottom=row+1;}
        const unsigned fw=right>left?right-left:0,fh=bottom>top?bottom-top:0;
        if(fw>=3&&fh>=3){
            const unsigned corner=fw<fh?fw/3:fh/3;const int c=int(corner>8?8:corner);
            if(fill)rect(s,x+3,y+3,width-6,height-6,{255,218,119});
            blit(s,frame,x+c,y+c,width-2*c,height-2*c,left+corner,top+corner,fw-2*corner,fh-2*corner);
            blit(s,frame,x+c,y,width-2*c,c,left+corner,top,fw-2*corner,corner);
            blit(s,frame,x+c,y+height-c,width-2*c,c,left+corner,top+fh-corner,fw-2*corner,corner);
            blit(s,frame,x,y+c,c,height-2*c,left,top+corner,corner,fh-2*corner);
            blit(s,frame,x+width-c,y+c,c,height-2*c,left+fw-corner,top+corner,corner,fh-2*corner);
            blit(s,frame,x,y,c,c,left,top,corner,corner);blit(s,frame,x+width-c,y,c,c,left+fw-corner,top,corner,corner);
            blit(s,frame,x,y+height-c,c,c,left,top+fh-corner,corner,corner);
            blit(s,frame,x+width-c,y+height-c,c,c,left+fw-corner,top+fh-corner,corner,corner);
            return;
        }
    }
    // A small pixel-built gold/orange frame also serves the missing-ROM screen.
    rect(s,x,y,width,height,{119,56,6});rect(s,x+1,y+1,width-2,height-2,{255,235,121});
    rect(s,x+3,y+3,width-6,height-6,{239,145,42});rect(s,x+5,y+5,width-10,height-10,{255,217,110});
    rect(s,x+5,y+height-5,width-10,2,{194,90,14});
}
inline void paint_top(const Surface& s,const Artwork* art=nullptr,Language language=Language::English) {
    if(!s.valid())return;
    if(art&&art->sky.valid())blit(s,art->sky,0,0,int(s.width),int(s.height));
    else gradient(s,{47,183,238},{205,252,244});
    if(art&&art->castle.valid()){
        int width=int(s.width),height=int(s.height);
        if(uint64_t(s.width)*art->castle.height>=uint64_t(s.height)*art->castle.width)
            height=int((uint64_t(art->castle.height)*s.width+art->castle.width-1)/art->castle.width);
        else width=int((uint64_t(art->castle.width)*s.height+art->castle.height-1)/art->castle.height);
        blit(s,art->castle,(int(s.width)-width)/2,(int(s.height)-height)/2,width,height);
    }
    if(art&&art->logo.valid()){
        const int width=s.width>=400?280:240,height=int(uint64_t(art->logo.height)*unsigned(width)/art->logo.width);
        blit(s,art->logo,(int(s.width)-width)/2,(int(s.height)-height)/2,width,height);
    }else{
        const Font empty{};menu_center(s,empty,48,"SUPER MARIO 64 DS");menu_center(s,empty,78,translated(language,Label::Loading));
    }
}
inline void paint_bottom(const Surface& s,const State& state,uint64_t now=0) {
    (void)now;if(!s.valid())return;
    const Artwork* art=state.artwork;const Font empty{};
    const Font& title=art?art->title_font:empty;const Font& body=art?art->body_font:empty;
    const Image no_image{};const Image& frame=art?art->frame:no_image;
    if(art&&art->pattern.valid())tiled(s,art->pattern);else gradient(s,{116,189,247},{140,223,243});
    const int x=(int(s.width)-264)/2;
    if(art&&art->mario.valid())blit(s,art->mario,int(s.width)/2-48,8,32,32);
    if(art&&art->yoshi.valid())blit(s,art->yoshi,int(s.width)/2+8,8,32,32);
    if(art&&art->star.valid()&&!(art->mario.valid()||art->yoshi.valid()))blit(s,art->star,int(s.width)/2-16,8,32,32);
    ink_center(s,title,58,translated(state.language,state.failed?Label::ErrorLabel:Label::Loading),{255,215,73});
    const auto button=state.failed?exit_button:cancel_button;
    if(state.failed){
        cartridge(s,frame,x-8,82,280,114,!body.valid());menu_wrap(s,body,x+4,90,state.headline,256,2);
        menu_wrap(s,body,x+4,132,state.detail,256,3);
        cartridge(s,frame,button.x,button.y,button.width,button.height);
        ink_center(s,body,218,translated(state.language,Label::Exit),{69,39,9});return;
    }
    cartridge(s,frame,x,82,264,34);
    phase_text(s,title,83,state.headline,240);
    ink_center(s,body,130,progress_hint(state));
    cartridge(s,frame,x,146,264,28);
    const unsigned bounded=state.percent>100?100:state.percent;const int fill=int(248u*bounded/100);
    rect(s,x+8,154,248,12,{126,80,25});
    if(fill){const Image fill_image{loading_style::fill_pixels,loading_style::fill_width,loading_style::fill_height};
        blit(s,fill_image,x+8,154,fill,12);}
    char percent[8];char* end=decimal(percent,bounded);*end++='%';*end=0;
    ink_center(s,title,160,percent,{255,231,102});
    char count[64];
    if(state.files_total){end=decimal(count,state.files_done);copy_text(end,4," / ");end=decimal(end+3,state.files_total);copy_text(end,sizeof(count)-size_t(end-count)," ");copy_text(end+1,sizeof(count)-size_t(end-count)-1,translated(state.language,Label::Files));}
    else copy_text(count,sizeof(count),translated(state.language,Label::Intro));
    ink_center(s,body,190,count);
    cartridge(s,frame,button.x,button.y,button.width,button.height);
    ink_center(s,body,218,translated(state.language,Label::Cancel),{69,39,9});
}
inline void paint(const Surface& top,const Surface& bottom,const State& state,uint64_t now=0) {
    paint_top(top,state.artwork,state.language);paint_bottom(bottom,state,now);
}

// Shared controller permits the actual update/throttle/lifetime path to be
// exercised on the host. Backend owns no resources in the 3DS implementation.
template<class Backend> class BasicSession {
    Backend backend_{};
    State state_{};
    uint64_t last_render_=0;
    bool drawn_=false,active_=true;
    unsigned last_percent_=101,last_group_=0;
    bool last_failed_=false;
    void render(bool force) {
        if(!active_)return;
        const uint64_t now=backend_.milliseconds();
        const bool dirty=state_.percent!=last_percent_||state_.phase_group!=last_group_||state_.failed!=last_failed_;
        if(!force&&drawn_&&(!dirty||now-last_render_<250))return;
        backend_.paint(state_,now);last_render_=now;drawn_=true;
        last_percent_=state_.percent;last_group_=state_.phase_group;last_failed_=state_.failed;
    }
public:
    explicit BasicSession(const Artwork* artwork=nullptr,Language language=Language::English):state_(language){state_.artwork=artwork;render(true);}
    ~BasicSession(){close();}
    BasicSession(const BasicSession&)=delete;
    BasicSession& operator=(const BasicSession&)=delete;
    bool update(const char* phase,uint64_t done,uint64_t total,unsigned complete,unsigned count) {
        if(!active_||!backend_.poll())return false;
        state_.work(phase,done,total,complete,count);render(false);return true;
    }
    void stage(Stage value){if(active_){state_.stage(value);render(true);}}
    void error(Error value,const char* diagnostic=nullptr){if(active_){state_.error(value,diagnostic);render(true);}}
    void stage(const char* value,unsigned percent){if(active_){state_.stage(value,percent);render(true);}}
    void error(const char* title,const char* detail){if(active_){state_.error(title,detail);render(true);}}
    void close(){if(active_){backend_.clear_both();active_=false;}}
    const State& state()const{return state_;}
    Backend& backend(){return backend_;}
};
} // namespace loading_screen
