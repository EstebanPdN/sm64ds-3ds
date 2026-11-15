#include "native_batch.hpp"
#include <cstring>
#include <limits>
#include <cstdlib>
#include <new>
#include <utility>
#include <algorithm>
#include "scene_frame.hpp"
#include "model_bank.hpp"
namespace sm64ds::render {
BatchGeometry::BatchGeometry(BatchGeometry&& other) noexcept { *this=std::move(other); }
BatchGeometry& BatchGeometry::operator=(BatchGeometry&& other) noexcept {
    if(this!=&other){reset();storage_=other.storage_;bytes_=other.bytes_;parts_=other.parts_;
        other.storage_=nullptr;other.bytes_=0;other.parts_=0;}
    return *this;
}
void BatchGeometry::reset() noexcept {std::free(storage_);storage_=nullptr;bytes_=0;parts_=0;}
Error BatchGeometry::prepare(const ModelBank& bank,std::size_t budget) noexcept {
    if(storage_)return Error::invalid_phase;
    if(!bank)return Error::invalid_data;
    std::size_t size=bank.parts()*sizeof(Part);
    if(size>budget)return Error::budget;
    auto* remap=static_cast<std::uint16_t*>(std::malloc(65536*sizeof(std::uint16_t)));
    if(!remap)return Error::out_of_memory;
    std::array<std::uint16_t,ModelBank::max_parts> unique{};
    Error error=Error::none;
    for(unsigned p=0;p<bank.parts();++p){const auto part=bank.part(p);const auto mesh=bank.mesh(part.mesh);
        const auto index=[&](unsigned i){const auto* b=mesh.indices+(part.first+i)*2;return unsigned(b[0])|(unsigned(b[1])<<8);};
        for(unsigned i=0;i<part.count;++i)remap[index(i)]=0xffffu;
        for(unsigned i=0;i<part.count;++i){auto& mapped=remap[index(i)];
            if(mapped==0xffffu){
                if(unique[p]==NativeBatchPlan::max_vertices){error=Error::capacity;break;}
                mapped=unique[p]++;
            }
        }
        if(error!=Error::none)break;
        // Subtract before multiplying: retain the supplied CPU budget on ARM32.
        if(part.count>(budget-size)/2){error=Error::budget;break;}
        size+=std::size_t(part.count)*2;
        const auto padding=(4-(size&3))&3;
        if(padding>budget-size){error=Error::budget;break;}size+=padding;
        const auto stride=sizeof(BatchVertex);
        if(unique[p]>(budget-size)/stride){error=Error::budget;break;}size+=unique[p]*stride;
    }
    if(error!=Error::none){std::free(remap);return error;}
    auto* storage=static_cast<std::uint8_t*>(std::malloc(size));
    if(!storage){std::free(remap);return Error::out_of_memory;}
    auto* entries=reinterpret_cast<Part*>(storage);std::size_t at=bank.parts()*sizeof(Part);
    for(unsigned p=0;p<bank.parts();++p){const auto part=bank.part(p);const auto mesh=bank.mesh(part.mesh);
        auto* entry=new(entries+p)Part{};entry->indices=at;entry->index_count=part.count;
        at+=part.count*2;at=(at+3)&~std::size_t(3);entry->vertices=at;entry->vertex_count=unique[p];entry->normals=mesh.normals;
        const auto stride=sizeof(BatchVertex);at+=unique[p]*stride;
        auto* output=reinterpret_cast<std::uint16_t*>(storage+entry->indices);unsigned vertices=0;
        const auto index=[&](unsigned i){const auto* b=mesh.indices+(part.first+i)*2;return unsigned(b[0])|(unsigned(b[1])<<8);};
        for(unsigned i=0;i<part.count;++i)remap[index(i)]=0xffffu;
        for(unsigned i=0;i<part.count;++i){const auto source=index(i);auto& mapped=remap[source];
            if(mapped==0xffffu){mapped=std::uint16_t(vertices++);
                auto* vertex=new(reinterpret_cast<BatchVertex*>(storage+entry->vertices)+mapped)BatchVertex{};
                const auto source_stride=mesh.normals?sizeof(NormalVertex):sizeof(Vertex);
                std::memcpy(&vertex->source,mesh.vertices+source*source_stride,source_stride);}
            output[i]=mapped;
        }
    }
    std::free(remap);storage_=storage;bytes_=size;parts_=bank.parts();return Error::none;
}
BatchSource BatchGeometry::source(unsigned part) const noexcept {
    BatchSource source;if(part>=parts_)return source;
    const auto& entry=static_cast<const Part*>(storage_)[part];const auto* storage=static_cast<const std::uint8_t*>(storage_);
    source.vertices_=reinterpret_cast<const BatchVertex*>(storage+entry.vertices);
    source.indices_=reinterpret_cast<const std::uint16_t*>(storage+entry.indices);
    source.vertex_count_=entry.vertex_count;source.index_count_=entry.index_count;source.normals_=entry.normals;
    return source;
}
void NativeBatchPlan::reset() noexcept {spans_=0;open_=sealed_=false;error_=Error::none;}
bool NativeBatchPlan::supports(const LightingInput* input) noexcept {
    if(!input)return true;
    const auto& m=input->material;
    if(m.toon||(m.mask&&m.specular&&m.use_shininess))return false;
    if(m.mask)for(auto value:input->normal)if(value<-32768||value>32767)return false;
    return true;
}
namespace {
bool same_lighting(const BatchSpan& span,const LightingInput* input) noexcept {
    if(span.lit!=bool(input))return false;
    if(!input)return true;
    if(!(span.lighting==input->material))return false;
    if(span.lights==input->lights)return true; // the frame interns its light sets
    for(unsigned i=0;i<4;++i)
        if((input->material.mask&(1u<<i))&&!(span.lights->light[i]==input->lights->light[i]))return false;
    return true;
}
}
Error NativeBatchPlan::append(BatchRange range,const Material& material,const Matrix* matrix,
    const LightingInput* lighting,unsigned& output) noexcept {
    if(sealed_||error_!=Error::none)return error_==Error::none?Error::invalid_phase:error_;
    if(!matrix||!range||range.slot>=8||range.first%3||range.count%3||range.count>std::numeric_limits<std::uint32_t>::max()-range.first)
        return error_=Error::invalid_data;
    if(material.uv_mode!=UvMode::affine||!supports(lighting))return Error::unsupported;
    BatchBasis basis{};
    if(lighting&&lighting->material.mask)for(unsigned row=0;row<3;++row)for(unsigned axis=0;axis<3;++axis)
        basis[row][axis]=std::int16_t(lighting->normal[axis*3+row]);
    const auto bit=std::uint8_t(1u<<range.slot);
    bool compatible=open_&&span_[spans_-1].mesh==range.mesh;
    if(compatible){const auto& group=span_[spans_-1];
        compatible=group.first+group.count==range.first&&group.material==material&&same_lighting(group,lighting)&&
            (!(group.used&bit)||((group.palette[range.slot]==matrix||!std::memcmp(group.palette[range.slot],matrix,sizeof(*matrix)))&&
                group.basis[range.slot]==basis));
    }
    if(!compatible){
        if(spans_==max_spans)return error_=Error::capacity;
        auto& group=span_[spans_++];group.mesh=range.mesh;group.material=material;
        group.lit=bool(lighting);group.lighting=lighting?lighting->material:LightingMaterial{};
        group.lights=lighting&&lighting->material.mask?lighting->lights:nullptr;
        group.first=range.first;group.count=0;group.used=0;open_=true;
    }
    auto& group=span_[spans_-1];group.palette[range.slot]=matrix;group.basis[range.slot]=basis;
    group.used|=bit;group.count+=range.count;output=unsigned(spans_-1);return Error::none;
}
Error NativeBatchPlan::seal() noexcept {
    if(sealed_)return Error::invalid_phase;
    open_=false;
    if(error_!=Error::none){spans_=0;return error_;}
    sealed_=true;return Error::none;
}
namespace {
// validate(const NormalVertex*,...) for one vertex (contracts.cpp).
inline bool usable(const NormalVertex& normal) noexcept {
    std::array<std::uint32_t,5> bits;std::memcpy(bits.data(),&normal.base,sizeof(bits));
    if((bits[0]&0x7fffffffu)>=0x7f800000u||(bits[1]&0x7fffffffu)>=0x7f800000u||
       (bits[2]&0x7fffffffu)>=0x7f800000u||(bits[3]&0x7fffffffu)>=0x7f800000u||
       (bits[4]&0x7fffffffu)>=0x7f800000u)return false;
    return !(normal.x<-512||normal.x>511||normal.y<-512||normal.y>511||normal.z<-512||normal.z>511||
        normal.uv_source<0||normal.uv_source>2);
}
}
bool pack_skin_vertices(const NormalVertex* source,std::size_t count,const std::uint8_t* selectors,
                        BatchVertex* output,UvDomain& domain) noexcept {
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    bool valid=count<=65535&&count%3==0;UvDomain next;
    for(std::size_t i=0;i<count;++i){const auto vertex=source[i];
        if(valid){if(usable(vertex))next.include(vertex.base.u,vertex.base.v);else valid=false;}
        output[i].source=vertex;
        const auto rows=packed_palette_rows(selectors?selectors[i]:0);
        std::memcpy(output[i].rows.data(),&rows,sizeof(rows));
    }
    if(valid)domain=next;
    return valid;
}
bool copy_skin_vertices(const NormalVertex* source,std::size_t count,NormalVertex* output,UvDomain& domain) noexcept {
    bool valid=count<=65535&&count%3==0;UvDomain next;
    for(std::size_t i=0;i<count;++i){const auto vertex=source[i];
        if(valid){if(usable(vertex))next.include(vertex.base.u,vertex.base.v);else valid=false;}
        output[i]=vertex;
    }
    if(valid)domain=next;
    return valid;
}
bool DynamicBatchRanges::prepare(const SceneView& scene) noexcept {
    ready_=false;eligible_.fill(false);
    if(scene.occurrences>max_occurrences||scene.skin_vertices>max_skin_vertices||scene.quad_vertices>max_quad_vertices)return false;
    std::fill_n(skin_.data(),scene.skin_vertices,0);std::fill_n(quad_.data(),scene.quad_vertices,0);
    for(unsigned kind=0;kind<2;++kind){
        // Native captures append each arena in order. Prove disjointness by
        // ranges before paying for a per-vertex owner map. Arbitrary external
        // scenes still take the map path, including unsorted overlaps.
        bool ordered=true;std::size_t end=0;
        for(unsigned i=0;i<scene.occurrences;++i){const auto& o=scene.occurrence[i];
            if(kind?(o.kind!=Occurrence::Kind::quads):(o.kind!=Occurrence::Kind::skin&&o.kind!=Occurrence::Kind::mesh))continue;
            eligible_[i]=true;ordered&=o.first>=end;end=std::size_t(o.first)+o.count;
        }
        if(!ordered){
            std::fill_n(owner_.data(),kind?scene.quad_vertices:scene.skin_vertices,std::uint16_t(0xffff));
            for(unsigned i=0;i<scene.occurrences;++i){const auto& o=scene.occurrence[i];
                if(kind?(o.kind!=Occurrence::Kind::quads):(o.kind!=Occurrence::Kind::skin&&o.kind!=Occurrence::Kind::mesh))continue;
                for(unsigned v=o.first;v<o.first+o.count;++v){const auto previous=owner_[v];
                    if(previous!=0xffff){eligible_[i]=false;eligible_[previous]=false;}else owner_[v]=std::uint16_t(i);
                }
            }
        }
        unsigned slot=0;
        for(unsigned i=0;i<scene.occurrences;++i){const auto& o=scene.occurrence[i];
            if(!eligible_[i]||(kind?(o.kind!=Occurrence::Kind::quads):(o.kind!=Occurrence::Kind::skin&&o.kind!=Occurrence::Kind::mesh)))continue;
            slot_[i]=std::uint8_t(slot++%8);auto* selectors=kind?quad_.data():skin_.data();
            std::fill_n(selectors+o.first,o.count,slot_[i]);
        }
    }
    ready_=true;return true;
}
}
