#include "game_materials.hpp"
#include "texture_sequence_hooks.hpp"
#include "perf_switch.hpp"
#include "ModelBase.h"
#include <array>
#include <cstring>

namespace sm64ds::render::game_hooks {
namespace {
// BUILD134 (exact cuts), same materials as the BUILD 133 body below:
//  - each material is built in its scratch slot (capture_material_scratch)
//    instead of in a temporary copied over a slot that was cleared first;
//    only the slots no binding names are cleared, after the others;
//  - the model's image state is checked once, not again for every material
//    (selected_image_checked): nothing can change it inside this call;
//  - the image entry found here is left in LiveMaterial::image for the part
//    stage, which otherwise repeats the search for every part.
// A refusal returns the same error for the same material; the scratch of a
// refused capture is discarded by every caller, as documented.
Error capture_materials_cut(const ModelBindings& bindings,const ModelComponents& model,
                            const std::int16_t* table,std::size_t pairs,LiveMaterial* output,std::uint8_t cutoff) noexcept {
    std::array<std::uint64_t,ModelBank::max_materials/64> visited{};
    const bool lenient=game::lenient_materials();
    for(unsigned i=0;i<bindings.materials;++i){
        const auto& association=bindings.material[i];const unsigned id=association.source_material;
        if(id>=bindings.source_materials)return Error::invalid_data;
        const auto mask=std::uint64_t{1}<<(id%64);
        if(visited[id/64]&mask)continue; // colour roles share one live material
        visited[id/64]|=mask;
        game::RuntimeMaterial source;
        std::memcpy(&source,reinterpret_cast<const char*>(model.materials)+id*sizeof(source),sizeof(source));
        const bool visible=!(source.polygon&0x80000000u)&&((source.polygon>>6)&3);
        game::SelectedImage selected;game::Extent uv_texture{0,0};
        std::int16_t sine=0,cosine=4096;std::uint16_t entry=LiveMaterial::no_image;
        if(visible){
            ImageIdentity identity;if(!selected_image_checked(model,id,identity))return Error::invalid_data;
            selected.texture=identity.texture;selected.palette=identity.palette;
            if(identity.texture!=ModelBank::no_texture){
                bool found=false;
                for(unsigned image=0;image<bindings.images;++image){const auto& b=bindings.image[image];
                    if(b.use!=ImageUse::model)continue;
                    if(b.texture!=identity.texture||b.palette!=identity.palette)continue;
                    if(b.image>=bindings.images)return Error::invalid_data;
                    selected.sampled={b.width,b.height};entry=std::uint16_t(image);found=true;break;
                }
                if(!found&&lenient){
                    // Playable approximation: same texture with any prepared palette,
                    // else the first model image (wrong animation frame/palette).
                    for(unsigned image=0;image<bindings.images&&!found;++image){const auto& b=bindings.image[image];
                        if(b.use==ImageUse::model&&b.texture==identity.texture){selected.texture=b.texture;selected.palette=b.palette;selected.sampled={b.width,b.height};entry=std::uint16_t(image);found=true;}}
                    for(unsigned image=0;image<bindings.images&&!found;++image){const auto& b=bindings.image[image];
                        if(b.use==ImageUse::model){selected.texture=b.texture;selected.palette=b.palette;selected.sampled={b.width,b.height};entry=std::uint16_t(image);found=true;}}
                    if(!found)selected.texture=ModelBank::no_texture;
                    found=true;
                }
                if(!found)return Error::unsupported; // animation pair not prepared
                if((source.texture_flags>>30)==1){
                    if(!model.modelFile->textures||source.texture>=model.modelFile->numTextures||!table||pairs!=4096)
                        return Error::invalid_data;
                    const auto dimensions=model.modelFile->textures[source.texture].unk_0c;
                    uv_texture={dimensions&65535u,dimensions>>16};
                    const unsigned angle=source.angle&4095u;sine=table[angle*2];cosine=table[angle*2+1];
                }
            }
        }
        const auto error=game::capture_material_scratch(source,selected,{association.width,association.height},
            uv_texture,sine,cosine,output[id],cutoff);
        if(error!=Error::none)return error;
        output[id].image=entry;
    }
    for(unsigned i=0;i<bindings.source_materials;++i)
        if(!(visited[i/64]&(std::uint64_t{1}<<(i%64)))){output[i]={};output[i].visible=false;}
    return Error::none;
}
}
Error capture_materials(const ModelBindings& bindings,const ModelComponents& model,
                        const std::int16_t* table,std::size_t pairs,LiveMaterial* output,std::size_t capacity,
                        std::uint8_t cutoff) noexcept {
    if(!bindings.material||!bindings.materials||!model.modelFile||!model.materials||!output||
       !bindings.source_materials||bindings.source_materials>ModelBank::max_materials||
       bindings.materials>ModelBank::max_materials||bindings.images>ModelBank::max_images||
       (bindings.images&&!bindings.image)||model.modelFile->numMaterials!=bindings.source_materials||
       capacity<bindings.source_materials||!image_state_valid(model))return Error::invalid_data;
    if(sm64ds::perf::cuts())return capture_materials_cut(bindings,model,table,pairs,output,cutoff);
    for(unsigned i=0;i<bindings.source_materials;++i){output[i]={};output[i].visible=false;}
    std::array<std::uint64_t,ModelBank::max_materials/64> visited{};
    for(unsigned i=0;i<bindings.materials;++i){
        const auto& association=bindings.material[i];const unsigned id=association.source_material;
        if(id>=bindings.source_materials)return Error::invalid_data;
        const auto mask=std::uint64_t{1}<<(id%64);
        if(visited[id/64]&mask)continue; // colour roles share one live material
        visited[id/64]|=mask;
        game::RuntimeMaterial source;
        std::memcpy(&source,reinterpret_cast<const char*>(model.materials)+id*sizeof(source),sizeof(source));
        const bool visible=!(source.polygon&0x80000000u)&&((source.polygon>>6)&3);
        game::SelectedImage selected;game::Extent uv_texture{0,0};
        std::int16_t sine=0,cosine=4096;
        if(visible){
            ImageIdentity identity;if(!selected_image(model,id,identity))return Error::invalid_data;
            selected.texture=identity.texture;selected.palette=identity.palette;
            if(identity.texture!=ModelBank::no_texture){
                bool found=false;
                for(unsigned image=0;image<bindings.images;++image){const auto& b=bindings.image[image];
                    if(b.use!=ImageUse::model)continue;
                    if(b.texture!=identity.texture||b.palette!=identity.palette)continue;
                    if(b.image>=bindings.images)return Error::invalid_data;
                    selected.sampled={b.width,b.height};found=true;break;
                }
                if(!found&&game::lenient_materials()){
                    // Playable approximation: same texture with any prepared palette,
                    // else the first model image (wrong animation frame/palette).
                    for(unsigned image=0;image<bindings.images&&!found;++image){const auto& b=bindings.image[image];
                        if(b.use==ImageUse::model&&b.texture==identity.texture){selected.texture=b.texture;selected.palette=b.palette;selected.sampled={b.width,b.height};found=true;}}
                    for(unsigned image=0;image<bindings.images&&!found;++image){const auto& b=bindings.image[image];
                        if(b.use==ImageUse::model){selected.texture=b.texture;selected.palette=b.palette;selected.sampled={b.width,b.height};found=true;}}
                    if(!found)selected.texture=ModelBank::no_texture;
                    found=true;
                }
                if(!found)return Error::unsupported; // animation pair not prepared
                if((source.texture_flags>>30)==1){
                    if(!model.modelFile->textures||source.texture>=model.modelFile->numTextures||!table||pairs!=4096)
                        return Error::invalid_data;
                    // The original uses the stored 16-bit dimensions at +0xc,
                    // and the LOW twelve angle bits, not angle >> 4.
                    const auto dimensions=model.modelFile->textures[source.texture].unk_0c;
                    uv_texture={dimensions&65535u,dimensions>>16};
                    const unsigned angle=source.angle&4095u;sine=table[angle*2];cosine=table[angle*2+1];
                }
            }
        }
        const auto error=game::capture_material(source,selected,{association.width,association.height},
            uv_texture,sine,cosine,output[id],cutoff);
        if(error!=Error::none)return error;
    }
    return Error::none;
}
}
