#pragma once
#include "model_bank.hpp"
#include <cstring>
namespace sm64ds::render {
// Load-time plan over an already validated, immutable ModelBank. It borrows
// bank bytes only until upload completes. No game transforms or per-frame data.
class MeshGroups {
public:
    struct Group {
        std::size_t vertex_bytes=0,index_bytes=0;
        std::uint64_t uv_admission=0;
        UvDomain uv{};
        bool normals=false;
    };
    struct Placement {unsigned group=0;std::uint32_t vertex=0,index=0;};
    explicit MeshGroups(const ModelBank& bank,bool merge=true) noexcept:bank_(bank){
        for(unsigned m=0;m<bank.meshes();++m){
            const auto mesh=bank.mesh(m);const auto stride=mesh.normals?sizeof(NormalVertex):sizeof(Vertex);
            UvDomain uv;
            for(std::size_t at=0;at<mesh.vertex_bytes;at+=stride){
                float pair[2];std::memcpy(pair,mesh.vertices+at+12,sizeof(pair));uv.include(pair[0],pair[1]);
            }
            // Equality of admission for EVERY supported source dimension keeps
            // the fixed-UV precision contract identical after taking the union.
            std::uint64_t admission=0;unsigned bit=0;
            if(merge)for(unsigned w=8;w<=1024;w*=2)for(unsigned h=8;h<=1024;h*=2,++bit)
                if(uv.accepts(w,h))admission|=std::uint64_t(1)<<bit;
            unsigned g=0;
            for(;merge&&g<count_;++g){const auto& a=group_[g];
                if(a.normals==mesh.normals&&a.uv_admission==admission&&
                   a.vertex_bytes/stride+mesh.vertex_bytes/stride<=65536&&a.index_bytes+mesh.index_bytes<=65535*2)break;
            }
            if(!merge)g=count_;
            if(g==count_){++count_;group_[g].normals=mesh.normals;group_[g].uv_admission=admission;}
            auto& a=group_[g];placement_[m]={g,std::uint32_t(a.vertex_bytes/stride),std::uint32_t(a.index_bytes/2)};
            a.vertex_bytes+=mesh.vertex_bytes;a.index_bytes+=mesh.index_bytes;
            for(unsigned axis=0;axis<2;++axis){
                if(uv.low[axis]<a.uv.low[axis])a.uv.low[axis]=uv.low[axis];
                if(uv.high[axis]>a.uv.high[axis])a.uv.high[axis]=uv.high[axis];
                if(uv.fractional_bits[axis]>a.uv.fractional_bits[axis])a.uv.fractional_bits[axis]=uv.fractional_bits[axis];
            }
        }
    }
    unsigned size() const noexcept {return count_;}
    const Group& group(unsigned g) const noexcept {return group_[g];}
    Placement placement(unsigned mesh) const noexcept {return placement_[mesh];}
    // Destinations belong to the selected group's exact-sized GPU allocation.
    // Validated source indices + the plan's vertex ceiling guarantee uint16.
    void copy(unsigned g,void* vertices,void* indices) const noexcept {
        auto* v=static_cast<std::uint8_t*>(vertices);auto* ix=static_cast<std::uint8_t*>(indices);
        const auto stride=group_[g].normals?sizeof(NormalVertex):sizeof(Vertex);
        for(unsigned m=0;m<bank_.meshes();++m){const auto p=placement_[m];if(p.group!=g)continue;
            const auto mesh=bank_.mesh(m);std::memcpy(v+p.vertex*stride,mesh.vertices,mesh.vertex_bytes);
            if(!p.vertex){std::memcpy(ix+p.index*2,mesh.indices,mesh.index_bytes);continue;}
            for(std::size_t at=0;at<mesh.index_bytes;at+=2){
                const auto index=std::uint32_t(mesh.indices[at])|(std::uint32_t(mesh.indices[at+1])<<8);
                const auto remapped=index+p.vertex;
                ix[p.index*2+at]=std::uint8_t(remapped);ix[p.index*2+at+1]=std::uint8_t(remapped>>8);
            }
        }
    }
private:
    ModelBank bank_;
    std::array<Group,ModelBank::max_meshes> group_{};
    std::array<Placement,ModelBank::max_meshes> placement_{};
    unsigned count_=0;
};
}
