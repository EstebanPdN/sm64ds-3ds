#include "canvas_catalog.hpp"
#include <cstring>
#ifdef SM64DS_NATIVE_CANVAS_CATALOG_HEADER
#include SM64DS_NATIVE_CANVAS_CATALOG_HEADER
#endif
namespace sm64ds::render {
Error CanvasCatalog::open(const CanvasBinding* banks,std::size_t count,CanvasCatalog& out) noexcept {
    if(count>65536||(count&&!banks))return Error::invalid_data;
    for(std::size_t i=0;i<count;++i){const auto& b=banks[i];
        if(b.bank_id>65535||(i&&b.bank_id<=banks[i-1].bank_id)||!b.parts||b.parts>ModelBank::max_parts||!b.images||b.images>ModelBank::max_images)return Error::invalid_data;
    }
    CanvasCatalog next;next.banks_=banks;next.size_=count;out=next;return Error::none;
}
const CanvasBinding* CanvasCatalog::find(std::uint32_t id) const noexcept {
    std::size_t lo=0,hi=size_;while(lo<hi){const auto mid=lo+(hi-lo)/2;if(banks_[mid].bank_id<id)lo=mid+1;else hi=mid;}
    return lo<size_&&banks_[lo].bank_id==id?banks_+lo:nullptr;
}
Error validate(const ModelBank& bank,const CanvasBinding& binding) noexcept {
    if(!bank||binding.bank_id>65535||!binding.images||bank.requires_material_bindings()||bank.matrices()!=1||bank.parts()!=binding.parts||bank.images()!=binding.images||
       std::memcmp(bank.source_sha256(),binding.source_sha256.data(),32))return Error::invalid_data;
    for(unsigned i=0;i<bank.materials();++i){const auto m=bank.surface(i);
        if(m.texture>=bank.images()||m.material.blend!=Blend::alpha||m.material.cull!=Cull::none||m.material.depth_test||m.material.depth_write||
           m.material.uv_mode!=UvMode::affine||m.material.uv!=std::array<float,6>{1,0,0,0,1,0}||m.material.tint!=0xffffffffu||
           m.material.wrap_s!=Wrap::clamp||m.material.wrap_t!=Wrap::clamp||m.material.alpha_cutoff)return Error::invalid_data;
    }
    for(unsigned i=0;i<bank.meshes();++i){const auto mesh=bank.mesh(i);if(mesh.normals)return Error::invalid_data;
        for(std::size_t v=0;v<mesh.vertex_bytes;v+=sizeof(Vertex)){Vertex vertex;std::memcpy(&vertex,static_cast<const std::uint8_t*>(mesh.vertices)+v,sizeof(vertex));
            if(vertex.z!=0||vertex.x<0||vertex.x>1||vertex.y<0||vertex.y>1)return Error::invalid_data;
        }
    }
    for(unsigned i=0;i<bank.parts();++i)if(bank.part(i).matrix)return Error::invalid_data;
    return Error::none;
}
CanvasCatalog prepared_canvas_catalog() noexcept {
    CanvasCatalog result;
#ifdef SM64DS_NATIVE_CANVAS_CATALOG_HEADER
    CanvasCatalog::open(prepared_canvases::banks,sizeof(prepared_canvases::banks)/sizeof(prepared_canvases::banks[0]),result);
#endif
    return result;
}
}
