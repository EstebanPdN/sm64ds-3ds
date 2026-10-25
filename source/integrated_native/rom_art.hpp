#pragma once
// Original menu artwork is embedded for boot/error screens without a ROM.
// The bounded ROM decoder remains available for independent pixel validation.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include "boot_allocator.hpp"
#include "loading_art.hpp"
#include "rom_art_manifest.hpp"
#include "rom_art_embedded.hpp"
#include "dsp_bootstrap.hpp"

namespace rom_art {
namespace detail {
inline uint16_t u16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline uint32_t u32(const uint8_t* p){return uint32_t(u16(p))|(uint32_t(u16(p+2))<<16);}
inline bool span(std::size_t n,std::size_t at,std::size_t length){return at<=n&&length<=n-at;}
inline uint32_t crc32(const void* data,std::size_t length){
    const auto* p=static_cast<const uint8_t*>(data);uint32_t crc=0xffffffffu;
    for(std::size_t i=0;i<length;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}
    return crc^0xffffffffu;
}
inline uint16_t color(uint16_t rgb){return uint16_t(0x8000u|((rgb&31u)<<10)|(rgb&0x3e0u)|((rgb>>10)&31u));}
inline uint16_t blend(uint16_t a,uint16_t b,unsigned step,unsigned total){
    uint16_t result=0;
    for(unsigned shift=0;shift<=10;shift+=5)
        result|=uint16_t(((((a>>shift)&31u)*(total-step)+((b>>shift)&31u)*step)/total)<<shift);
    return result;
}
// The original LZ10 stream may be wrapped by its four-byte LZ77 marker.
// Output size is supplied by checked metadata, never allocated from the stream.
inline bool lz10(const uint8_t* input,std::size_t stored,uint8_t* output,std::size_t decoded){
    if(!input||!output)return false;
    if(stored>=4&&!std::memcmp(input,"LZ77",4)){input+=4;stored-=4;}
    if(stored<4||input[0]!=0x10||(u32(input)>>8)!=decoded)return false;
    std::size_t read=4,written=0;
    while(written<decoded){
        if(read>=stored)return false;
        const unsigned flags=input[read++];
        for(unsigned mask=0x80;mask&&written<decoded;mask>>=1){
            if(flags&mask){
                if(!span(stored,read,2))return false;
                const unsigned pair=(unsigned(input[read])<<8)|input[read+1];read+=2;
                const std::size_t length=(pair>>12)+3,distance=(pair&0xfff)+1;
                if(distance>written||length>decoded-written)return false;
                for(std::size_t i=0;i<length;++i){output[written]=output[written-distance];++written;}
            }else{
                if(read>=stored)return false;
                output[written++]=input[read++];
            }
        }
    }
    return true;
}
struct File {
    std::FILE* handle=nullptr;
    explicit File(const char* path):handle(path&&*path?std::fopen(path,"rb"):nullptr){}
    ~File(){if(handle)std::fclose(handle);}
    File(const File&)=delete;File& operator=(const File&)=delete;
};
struct Texture {const uint8_t* pixels=nullptr;const uint8_t* palette=nullptr;std::size_t palette_count=0;};
inline bool texture(const boot_memory::Vector<uint8_t>& b,unsigned index,unsigned palette,
                    unsigned width,unsigned height,unsigned format,Texture& result){
    if(b.size()<0x24||index>=u32(b.data()+0x14)||palette>=u32(b.data()+0x1c))return false;
    const std::size_t to=u32(b.data()+0x18),po=u32(b.data()+0x20);
    if(!span(b.size(),to,std::size_t(index+1)*20)||!span(b.size(),po,std::size_t(palette+1)*16))return false;
    const uint8_t* t=b.data()+to+index*20;const uint8_t* p=b.data()+po+palette*16;
    const uint32_t flags=u32(t+16),length=u32(t+8),pal_length=u32(p+8);
    if((8u<<((flags>>20)&7))!=width||(8u<<((flags>>23)&7))!=height||((flags>>26)&7)!=format||
       length!=width*height*(format==3?1u:2u)/2u||pal_length%2||
       !span(b.size(),u32(t+4),length)||!span(b.size(),u32(p+4),pal_length))return false;
    result={b.data()+u32(t+4),b.data()+u32(p+4),pal_length/2};return true;
}
inline unsigned tiled4(const uint8_t* data,unsigned x,unsigned y){
    const std::size_t at=((y/8)*32+x/8)*64+(y%8)*8+x%8;
    return (data[at/2]>>(4*(at%2)))&15;
}
} // namespace detail

class Resources {
    // sky aliases castle. Fonts retain canonical ASCII and printable Latin1
    // cells, preserving original glyph shapes and advances for six languages.
    static constexpr unsigned FontHeight=rom_art_manifest::font_height;
    static constexpr std::size_t Castle=0,Logo=Castle+256*192,Pattern=Logo+240*128,
        Frame=Pattern+64*256,Star=Frame+24*24,Title=Star+16*16,Body=Title+256*FontHeight,
        PixelCount=Body+128*FontHeight;
    boot_memory::Vector<uint16_t> pixels_;
    loading_screen::Artwork artwork_{};
    char error_[192]{};
    std::size_t peak_bytes_=0;
    bool fail(const char* text){std::snprintf(error_,sizeof error_,"%s",text);release();return false;}
    bool read(std::FILE* f,const rom_art_manifest::Span& s,uint8_t* output,std::size_t capacity){
        if(!output||s.size>capacity||!detail::span(rom_art_manifest::rom_size,s.offset,s.size)||
           std::fseek(f,long(s.offset),SEEK_SET)!=0)return fail("invalid ROM artwork span or seek");
        std::size_t done=0;
        while(done<s.size){const std::size_t chunk=(s.size-done)>65536?65536:s.size-done;
            if(std::fread(output+done,1,chunk,f)!=chunk)return fail("cannot read complete ROM artwork span");
            done+=chunk;}
        return detail::crc32(output,s.size)==s.crc32||fail("ROM artwork span CRC does not match EU revision");
    }
    void peak(const boot_memory::Vector<uint8_t>& work,const boot_memory::Vector<uint8_t>& packed){
        const std::size_t bytes=retained_bytes()+work.capacity()+packed.capacity();if(bytes>peak_bytes_)peak_bytes_=bytes;
    }
    bool compressed(std::FILE* f,const rom_art_manifest::Span& s,std::size_t decoded,
                    boot_memory::Vector<uint8_t>& packed,boot_memory::Vector<uint8_t>& work){
        packed.resize(s.size);work.resize(decoded);peak(work,packed);
        if(!read(f,s,packed.data(),packed.size()))return false;
        return detail::lz10(packed.data(),packed.size(),work.data(),work.size())||fail("invalid bounded ROM LZ10 artwork stream");
    }
    void bind(const uint16_t* p){
        using loading_screen::Image;
        artwork_.castle=Image{p+Castle,256,192};artwork_.sky=Image{p+Castle,256,64};
        artwork_.logo=Image{p+Logo,240,128};artwork_.pattern=Image{p+Pattern,64,256};
        artwork_.frame=Image{p+Frame,24,24};artwork_.star=Image{p+Star,16,16};
        artwork_.title_font={Image{p+Title,256,FontHeight},rom_art_manifest::glyphs,rom_art_manifest::advances16,16,16};
        artwork_.body_font={Image{p+Body,128,FontHeight},rom_art_manifest::glyphs,rom_art_manifest::advances8,8,16};
    }
    bool check_pixels(){
        const loading_screen::Image images[]={artwork_.castle,artwork_.sky,artwork_.logo,artwork_.pattern,
            artwork_.frame,artwork_.star,artwork_.title_font.image,artwork_.body_font.image};
        static_assert(sizeof images/sizeof images[0]==sizeof rom_art_manifest::images/sizeof rom_art_manifest::images[0],"art metadata count");
        for(unsigned i=0;i<sizeof images/sizeof images[0];++i){const auto& a=images[i];const auto& e=rom_art_manifest::images[i];
            if(a.width!=e.width||a.height!=e.height||detail::crc32(a.pixels,std::size_t(a.width)*a.height*2)!=e.crc32)
                return fail("decoded ROM artwork differs from pinned reference");}
        return true;
    }
public:
    Resources()=default;
    ~Resources(){release();}
    Resources(const Resources&)=delete;Resources& operator=(const Resources&)=delete;
    const loading_screen::Artwork& views()const{return artwork_;}
    const char* error()const{return error_;}
    std::size_t retained_bytes()const{return pixels_.capacity()*sizeof(uint16_t);}
    std::size_t peak_bytes()const{return peak_bytes_;}
    void release(){artwork_={};boot_memory::Vector<uint16_t>().swap(pixels_);}
    bool load_embedded(){
        release();error_[0]=0;peak_bytes_=0;
        static_assert(sizeof rom_art_embedded::pixels/sizeof(uint16_t)==PixelCount,"embedded artwork size");
        bind(rom_art_embedded::pixels);return check_pixels();
    }
    bool load(const char* path){
        release();error_[0]=0;peak_bytes_=0;
        try{
            detail::File file(path);
            if(!file.handle)return fail("cannot open ROM for original artwork");
            if(std::fseek(file.handle,0,SEEK_END)!=0||std::ftell(file.handle)!=long(rom_art_manifest::rom_size))
                return fail("ROM artwork requires the supported EU ROM size");
            pixels_.resize(PixelCount);
            boot_memory::Vector<uint8_t> work(rom_art_manifest::overlay_decoded),packed;
            peak(work,packed);
            if(!read(file.handle,rom_art_manifest::overlay7,work.data(),work.size()))return false;
            dsp_bootstrap::BlzLayout layout;
            const std::size_t stored=rom_art_manifest::overlay7.size;
            if(stored<8||!dsp_bootstrap::blz_layout(work.data()+stored-8,stored,layout)||
               layout.decoded!=work.size()||!dsp_bootstrap::decompress_blz_in_place(work.data(),work.size(),layout))
                return fail("invalid original title-overlay BLZ stream");
            using namespace rom_art_manifest;
            if(!detail::span(work.size(),castle_tiles,65536)||!detail::span(work.size(),castle_map,2048)||
               !detail::span(work.size(),castle_palette,512)||!detail::span(work.size(),logo_tiles,32768)||
               !detail::span(work.size(),logo_palette,512)||!detail::span(work.size(),pattern_colors,8))
                return fail("original title-overlay resource outside decoded bounds");
            for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
                const uint16_t entry=detail::u16(work.data()+castle_map+((y/8)*32+x/8)*2);
                const unsigned u=(entry&0x400)?7-x%8:x%8,v=(entry&0x800)?7-y%8:y%8;
                const unsigned index=work[castle_tiles+(entry&0x3ff)*64+v*8+u];
                pixels_[Castle+y*256+x]=detail::color(detail::u16(work.data()+castle_palette+index*2));
            }
            for(unsigned y=0;y<128;++y)for(unsigned x=0;x<240;++x){
                const unsigned index=work[logo_tiles+((y/8)*32+x/8)*64+(y%8)*8+x%8];
                pixels_[Logo+y*240+x]=index?detail::color(detail::u16(work.data()+logo_palette+index*2)):0;
            }
            uint16_t gradient[4];for(unsigned i=0;i<4;++i)gradient[i]=detail::u16(work.data()+pattern_colors+i*2);
            if(!compressed(file.handle,texbank,texbank_decoded,packed,work))return false;
            detail::Texture texture;
            if(!detail::texture(work,9,9,64,256,6,texture)||texture.palette_count<8||detail::u16(texture.palette)!=0x7fff)
                return fail("original wallpaper texture layout changed");
            for(unsigned y=0;y<256;++y){const uint16_t bg=detail::blend(gradient[0],gradient[1],y,255),ink=detail::blend(gradient[2],gradient[3],y,255);
                for(unsigned x=0;x<64;++x){const unsigned value=texture.pixels[y*64+x];
                    if(value&7)return fail("original wallpaper palette index changed");
                    pixels_[Pattern+y*64+x]=detail::color(detail::blend(bg,ink,value>>3,31));}}
            if(!detail::texture(work,2,2,256,256,4,texture)||texture.palette_count!=256)
                return fail("original star texture layout changed");
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const unsigned index=texture.pixels[(236+y)*256+4+x];
                pixels_[Star+y*16+x]=index?detail::color(detail::u16(texture.palette+index*2)):0;}
            uint8_t palettes[512],small[32];
            if(!read(file.handle,font_palette,palettes,sizeof palettes)||!read(file.handle,small_palette,small,sizeof small))return false;
            if(!compressed(file.handle,frame_tiles,16384,packed,work))return false;
            for(unsigned y=0;y<24;++y)for(unsigned x=0;x<24;++x){
                const unsigned tile=frame_ids[(y/8)*3+x/8],at=tile*64+(y%8)*8+x%8;
                if(at/2>=work.size())return fail("original menu frame tile outside bounds");
                const unsigned index=(work[at/2]>>(4*(at%2)))&15;
                pixels_[Frame+y*24+x]=index?detail::color(detail::u16(palettes+(16+index)*2)):0;}
            for(unsigned font=0;font<2;++font){
                const unsigned cw=font?8:16,columns=font?32:16,width=cw*16;
                const std::size_t output=font?Body:Title;const uint8_t* pal=font?small:palettes+32;
                if(font){work.resize(font8.size);if(!read(file.handle,font8,work.data(),work.size()))return false;}
                else if(!compressed(file.handle,font16,32768,packed,work))return false;
                for(unsigned cell=0;cell<sizeof codes/sizeof codes[0];++cell){const unsigned code=codes[cell];
                    const unsigned sx=(code%columns)*cw,sy=(code/columns)*16,tx=(cell%16)*cw,ty=(cell/16)*16;
                    if(sy+16>(font?128u:256u))return fail("original font character outside atlas");
                    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<cw;++x){const unsigned index=detail::tiled4(work.data(),sx+x,sy+y);
                        pixels_[output+(ty+y)*width+tx+x]=index?detail::color(detail::u16(pal+index*2)):0;}}
            }
            bind(pixels_.data());return check_pixels();
        }catch(const std::bad_alloc&){return fail("not enough libc memory for original ROM artwork");}
    }
};
} // namespace rom_art
