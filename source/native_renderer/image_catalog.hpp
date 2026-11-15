#pragma once
#include "image_bank.hpp"

namespace sm64ds::render {
struct ImageAsset {
    std::uint16_t width,height;
    ImageBank::Alpha alpha;
    TextureFormat format=TextureFormat::rgba8;
};
struct ImageBankBinding {
    std::uint32_t bank_id;
    std::array<std::uint8_t,32> source_sha256;
    const ImageAsset* image;
    unsigned images;
};
// Image-only IDs form their own namespace, independent of model file IDs.
// This metadata contains native texture formats, never source addresses,
// source texture formats or sampler packets.
class ImageCatalog {
public:
    static Error open(const ImageBankBinding*,std::size_t,ImageCatalog&) noexcept;
    const ImageBankBinding* find(std::uint32_t) const noexcept;
    const ImageBankBinding* at(std::size_t index) const noexcept {return index<size_?banks_+index:nullptr;}
    std::size_t size() const noexcept {return size_;}
private:
    const ImageBankBinding* banks_=nullptr;
    std::size_t size_=0;
};
Error validate(const ImageBank&,const ImageBankBinding&) noexcept;
ImageCatalog prepared_image_catalog() noexcept;
}
