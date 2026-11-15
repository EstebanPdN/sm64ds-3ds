#include "native_images.hpp"
#include <cstring>

namespace sm64ds::render {
Error NativeImages::upload(Citro3DBackend& renderer,const ImageBank& bank,
                          const std::array<bool,ImageBank::max_images>& required) noexcept {
    if(!renderer.can_upload())return Error::invalid_phase;
    if(!bank||(owner_&&(owner_!=&renderer||count_!=bank.images()||
       std::memcmp(source_sha256_.data(),bank.source_sha256(),32))))return Error::invalid_data;
    bool any=false;
    for(unsigned i=0;i<required.size();++i)if(required[i]){
        if(i>=bank.images())return Error::invalid_data;
        any=true;
    }
    if(!any)return Error::invalid_data;
    std::array<Texture,ImageBank::max_images> staged{};
    for(unsigned i=0;i<bank.images();++i)if(required[i]&&!image_[i]){
        const auto source=bank.image(i);
        const auto error=renderer.create_texture(source.width,source.height,source.data,source.bytes,staged[i],source.format);
        if(error!=Error::none){
            for(unsigned j=0;j<i;++j)if(staged[j])renderer.destroy(staged[j]);
            return error;
        }
    }
    for(unsigned i=0;i<bank.images();++i)if(staged[i])image_[i]=staged[i];
    if(!owner_){
        owner_=&renderer;count_=bank.images();
        std::memcpy(source_sha256_.data(),bank.source_sha256(),32);
    }
    return Error::none;
}
void NativeImages::reset() noexcept {
    if(owner_)for(unsigned i=0;i<count_;++i)if(image_[i]){
        if(owner_->can_upload())owner_->destroy(image_[i]);else owner_->retire(image_[i]);
    }
    image_={};source_sha256_={};count_=0;owner_=nullptr;
}
}
