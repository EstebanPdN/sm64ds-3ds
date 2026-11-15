#include "image_catalog.hpp"
#include <cstring>
#ifdef SM64DS_NATIVE_IMAGE_CATALOG_HEADER
#include SM64DS_NATIVE_IMAGE_CATALOG_HEADER
#endif
namespace sm64ds::render {
Error ImageCatalog::open(const ImageBankBinding* banks,std::size_t count,ImageCatalog& output) noexcept {
    if(count>64||(count&&!banks))return Error::invalid_data;
    for(std::size_t i=0;i<count;++i){const auto& b=banks[i];
        if(b.bank_id>65535||(i&&b.bank_id<=banks[i-1].bank_id)||
           !b.images||b.images>ImageBank::max_images||!b.image)return Error::invalid_data;
        for(unsigned j=0;j<b.images;++j)if(!texture_bytes(b.image[j].width,b.image[j].height,b.image[j].format)||
           static_cast<unsigned>(b.image[j].alpha)>1)return Error::invalid_data;
    }
    ImageCatalog next;next.banks_=banks;next.size_=count;output=next;return Error::none;
}
const ImageBankBinding* ImageCatalog::find(std::uint32_t id) const noexcept {
    std::size_t low=0,high=size_;
    while(low<high){const auto middle=low+(high-low)/2;if(banks_[middle].bank_id<id)low=middle+1;else high=middle;}
    return low<size_&&banks_[low].bank_id==id?banks_+low:nullptr;
}
Error validate(const ImageBank& bank,const ImageBankBinding& binding) noexcept {
    if(!bank||!binding.image||bank.images()!=binding.images||
       std::memcmp(bank.source_sha256(),binding.source_sha256.data(),32))return Error::invalid_data;
    for(unsigned i=0;i<binding.images;++i){const auto image=bank.image(i);const auto& expected=binding.image[i];
        if(image.width!=expected.width||image.height!=expected.height||image.alpha!=expected.alpha||image.format!=expected.format)return Error::invalid_data;
    }
    return Error::none;
}
ImageCatalog prepared_image_catalog() noexcept {
    ImageCatalog result;
#ifdef SM64DS_NATIVE_IMAGE_CATALOG_HEADER
    ImageCatalog::open(prepared_images::banks,sizeof(prepared_images::banks)/sizeof(prepared_images::banks[0]),result);
#endif
    return result;
}
}
