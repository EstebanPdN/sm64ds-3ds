#pragma once
#include "citro3d_backend.hpp"
#include "image_bank.hpp"

namespace sm64ds::render {
// Render-thread ownership of an image-only bank. Extensions upload only
// missing images after a fence, retaining every previously resident handle.
// A failed extension destroys only its new textures; the old set stays live.
// The backend must outlive this owner, including destruction/retirement.
class NativeImages {
public:
    NativeImages()=default;
    ~NativeImages(){reset();}
    NativeImages(const NativeImages&)=delete;
    NativeImages& operator=(const NativeImages&)=delete;
    Error upload(Citro3DBackend&,const ImageBank&,const std::array<bool,ImageBank::max_images>&) noexcept;
    Texture image(unsigned index) const noexcept {return index<count_?image_[index]:Texture{};}
    unsigned images() const noexcept {return count_;}
    void reset() noexcept;
private:
    Citro3DBackend* owner_=nullptr;
    unsigned count_=0;
    std::array<std::uint8_t,32> source_sha256_{};
    std::array<Texture,ImageBank::max_images> image_{};
};
}
