#include "model_instance.hpp"
#include "perf_switch.hpp"
#include <cstddef>
#include <cstring>
#include <new>

namespace sm64ds::render {
Error validate(const ModelBank& bank,const ModelBindings& bindings) noexcept {
    if(!bank || bindings.file_id>65535 || bindings.bones!=bank.matrices() ||
       !bindings.source_materials || bindings.source_materials>65536 ||
       bindings.images!=bank.images() || bindings.materials!=bank.materials() || bindings.parts!=bank.parts() ||
       (bindings.images&&!bindings.image) || !bindings.material || !bindings.part ||
       std::memcmp(bindings.source_sha256.data(),bank.source_sha256(),32))return Error::invalid_data;
    // Each native image is represented exactly once. Identities, rather than
    // equal initial pixels, preserve separately animated source resources.
    std::array<bool,ModelBank::max_images> images{};
    for(unsigned i=0;i<bindings.images;++i){
        const auto& b=bindings.image[i];
        if(b.image>=bindings.images || images[b.image] || b.texture>65535 ||unsigned(b.use)>unsigned(ImageUse::sprite)||
           (b.palette!=ModelBank::no_texture&&b.palette>65535))return Error::invalid_data;
        const auto native=bank.image(b.image);
        if(b.width!=native.width||b.height!=native.height)return Error::invalid_data;
        images[b.image]=true;
        for(unsigned j=0;j<i;++j)if(b.texture==bindings.image[j].texture&&b.palette==bindings.image[j].palette&&
            b.use==bindings.image[j].use)
            return Error::invalid_data;
    }
    for(unsigned i=0;i<bindings.materials;++i){
        const auto& m=bindings.material[i];
        if(m.source_material>=bindings.source_materials || unsigned(m.colour)>unsigned(ColourSource::lighting) ||
           (m.diffuse_override&&m.colour!=ColourSource::lighting) ||
           !m.width || !m.height || m.width>1024 || m.height>1024)return Error::invalid_data;
        const auto image=bank.surface(i).texture;
        if(image!=ModelBank::no_texture){
            const auto dimensions=bank.image(image);
            if(m.width!=dimensions.width || m.height!=dimensions.height)return Error::invalid_data;
        }else if(m.width!=1 || m.height!=1)return Error::invalid_data;
        for(unsigned j=0;j<i;++j){const auto& previous=bindings.material[j];
            if(previous.source_material==m.source_material&&(previous.width!=m.width||previous.height!=m.height))
                return Error::invalid_data; // colour runs share the same source UV basis
        }
    }
    for(unsigned i=0;i<bindings.parts;++i){
        const auto& b=bindings.part[i];const auto p=bank.part(i);
        if(b.bone>=bindings.bones || b.material>=bindings.materials ||
           b.bone!=p.matrix || b.material!=p.material)return Error::invalid_data;
        if(bindings.material[b.material].normal_uv && (b.billboard||b.normal_bone>=bindings.bones||!bank.mesh(p.mesh).normals))return Error::invalid_data;
        if(bindings.material[b.material].colour==ColourSource::lighting&&
           (b.lighting_bone>=bindings.bones||!bank.mesh(p.mesh).normals))return Error::invalid_data;
    }
    return Error::none;
}
namespace {
Error parameters(const ModelBindings& bindings,const InstanceInputs& input,unsigned part,PreparedPart& out,LightingPalette* palette) noexcept {
    const auto& b=bindings.part[part];
    if(b.material>=bindings.materials || b.bone>=bindings.bones)return Error::invalid_data;
    const auto& association=bindings.material[b.material];
    if(association.source_material>=input.materials)return Error::invalid_data;
    const auto& live=input.material[association.source_material];
    out.visible=live.visible;
    if(!live.visible)return Error::none;
    if((association.colour==ColourSource::diffuse&&!live.diffuse_available)||
       (association.colour==ColourSource::emission&&!live.emission_available))return Error::unsupported;
    const auto* pose=b.billboard?input.billboard:input.ordinary;
    if(!pose || b.bone>=input.poses || !finite(pose[b.bone]) || !valid(live.state) ||
       unsigned(association.colour)>unsigned(ColourSource::lighting))return Error::invalid_data;
    out.object_to_clip=pose[b.bone];out.material=live.state;out.material.texture={};
    const auto rgb=association.colour==ColourSource::diffuse?live.diffuse:
                   association.colour==ColourSource::emission?live.emission:0xffffffffu;
    out.material.tint=(rgb&0xffffff00u)|(live.state.tint&255u);
    if(association.colour==ColourSource::lighting){
        if(!palette)return Error::unsupported;
        LightingInput lighting;lighting.lights=input.lights;lighting.material=live.lighting;
        if(association.diffuse_override){lighting.material.diffuse=association.diffuse_ambient&32767u;
            lighting.material.ambient=(association.diffuse_ambient>>16)&32767u;}
        if(lighting.material.mask){
            const auto* bases=b.billboard?input.light_billboard:input.light_ordinary;
            if(!bases||b.lighting_bone>=input.poses)return Error::invalid_data;
            std::memcpy(lighting.normal.data(),bases[b.lighting_bone].value,sizeof(lighting.normal));
        }
        const auto error=palette->intern(lighting,out.lighting);if(error!=Error::none)return error;
    }
    if(live.texture==ModelBank::no_texture&&live.state.uv_mode!=UvMode::affine)return Error::invalid_data;
    if(live.state.uv_mode==UvMode::normal){
        if(!association.normal_uv||b.billboard||b.normal_bone>=input.poses||!input.normal||
            !texture_dimensions(live.normal_half_width*2,live.normal_half_height*2))return Error::invalid_data;
        const auto& normal=input.normal[b.normal_bone];
        for(unsigned row=0;row<2;++row)for(unsigned axis=0;axis<3;++axis){
            const auto factor=row?0u-std::uint32_t(live.normal_half_height):std::uint32_t(live.normal_half_width);
            const auto bits=std::uint32_t(normal.value[axis*3+row])*factor*16u;
            std::memcpy(&out.material.normal_uv().coefficient[row*3+axis],&bits,4);
        }
    }else if(association.normal_uv)return Error::unsupported;
    if(live.texture!=ModelBank::no_texture){
        bool found=false;
        // BUILD134: the material stage already found the first entry with this
        // identity (LiveMaterial::image); confirm it instead of searching.
        if(live.image<bindings.images&&perf::cuts()){
            const auto& image=bindings.image[live.image];
            if(image.use==ImageUse::model&&image.texture==live.texture&&image.palette==live.palette){
                if(image.image>=bindings.images)return Error::invalid_data;
                out.image=image.image;found=true;
            }
        }
        for(unsigned i=0;!found&&i<bindings.images;++i){
            const auto& image=bindings.image[i];
            if(image.use!=ImageUse::model||image.texture!=live.texture || image.palette!=live.palette)continue;
            if(image.image>=bindings.images)return Error::invalid_data;
            out.image=image.image;found=true;break;
        }
        if(!found)return Error::invalid_data; // unprepared animation identity
    }
    return Error::none;
}
bool inputs_valid(const ModelBindings& bindings,const InstanceInputs& input,const void* output,std::size_t capacity) noexcept {
    return output && capacity>=bindings.parts && bindings.parts && bindings.parts<=ModelBank::max_parts &&
       bindings.part && bindings.material && bindings.materials && bindings.materials<=ModelBank::max_materials &&
       bindings.images<=ModelBank::max_images && (!bindings.images||bindings.image) &&
       input.material && input.materials>=bindings.source_materials;
}
Error resolved(const ModelBindings& bindings,const InstanceInputs& input,unsigned part,PartInstance& out,LightingPalette* lighting) noexcept {
    PreparedPart next;const auto error=parameters(bindings,input,part,next,lighting);if(error!=Error::none)return error;
    if(next.visible&&next.image!=ModelBank::no_texture){
        if(!input.images||next.image>=input.image_count||!input.images[next.image])return Error::stale_handle;
        next.material.texture=input.images[next.image];
    }
    out={next.object_to_clip,next.material,next.visible,next.lighting};return Error::none;
}
}
Error capture_instance(const ModelBindings& bindings,const InstanceInputs& input,PartInstance* output,std::size_t capacity,LightingPalette* lighting) noexcept {
    if(!inputs_valid(bindings,input,output,capacity))return Error::invalid_data;
    // Preflight does not alias caller output: a refused animation/material/pose
    // cannot leave a partially updated occurrence in the published frame.
    const auto mark=lighting?lighting->mark():LightingPalette::Mark{};
    for(unsigned i=0;i<bindings.parts;++i){PartInstance next;const auto e=resolved(bindings,input,i,next,lighting);
        if(e!=Error::none){if(lighting)lighting->rollback(mark);return e;}}
    for(unsigned i=0;i<bindings.parts;++i){PartInstance next;resolved(bindings,input,i,next,lighting);output[i]=next;}
    return Error::none;
}
namespace {
// BUILD134 (exact cut): a model's parts repeat a few material bindings, bones
// and lighting bases many times (a character has 127-171 parts over 2-21
// materials, 18-24 bones and 19-53 lit material/basis pairs). Everything a
// part takes from its material binding alone - admission, colour role, image
// identity - is decided by the first part that uses the binding, through the
// unchanged parameters() above; a bone's pose is tested once; a lighting
// state is interned once for each material/basis pair. Later parts copy the
// same values into the frame directly. Every refusal keeps its part, its
// order and its error; every published value is the one parameters() writes
// (tests/native_renderer/producer_cuts.cpp compares the two bit for bit).
struct LightingMemo {
    static constexpr unsigned slots=128,limit=96;
    std::uint32_t key[slots];std::uint16_t id[slots];unsigned used=0;bool ready=false;
    static unsigned start(std::uint32_t k) noexcept {return (k*2654435761u)>>25;}
    const std::uint16_t* find(std::uint32_t k) const noexcept {
        if(!ready)return nullptr;
        for(unsigned at=start(k);;at=(at+1)&(slots-1)){if(key[at]==k)return id+at;if(!key[at])return nullptr;}
    }
    void put(std::uint32_t k,std::uint16_t value) noexcept {
        if(!ready){std::memset(key,0,sizeof(key));ready=true;}
        if(used==limit)return; // a miss interns again: same identifier
        unsigned at=start(k);while(key[at])at=(at+1)&(slots-1);
        key[at]=k;id[at]=value;++used;
    }
};
static_assert(ModelBank::max_images<255&&ModelBank::max_materials<=512&&ModelBank::max_matrices%32==0&&ModelBank::max_materials%32==0);
Error capture_unpublished(const ModelBindings& bindings,const InstanceInputs& input,PreparedPart* output,LightingPalette* palette) noexcept {
    std::uint32_t admitted[ModelBank::max_materials/32]{};   // material bindings that passed parameters()
    std::uint8_t image_of[ModelBank::max_materials];         // their prepared image, 255 = none
    std::uint32_t finite_pose[2][ModelBank::max_matrices/32]{};
    LightingMemo memo;
    for(unsigned i=0;i<bindings.parts;++i){
        auto& out=output[i];const auto& b=bindings.part[i];
        if(b.material>=bindings.materials || b.bone>=bindings.bones)return Error::invalid_data;
        const auto& association=bindings.material[b.material];
        if(association.source_material>=input.materials)return Error::invalid_data;
        const auto& live=input.material[association.source_material];
        // A default part, built in the frame (what `PreparedPart next;` held).
        if(!live.visible){::new(static_cast<void*>(&out)) PreparedPart;out.visible=false;continue;}
        const unsigned facing=b.billboard?1u:0u;
        const std::uint32_t material_bit=1u<<(b.material%32),pose_bit=1u<<(b.bone%32);
        const bool lit=association.colour==ColourSource::lighting;
        // Unlit materials and lights off (mask 0) carry no basis: one state for the binding.
        const std::uint32_t light_key=((std::uint32_t(b.material)+1u)<<17)|
            (live.lighting.mask?(std::uint32_t(facing)<<16)|b.lighting_bone:0u);
        if(!(admitted[b.material/32]&material_bit)){
            ::new(static_cast<void*>(&out)) PreparedPart;
            const auto error=parameters(bindings,input,i,out,palette);if(error!=Error::none)return error;
            admitted[b.material/32]|=material_bit;
            image_of[b.material]=out.image==ModelBank::no_texture?std::uint8_t(255):std::uint8_t(out.image);
            finite_pose[facing][b.bone/32]|=pose_bit;
            if(lit)memo.put(light_key,out.lighting);
            continue;
        }
        const auto* pose=facing?input.billboard:input.ordinary;
        if(!pose || b.bone>=input.poses)return Error::invalid_data;
        if(!(finite_pose[facing][b.bone/32]&pose_bit)){
            if(!finite(pose[b.bone]))return Error::invalid_data;
            finite_pose[facing][b.bone/32]|=pose_bit;
        }
        std::uint16_t state=no_lighting;
        if(lit){
            if(const auto* known=memo.find(light_key))state=*known;
            else{
                LightingInput lighting;lighting.lights=input.lights;lighting.material=live.lighting;
                if(association.diffuse_override){lighting.material.diffuse=association.diffuse_ambient&32767u;
                    lighting.material.ambient=(association.diffuse_ambient>>16)&32767u;}
                if(lighting.material.mask){
                    const auto* bases=facing?input.light_billboard:input.light_ordinary;
                    if(!bases||b.lighting_bone>=input.poses)return Error::invalid_data;
                    std::memcpy(lighting.normal.data(),bases[b.lighting_bone].value,sizeof(lighting.normal));
                }
                const auto error=palette->intern(lighting,state);if(error!=Error::none)return error;
                memo.put(light_key,state);
            }
        }
        out.object_to_clip=pose[b.bone];out.material=live.state;out.material.texture={};
        const auto rgb=association.colour==ColourSource::diffuse?live.diffuse:
                       association.colour==ColourSource::emission?live.emission:0xffffffffu;
        out.material.tint=(rgb&0xffffff00u)|(live.state.tint&255u);
        if(live.state.uv_mode==UvMode::normal){
            if(!association.normal_uv||b.billboard||b.normal_bone>=input.poses||!input.normal||
                !texture_dimensions(live.normal_half_width*2,live.normal_half_height*2))return Error::invalid_data;
            const auto& normal=input.normal[b.normal_bone];
            for(unsigned row=0;row<2;++row)for(unsigned axis=0;axis<3;++axis){
                const auto factor=row?0u-std::uint32_t(live.normal_half_height):std::uint32_t(live.normal_half_width);
                const auto bits=std::uint32_t(normal.value[axis*3+row])*factor*16u;
                std::memcpy(&out.material.normal_uv().coefficient[row*3+axis],&bits,4);
            }
        }
        // Deterministic tail (image, visibility, lighting, highlight and padding).
        std::memset(reinterpret_cast<unsigned char*>(&out)+offsetof(PreparedPart,image),0,sizeof(PreparedPart)-offsetof(PreparedPart,image));
        out.image=image_of[b.material]==255?ModelBank::no_texture:std::uint32_t(image_of[b.material]);
        out.visible=true;out.lighting=state;out.highlight=false;
    }
    return Error::none;
}
}
Error capture_prepared(const ModelBindings& bindings,const InstanceInputs& input,PreparedPart* output,std::size_t capacity,LightingPalette* lighting,CaptureOutput ownership) noexcept {
    if(!inputs_valid(bindings,input,output,capacity))return Error::invalid_data;
    const auto mark=lighting?lighting->mark():LightingPalette::Mark{};
    if(ownership==CaptureOutput::unpublished&&perf::cuts()){
        const auto e=capture_unpublished(bindings,input,output,lighting);
        if(e!=Error::none&&lighting)lighting->rollback(mark);
        return e;
    }
    if(ownership==CaptureOutput::retained)for(unsigned i=0;i<bindings.parts;++i){PreparedPart next;const auto e=parameters(bindings,input,i,next,lighting);
        if(e!=Error::none){if(lighting)lighting->rollback(mark);return e;}}
    for(unsigned i=0;i<bindings.parts;++i){PreparedPart next;const auto e=parameters(bindings,input,i,next,lighting);
        if(e!=Error::none){if(lighting)lighting->rollback(mark);return e;}output[i]=next;}
    return Error::none;
}
}
