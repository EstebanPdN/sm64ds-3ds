#include "native_model.hpp"
#include <cstdlib>
#include <new>
#include <type_traits>
#include <cstdio>
#include <cstring>
#include <utility>
#include <algorithm>

namespace sm64ds::render {
Error NativeModel::span(unsigned index,DrawSpan& output) const noexcept {
    if(!geometry_ready_||index>=parts_)return Error::invalid_data;
    const auto& p=part_[index];
    if(p.mesh>=meshes_||p.material>=materials_)return Error::invalid_data;
    output={mesh_[p.mesh],material_[p.material],p.first,p.count};return Error::none;
}
namespace {
std::size_t aligned(std::size_t n,std::size_t a) noexcept { return (n+a-1)&~(a-1); }
}
void NativeModel::take(NativeModel& other) noexcept {
    batch_storage_=other.batch_storage_;batch_mesh_=other.batch_mesh_;batch_range_=other.batch_range_;
    batch_meshes_=other.batch_meshes_;batch_bytes_=other.batch_bytes_;
    other.batch_storage_=nullptr;other.batch_mesh_=nullptr;other.batch_range_=nullptr;other.batch_meshes_=0;other.batch_bytes_=0;
    owner_=other.owner_;storage_=other.storage_;mesh_=other.mesh_;image_=other.image_;
    material_=other.material_;part_=other.part_;meshes_=other.meshes_;images_=other.images_;
    materials_=other.materials_;parts_=other.parts_;matrices_=other.matrices_;bytes_=other.bytes_;
    material_bindings_=other.material_bindings_;
    geometry_ready_=other.geometry_ready_;empty_parts_=other.empty_parts_;
    source_sha256_=other.source_sha256_;
    other.owner_=nullptr;other.storage_=nullptr;other.mesh_=nullptr;other.image_=nullptr;
    other.material_=nullptr;other.part_=nullptr;
    other.meshes_=other.images_=other.materials_=other.parts_=other.matrices_=0;other.bytes_=0;
    other.material_bindings_=false;
    other.geometry_ready_=false;other.empty_parts_.fill(0);other.source_sha256_={};
}
NativeModel::NativeModel(NativeModel&& other) noexcept { take(other); }
NativeModel& NativeModel::operator=(NativeModel&& other) noexcept {
    if(this!=&other){reset();take(other);}return *this;
}
void NativeModel::reset() noexcept {
    if(owner_){
        // Upload phase begins only after the previous queue is complete. A
        // cache eviction here can reclaim its budget before loading another
        // asset. Draw-time release remains deferred until the next fence.
        for(unsigned i=0;i<batch_meshes_;++i)if(batch_mesh_[i]){
            if(owner_->can_upload())owner_->destroy(batch_mesh_[i]);else owner_->retire(batch_mesh_[i]);
        }
        for(unsigned i=0;i<meshes_;++i)if(mesh_[i]){
            if(owner_->can_upload())owner_->destroy(mesh_[i]);else owner_->retire(mesh_[i]);
        }
        for(unsigned i=0;i<images_;++i)if(image_[i]){
            if(owner_->can_upload())owner_->destroy(image_[i]);else owner_->retire(image_[i]);
        }
    }
    std::free(batch_storage_);batch_storage_=nullptr;batch_mesh_=nullptr;batch_range_=nullptr;batch_meshes_=0;batch_bytes_=0;
    std::free(storage_);storage_=nullptr;owner_=nullptr;mesh_=nullptr;image_=nullptr;material_=nullptr;part_=nullptr;
    meshes_=images_=materials_=parts_=matrices_=0;bytes_=0;
    material_bindings_=false;
    geometry_ready_=false;empty_parts_.fill(0);source_sha256_={};
}
Error NativeModel::upload(Citro3DBackend& renderer,const ModelBank& bank,std::size_t batch_budget,bool trace) noexcept {
    if(storage_ || !renderer.can_upload())return Error::invalid_phase;
    if(!bank)return Error::invalid_data;
    static const bool merge=[](){const auto* option=std::getenv("SM64DS_NATIVE_MESH_GROUPS");return option&&*option=='1';}();
    if(trace)std::puts("Native canvas upload: phase=group-begin");
    const MeshGroups groups(bank,merge);
    if(trace)std::puts("Native canvas upload: phase=group-done");
    NativeModel next;
    std::memcpy(next.source_sha256_.data(),bank.source_sha256(),next.source_sha256_.size());
    next.meshes_=groups.size();next.images_=bank.images();next.materials_=bank.materials();
    next.parts_=bank.parts();next.matrices_=bank.matrices();
    next.material_bindings_=bank.requires_material_bindings();
    const auto image_at=aligned(next.meshes_*sizeof(Mesh),alignof(Texture));
    const auto material_at=aligned(image_at+next.images_*sizeof(Texture),alignof(Material));
    const auto part_at=aligned(material_at+next.materials_*sizeof(Material),alignof(ModelBank::Part));
    next.bytes_=part_at+next.parts_*sizeof(ModelBank::Part);
    // Ordinary metadata uses the system allocator explicitly, never the retail
    // game's global operator new. No second decoded vertex/image allocation.
    next.storage_=std::malloc(next.bytes_);
    if(!next.storage_)return Error::out_of_memory;
    if(trace)std::printf("Native canvas upload: phase=metadata bytes=%u\n",unsigned(next.bytes_));
    auto* base=static_cast<unsigned char*>(next.storage_);
    next.mesh_=reinterpret_cast<Mesh*>(base);next.image_=reinterpret_cast<Texture*>(base+image_at);
    next.material_=reinterpret_cast<Material*>(base+material_at);next.part_=reinterpret_cast<ModelBank::Part*>(base+part_at);
    for(unsigned i=0;i<next.meshes_;++i)new(next.mesh_+i)Mesh{};
    for(unsigned i=0;i<next.images_;++i)new(next.image_+i)Texture{};
    for(unsigned i=0;i<next.materials_;++i)new(next.material_+i)Material{};
    for(unsigned i=0;i<next.parts_;++i)new(next.part_+i)ModelBank::Part{};
    static_assert(std::is_trivially_destructible<Mesh>::value && std::is_trivially_destructible<Texture>::value &&
        std::is_trivially_destructible<Material>::value && std::is_trivially_destructible<ModelBank::Part>::value);
    auto failed=[&](Error error){
        // Newly created resources have not been drawn and this is still the
        // upload phase: rollback can reclaim them without deferred retirement.
        for(unsigned i=0;i<next.meshes_;++i)if(next.mesh_[i])renderer.destroy(next.mesh_[i]);
        for(unsigned i=0;i<next.images_;++i)if(next.image_[i])renderer.destroy(next.image_[i]);
        return error; // next has no owner, so its destructor only frees metadata
    };
    for(unsigned i=0;i<next.meshes_;++i){
        if(trace)std::printf("Native canvas upload: phase=mesh-begin index=%u bytes=%u\n",i,unsigned(groups.group(i).vertex_bytes+groups.group(i).index_bytes));
        const auto result=renderer.create_mesh_group(groups,i,next.mesh_[i]);
        if(result!=Error::none)return failed(result);
        if(trace)std::printf("Native canvas upload: phase=mesh-done index=%u\n",i);
    }
    for(unsigned i=0;i<next.images_;++i){
        const auto source=bank.image(i);
        if(trace)std::printf("Native canvas upload: phase=texture-begin index=%u bytes=%u\n",i,unsigned(source.bytes));
        const auto result=renderer.create_texture(source.width,source.height,source.data,source.bytes,next.image_[i]);
        if(result!=Error::none)return failed(result);
        if(trace)std::printf("Native canvas upload: phase=texture-done index=%u\n",i);
    }
    for(unsigned i=0;i<next.materials_;++i){
        const auto source=bank.surface(i);next.material_[i]=source.material;
        if(source.texture!=ModelBank::no_texture)next.material_[i].texture=next.image_[source.texture];
    }
    for(unsigned i=0;i<next.parts_;++i){auto part=bank.part(i);const auto placed=groups.placement(part.mesh);
        part.mesh=placed.group;part.first+=placed.index;next.part_[i]=part;
        if(!bank.has_distinct_index_triangle(i))next.empty_parts_[i/32]|=1u<<(i%32);
    }
    std::printf("Native geometry residency: sourceMeshes=%u gpuGroups=%u merge=%u\n",bank.meshes(),groups.size(),unsigned(merge));
    if(batch_budget){
        BatchGeometry packed;auto cached=packed.prepare(bank,batch_budget);
        if(cached==Error::none){
            const unsigned groups=(bank.parts()+7)/8;
            const auto range_at=aligned(groups*sizeof(Mesh),alignof(BatchRange));
            next.batch_bytes_=range_at+bank.parts()*sizeof(BatchRange);
            next.batch_storage_=std::malloc(next.batch_bytes_);
            if(!next.batch_storage_)cached=Error::out_of_memory;
            else{
                next.batch_mesh_=static_cast<Mesh*>(next.batch_storage_);
                next.batch_range_=reinterpret_cast<BatchRange*>(static_cast<std::uint8_t*>(next.batch_storage_)+range_at);
                for(unsigned i=0;i<groups;++i)new(next.batch_mesh_+i)Mesh{};
                for(unsigned i=0;i<bank.parts();++i)new(next.batch_range_+i)BatchRange{};
                for(unsigned g=0;g<groups;++g){const auto first=g*8,count=std::min(8u,bank.parts()-first);
                    cached=renderer.create_resident_batch(packed,first,count,next.batch_mesh_[g],next.batch_range_+first);
                    if(cached!=Error::none)break;
                    ++next.batch_meshes_;
                }
            }
        }
        if(cached!=Error::none){
            for(unsigned i=0;i<next.batch_meshes_;++i)renderer.destroy(next.batch_mesh_[i]);
            std::free(next.batch_storage_);next.batch_storage_=nullptr;next.batch_mesh_=nullptr;next.batch_range_=nullptr;
            next.batch_meshes_=0;next.batch_bytes_=0;
        }
        std::printf("Native resident palette: parts=%u groups=%u metadata=%u transient=%u status=%s\n",
            bank.parts(),next.batch_meshes_,unsigned(next.batch_bytes_),unsigned(packed.bytes()),describe(cached));
    }
    next.owner_=&renderer;next.geometry_ready_=true;take(next);return Error::none;
}
Error NativeModel::upload_images(Citro3DBackend& renderer,const ModelBank& bank,
                                const std::array<bool,ModelBank::max_images>& required) noexcept {
    if(!renderer.can_upload())return Error::invalid_phase;
    if(!bank||!bank.images())return Error::invalid_data;
    if(storage_&&(owner_!=&renderer||images_!=bank.images()||
       std::memcmp(source_sha256_.data(),bank.source_sha256(),source_sha256_.size())))return Error::invalid_data;
    bool any=false;
    for(unsigned i=0;i<required.size();++i)if(required[i]){if(i>=bank.images())return Error::invalid_data;any=true;}
    if(!any)return Error::invalid_data;
    NativeModel next;
    if(!storage_){
        next.images_=bank.images();next.bytes_=next.images_*sizeof(Texture);
        next.storage_=std::malloc(next.bytes_);if(!next.storage_)return Error::out_of_memory;
        next.image_=static_cast<Texture*>(next.storage_);
        for(unsigned i=0;i<next.images_;++i)new(next.image_+i)Texture{};
        std::memcpy(next.source_sha256_.data(),bank.source_sha256(),next.source_sha256_.size());
    }
    auto& destination=storage_?*this:next;
    std::array<Texture,ModelBank::max_images> staged{};
    for(unsigned i=0;i<bank.images();++i)if(required[i]&&!destination.image_[i]){
        const auto image=bank.image(i);const auto error=renderer.create_texture(image.width,image.height,image.data,image.bytes,staged[i]);
        if(error!=Error::none){
            for(unsigned j=0;j<i;++j)if(staged[j])renderer.destroy(staged[j]);
            return error;
        }
    }
    for(unsigned i=0;i<bank.images();++i)if(staged[i])destination.image_[i]=staged[i];
    if(!storage_){next.owner_=&renderer;take(next);}return Error::none;
}
Material NativeModel::material(unsigned index) const noexcept {
    return storage_ && index<materials_?material_[index]:Material{};
}
Texture NativeModel::image(unsigned index) const noexcept {
    return storage_ && index<images_?image_[index]:Texture{};
}
Error NativeModel::draw(const Matrix* matrices,std::size_t count,const Material* overrides,std::size_t override_count) const noexcept {
    if(!owner_ || !owner_->can_draw()||!geometry_ready_)return Error::invalid_phase;
    if(!matrices || count<matrices_ || (material_bindings_ && !overrides) ||
        (overrides?override_count!=materials_:override_count!=0))return Error::invalid_data;
    const auto* materials=overrides?overrides:material_;
    // Refuse bad instance inputs before the first GPU draw of this model.
    for(unsigned i=0;i<materials_;++i){
        if(!valid(materials[i]))return Error::invalid_data;
        if(!owner_->texture_alive(materials[i].texture))return Error::stale_handle;
    }
    for(unsigned i=0;i<parts_;++i){
        if(empty_parts_[i/32]&(1u<<(i%32)))continue;
        const auto& p=part_[i];const auto& m=materials[p.material];
        if(!finite(matrices[p.matrix]) || (m.uv_mode==UvMode::fixed&&
            (!m.texture||!owner_->fixed_uv_compatible(mesh_[p.mesh],m.fixed_uv))) ||
            (m.uv_mode==UvMode::normal&&(!m.texture||!owner_->normal_uv_compatible(mesh_[p.mesh],m.normal_uv()))))return Error::invalid_data;
    }
    for(unsigned i=0;i<parts_;++i){
        if(empty_parts_[i/32]&(1u<<(i%32)))continue;
        const auto& p=part_[i];
        const auto result=owner_->draw(mesh_[p.mesh],materials[p.material],matrices[p.matrix],p.first,p.count);
        if(result!=Error::none)return result;
    }
    return Error::none;
}
Error NativeModel::draw_prepared(const PreparedPart* instance,std::size_t count,LightingPaletteView lighting) const noexcept {
    if(!owner_||!owner_->can_draw()||!geometry_ready_)return Error::invalid_phase;
    if(!instance||count!=parts_)return Error::invalid_data;
    static const bool census=[](){const auto* v=std::getenv("SM64DS_NATIVE_BATCH_CENSUS");return v&&*v=='1';}();
    static unsigned census_calls=0;
    if(census&&parts_>1&&(census_calls++%240)==0){
        unsigned visible=0,affine=0,pairs=0,linear_pairs=0,state_pairs=0,runs=0,batched=0,run_length=0;
        const PreparedPart* previous=nullptr;unsigned previous_index=0;
        for(unsigned i=0;i<parts_;++i){
            if(empty_parts_[i/32]&(1u<<(i%32)))continue;
            const auto& p=instance[i];if(!p.visible)continue;
            ++visible;affine+=p.material.uv_mode==UvMode::affine;
            bool same_linear=previous!=nullptr,same_state=previous!=nullptr;
            if(previous){
                ++pairs;
                for(unsigned row=0;row<4;++row)for(unsigned c=0;c<3;++c)
                    same_linear&=p.object_to_clip.row[row][c]==previous->object_to_clip.row[row][c];
                same_state=p.material==previous->material&&p.image==previous->image&&p.lighting==previous->lighting;
                linear_pairs+=same_linear;state_pairs+=same_state;
            }
            if(previous&&same_linear&&same_state&&p.material.uv_mode==UvMode::affine&&i/16==previous_index/16){
                if(run_length==1){++runs;batched+=2;}else ++batched;
                ++run_length;
            }else run_length=1;
            previous=&p;previous_index=i;
        }
        std::printf("Native batch census: parts=%u visible=%u affine=%u adjacent=%u sameLinear=%u sameState=%u candidateRuns=%u candidateDraws=%u\n",
            parts_,visible,affine,pairs,linear_pairs,state_pairs,runs,batched);
    }
    const auto validated=validate_prepared(instance,count,lighting);if(validated!=Error::none)return validated;
    for(unsigned i=0;i<parts_;++i){
        if(empty_part(i)||!instance[i].visible)continue;
        const auto error=draw_prepared_part(i,instance[i],lighting);if(error!=Error::none)return error;
    }
    return Error::none;
}
Error NativeModel::validate_prepared(const PreparedPart* instance,std::size_t count,LightingPaletteView lighting,bool palette_resolved) const noexcept {
    if(!owner_||!geometry_ready_||(!owner_->can_upload()&&!owner_->can_draw()))return Error::invalid_phase;
    if(!instance||count!=parts_)return Error::invalid_data;
    for(unsigned i=0;i<parts_;++i){
        if(empty_parts_[i/32]&(1u<<(i%32)))continue;
        const auto& p=instance[i];
        if(!p.visible)continue;
        if(p.lighting!=no_lighting){
            // BUILD134: a palette whose every state already resolved leaves
            // one way for resolve_lighting to refuse a part: its index.
            if(palette_resolved){if(p.lighting>=lighting.state_count)return Error::invalid_data;}
            else{LightingInput input;
                const auto error=resolve_lighting(lighting,p.lighting,input);if(error!=Error::none)return error;}
            if(!owner_->lighting_compatible(mesh_[part_[i].mesh]))return Error::invalid_data;}
        if(p.material.texture||!finite(p.object_to_clip)||!valid(p.material))return Error::invalid_data;
        if(p.image!=ModelBank::no_texture&&p.image>=images_)return Error::invalid_data;
        const auto texture=p.image==ModelBank::no_texture?Texture{}:image_[p.image];
        if((p.image!=ModelBank::no_texture&&!texture)||!owner_->texture_alive(texture))return Error::stale_handle;
        if(p.material.uv_mode==UvMode::fixed&&(!texture||!owner_->fixed_uv_compatible(mesh_[part_[i].mesh],p.material.fixed_uv)))
            return Error::invalid_data;
        if(p.material.uv_mode==UvMode::normal&&(!texture||!owner_->normal_uv_compatible(mesh_[part_[i].mesh],p.material.normal_uv())))
            return Error::invalid_data;
    }
    return Error::none;
}
Error NativeModel::draw_prepared_part(unsigned index,const PreparedPart& part,LightingPaletteView palette) const noexcept {
    // Called only after complete-model validation, while the owner keeps its
    // resources pinned. Read typed lighting values without validating again.
    auto material=part.material;material.texture=part.image==ModelBank::no_texture?Texture{}:image_[part.image];
    LightingInput input;
    if(part.lighting!=no_lighting){const auto& light=palette.state[part.lighting];
        input={light.material.mask?palette.lights+light.lights:nullptr,light.normal,light.material};}
    const auto& geometry=part_[index];
    return owner_->draw(mesh_[geometry.mesh],material,part.object_to_clip,geometry.first,geometry.count,
        part.lighting==no_lighting?nullptr:&input);
}
}
