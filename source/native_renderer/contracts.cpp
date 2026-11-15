#include "contracts.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace sm64ds::render {
const char* describe(Error e) noexcept {
    switch (e) {
    case Error::none: return "ok";
    case Error::invalid_data: return "invalid renderer data";
    case Error::invalid_phase: return "invalid renderer phase";
    case Error::stale_handle: return "stale resource handle";
    case Error::capacity: return "resource slot capacity";
    case Error::budget: return "linear memory budget";
    case Error::out_of_memory: return "allocation failed";
    case Error::gpu_failure: return "GPU operation failed";
    case Error::unsupported: return "unsupported native rendering feature";
    }
    return "unknown renderer error";
}
Matrix identity() noexcept {
    Matrix m{};
    for (unsigned i = 0; i < 4; ++i) m.row[i][i] = 1;
    return m;
}
bool finite(const Matrix& m) noexcept {
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    for(const auto& row:m.row){
        std::array<std::uint32_t,4> bits;std::memcpy(bits.data(),&row[0],sizeof(bits));
        // Test exponent bits in ARM integer registers, avoiding sixteen VFP
        // comparisons and flag transfers for every prepared matrix.
        if((bits[0]&0x7fffffffu)>=0x7f800000u||(bits[1]&0x7fffffffu)>=0x7f800000u||
           (bits[2]&0x7fffffffu)>=0x7f800000u||(bits[3]&0x7fffffffu)>=0x7f800000u)return false;
    }
    return true;
}
bool UvDomain::accepts(unsigned width,unsigned height) const noexcept {
    if(!texture_dimensions(width,height)||fractional_bits[0]>14||fractional_bits[1]>14 ||
       (1u<<fractional_bits[0])>width*16 || (1u<<fractional_bits[1])>height*16)return false;
    return low[0]*float(width*16)>=-32768 && high[0]*float(width*16)<=32767 &&
        low[1]*float(height*16)>=-32768 && high[1]*float(height*16)<=32767;
}
Error validate(const MeshView& mesh,UvDomain* domain) noexcept {
    if (!mesh.vertices || !mesh.indices || !mesh.vertex_count ||
        mesh.vertex_count > 65536 || !mesh.index_count ||
        mesh.index_count > 65535 || mesh.index_count % 3) return Error::invalid_data;
    UvDomain next;
    for (std::size_t i = 0; i < mesh.vertex_count; ++i) {
        const auto& v = mesh.vertices[i];
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
            !std::isfinite(v.u) || !std::isfinite(v.v)) return Error::invalid_data;
        if(domain)next.include(v.u,v.v);
    }
    for (std::size_t i = 0; i < mesh.index_count; ++i)
        if (mesh.indices[i] >= mesh.vertex_count) return Error::invalid_data;
    if(domain)*domain=next;
    return Error::none;
}
Error validate(const MeshBytes& mesh,UvDomain* domain) noexcept {
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
    const auto stride=mesh.normals?sizeof(NormalVertex):sizeof(Vertex);
    if (!mesh.vertices || !mesh.indices || !mesh.vertex_bytes || !mesh.index_bytes ||
        mesh.vertex_bytes%stride || mesh.index_bytes%2 ||
        mesh.vertex_bytes/stride>65536 || mesh.index_bytes/2>65535 ||
        (mesh.index_bytes/2)%3) return Error::invalid_data;
    UvDomain next;
    for (std::size_t at=0; at<mesh.vertex_bytes; at+=stride){
        float uv[2]{};
        for (unsigned component=0; component<5; ++component) {
            const auto* p=mesh.vertices+at+component*4;
            const std::uint32_t bits=std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|
                (std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
            float value; std::memcpy(&value,&bits,4);
            if (!std::isfinite(value)) return Error::invalid_data;
            if(domain&&component>=3)uv[component-3]=value;
        }
        if(domain)next.include(uv[0],uv[1]);
        if(mesh.normals)for(unsigned axis=0;axis<4;++axis){
            const auto* p=mesh.vertices+at+24+axis*2;
            const unsigned bits=unsigned(p[0])|(unsigned(p[1])<<8);
            const int value=bits>=32768?int(bits)-65536:int(bits);
            if(axis<3?(value<-512||value>511):(value<0||value>2))return Error::invalid_data;
        }
    }
    for (std::size_t at=0; at<mesh.index_bytes; at+=2)
        if ((unsigned(mesh.indices[at])|(unsigned(mesh.indices[at+1])<<8)) >= mesh.vertex_bytes/stride)
            return Error::invalid_data;
    if(domain)*domain=next;
    return Error::none;
}
Error validate(const NormalVertex* vertices,std::size_t count,UvDomain* domain) noexcept {
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    if(!vertices||!count||count>65535||count%3)return Error::invalid_data;
    UvDomain next;
    for(std::size_t i=0;i<count;++i){const auto& normal=vertices[i];const auto& v=normal.base;
        // IEEE754 exponent checks avoid serial VFP comparisons/flag transfers
        // on ARM11. memcpy retains the float object/aliasing contract.
        std::array<std::uint32_t,5> bits;std::memcpy(bits.data(),&v,sizeof(bits));
        if((bits[0]&0x7fffffffu)>=0x7f800000u||(bits[1]&0x7fffffffu)>=0x7f800000u||
           (bits[2]&0x7fffffffu)>=0x7f800000u||(bits[3]&0x7fffffffu)>=0x7f800000u||
           (bits[4]&0x7fffffffu)>=0x7f800000u)return Error::invalid_data;
        if(normal.x<-512||normal.x>511||normal.y<-512||normal.y>511||normal.z<-512||normal.z>511||
           normal.uv_source<0||normal.uv_source>2)return Error::invalid_data;
        if(domain)next.include(v.u,v.v);
    }
    if(domain)*domain=next;
    return Error::none;
}
std::size_t mesh_bytes(const MeshView& mesh) noexcept {
    // Counts must have passed validate(). Align index storage independently.
    return ((mesh.vertex_count * sizeof(Vertex) + 0x7f) & ~std::size_t(0x7f)) +
           ((mesh.index_count * sizeof(std::uint16_t) + 0x7f) & ~std::size_t(0x7f));
}
bool texture_dimensions(unsigned w, unsigned h) noexcept {
    return w >= 8 && h >= 8 && w <= 1024 && h <= 1024 && !(w & (w-1)) && !(h & (h-1));
}
std::size_t texture_bytes(unsigned w,unsigned h,TextureFormat format) noexcept {
    if(!texture_dimensions(w,h))return 0;
    if(format==TextureFormat::rgba8)return std::size_t(w)*h*4;
    if(format==TextureFormat::alpha4)return std::size_t(w)*h/2;
    return 0;
}
Error cook_rgba8(const std::uint8_t* src, std::size_t ns, unsigned w, unsigned h,
                 std::uint8_t* dst, std::size_t nd) noexcept {
    if (!src || !dst || !texture_dimensions(w,h)) return Error::invalid_data;
    const std::size_t bytes = std::size_t(w)*h*4;
    const auto a = reinterpret_cast<std::uintptr_t>(src), b = reinterpret_cast<std::uintptr_t>(dst);
    if (ns != bytes || nd != bytes || (a <= b ? b-a < bytes : a-b < bytes)) return Error::invalid_data;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const unsigned gy = h-1-y;
        const unsigned morton = (x&1) | ((gy&1)<<1) | ((x&2)<<1) |
            ((gy&2)<<2) | ((x&4)<<2) | ((gy&4)<<3);
        const std::size_t out = (((gy/8)*(w/8)+x/8)*64+morton)*4;
        const std::size_t in = (std::size_t(y)*w+x)*4;
        for (unsigned c = 0; c < 4; ++c) dst[out+c] = src[in+3-c];
    }
    return Error::none;
}
bool Material::operator==(const Material& b) const noexcept {
    return texture == b.texture && blend == b.blend && cull == b.cull &&
        depth_test == b.depth_test && depth_write == b.depth_write && sample_alpha==b.sample_alpha && alpha_cutoff == b.alpha_cutoff &&
        uv_mode==b.uv_mode && (uv_mode==UvMode::fixed?fixed_uv==b.fixed_uv:
            uv_mode==UvMode::normal?normal_uv()==b.normal_uv():uv==b.uv) &&
        tint == b.tint && brightness==b.brightness && fog==b.fog && wrap_s == b.wrap_s && wrap_t == b.wrap_t;
}
bool valid(const Material& m) noexcept {
    if(m.uv_mode==UvMode::affine){for(float v:m.uv)if(!std::isfinite(v))return false;}
    else if(m.uv_mode==UvMode::fixed){if(!texture_dimensions(m.fixed_uv.width,m.fixed_uv.height))return false;}
    else if(m.uv_mode==UvMode::normal){if(!texture_dimensions(m.normal_uv().width,m.normal_uv().height))return false;}
    else return false;
    return (m.blend == Blend::opaque || m.blend == Blend::alpha || m.blend==Blend::premultiplied || m.blend==Blend::highlight) &&
        (m.cull == Cull::none || m.cull == Cull::back || m.cull == Cull::front) &&
        (unsigned(m.wrap_s)<=unsigned(Wrap::mirror)||m.wrap_s==Wrap::transparent_border) &&
        (unsigned(m.wrap_t)<=unsigned(Wrap::mirror)||m.wrap_t==Wrap::transparent_border) && m.brightness>=-16 && m.brightness<=16;
}
bool FrameState::begin() noexcept {
    if (phase_ != Phase::idle) return false;
    phase_ = Phase::upload; return true;
}
bool FrameState::pass() noexcept {
    if (phase_ == Phase::idle) return false;
    phase_ = Phase::drawing; return true;
}
bool FrameState::end() noexcept {
    if (phase_ == Phase::idle) return false;
    phase_ = Phase::idle; return true;
}
}
