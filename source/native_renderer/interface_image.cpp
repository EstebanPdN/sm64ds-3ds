#include "interface_image.hpp"
#include <algorithm>
#include <cstring>
namespace sm64ds::render {
Error validate(const InterfaceImage& image) noexcept {
    if(!image.pixels||!image.width||!image.height||image.width>400||image.height>240||
       image.stride<image.width||image.stride>400||
       image.count<std::size_t(image.height-1)*image.stride+image.width)return Error::invalid_data;
    return Error::none;
}
Error interface_crop(const InterfaceImage& image,InterfaceCrop& out) noexcept {
    const auto error=validate(image);if(error!=Error::none)return error;
    unsigned left=image.width,top=image.height,right=0,bottom=0;
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)
        if(image.pixels[std::size_t(y)*image.stride+x]>>24){
            left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=y+1;
        }
    if(!right){out={};return Error::none;}
    InterfaceCrop next{left,top,right-left,bottom-top,8,8};
    while(next.texture_width<next.width)next.texture_width*=2;
    while(next.texture_height<next.height)next.texture_height*=2;
    out=next;return Error::none;
}
Error tile_interface(const InterfaceImage& image,const InterfaceCrop& crop,void* destination,std::size_t bytes) noexcept {
    if(validate(image)!=Error::none||!destination||crop.empty()||
       !texture_dimensions(crop.texture_width,crop.texture_height)||
       crop.x>=image.width||crop.y>=image.height||!crop.width||!crop.height||
       crop.width>image.width-crop.x||crop.height>image.height-crop.y||
       crop.width>crop.texture_width||crop.height>crop.texture_height||
       bytes!=std::size_t(crop.texture_width)*crop.texture_height*4)return Error::invalid_data;
    const auto a=reinterpret_cast<std::uintptr_t>(image.pixels),b=reinterpret_cast<std::uintptr_t>(destination);
    const auto source_bytes=(std::size_t(image.height-1)*image.stride+image.width)*4;
    if(a<=b?b-a<source_bytes:a-b<bytes)return Error::invalid_data;
    auto* out=static_cast<std::uint8_t*>(destination);std::memset(out,0,bytes);
    for(unsigned y=0;y<crop.height;++y)for(unsigned x=0;x<crop.width;++x){
        const unsigned gy=crop.texture_height-1-y;
        const unsigned morton=(x&1)|((gy&1)<<1)|((x&2)<<1)|((gy&2)<<2)|((x&4)<<2)|((gy&4)<<3);
        const auto offset=(((gy/8)*(crop.texture_width/8)+x/8)*64+morton)*4;
        const auto color=image.pixels[std::size_t(crop.y+y)*image.stride+crop.x+x];
        out[offset]=std::uint8_t(color>>24);out[offset+1]=std::uint8_t(color);
        out[offset+2]=std::uint8_t(color>>8);out[offset+3]=std::uint8_t(color>>16);
    }
    return Error::none;
}
void tile_frame_image_2x(const std::uint32_t* image,std::uint32_t* texture) noexcept {tile_frame_rows_2x(image,texture,0,192);}
void tile_frame_image_1x(const std::uint32_t* image,std::uint32_t* texture) noexcept {tile_frame_rows_1x(image,texture,0,192);}
void tile_frame_rows_2x(const std::uint32_t* image,std::uint32_t* texture,unsigned first,unsigned end) noexcept {
    // Each doubled source pixel occupies four contiguous Morton words. Read
    // and convert it once, then write the complete 2x2 block. Tile order and
    // the vertical flip are identical to the per-destination-pixel uploader.
    static constexpr unsigned mx[4]={0,4,16,20},my[4]={0,8,32,40};
    for(unsigned y=first;y<end&&y<192;++y){
        const unsigned gy=510-y*2;
        auto* row=texture+(gy>>3)*64*64+my[(gy&7)>>1];
        const auto* in=image+y*256;
        for(unsigned tile=0;tile<64;++tile){
            auto* out=row+tile*64;const auto* source=in+tile*4;
            for(unsigned x=0;x<4;++x){
                const auto value=source[x];const auto rgba=(value<<8)|(value>>24);
                auto* pixel=out+mx[x];pixel[0]=pixel[1]=pixel[2]=pixel[3]=rgba;
            }
        }
    }
}
void tile_frame_rows_1x(const std::uint32_t* image,std::uint32_t* texture,unsigned first,unsigned end) noexcept {
    static constexpr unsigned mx[8]={0,1,4,5,16,17,20,21},my[8]={0,2,8,10,32,34,40,42};
    for(unsigned y=first;y<end&&y<192;++y){
        const unsigned gy=255-y;
        auto* row=texture+(gy>>3)*32*64+my[gy&7];
        const auto* in=image+y*256;
        for(unsigned tile=0;tile<32;++tile)for(unsigned x=0;x<8;++x){
            const auto pixel=in[tile*8+x];row[tile*64+mx[x]]=(pixel<<8)|(pixel>>24);
        }
    }
}
}
