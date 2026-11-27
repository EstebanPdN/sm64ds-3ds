#include "game_material.hpp"
#include "perf_switch.hpp"
#include <cstring>
#include <new>

namespace sm64ds::render::game {
namespace {
std::int32_t word(std::uint32_t bits) noexcept {std::int32_t result;std::memcpy(&result,&bits,4);return result;}
std::uint32_t product_shift(std::int32_t a,std::int32_t b,unsigned shift) noexcept {
    return std::uint32_t(std::uint64_t(std::int64_t(a)*b)>>shift);
}
bool lenient=false,carry_fog=false;
std::uint32_t rgb(std::uint32_t packed) noexcept {
    const auto channel=[packed](unsigned shift){const unsigned v=(packed>>shift)&31;return (v<<3)|(v>>2);};
    return (channel(0)<<24)|(channel(5)<<16)|(channel(10)<<8)|255;
}
// BUILD134 (exact cut): basis/sampled for two texture dimensions. Both are
// powers of two from 8 to 1024 (texture_dimensions), so the quotient is the
// power of two 2^(log2 basis - log2 sampled): exactly representable, hence
// exactly what float(basis)/sampled rounds to in every rounding mode. Built
// from its exponent instead of two conversions and a 19-cycle VFP divide.
float dimension_ratio(unsigned basis,unsigned sampled) noexcept {
    const std::uint32_t bits=std::uint32_t(127+__builtin_ctz(basis)-__builtin_ctz(sampled))<<23;
    float value;std::memcpy(&value,&bits,4);return value;
}
// `next` is a default LiveMaterial. A refusal leaves it partly written.
Error build(const RuntimeMaterial& source,const SelectedImage& selected,Extent basis,Extent uv_texture,
            std::int16_t sine,std::int16_t cosine,LiveMaterial& next,std::uint8_t cutoff) noexcept {
    const auto polygon=source.polygon;const unsigned faces=(polygon>>6)&3;
    if((polygon&0x80000000u)||!faces){next.visible=false;return Error::none;}
    const unsigned alpha=(polygon>>16)&31;
    if(lenient){
        const unsigned mode=(polygon>>4)&3;
        if(!alpha||mode==3){next.visible=false;return Error::none;} // wireframe / shadow volume
    }else if(!alpha || ((polygon>>4)&3)!=0 || (polygon&0xc000u))return Error::unsupported;
    next.state.cull=faces==3?Cull::none:faces==2?Cull::back:Cull::front;
    next.state.blend=alpha==31?Blend::opaque:Blend::alpha;
    next.state.fog=lenient&&carry_fog&&(polygon&0x8000u);
    next.state.shadow_id=((polygon>>24)&63u)==1u;
    next.state.depth_write=alpha==31 || bool(polygon&(1u<<11));next.state.alpha_cutoff=cutoff;
    // Playable approximation of the DS same-polygon-ID translucency rule: a
    // fading model must not show its own back layers (hair, crown, ...).
    if(lenient&&alpha<31)next.state.depth_write=true;
    next.state.tint=0xffffff00u|(alpha*255/31);
    next.diffuse=rgb(source.diffuse_ambient);next.emission=rgb(source.specular_emission>>16);
    next.lighting={std::uint16_t(source.diffuse_ambient&32767u),std::uint16_t((source.diffuse_ambient>>16)&32767u),
        std::uint16_t(source.specular_emission&32767u),std::uint16_t((source.specular_emission>>16)&32767u),
        std::uint8_t(polygon&15),bool(source.specular_emission&0x8000u)};
    next.diffuse_available=bool(source.diffuse_ambient&0x8000);next.emission_available=(polygon&15)==0;
    next.texture=selected.texture;next.palette=selected.palette;
    if(selected.texture!=ModelBank::no_texture){
        if(selected.texture>65535 || (selected.palette!=ModelBank::no_texture&&selected.palette>65535) ||
           !texture_dimensions(basis.width,basis.height)||!texture_dimensions(selected.sampled.width,selected.sampled.height))return Error::invalid_data;
        if(!selected.binary_alpha&&!lenient)return Error::unsupported;
        // Playable: textures may carry partial alpha (A3I5/A5I3); blending an
        // opaque texel is identical, so blend every textured material.
        if(lenient)next.state.blend=Blend::alpha;
        next.state.wrap_s=(source.texture_flags&(1u<<16))?((source.texture_flags&(1u<<18))?Wrap::mirror:Wrap::repeat):Wrap::clamp;
        next.state.wrap_t=(source.texture_flags&(1u<<17))?((source.texture_flags&(1u<<19))?Wrap::mirror:Wrap::repeat):Wrap::clamp;
        unsigned mode=source.texture_flags>>30;
        if(mode>2){if(!lenient)return Error::unsupported;mode=0;}
        if(mode==0){
            const bool cut=perf::cuts();
            const float x=cut?dimension_ratio(basis.width,selected.sampled.width):float(basis.width)/selected.sampled.width;
            const float y=cut?dimension_ratio(basis.height,selected.sampled.height):float(basis.height)/selected.sampled.height;
            next.state.uv={{x,0,0,0,y,0}};
        }else if(mode==2){
            next.state.uv_mode=UvMode::normal;
            next.normal_half_width=4u<<((source.texture_flags>>20)&7);
            next.normal_half_height=4u<<((source.texture_flags>>23)&7);
            next.state.normal_uv().width=basis.width;next.state.normal_uv().height=basis.height;
            next.state.normal_uv().offset={{std::int16_t(std::uint32_t(source.translate_x)>>8),std::int16_t(std::uint32_t(source.translate_y)>>8)}};
        }else{
            if(!texture_dimensions(uv_texture.width,uv_texture.height))return Error::invalid_data;
            const auto C=product_shift(source.scale_x,sine,12),B=product_shift(source.scale_x,cosine,12);
            const auto A=product_shift(source.scale_y,sine,12),D=product_shift(source.scale_y,cosine,12);
            const auto w=uv_texture.width,h=uv_texture.height;
            const auto D0=word(A*(0u-h))/std::int32_t(w),D1=word(C*w)/std::int32_t(h);
            const auto sx=std::uint32_t(source.scale_x),sy=std::uint32_t(source.scale_y);
            // Exactly func_02044b30 (texgen mode 1): mtx[9]=W*(sx-(C+B))*8-W*((sx*tx)>>8),
            // mtx[10]=H*((A-D)-sy+0x2000)*8+H*((sy*ty)>>8); W/H multiply the whole term.
            const auto ts=w*(sx-(C+B))*8-w*product_shift(source.scale_x,source.translate_x,8);
            const auto tt=h*((A-D)-sy+8192u)*8+h*product_shift(source.scale_y,source.translate_y,8);
            next.state.uv_mode=UvMode::fixed;
            next.state.fixed_uv={{word(B),D1,word(ts),D0,word(D),word(tt)},std::uint16_t(basis.width),std::uint16_t(basis.height)};
        }
    }
    return Error::none;
}
}
void set_lenient_materials(bool value) noexcept {lenient=value;}
void set_material_fog(bool value) noexcept {carry_fog=value;}
bool lenient_materials() noexcept {return lenient;}
Error capture_material(const RuntimeMaterial& source,const SelectedImage& selected,Extent basis,Extent uv_texture,
                       std::int16_t sine,std::int16_t cosine,LiveMaterial& output,std::uint8_t cutoff) noexcept {
    LiveMaterial next;
    const auto error=build(source,selected,basis,uv_texture,sine,cosine,next,cutoff);
    if(error==Error::none)output=next;
    return error;
}
Error capture_material_scratch(const RuntimeMaterial& source,const SelectedImage& selected,Extent basis,Extent uv_texture,
                               std::int16_t sine,std::int16_t cosine,LiveMaterial& output,std::uint8_t cutoff) noexcept {
    ::new(static_cast<void*>(&output)) LiveMaterial;
    return build(source,selected,basis,uv_texture,sine,cosine,output,cutoff);
}
}
