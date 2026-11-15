#include "model_bank.hpp"
#include "bank_crc.hpp"
#include <cstring>

namespace sm64ds::render {
namespace {
std::uint32_t read32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
std::size_t align(std::size_t n, std::size_t a) noexcept { return (n+a-1)&~(a-1); }
bool zeros(const std::uint8_t* p,std::size_t n) noexcept {
    for(std::size_t i=0;i<n;++i)if(p[i])return false;
    return true;
}
}
std::uint32_t ModelBank::checksum(const std::uint8_t* data,std::size_t bytes) noexcept {
    return detail::bank_crc32(data,bytes,52);
}
MeshBytes ModelBank::mesh(unsigned index) const noexcept {
    if(!data_ || index>=meshes_)return {};
    const auto* r=data_+mesh_table_+index*record_bytes;
    return {data_+read32(r),read32(r+4),data_+read32(r+8),read32(r+12),bool(read32(r+16)&1)};
}
ModelBank::Image ModelBank::image(unsigned index) const noexcept {
    if(!data_ || index>=images_)return {};
    const auto* r=data_+image_table_+index*record_bytes;
    return {data_+read32(r),read32(r+4),read32(r+8),read32(r+12)};
}
ModelBank::Surface ModelBank::surface(unsigned index) const noexcept {
    if(!data_ || index>=materials_)return {no_texture,{}};
    const auto* r=data_+material_table_+index*material_record_bytes;
    const unsigned depth=read32(r+12);
    Material m;
    m.blend=static_cast<Blend>(read32(r+4));m.cull=static_cast<Cull>(read32(r+8));
    m.depth_test=bool(depth&1);m.depth_write=bool(depth&2);m.alpha_cutoff=std::uint8_t(read32(r+16));
    m.wrap_s=static_cast<Wrap>((depth>>2)&3);m.wrap_t=static_cast<Wrap>((depth>>4)&3);
    for(unsigned i=0;i<6;++i){const auto bits=read32(r+20+i*4);std::memcpy(&m.uv[i],&bits,4);}
    m.tint=read32(r+44);
    return {read32(r),m};
}
ModelBank::Part ModelBank::part(unsigned index) const noexcept {
    if(!data_ || index>=parts_)return {};
    const auto* r=data_+part_table_+index*record_bytes;
    return {read32(r),read32(r+4),read32(r+8),read32(r+12),read32(r+16)};
}
bool ModelBank::has_distinct_index_triangle(unsigned index) const noexcept {
    if(!data_||index>=parts_)return false;
    const auto p=part(index);const auto m=mesh(p.mesh);
    const auto* indices=m.indices+p.first*2;
    for(std::uint32_t i=0;i<p.count;i+=3){
        const unsigned a=indices[i*2]|(unsigned(indices[i*2+1])<<8);
        const unsigned b=indices[i*2+2]|(unsigned(indices[i*2+3])<<8);
        const unsigned c=indices[i*2+4]|(unsigned(indices[i*2+5])<<8);
        if(a!=b&&b!=c&&a!=c)return true;
    }
    return false;
}
Error ModelBank::open(const void* input,std::size_t bytes,ModelBank& output) noexcept {
    if(!input || bytes<header_bytes || bytes>64*1024*1024)return Error::invalid_data;
    const auto* p=static_cast<const std::uint8_t*>(input);
    const bool extended=std::memcmp(p,"SM64NMD3",8)==0&&read32(p+8)==3;
    if((!extended&&(std::memcmp(p,"SM64NMD2",8)||read32(p+8)!=2)) || read32(p+12)!=bytes ||
        read32(p+88)>1 || !zeros(p+92,36))return Error::invalid_data;
    ModelBank v;
    v.data_=p;v.bytes_=bytes;v.meshes_=read32(p+16);v.images_=read32(p+20);
    v.materials_=read32(p+24);v.parts_=read32(p+28);v.matrices_=read32(p+48);
    v.material_bindings_=read32(p+88)!=0;
    if(!v.meshes_ || v.meshes_>max_meshes || v.images_>max_images || !v.materials_ ||
        v.materials_>max_materials || !v.parts_ || v.parts_>max_parts || !v.matrices_ ||
        v.matrices_>max_matrices)return Error::capacity;
    v.mesh_table_=header_bytes;
    v.image_table_=v.mesh_table_+v.meshes_*record_bytes;
    v.material_table_=v.image_table_+v.images_*record_bytes;
    v.part_table_=v.material_table_+v.materials_*material_record_bytes;
    std::size_t cursor=align(v.part_table_+v.parts_*record_bytes,128);
    if(cursor>bytes || read32(p+32)!=v.mesh_table_ || read32(p+36)!=v.image_table_ ||
        read32(p+40)!=v.material_table_ || read32(p+44)!=v.part_table_ ||
        checksum(p,bytes)!=read32(p+52))return Error::invalid_data;
    // Canonical, ordered payload ranges exclude overlap, aliasing with tables,
    // source-directed pointers, arithmetic wrap and hidden trailing payloads.
    for(unsigned i=0;i<v.meshes_;++i){
        const auto* r=p+v.mesh_table_+i*record_bytes;
        const auto vb=read32(r+4),ib=read32(r+12);
        if(read32(r+16)>(extended?1u:0u)||!zeros(r+20,12) || read32(r)!=cursor || vb>bytes-cursor)return Error::invalid_data;
        cursor=align(cursor+vb,4);
        if(cursor>bytes || read32(r+8)!=cursor || ib>bytes-cursor)return Error::invalid_data;
        cursor=align(cursor+ib,4);
        if(cursor>bytes || validate(v.mesh(i))!=Error::none)return Error::invalid_data;
    }
    for(unsigned i=0;i<v.images_;++i){
        cursor=align(cursor,128);
        if(cursor>bytes)return Error::invalid_data;
        const auto* r=p+v.image_table_+i*record_bytes;
        const auto size=read32(r+4),w=read32(r+8),h=read32(r+12);
        if(!zeros(r+16,16) || !texture_dimensions(w,h) || size!=std::size_t(w)*h*4 ||
            read32(r)!=cursor || size>bytes-cursor)return Error::invalid_data;
        cursor+=size;
    }
    if(cursor!=bytes)return Error::invalid_data;
    for(unsigned i=0;i<v.materials_;++i){
        const auto* r=p+v.material_table_+i*material_record_bytes;
        const auto t=read32(r);
        if((t!=no_texture && t>=v.images_) || read32(r+4)>1 || read32(r+8)>2 ||
            read32(r+12)>63 || read32(r+16)>255 || !zeros(r+48,16) ||
            !valid(v.surface(i).material))return Error::invalid_data;
    }
    for(unsigned i=0;i<v.parts_;++i){
        const auto* r=p+v.part_table_+i*record_bytes;
        const auto part=v.part(i);
        if(part.mesh>=v.meshes_ || part.material>=v.materials_ || part.matrix>=v.matrices_ ||
            !part.count || part.first%3 || part.count%3 || !zeros(r+20,12))return Error::invalid_data;
        const auto count=v.mesh(part.mesh).index_bytes/2;
        if(part.first>count || part.count>count-part.first)return Error::invalid_data;
    }
    output=v;return Error::none;
}
}
