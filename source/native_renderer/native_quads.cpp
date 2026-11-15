#include "native_quads.hpp"
#include "model_instance.hpp"
#include <cmath>
#include <cstring>
namespace sm64ds::render {
const ImageBinding* sprite_image(const ModelBindings& binding,std::uint32_t texture,std::uint32_t palette) noexcept {
    if(binding.images>ModelBank::max_images||(binding.images&&!binding.image))return nullptr;
    for(unsigned i=0;i<binding.images;++i){const auto& image=binding.image[i];
        if(image.use==ImageUse::sprite&&image.texture==texture&&image.palette==palette&&image.image<binding.images)return &image;}
    return nullptr;
}
bool valid(const QuadState& state) noexcept {
    return !state.material.texture&&state.material.uv_mode==UvMode::affine&&
        valid(state.material)&&finite(state.object_to_clip)&&(state.polygon_id<=63||state.polygon_id==255)&&
        static_cast<unsigned>(state.bank)<=1&&(state.image!=QuadState::no_image||state.bank==QuadState::Bank::model);
}
bool same(const QuadState& a,const QuadState& b) noexcept {
    return a.image==b.image&&a.bank==b.bank&&a.polygon_id==b.polygon_id&&a.fog==b.fog&&a.material==b.material&&
        std::memcmp(&a.object_to_clip,&b.object_to_clip,sizeof(Matrix))==0;
}
Error validate_quads(const Vertex* vertices,std::size_t count,UvDomain* domain) noexcept {
    if(!vertices||!count||count%4||count>max_frame_quads*4)return Error::invalid_data;
    UvDomain next;
    for(std::size_t i=0;i<count;++i){const auto& v=vertices[i];
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
           !std::isfinite(v.u)||!std::isfinite(v.v))return Error::invalid_data;
        if(domain)next.include(v.u,v.v); // BUILD134: not for a caller that discards it
    }
    if(domain)*domain=next;
    return Error::none;
}
}
