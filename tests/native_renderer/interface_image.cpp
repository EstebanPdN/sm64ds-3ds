#include "interface_image.hpp"
#include <cassert>
#include <vector>
#include <limits>
using namespace sm64ds::render;
int main(){
    std::vector<std::uint32_t> pixels(324*240,0);
    InterfaceImage image{pixels.data(),pixels.size(),320,240,324};InterfaceCrop crop;
    assert(interface_crop(image,crop)==Error::none&&crop.empty());
    pixels[231*324+6]=0xff123456;pixels[238*324+53]=0x01010203;
    assert(interface_crop(image,crop)==Error::none);
    assert(crop.x==6&&crop.y==231&&crop.width==48&&crop.height==8&&crop.texture_width==64&&crop.texture_height==8);
    std::vector<std::uint8_t> tiled(64*8*4,0x77);
    assert(tile_interface(image,crop,tiled.data(),tiled.size())==Error::none);
    // Independent destination decoder, also checks transparent padded pixels.
    for(unsigned gy=0;gy<8;++gy)for(unsigned x=0;x<64;++x){
        unsigned morton=0;for(unsigned bit=0;bit<3;++bit){morton|=((x>>bit)&1)<<(2*bit);morton|=((gy>>bit)&1)<<(2*bit+1);}
        const auto i=((x/8)*64+morton)*4;const unsigned sy=7-gy;
        const auto expected=x<48?pixels[(231+sy)*324+6+x]:0;
        assert(tiled[i]==std::uint8_t(expected>>24)&&tiled[i+1]==std::uint8_t(expected)&&
               tiled[i+2]==std::uint8_t(expected>>8)&&tiled[i+3]==std::uint8_t(expected>>16));
    }
    const auto good=crop;auto invalid=crop;invalid.x=std::numeric_limits<unsigned>::max();
    const auto before=tiled;assert(tile_interface(image,invalid,tiled.data(),tiled.size())==Error::invalid_data&&tiled==before);
    assert(tile_interface(image,good,pixels.data(),tiled.size())==Error::invalid_data);
    auto bad=image;bad.count=1;crop={1,2,3,4,8,8};
    assert(interface_crop(bad,crop)==Error::invalid_data&&crop.x==1);
    bad=image;bad.stride=std::numeric_limits<unsigned>::max();assert(validate(bad)==Error::invalid_data);
    bad=image;bad.height=0;assert(validate(bad)==Error::invalid_data);
    pixels.assign(pixels.size(),0xffffffff);image.pixels=pixels.data();
    assert(interface_crop(image,crop)==Error::none&&crop.texture_width==512&&crop.texture_height==256);
    // A padded row outside the visible width never makes an empty image visible.
    pixels.assign(pixels.size(),0);pixels[320]=0xffffffff;image.pixels=pixels.data();
    assert(interface_crop(image,crop)==Error::none&&crop.empty());
    std::vector<std::uint32_t> frame(256*192),texture(512*512+2,0x719ab235);
    std::uint32_t seed=0x534d3634;
    for(unsigned pass=0;pass<2;++pass){
        for(auto& pixel:frame){seed=seed*1664525u+1013904223u;pixel=seed;}
        tile_frame_image_2x(frame.data(),texture.data()+1);
        assert(texture.front()==0x719ab235&&texture.back()==0x719ab235);
        for(unsigned gy=0;gy<512;++gy)for(unsigned x=0;x<512;++x){
            unsigned morton=0;
            for(unsigned bit=0;bit<3;++bit){morton|=((x>>bit)&1)<<(2*bit);morton|=((gy>>bit)&1)<<(2*bit+1);}
            const auto offset=((gy/8)*64+x/8)*64+morton;
            auto expected=0x719ab235u;
            if(gy>=128){const auto value=frame[((511-gy)/2)*256+x/2];expected=(value<<8)|(value>>24);}
            assert(texture[offset+1]==expected);
        }
        std::vector<std::uint32_t> native(256*256+2,0x719ab235);
        tile_frame_image_1x(frame.data(),native.data()+1);
        assert(native.front()==0x719ab235&&native.back()==0x719ab235);
        for(unsigned gy=0;gy<256;++gy)for(unsigned x=0;x<256;++x){
            unsigned morton=0;
            for(unsigned bit=0;bit<3;++bit){morton|=((x>>bit)&1)<<(2*bit);morton|=((gy>>bit)&1)<<(2*bit+1);}
            auto expected=0x719ab235u;
            if(gy>=64){const auto value=frame[(255-gy)*256+x];expected=(value<<8)|(value>>24);}
            assert(native[1+((gy/8)*32+x/8)*64+morton]==expected);
        }
        // Band-delta uploads: re-tiling one changed 8-row band must equal a
        // full re-tile and write only that band's contiguous word range.
        for(const bool sharp:{false,true}){
            const std::size_t words=sharp?512*512:256*256;
            std::vector<std::uint32_t> full(words,0x5a5a5a5a),delta(words,0x5a5a5a5a);
            if(sharp){tile_frame_image_2x(frame.data(),full.data());tile_frame_image_2x(frame.data(),delta.data());}
            else{tile_frame_image_1x(frame.data(),full.data());tile_frame_image_1x(frame.data(),delta.data());}
            auto changed=frame;
            for(unsigned band:{0u,7u,23u}){
                for(unsigned x=0;x<256;++x)changed[(band*frame_band_rows+3)*256+x]^=0x00ff00ffu*(band+1);
                const auto before=delta;
                if(sharp)tile_frame_rows_2x(changed.data(),delta.data(),band*frame_band_rows,(band+1)*frame_band_rows);
                else tile_frame_rows_1x(changed.data(),delta.data(),band*frame_band_rows,(band+1)*frame_band_rows);
                const auto low=frame_band_word(sharp,band),high=low+frame_band_words(sharp);
                for(std::size_t i=0;i<words;++i)if(i<low||i>=high)assert(delta[i]==before[i]);
            }
            if(sharp)tile_frame_image_2x(changed.data(),full.data());else tile_frame_image_1x(changed.data(),full.data());
            assert(full==delta);
        }
    }
}
