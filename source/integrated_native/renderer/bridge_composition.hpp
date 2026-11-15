#pragma once
#include <algorithm>
#include <cstdint>

namespace sm64ds::render::bridge_policy {
// Temporary PPU bridge policy, independent of the PPU storage/cache. The
// native geometry stays on the GPU. Only its window/effect overlay is built
// here; master brightness belongs to the final LCD, not this layer.
struct Pixel {
    std::uint32_t front=0, resolved=0;
    unsigned front_layer=5,front_priority=4,back_layer=5,back_priority=4;
    unsigned geometry_priority=0,window=63,obj_alpha=0;
    unsigned blend=0,alpha=0,brightness=0;
};
inline bool geometry_before(unsigned gp,unsigned layer,unsigned priority) noexcept {
    return gp<priority||(gp==priority&&(layer&127)!=4);
}
inline std::uint32_t foreground(const Pixel& p) noexcept {
    const auto layer=p.front_layer&127u;
    // Windows which hide BG0 must cover the geometry with the actual 2D
    // composite, even when that candidate normally lies behind BG0.
    if(!(p.window&1))return p.resolved|0xff000000u;
    const bool gx_front=geometry_before(p.geometry_priority,layer,p.front_priority);
    const unsigned mode=(p.blend>>6)&3;
    if(gx_front){
        // BLDCNT effects apply to BG0 only inside enabled effect windows.
        if((p.window&32)&&(p.blend&1)&&(mode==2||mode==3)){
            const auto strength=std::min(p.brightness,16u);
            return ((strength*255+8)/16<<24)|(mode==2?0x00ffffffu:0u);
        }
        return 0;
    }
    const bool semi=(p.front_layer&128)!=0;
    const bool gx_back=geometry_before(p.geometry_priority,p.back_layer,p.back_priority);
    const bool blend_first=semi||((p.window&32)&&mode==1&&(p.blend&(1u<<layer)));
    if(gx_back&&blend_first&&(p.blend&0x100)){
        const auto a=semi&&p.obj_alpha<=16?p.obj_alpha:std::min(p.alpha&31u,16u);
        const auto b=semi&&p.obj_alpha<=16?16-a:std::min((p.alpha>>8)&31u,16u);
        // Source-over exactly represents complementary alpha coefficients.
        // Other pairs still require the native two-source blend pass.
        if(a+b==16)return (p.front&0xffffffu)|(((a*255+8)/16)<<24);
    }
    return p.resolved|0xff000000u;
}
// Exact table form of foreground(). Besides the per-frame state, the result
// depends on a pixel only through (front layer + semi bit, front priority,
// back layer, back priority), window bits 0 (BG0/geometry) and 5 (effects),
// and, for a semi OBJ only, whether its own alpha (<= 16) is the blend
// coefficient. Each combination is resolved once by foreground() itself into
// one of five colour forms and reused. BUILD124: windows and per-OBJ alpha
// joined the table (pause and course-clear menus composed their top screen
// per pixel in ~35 ms on the Old 3DS). Fields above 7, and any combination
// whose result fits no form, use foreground().
class ForegroundTable {
public:
    void configure(unsigned geometry_priority,unsigned blend,unsigned alpha,unsigned brightness) noexcept {
        if(valid_&&gp_==geometry_priority&&blend_==blend&&alpha_==alpha&&brightness_==brightness)return;
        valid_=true;gp_=geometry_priority;blend_=blend;alpha_=alpha;brightness_=brightness;
        for(auto& e:entry_)e=kUnknown;
        register_set_=tone_set_=false;
    }
    // Inlined per pixel: one byte lookup and a colour form. Learning a new
    // combination and the reference path stay out of line (ARM11: a call
    // per pixel cost more than the table saves).
    __attribute__((always_inline)) std::uint32_t pixel(std::uint32_t front,std::uint32_t resolved,unsigned front_layer,unsigned front_priority,
                        unsigned back_layer,unsigned back_priority,unsigned window=63u,unsigned obj_alpha=0u) noexcept {
        if(__builtin_expect(((front_layer&127u)|front_priority|back_layer|back_priority)>7u,0))
            return reference(front,resolved,front_layer,front_priority,back_layer,back_priority,window,obj_alpha);
        const unsigned own=(front_layer>>7)&(obj_alpha<=16u?1u:0u);
        const unsigned index=(own<<15)|((window&1u)<<14)|(((window>>5)&1u)<<13)|(((front_layer>>7)&1u)<<12)|
                             ((front_layer&7u)<<9)|(front_priority<<6)|(back_layer<<3)|back_priority;
        unsigned e=entry_[index];
        if(__builtin_expect(e==kUnknown,0))e=learn(index,front_layer,front_priority,back_layer,back_priority,window,own);
        switch(e){
        case kTransparent:return 0;
        case kResolved:return resolved|0xff000000u;
        case kRegisterAlpha:return (front&0xffffffu)|(register_alpha_<<24);
        case kOwnAlpha:return (front&0xffffffu)|(((obj_alpha*255u+8u)/16u)<<24);
        case kTone:return tone_;
        default:return reference(front,resolved,front_layer,front_priority,back_layer,back_priority,window,obj_alpha);
        }
    }
private:
    enum : std::uint8_t {kUnknown,kTransparent,kResolved,kRegisterAlpha,kOwnAlpha,kTone,kSlow};
    __attribute__((noinline)) std::uint32_t reference(std::uint32_t front,std::uint32_t resolved,unsigned fl,unsigned fp,unsigned bl,unsigned bp,
                            unsigned window,unsigned obj_alpha) const noexcept {
        Pixel p{front,resolved,fl,fp,bl,bp,gp_,window,obj_alpha,blend_,alpha_,brightness_};return foreground(p);
    }
    __attribute__((noinline)) unsigned learn(unsigned index,unsigned fl,unsigned fp,unsigned bl,unsigned bp,unsigned window,unsigned own) noexcept {
        return entry_[index]=classify(fl,fp,bl,bp,window,own);
    }
    std::uint8_t classify(unsigned fl,unsigned fp,unsigned bl,unsigned bp,unsigned window,unsigned own) noexcept {
        // Distinct sentinel colours identify which form foreground() chose;
        // geometry results are colour-independent constants (0 or a tone).
        constexpr std::uint32_t front_sentinel=0x00123456u,resolved_sentinel=0x00654321u;
        const unsigned w=(window&1u)|(window&32u);
        const auto r=reference(front_sentinel,resolved_sentinel,fl,fp,bl,bp,w,own?5u:17u);
        if(r==(resolved_sentinel|0xff000000u))return kResolved;
        if((r&0xffffffu)==front_sentinel){
            if(!own){
                // Every register-alpha form of one state shares one alpha.
                if(!register_set_){register_set_=true;register_alpha_=r>>24;}
                return (r>>24)==register_alpha_?kRegisterAlpha:kSlow;
            }
            // The own alpha must follow the pixel's value, endpoints included.
            for(unsigned a:{0u,5u,11u,16u})if(reference(front_sentinel,resolved_sentinel,fl,fp,bl,bp,w,a)!=(front_sentinel|(((a*255u+8u)/16u)<<24)))return kSlow;
            return kOwnAlpha;
        }
        if(r==0)return kTransparent;
        if(!tone_set_){tone_set_=true;tone_=r;}
        return r==tone_?kTone:kSlow;
    }
    std::uint8_t entry_[1u<<16]{};
    std::uint32_t register_alpha_=0,tone_=0;bool register_set_=false,tone_set_=false;
    unsigned gp_=0,blend_=0,alpha_=0,brightness_=0;bool valid_=false;
};
inline bool geometry_effect_edge(std::uint32_t pixel,unsigned window,unsigned blend,unsigned brightness) noexcept {
    if(!(window&1))return false;
    Pixel p;p.window=window;p.blend=blend;p.brightness=brightness;
    return pixel==foreground(p);
}
// Extend only one proven background layer. Sampling the flattened image
// repeats logos/arrows and stretches foreground objects into the side bars.
// Columns 0..31 and 32..63 hold the left/right strips; unused columns stay 0.
inline bool sky_strips(const std::uint32_t* front,const std::uint32_t* back,
                       const std::uint8_t* fl,const std::uint8_t* bl,
                       std::uint32_t* out,unsigned blend,unsigned brightness,
                       bool reflected) noexcept {
    const unsigned mode=(blend>>6)&3;
    if(mode==1&&(blend&63)&&(blend&0x3f00))return false;
    auto source=[&](unsigned x){return reflected?(x<32?31-x:287-x):(x<32?224+x:x-32);};
    unsigned common=0x2f; // BG0..3 or backdrop, never OBJ
    for(unsigned y=0;y<192&&common;++y)for(unsigned x=0;x<64;++x){
        const auto i=y*256+source(x);common&=(1u<<(fl[i]&7))|(1u<<(bl[i]&7));
    }
    if(!common)return false;
    unsigned layer=0;while(!(common&(1u<<layer)))++layer;
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<64;++x){
        const auto i=y*256+source(x);auto color=(fl[i]&7)==layer?front[i]:back[i];
        if((mode==2||mode==3)&&(blend&(1u<<layer))){
            std::uint32_t rgb=0;const auto factor=std::min(brightness,16u);
            for(unsigned shift=0;shift<24;shift+=8){
                unsigned q=(color>>(shift+2))&63;
                q=mode==2?q+((63-q)*factor+8)/16:q-(q*factor+7)/16;
                rgb|=(q<<2)<<shift;
            }
            color=rgb;
        }
        out[y*256+x]=color|0xff000000u;
    }
    return true;
}
}
