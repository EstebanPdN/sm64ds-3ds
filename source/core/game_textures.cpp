#include "sm64ds/game_textures.hpp"
#include "ntr/texture.h"
namespace sm64ds {
namespace {
// The native game/renderer owns one execution context. Never reset or wrap
// this identity when a decoded cache or its source object is replaced.
uint64_t decoded_revision=0;
uint64_t new_revision() {
    return decoded_revision==UINT64_MAX?0:++decoded_revision;
}
}
uint64_t GameTextures::revision(const uint32_t* pixels,int width,int height) const {
    if(!pixels)return 0;
    for(const auto& item:cache_)if(item.second.pixels.data()==pixels &&
        item.second.width==width && item.second.height==height)return item.second.revision;
    return 0;
}
void GameTextures::begin_frame() {
    for(auto it=cache_.begin();it!=cache_.end();) {
        if(std::get<2>(it->first)!=geometry_.texture_generation ||
           std::get<3>(it->first)!=geometry_.palette_generation) it=cache_.erase(it);
        else ++it;
    }
}
bool GameTextures::resolver(void* context,uint32_t image,uint32_t palette,
                           const uint32_t*& pixels,int& width,int& height) {
    return context && static_cast<GameTextures*>(context)->resolve(image,palette,pixels,width,height);
}
bool GameTextures::resolve(uint32_t image,uint32_t palette,const uint32_t*& pixels,int& width,int& height) {
    const auto& g=geometry_;
    const uint32_t format=(image>>26)&7;
    if(!format) {pixels=nullptr;width=height=0;return true;}
    if(g.texture_transfer || g.palette_transfer || g.texture_capacity>sizeof(g.textures) ||
       g.palette_capacity>sizeof(g.palettes) || palette>0x1fffu)return false;
    const Key key{image&0x3ff0ffffu,palette,g.texture_generation,g.palette_generation};
    auto found=cache_.find(key);
    if(found==cache_.end()) {
        ntr::TextureDesc d;
        d.width=8<<((image>>20)&7);d.height=8<<((image>>23)&7);
        d.format=int(format);d.color0_transparent=(image>>29)&1;
        const uint32_t offset=(image&0xffffu)*8;
        const uint32_t count=uint32_t(d.width)*d.height;
        const uint32_t bits[]={0,8,2,4,8,2,8,16};
        const uint32_t bytes=count*bits[format]/8;
        if(offset>g.texture_capacity || bytes>g.texture_capacity-offset)return false;
        d.data=g.textures+offset;d.data_len=int32_t(bytes);
        if(format==5) {
            // Compressed texels reside in slot 0 or 2. Slot 1 contains their
            // index words: lower 64 KiB for slot 0, upper 64 KiB for slot 2.
            const uint32_t slot=offset/0x20000,within=offset%0x20000;
            if((slot!=0 && slot!=2) || bytes>0x20000-within)return false;
            const uint32_t index=0x20000+(slot==2?0x10000:0)+within/2;
            if(index>g.texture_capacity || bytes/2>g.texture_capacity-index)return false;
            d.index=g.textures+index;d.index_len=int32_t(bytes/2);
        }
        if(format!=7) {
            const uint32_t base=palette*(format==2?8u:16u);
            const uint32_t colours[]={0,32,4,16,256,0,8,0};
            if(base>g.palette_capacity || colours[format]*2>g.palette_capacity-base)return false;
            d.pal=g.palettes+base;d.pal_len=int32_t(g.palette_capacity-base);
            if(format==5) {
                for(int i=0;i<d.index_len;i+=2) {
                    const uint16_t entry=uint16_t(d.index[i])|uint16_t(d.index[i+1])<<8;
                    const unsigned mode=entry>>14;
                    const uint32_t needed=(entry&0x3fff)*4+((mode==0)?6:(mode==2)?8:4);
                    if(needed>uint32_t(d.pal_len))return false;
                }
            }
        }
        Texture decoded{{},d.width,d.height};
        if(!ntr::texture_decode(d,decoded.pixels))return false;
        decoded.revision=new_revision();
        found=cache_.emplace(key,std::move(decoded)).first;
    }
    pixels=found->second.pixels.data();width=found->second.width;height=found->second.height;
    return true;
}
}
