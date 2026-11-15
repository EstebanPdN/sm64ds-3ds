#include "image_bank.hpp"
#include "bank_crc.hpp"
#include <cstring>

namespace sm64ds::render {
namespace {
std::uint32_t read32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
bool zeros(const std::uint8_t* p,std::size_t n) noexcept {
    for(std::size_t i=0;i<n;++i)if(p[i])return false;
    return true;
}
}
std::uint32_t ImageBank::checksum(const std::uint8_t* data,std::size_t bytes) noexcept {
    return detail::bank_crc32(data,bytes,28);
}
ImageBank::Image ImageBank::image(unsigned index) const noexcept {
    if(!data_||index>=images_)return {};
    const auto* r=data_+header_bytes+index*record_bytes;
    return {data_+read32(r),read32(r+4),read32(r+8),read32(r+12),static_cast<Alpha>(read32(r+16)),static_cast<TextureFormat>(read32(r+20))};
}
Error ImageBank::open(const void* input,std::size_t bytes,ImageBank& output) noexcept {
    if(!input||bytes<header_bytes||bytes>max_bytes)return Error::invalid_data;
    const auto* p=static_cast<const std::uint8_t*>(input);
    const auto version=read32(p+8);
    if(!((version==1&&!std::memcmp(p,"SM64NIM1",8))||(version==2&&!std::memcmp(p,"SM64NIM2",8)))||read32(p+12)!=bytes||
       read32(p+20)!=header_bytes||!zeros(p+64,64))return Error::invalid_data;
    const auto count=read32(p+16);
    if(!count||count>max_images)return Error::capacity;
    const std::size_t table_end=header_bytes+count*record_bytes;
    std::size_t cursor=(table_end+127)&~std::size_t(127),gpu_bytes=0;
    if(cursor>bytes||!zeros(p+table_end,cursor-table_end)||checksum(p,bytes)!=read32(p+28))return Error::invalid_data;
    for(unsigned i=0;i<count;++i){
        const auto* r=p+header_bytes+i*record_bytes;
        const auto offset=read32(r),size=read32(r+4),w=read32(r+8),h=read32(r+12);
        const auto format=static_cast<TextureFormat>(read32(r+20));const auto expected=texture_bytes(w,h,format);
        if(!expected||size!=expected||read32(r+16)>1||
           (version==1&&format!=TextureFormat::rgba8)||!zeros(r+24,8)||offset!=cursor||size>bytes-cursor)return Error::invalid_data;
        // Small A4 images need explicit zero padding to the next payload.
        const auto end=cursor+size;cursor=(end+127)&~std::size_t(127);
        if(cursor>bytes||!zeros(p+end,cursor-end))return Error::invalid_data;
        gpu_bytes+=size;
    }
    if(cursor!=bytes||gpu_bytes!=read32(p+24))return Error::invalid_data;
    ImageBank next;next.data_=p;next.images_=count;next.gpu_bytes_=gpu_bytes;
    output=next;return Error::none;
}
}
