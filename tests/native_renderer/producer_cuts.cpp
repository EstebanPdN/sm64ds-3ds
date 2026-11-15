// BUILD134 producer cost cuts: what the game-thread producers publish must be,
// bit for bit, what BUILD 133 published. Three things are compared over
// randomized definitions and live states:
//   - the cuts (perf::set_cuts(1));
//   - the BUILD 133 paths kept in the library behind the console switch
//     (perf::set_cuts(0));
//   - reference copies of the BUILD 133 routines held in this file.
// The same source is built for the host (ctest native_producer_cuts) and, with
// the console compiler and flags, for an ARM11 emulation
// (tools/test_native_producer_cuts_arm.py), where it also counts instructions.
// Definitions are immutable for the producers' kept marks: this test never
// rewrites one in place without the documented forget calls.
#include "scene_producer.hpp"
#include "game_materials.hpp"
#include "game_lighting.hpp"
#include "texture_sequence_hooks.hpp"
#include "perf_switch.hpp"
#include "ModelBase.h"
#include "TextureSequence.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef SM64DS_PRODUCER_CUTS_CATALOG
#include SM64DS_PRODUCER_CUTS_CATALOG
#endif
// The console's VFPv2 has no fused multiply-add: the references are compiled
// the way the console compiles them.
#if !defined(__3DS__)&&defined(__clang__)
#pragma clang fp contract(off)
#elif !defined(__3DS__)&&defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
namespace perf=sm64ds::perf;

// The four seams of the hook sources this test compiles on its own.
namespace sm64ds::render::game_hooks {
bool tracking_enabled() noexcept {return true;}
bool native_active() noexcept {return true;}
bool tracks_model(const ::ModelComponents&) noexcept {return true;}
void report_image_failure() noexcept {}
}
extern "C" {
void __real_func_020462d0(ModelComponents*,BMD_File*,void*){}
void __real_func_02046bbc(ModelComponents*,BTP_File*,unsigned){}
void __real_func_ov007_020c232c(void*){}
void __wrap_func_02046bbc(ModelComponents*,BTP_File*,unsigned);
void __wrap_func_ov007_020c232c(void*);
#ifdef __3DS__
void __real_func_020555a4(void*){}
#ifndef SM64DS_PRODUCER_CUTS_CATALOG
unsigned long long svcGetSystemTick(void){static unsigned long long ticks;return ticks+=100;}
#endif
#endif
}
namespace {
// What the comparisons covered (host report).
struct Coverage {
    std::uint64_t poses=0,pose_refusals=0,marked=0,mark_refusals=0,part_models=0,part_refusals=0,parts=0,lit_parts=0;
    std::uint64_t material_models=0,material_refusals=0,materials=0,sequences=0,poisoned=0;
    std::uint64_t updates=0,models=0,model_refusals=0,frame_parts=0,skin_pieces=0,skin_vertices=0,repeated_poses=0;
} coverage;
#define CHECK(condition) do{if(!(condition))return __LINE__;}while(0)
#define RUN(call) do{const unsigned line_=(call);if(line_)return line_;}while(0)
std::uint32_t seed=0x2468ace1u;
unsigned effort=8; // the host runs every comparison eight times as long as the emulation
std::uint32_t rnd() noexcept {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
unsigned pick(unsigned n) noexcept {return rnd()%n;}
bool chance(unsigned percent) noexcept {return rnd()%100u<percent;}
int signed_in(int limit) noexcept {return int(rnd()%unsigned(2*limit+1))-limit;}
float bits_float(std::uint32_t bits) noexcept {float value;std::memcpy(&value,&bits,4);return value;}
// Finite floats of every kind a matrix or UV row can hold.
float any_float() noexcept {
    switch(pick(10)){
    case 0:return 0.f;
    case 1:return -0.f;
    case 2:return float(signed_in(8));
    case 3:return float(signed_in(4096))/4096.f;
    case 4:return float(signed_in(1<<20))*1e3f;
    case 5:return chance(2)?float(signed_in(3))*3e37f:float(signed_in(1<<20))*1e-9f; // a few sums leave the finite range
    default:{std::uint32_t bits=rnd();const unsigned exponent=100+pick(56); // 2^-27 .. 2^28
        bits=(bits&0x807fffffu)|(exponent<<23);return bits_float(bits);}
    }
}
unsigned power_of_two(unsigned low,unsigned high) noexcept {return 1u<<(low+pick(high-low+1));} // 2^low..2^high

// ---------------------------------------------------------------- references
// BUILD 133 routines, copied unchanged (only names of file-local helpers).
namespace reference {
Matrix projected(const game::Affine& affine,const Matrix& projection) noexcept {
    Matrix result{};
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column){
        float value=column==3?projection.row[row][3]:0;
        for(unsigned k=0;k<3;++k)value+=projection.row[row][k]*(float(affine.value[column*3+k])/4096.f);
        result.row[row][column]=value;
    }
    return result;
}
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
        for(unsigned i=0;i<bindings.images;++i){
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
// capture_prepared(..., CaptureOutput::unpublished)
Error capture_prepared(const ModelBindings& bindings,const InstanceInputs& input,PreparedPart* output,std::size_t capacity,LightingPalette* lighting) noexcept {
    if(!inputs_valid(bindings,input,output,capacity))return Error::invalid_data;
    const auto mark=lighting?lighting->mark():LightingPalette::Mark{};
    for(unsigned i=0;i<bindings.parts;++i){PreparedPart next;const auto e=parameters(bindings,input,i,next,lighting);
        if(e!=Error::none){if(lighting)lighting->rollback(mark);return e;}output[i]=next;}
    return Error::none;
}
bool carry_fog=false; // mirrors game::set_material_fog
std::int32_t word(std::uint32_t bits) noexcept {std::int32_t result;std::memcpy(&result,&bits,4);return result;}
std::uint32_t product_shift(std::int32_t a,std::int32_t b,unsigned shift) noexcept {
    return std::uint32_t(std::uint64_t(std::int64_t(a)*b)>>shift);
}
std::uint32_t rgb(std::uint32_t packed) noexcept {
    const auto channel=[packed](unsigned shift){const unsigned v=(packed>>shift)&31;return (v<<3)|(v>>2);};
    return (channel(0)<<24)|(channel(5)<<16)|(channel(10)<<8)|255;
}
Error capture_material(const game::RuntimeMaterial& source,const game::SelectedImage& selected,game::Extent basis,game::Extent uv_texture,
                       std::int16_t sine,std::int16_t cosine,LiveMaterial& output,std::uint8_t cutoff) noexcept {
    const bool lenient=game::lenient_materials();
    LiveMaterial next;
    const auto polygon=source.polygon;const unsigned faces=(polygon>>6)&3;
    if((polygon&0x80000000u)||!faces){next.visible=false;output=next;return Error::none;}
    const unsigned alpha=(polygon>>16)&31;
    if(lenient){
        const unsigned mode=(polygon>>4)&3;
        if(!alpha||mode==3){next.visible=false;output=next;return Error::none;} // wireframe / shadow volume
    }else if(!alpha || ((polygon>>4)&3)!=0 || (polygon&0xc000u))return Error::unsupported;
    next.state.cull=faces==3?Cull::none:faces==2?Cull::back:Cull::front;
    next.state.blend=alpha==31?Blend::opaque:Blend::alpha;
    next.state.fog=lenient&&carry_fog&&(polygon&0x8000u);
    next.state.depth_write=alpha==31 || bool(polygon&(1u<<11));next.state.alpha_cutoff=cutoff;
    if(lenient&&alpha<31)next.state.depth_write=true;
    next.state.tint=0xffffff00u|(alpha*255/31);
    next.diffuse=rgb(source.diffuse_ambient);next.emission=rgb(source.specular_emission>>16);
    next.lighting={std::uint16_t(source.diffuse_ambient&32767u),std::uint16_t((source.diffuse_ambient>>16)&32767u),
        std::uint16_t(source.specular_emission&32767u),std::uint16_t((source.specular_emission>>16)&32767u),
        std::uint8_t(polygon&15),bool(source.specular_emission&0x8000u)};
    next.diffuse_available=bool(source.diffuse_ambient&0x8000);next.emission_available=(polygon&15)==0;
    next.texture=selected.texture;next.palette=selected.palette;
    if(selected.texture!=ModelBank::no_texture){
        if(selected.texture>65535 || (selected.palette!=ModelBank::no_texture&&selected.palette>65535) ||
           !texture_dimensions(basis.width,basis.height)||!texture_dimensions(selected.sampled.width,selected.sampled.height))return Error::invalid_data;
        if(!selected.binary_alpha&&!lenient)return Error::unsupported;
        if(lenient)next.state.blend=Blend::alpha;
        next.state.wrap_s=(source.texture_flags&(1u<<16))?((source.texture_flags&(1u<<18))?Wrap::mirror:Wrap::repeat):Wrap::clamp;
        next.state.wrap_t=(source.texture_flags&(1u<<17))?((source.texture_flags&(1u<<19))?Wrap::mirror:Wrap::repeat):Wrap::clamp;
        unsigned mode=source.texture_flags>>30;
        if(mode>2){if(!lenient)return Error::unsupported;mode=0;}
        if(mode==0){
            const float x=float(basis.width)/selected.sampled.width,y=float(basis.height)/selected.sampled.height;
            next.state.uv={{x,0,0,0,y,0}};
        }else if(mode==2){
            next.state.uv_mode=UvMode::normal;
            next.normal_half_width=4u<<((source.texture_flags>>20)&7);
            next.normal_half_height=4u<<((source.texture_flags>>23)&7);
            next.state.normal_uv().width=basis.width;next.state.normal_uv().height=basis.height;
            next.state.normal_uv().offset={{std::int16_t(std::uint32_t(source.translate_x)>>8),std::int16_t(std::uint32_t(source.translate_y)>>8)}};
        }else{
            if(!texture_dimensions(uv_texture.width,uv_texture.height))return Error::invalid_data;
            const auto C=product_shift(source.scale_x,sine,12),B=product_shift(source.scale_x,cosine,12);
            const auto A=product_shift(source.scale_y,sine,12),D=product_shift(source.scale_y,cosine,12);
            const auto w=uv_texture.width,h=uv_texture.height;
            const auto D0=word(A*(0u-h))/std::int32_t(w),D1=word(C*w)/std::int32_t(h);
            const auto sx=std::uint32_t(source.scale_x),sy=std::uint32_t(source.scale_y);
            const auto ts=w*(sx-(C+B))*8-w*product_shift(source.scale_x,source.translate_x,8);
            const auto tt=h*((A-D)-sy+8192u)*8+h*product_shift(source.scale_y,source.translate_y,8);
            next.state.uv_mode=UvMode::fixed;
            next.state.fixed_uv={{word(B),D1,word(ts),D0,word(D),word(tt)},std::uint16_t(basis.width),std::uint16_t(basis.height)};
        }
    }
    output=next;return Error::none;
}
// What the texture-sequence hooks hold for one model, as this test fed them:
// the reference does not read their table.
struct Sequences {
    bool poisoned=false;unsigned count=0;
    struct Choice {unsigned material,texture,palette;} choice[8];
    bool selected(const ModelComponents& model,unsigned material,hooks::ImageIdentity& output) const noexcept {
        if(poisoned)return false;
        for(unsigned i=0;i<count;++i)if(choice[i].material==material){output={choice[i].texture,choice[i].palette};return true;}
        game::RuntimeMaterial source;
        std::memcpy(&source,reinterpret_cast<const char*>(model.materials)+material*sizeof(source),sizeof(source));
        output={source.texture,source.palette};return true;
    }
};
Error capture_materials(const ModelBindings& bindings,const ModelComponents& model,
                        const std::int16_t* table,std::size_t pairs,LiveMaterial* output,std::size_t capacity,
                        std::uint8_t cutoff,const Sequences& sequences) noexcept {
    if(!bindings.material||!bindings.materials||!model.modelFile||!model.materials||!output||
       !bindings.source_materials||bindings.source_materials>ModelBank::max_materials||
       bindings.materials>ModelBank::max_materials||bindings.images>ModelBank::max_images||
       (bindings.images&&!bindings.image)||model.modelFile->numMaterials!=bindings.source_materials||
       capacity<bindings.source_materials||sequences.poisoned)return Error::invalid_data;
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
            hooks::ImageIdentity identity;if(!sequences.selected(model,id,identity))return Error::invalid_data;
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
                    const auto dimensions=model.modelFile->textures[source.texture].unk_0c;
                    uv_texture={dimensions&65535u,dimensions>>16};
                    const unsigned angle=source.angle&4095u;sine=table[angle*2];cosine=table[angle*2+1];
                }
            }
        }
        const auto error=reference::capture_material(source,selected,{association.width,association.height},
            uv_texture,sine,cosine,output[id],cutoff);
        if(error!=Error::none)return error;
    }
    return Error::none;
}
// Skin: the transform of SceneProducer::append_skin.
struct Mat {float m[12];};
Mat load(const void* transforms,unsigned bone) noexcept {
    std::int32_t raw[12];std::memcpy(raw,static_cast<const char*>(transforms)+bone*48,48);
    Mat out;for(unsigned i=0;i<12;++i)out.m[i]=float(raw[i])/4096.f;return out;
}
void apply(const Mat& a,const float* v,float w,float* out) noexcept {
    for(unsigned k=0;k<3;++k)out[k]=a.m[k]*v[0]+a.m[3+k]*v[1]+a.m[6+k]*v[2]+(w?a.m[9+k]:0.f);
}
bool inverse(const Mat& a,Mat& out) noexcept {
    const float* c=a.m;
    const float e00=c[0],e10=c[1],e20=c[2],e01=c[3],e11=c[4],e21=c[5],e02=c[6],e12=c[7],e22=c[8];
    const float det=e00*(e11*e22-e12*e21)-e01*(e10*e22-e12*e20)+e02*(e10*e21-e11*e20);
    if(!(det>1e-12f||det<-1e-12f))return false;
    const float s=1.f/det;
    float i[9]={(e11*e22-e12*e21)*s,(e12*e20-e10*e22)*s,(e10*e21-e11*e20)*s,
                (e02*e21-e01*e22)*s,(e00*e22-e02*e20)*s,(e01*e20-e00*e21)*s,
                (e01*e12-e02*e11)*s,(e02*e10-e00*e12)*s,(e00*e11-e01*e10)*s};
    for(unsigned n=0;n<9;++n)out.m[n]=i[n];
    for(unsigned k=0;k<3;++k)out.m[9+k]=-(i[k]*c[9]+i[3+k]*c[10]+i[6+k]*c[11]);
    return true;
}
struct SkinOccurrence {std::uint32_t part,first,count,piece;};
// Vertices and occurrences append_skin publishes for one model whose parts
// were captured as `parts`. Returns false when storage would not hold them.
bool skin(const ModelBindings& bindings,const void* transforms,const PreparedPart* parts,std::uint32_t first_part,
          NormalVertex* storage,std::size_t capacity,std::size_t& used,SkinOccurrence* occurrence,std::size_t& occurrences) noexcept {
    static Mat pose_value[ModelBank::max_matrices],inverse_value[ModelBank::max_matrices];
    static std::uint8_t state[ModelBank::max_matrices];
    std::memset(state,0,sizeof(state));
    const auto pose=[&](unsigned bone)->const Mat& {
        if(!(state[bone]&1u)){pose_value[bone]=load(transforms,bone);state[bone]|=1u;}
        return pose_value[bone];
    };
    const auto invert=[&](unsigned bone) {
        if(!(state[bone]&6u))state[bone]|=inverse(pose(bone),inverse_value[bone])?2u:4u;
        return bool(state[bone]&2u);
    };
    for(unsigned g=0;g<bindings.skins;++g){const auto& group=bindings.skin[g];
        if(group.part>=bindings.parts||group.count%3||!parts[group.part].visible)continue;
        const auto& binding=bindings.part[group.part];if(binding.billboard)continue;
        const unsigned target=binding.bone,normal_target=binding.lighting_bone!=0xffffu?binding.lighting_bone:target;
        if(target>=bindings.bones||normal_target>=bindings.bones)continue;
        if(!invert(target)||!invert(normal_target))continue;
        const auto& to_target=inverse_value[target];
        const auto& to_normal=inverse_value[normal_target];
        if(capacity-used<group.count)return false; // append_skin stops this model here
        auto* out=storage+used;
        for(std::uint32_t v=0;v<group.count;++v){const auto& s=bindings.skin_vertex[group.first+v];
            auto& o=out[v];const float p[3]={s.x,s.y,s.z};float world[3],local[3];
            if(s.bone==target){o.base.x=s.x;o.base.y=s.y;o.base.z=s.z;}
            else{const auto& bone=pose(s.bone<bindings.bones?s.bone:target);
                apply(bone,p,1,world);apply(to_target,world,1,local);o.base.x=local[0];o.base.y=local[1];o.base.z=local[2];}
            o.base.u=s.u;o.base.v=s.v;o.base.r=s.r;o.base.g=s.g;o.base.b=s.b;o.base.a=255;
            if(s.bone==normal_target){o.x=s.nx;o.y=s.ny;o.z=s.nz;}
            else{const float n[3]={float(s.nx),float(s.ny),float(s.nz)};
                const auto& bone=pose(s.bone<bindings.bones?s.bone:normal_target);
                apply(bone,n,0,world);apply(to_normal,world,0,local);
                const float len=std::sqrt(local[0]*local[0]+local[1]*local[1]+local[2]*local[2]);
                const float scale=len>1e-6f?std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2])/len:0.f;
                auto clamp10=[](float x){return std::int16_t(x>511.f?511:x<-512.f?-512:int(x+(x>=0?.5f:-.5f)));};
                o.x=clamp10(local[0]*scale);o.y=clamp10(local[1]*scale);o.z=clamp10(local[2]*scale);}
            o.uv_source=s.uv_source;
        }
        occurrence[occurrences++]={first_part+group.part,std::uint32_t(used),group.count,g};
        used+=group.count;
    }
    return true;
}
}

// ---------------------------------------------------------------- comparisons
bool same(const Material& a,const Material& b) noexcept {
    return a.texture==b.texture&&a.blend==b.blend&&a.cull==b.cull&&a.wrap_s==b.wrap_s&&a.wrap_t==b.wrap_t&&
        a.depth_test==b.depth_test&&a.depth_write==b.depth_write&&a.sample_alpha==b.sample_alpha&&a.alpha_cutoff==b.alpha_cutoff&&
        !std::memcmp(a.uv.data(),b.uv.data(),sizeof(a.uv))&&a.uv_mode==b.uv_mode&&a.fixed_uv.coefficient==b.fixed_uv.coefficient&&
        a.fixed_uv.width==b.fixed_uv.width&&a.fixed_uv.height==b.fixed_uv.height&&a.fixed_uv.offset==b.fixed_uv.offset&&
        a.tint==b.tint&&a.brightness==b.brightness&&a.fog==b.fog;
}
bool same(const PreparedPart& a,const PreparedPart& b) noexcept {
    return !std::memcmp(&a.object_to_clip,&b.object_to_clip,sizeof(Matrix))&&same(a.material,b.material)&&
        a.image==b.image&&a.visible==b.visible&&a.lighting==b.lighting&&a.highlight==b.highlight;
}
// Everything but the BUILD134 image note, which only the cut path fills.
bool same(const LiveMaterial& a,const LiveMaterial& b) noexcept {
    return same(a.state,b.state)&&a.texture==b.texture&&a.palette==b.palette&&a.diffuse==b.diffuse&&a.emission==b.emission&&
        a.visible==b.visible&&a.diffuse_available==b.diffuse_available&&a.emission_available==b.emission_available&&
        a.normal_half_width==b.normal_half_width&&a.normal_half_height==b.normal_half_height&&a.lighting==b.lighting;
}
bool same(const LightSet& a,const LightSet& b) noexcept {
    return a.light==b.light&&a.shininess==b.shininess&&a.toon==b.toon&&a.known==b.known&&
        a.table_known==b.table_known&&a.toon_known==b.toon_known;
}
bool same(const LightingPaletteView& a,const LightingPaletteView& b) noexcept {
    if(a.light_count!=b.light_count||a.state_count!=b.state_count)return false;
    for(std::size_t i=0;i<a.light_count;++i)if(!same(a.lights[i],b.lights[i]))return false;
    for(std::size_t i=0;i<a.state_count;++i)
        if(a.state[i].normal!=b.state[i].normal||!(a.state[i].material==b.state[i].material)||a.state[i].lights!=b.state[i].lights)return false;
    return true;
}
// A slot no capture wrote would show: valid values no producer makes.
PreparedPart unwritten_part(unsigned salt) noexcept {
    PreparedPart part;for(auto& row:part.object_to_clip.row)for(auto& value:row)value=7.f+float(salt);
    part.material.tint=0xdeadbe00u+salt;part.material.alpha_cutoff=std::uint8_t(200+salt);part.material.brightness=std::int8_t(9+salt);
    part.material.uv={{float(salt),5,5,5,5,5}};part.image=90+salt;part.visible=true;part.lighting=std::uint16_t(1234+salt);part.highlight=true;
    return part;
}
LiveMaterial unwritten_material(unsigned salt) noexcept {
    LiveMaterial live;live.state=unwritten_part(salt).material;live.texture=77+salt;live.palette=55+salt;live.diffuse=salt;live.emission=salt+1;
    live.normal_half_width=std::uint16_t(3+salt);live.lighting.diffuse=std::uint16_t(100+salt);live.image=std::uint16_t(40+salt);return live;
}

// ---------------------------------------------------------------- definitions
// Unique storage for every definition of a pass (the producers key their kept
// marks on definition addresses); refill() forgets those marks first.
constexpr unsigned pool_parts=4096,pool_materials=2048,pool_images=1024,pool_groups=1024,pool_vertices=32768,pool_definitions=512;
PartBinding part_pool[pool_parts];MaterialBinding material_pool[pool_materials];ImageBinding image_pool[pool_images];
SkinGroup group_pool[pool_groups];SkinVertex vertex_pool[pool_vertices];ModelBindings definition_pool[pool_definitions];
unsigned used_parts=0,used_materials=0,used_images=0,used_groups=0,used_vertices=0,used_definitions=0;
void refill() noexcept {
    used_parts=used_materials=used_images=used_groups=used_vertices=used_definitions=0;
    game::forget_skin_bones();hooks::forget_skin_reuse();
}
bool room(unsigned parts,unsigned materials,unsigned images,unsigned groups,unsigned vertices) noexcept {
    return used_parts+parts<=pool_parts&&used_materials+materials<=pool_materials&&used_images+images<=pool_images&&
        used_groups+groups<=pool_groups&&used_vertices+vertices<=pool_vertices&&used_definitions<pool_definitions;
}
struct Shape {
    unsigned bones=1,source_materials=1,materials=1,images=1,parts=1,groups=0,group_vertices=0;
    bool wild=false;     // out-of-range indices, repeated image identities, odd group sizes
    bool billboards=true,lit=true;
    unsigned normal_uv=12; // percent of material bindings that take their UV from normals
};
// A model definition. `wild` ones break the catalog's rules on purpose; the
// producers must refuse them exactly as before.
const ModelBindings& define(const Shape& shape) noexcept {
    if(!room(shape.parts,shape.materials,shape.images,shape.groups,shape.groups*shape.group_vertices))refill();
    auto* images=image_pool+used_images;auto* materials=material_pool+used_materials;auto* parts=part_pool+used_parts;
    auto* groups=group_pool+used_groups;auto* vertices=vertex_pool+used_vertices;
    for(unsigned i=0;i<shape.images;++i){
        images[i]={pick(6),chance(15)?ModelBank::no_texture:pick(4),i,std::uint16_t(power_of_two(3,7)),std::uint16_t(power_of_two(3,7)),
            chance(shape.wild?25:5)?ImageUse::sprite:ImageUse::model};
        if(!shape.wild){images[i].texture=i/2;images[i].palette=i%2;images[i].use=ImageUse::model;} // identities are unique in a catalog
        else if(chance(8))images[i].image=shape.images+pick(3);
    }
    for(unsigned i=0;i<shape.materials;++i){
        auto& m=materials[i];m={};m.source_material=std::uint16_t(i<shape.source_materials&&chance(85)?i:pick(shape.source_materials));
        if(shape.wild&&chance(4))m.source_material=std::uint16_t(shape.source_materials+pick(3));
        m.width=std::uint16_t(power_of_two(3,7));m.height=std::uint16_t(power_of_two(3,7));
        m.colour=shape.lit&&chance(60)?ColourSource::lighting:static_cast<ColourSource>(pick(3));
        if(shape.wild&&chance(2))m.colour=static_cast<ColourSource>(7);
        m.uv_transform=chance(30);m.normal_uv=chance(shape.normal_uv);
        m.diffuse_override=m.colour==ColourSource::lighting&&chance(25);m.diffuse_ambient=rnd();
    }
    for(unsigned i=0;i<shape.parts;++i){
        auto& p=parts[i];p.bone=std::uint16_t(pick(shape.bones));p.material=std::uint16_t(pick(shape.materials));
        p.billboard=shape.billboards&&chance(10);p.normal_bone=std::uint16_t(pick(shape.bones));
        p.lighting_bone=chance(70)?p.bone:chance(80)?std::uint16_t(pick(shape.bones)):std::uint16_t(0xffffu);
        // A catalog gives every lit part its lighting bone.
        if(!shape.wild&&p.lighting_bone==0xffffu&&materials[p.material].colour==ColourSource::lighting)p.lighting_bone=p.bone;
        if(shape.wild){
            if(chance(2))p.bone=std::uint16_t(shape.bones+pick(2));
            if(chance(2))p.material=std::uint16_t(shape.materials+pick(2));
            if(chance(3))p.lighting_bone=std::uint16_t(shape.bones+pick(4));
            if(chance(3))p.normal_bone=std::uint16_t(shape.bones+pick(4));
        }
    }
    unsigned vertex=0;
    for(unsigned g=0;g<shape.groups;++g){
        auto& group=groups[g];group.part=std::uint16_t(pick(shape.parts));group.first=vertex;group.count=shape.group_vertices;
        if(chance(20))group.count=3*(1+pick(shape.group_vertices/3));
        if(shape.wild){if(chance(6))group.part=std::uint16_t(shape.parts+pick(2));if(chance(6)&&group.count>3)group.count-=1;}
        // A few positions and normals on a few bones, as triangles sharing vertices do.
        const unsigned target=parts[group.part<shape.parts?group.part:0].bone;
        SkinVertex corner[6];
        for(auto& c:corner){c={};c.x=float(signed_in(4096))/256.f;c.y=float(signed_in(4096))/256.f;c.z=chance(10)?-0.f:float(signed_in(4096))/256.f;
            c.bone=std::uint16_t(chance(45)&&target<shape.bones?target:pick(shape.bones));
            if(shape.wild&&chance(1))c.bone=std::uint16_t(shape.bones+pick(2));
            c.nx=std::int16_t(signed_in(511));c.ny=std::int16_t(signed_in(511));c.nz=std::int16_t(chance(15)?0:signed_in(511));
            if(chance(5))c.nx=c.ny=c.nz=0;}
        for(unsigned v=0;v<group.count;++v){auto& s=vertices[vertex+v];s=corner[pick(chance(70)?4:6)];
            // Twins in all but one transform input.
            if(chance(12)){const auto value=std::int16_t(signed_in(511));const unsigned axis=pick(3);(axis==0?s.nx:axis==1?s.ny:s.nz)=value;}
            if(chance(8)){const float value=chance(20)?-s.x:float(signed_in(64));const unsigned axis=pick(3);(axis==0?s.x:axis==1?s.y:s.z)=value;}
            if(chance(6))s.bone=std::uint16_t(pick(shape.bones));
            s.u=float(signed_in(1024))/64.f;s.v=float(signed_in(1024))/64.f;s.r=std::uint8_t(rnd());s.g=std::uint8_t(rnd());s.b=std::uint8_t(rnd());
            s.uv_source=std::int16_t(pick(3));}
        vertex+=shape.group_vertices;
    }
    auto& d=definition_pool[used_definitions++];
    d={300u+pick(60000),{},shape.bones,shape.source_materials,shape.images?images:nullptr,shape.images,materials,shape.materials,parts,shape.parts,
        shape.groups?groups:nullptr,shape.groups,shape.groups?vertices:nullptr};
    used_images+=shape.images;used_materials+=shape.materials;used_parts+=shape.parts;used_groups+=shape.groups;used_vertices+=vertex;
    return d;
}
Shape any_shape(bool wild) noexcept {
    Shape s;s.wild=wild;s.bones=1+pick(24);s.source_materials=1+pick(10);s.materials=1+pick(s.source_materials+3);s.images=pick(9);
    s.parts=1+pick(chance(30)?90:14);s.groups=chance(60)?pick(7):0;s.group_vertices=3*(1+pick(20));return s;
}
game::Affine any_bone(bool regular) noexcept {
    game::Affine bone{};
    for(unsigned i=0;i<9;++i)bone.value[i]=signed_in(regular?8192:1<<22);
    if(regular)for(unsigned i=0;i<3;++i)bone.value[i*4]+=bone.value[i*4]<0?-4096:4096; // away from singular
    for(unsigned i=9;i<12;++i)bone.value[i]=signed_in(regular?1<<18:1<<26);
    return bone;
}
Matrix any_matrix() noexcept {Matrix m;for(auto& row:m.row)for(auto& value:row)value=any_float();return m;}

// ---------------------------------------------------------------- poses
unsigned check_projection() noexcept {
    static const PartBinding parts[]={{0,0,false},{0,0,true}};
    static const ModelBindings ordinary_only{261,{},1,1,nullptr,0,nullptr,0,parts,1},both{262,{},1,1,nullptr,0,nullptr,0,parts,2};
    for(unsigned trial=0;trial<5000*effort;++trial){
        game::Affine bone{};
        for(auto& value:bone.value)value=chance(5)?int(rnd()):signed_in(chance(50)?1<<14:1<<24);
        const Matrix projection=any_matrix();
        Matrix cut=identity(),old=identity(),facing_cut=identity(),facing_old=identity();
        perf::set_cuts(1);const auto e1=game::build_poses(ordinary_only,&bone,1,nullptr,nullptr,projection,&cut,nullptr,1);
        perf::set_cuts(0);const auto e0=game::build_poses(ordinary_only,&bone,1,nullptr,nullptr,projection,&old,nullptr,1);
        const Matrix expected=reference::projected(bone,projection);
        CHECK(e1==e0&&e1==(finite(expected)?Error::none:Error::invalid_data));
        ++coverage.poses;coverage.pose_refusals+=e1!=Error::none;
        // Compared even when refused: the same bits reached the same test.
        CHECK(!std::memcmp(&cut,&expected,sizeof(Matrix))&&!std::memcmp(&old,&expected,sizeof(Matrix)));
        // The composed and the billboard pose, through a camera and a scale.
        const game::Affine camera=any_bone(true);const std::int32_t size[3]={signed_in(16384),signed_in(16384),signed_in(16384)};
        const game::Affine* view=chance(80)?&camera:nullptr;const std::int32_t* scale=chance(50)?size:nullptr;
        cut=old=facing_cut=facing_old=identity();
        perf::set_cuts(1);const auto f1=game::build_poses(both,&bone,1,view,scale,projection,&cut,&facing_cut,1);
        perf::set_cuts(0);const auto f0=game::build_poses(both,&bone,1,view,scale,projection,&old,&facing_old,1);
        CHECK(f1==f0&&!std::memcmp(&cut,&old,sizeof(Matrix))&&!std::memcmp(&facing_cut,&facing_old,sizeof(Matrix)));
    }
    return 0;
}
// The bones a definition's skin vertices name: kept marks against a new scan.
unsigned check_skin_marks() noexcept {
    static Matrix out[3][2][64];static game::Affine bones[64];
    static const ModelBindings* seen[96];unsigned seen_count=0,pass=used_definitions;
    for(unsigned trial=0;trial<500*effort;++trial){
        const ModelBindings* definition;
        if(used_definitions<pass){seen_count=0;} // the pools were refilled: earlier definitions are gone
        pass=used_definitions;
        if(seen_count&&chance(50))definition=seen[pick(seen_count)]; // again, long after other models
        else{
            Shape shape=any_shape(chance(30));shape.groups=1+pick(6);shape.parts=1+pick(12);
            const unsigned before=used_definitions;definition=&define(shape);
            if(used_definitions<=before)seen_count=0;
            seen[seen_count<96?seen_count++:pick(96)]=definition;pass=used_definitions;
        }
        for(auto& bone:bones)bone=any_bone(true);
        const Matrix projection=any_matrix();const game::Affine camera=any_bone(true);const bool view=chance(50);
        Error error[3];
        for(unsigned mode=0;mode<3;++mode){
            for(auto& kind:out[mode])for(auto& matrix:kind)matrix=identity();
            perf::set_cuts(mode!=2);
            error[mode]=game::build_poses(*definition,bones,64,view?&camera:nullptr,nullptr,projection,out[mode][0],out[mode][1],64);
        }
        // Mode 0 fills or finds the kept marks, mode 1 finds them, mode 2 scans as BUILD 133.
        CHECK(error[0]==error[1]&&error[0]==error[2]);
        CHECK(!std::memcmp(out[0],out[1],sizeof(out[0]))&&!std::memcmp(out[0],out[2],sizeof(out[0])));
        ++coverage.marked;coverage.mark_refusals+=error[0]!=Error::none;
    }
    return 0;
}

// ---------------------------------------------------------------- parts
std::uint16_t first_image(const ModelBindings& d,std::uint32_t texture,std::uint32_t palette) noexcept {
    for(unsigned i=0;i<d.images;++i)
        if(d.image[i].use==ImageUse::model&&d.image[i].texture==texture&&d.image[i].palette==palette)return std::uint16_t(i);
    return LiveMaterial::no_image;
}
// `wild` states break rules at random; the others are what the material
// stage produces for this definition (so most captures are admitted).
LiveMaterial any_live(const ModelBindings& d,unsigned source,bool wild) noexcept {
    LiveMaterial m;m.visible=!chance(12);auto& s=m.state;
    s.blend=static_cast<Blend>(pick(4));s.cull=static_cast<Cull>(pick(3));s.wrap_s=static_cast<Wrap>(pick(3));s.wrap_t=chance(10)?Wrap::transparent_border:static_cast<Wrap>(pick(3));
    s.depth_test=chance(80);s.depth_write=chance(80);s.sample_alpha=chance(80);s.alpha_cutoff=std::uint8_t(rnd());
    s.tint=rnd();s.brightness=std::int8_t(signed_in(16));s.fog=chance(20);
    const unsigned mode=pick(100);
    for(auto& value:s.uv)value=any_float();
    if(mode>=85){s.uv_mode=UvMode::normal;}
    else if(mode>=70){s.uv_mode=UvMode::fixed;}
    for(auto& value:s.fixed_uv.coefficient)value=int(rnd());
    s.fixed_uv.width=std::uint16_t(power_of_two(3,10));s.fixed_uv.height=std::uint16_t(power_of_two(3,10));
    s.fixed_uv.offset={{std::int16_t(rnd()),std::int16_t(rnd())}};
    if(wild&&chance(3)){
        switch(pick(4)){
        case 0:s.blend=static_cast<Blend>(99);break;
        case 1:s.uv[pick(6)]=bits_float(0x7fc00000u);break; // not finite (only matters for affine)
        case 2:s.fixed_uv.width=24;break;
        default:s.brightness=40;break;}
    }
    if(d.images&&chance(80)){const auto& image=d.image[pick(d.images)];m.texture=image.texture;m.palette=image.palette;}
    else if(chance(50)){m.texture=pick(8);m.palette=chance(20)?ModelBank::no_texture:pick(5);}
    m.diffuse=rnd();m.emission=rnd();m.diffuse_available=!chance(6);m.emission_available=!chance(6);
    m.normal_half_width=std::uint16_t(power_of_two(2,9));m.normal_half_height=std::uint16_t(power_of_two(2,9));
    if(wild&&chance(4))m.normal_half_width=3;
    m.lighting.diffuse=std::uint16_t(rnd()&32767u);m.lighting.ambient=std::uint16_t(rnd()&32767u);
    m.lighting.specular=std::uint16_t(chance(40)?0:rnd()&32767u);m.lighting.emission=std::uint16_t(rnd()&32767u);
    if(wild&&chance(2))m.lighting.diffuse=40000;
    m.lighting.mask=std::uint8_t(chance(20)?0:pick(16));m.lighting.use_shininess=chance(30);
    m.lighting.toon=chance(4);m.lighting.grey=chance(4);
    // The material stage's note: absent, right, or nonsense (never a later
    // twin of the first entry: the note is defined as the first one).
    if(!wild){
        bool normal=false;
        for(unsigned i=0;i<d.materials;++i)if(d.material[i].source_material==source)normal|=d.material[i].normal_uv;
        m.diffuse_available=m.emission_available=true;
        if(d.images){const auto& image=d.image[pick(d.images)];m.texture=image.texture;m.palette=image.palette;}
        else m.texture=m.palette=ModelBank::no_texture;
        s.uv_mode=m.texture==ModelBank::no_texture?UvMode::affine:normal?UvMode::normal:chance(25)?UvMode::fixed:UvMode::affine;
        const bool table=m.lighting.specular&&m.lighting.use_shininess;
        m.lighting.toon=m.lighting.mask&&!table&&chance(6);m.lighting.grey=m.lighting.mask&&!table&&!m.lighting.toon&&chance(6);
    }
    const auto first=first_image(d,m.texture,m.palette);
    const unsigned note=pick(3);
    if(note==1)m.image=first;
    else if(note==2){m.image=std::uint16_t(pick(12));
        if(m.image<d.images&&m.image!=first&&d.image[m.image].use==ImageUse::model&&
           d.image[m.image].texture==m.texture&&d.image[m.image].palette==m.palette)m.image=LiveMaterial::no_image;}
    return m;
}
LightSet any_lights(bool wild) noexcept {
    LightSet set;set.known=std::uint8_t(!wild||chance(70)?15:pick(16));set.table_known=!wild||chance(70);set.toon_known=!wild||chance(50);set.generation=rnd();
    for(auto& light:set.light){
        for(auto& value:light.direction)value=std::int16_t(signed_in(1023));
        for(auto& value:light.colour)value=std::uint8_t(pick(32));
        light.specular_reciprocal=chance(20)?262144:signed_in(131072);
        if(wild&&chance(2))light.direction[0]=2000;
        if(wild&&chance(2))light.specular_reciprocal=200000;
    }
    if(chance(50))set.light[1]=set.light[0];
    for(auto& value:set.shininess)value=std::uint8_t(rnd());
    for(auto& value:set.toon)value=std::uint16_t(rnd());
    return set;
}
unsigned check_parts() noexcept {
    constexpr unsigned capacity=128;
    static PreparedPart out[3][capacity];static LiveMaterial live[16];
    static Matrix ordinary[32],billboard[32];static NormalBasis normal[32],light_ordinary[32],light_billboard[32];
    static LightSet light_storage[3][8];static PreparedLighting state_storage[3][96];
    for(unsigned frame=0;frame<750*effort;++frame){
        const std::size_t light_capacity=chance(8)?1+pick(2):8,state_capacity=chance(8)?1+pick(6):96;
        LightingPalette palette[3]={LightingPalette(light_storage[0],light_capacity,state_storage[0],state_capacity),
            LightingPalette(light_storage[1],light_capacity,state_storage[1],state_capacity),
            LightingPalette(light_storage[2],light_capacity,state_storage[2],state_capacity)};
        const bool no_palette=chance(3);
        for(unsigned models=1+pick(5);models;--models){
            const bool wild=chance(35);
            Shape shape=any_shape(wild);shape.groups=0;
            if(!wild){shape.normal_uv=chance(12)?100:0;if(shape.normal_uv){shape.billboards=false;shape.images+=!shape.images;}}
            const auto& d=define(shape);
            const LightSet lights=any_lights(wild);
            for(unsigned i=0;i<16;++i)live[i]=any_live(d,i,wild);
            for(auto& matrix:ordinary)matrix=any_matrix();
            for(auto& matrix:billboard)matrix=any_matrix();
            if(wild&&chance(10))ordinary[pick(d.bones)].row[pick(4)][pick(4)]=bits_float(chance(50)?0x7f800000u:0xffc00001u);
            if(wild&&chance(5))billboard[pick(d.bones)].row[pick(4)][pick(4)]=bits_float(0xff800000u);
            if(chance(wild?6:3)){const auto& part=d.part[pick(d.parts)]; // a pose some part does use, one facing only
                if(part.bone<d.bones)(chance(50)?billboard:ordinary)[part.bone].row[pick(4)][pick(4)]=bits_float(0x7fc00000u);}
            for(auto* bases:{normal,light_ordinary,light_billboard})for(unsigned i=0;i<32;++i)for(auto& value:bases[i].value)value=int(rnd());
            InstanceInputs input{ordinary,billboard,d.bones,live,d.source_materials,nullptr,0,normal,light_ordinary,light_billboard,&lights};
            if(wild){
                if(chance(4))input.poses=d.bones-1;
                if(chance(3))input.billboard=nullptr;
                if(chance(2))input.ordinary=nullptr;
                if(chance(3))input.normal=nullptr;
                if(chance(3))input.light_ordinary=nullptr;
                if(chance(2))input.light_billboard=nullptr;
                if(chance(2))input.lights=nullptr;
                if(chance(2)&&d.source_materials>1)input.materials=d.source_materials-1;
            }
            const std::size_t room_for=wild&&chance(2)?d.parts-1:capacity;
            Error error[3];
            for(unsigned mode=0;mode<3;++mode)for(unsigned i=0;i<capacity;++i)out[mode][i]=unwritten_part(mode);
            error[0]=reference::capture_prepared(d,input,out[0],room_for,no_palette?nullptr:&palette[0]);
            perf::set_cuts(1);error[1]=capture_prepared(d,input,out[1],room_for,no_palette?nullptr:&palette[1],CaptureOutput::unpublished);
            perf::set_cuts(0);error[2]=capture_prepared(d,input,out[2],room_for,no_palette?nullptr:&palette[2],CaptureOutput::unpublished);
            CHECK(error[0]==error[1]&&error[0]==error[2]);
            if(error[0]==Error::none)for(unsigned i=0;i<d.parts;++i){CHECK(same(out[0][i],out[1][i])&&same(out[0][i],out[2][i]));
                ++coverage.parts;coverage.lit_parts+=out[0][i].lighting!=no_lighting;}
            ++coverage.part_models;coverage.part_refusals+=error[0]!=Error::none;
            // Identifiers, their order and what a refusal rolled back.
            CHECK(same(palette[0].view(),palette[1].view())&&same(palette[0].view(),palette[2].view()));
        }
    }
    return 0;
}

// ---------------------------------------------------------------- materials
game::RuntimeMaterial any_runtime(const ModelBindings& d,bool tidy) noexcept {
    game::RuntimeMaterial m{};
    if(d.images&&chance(tidy?95:75)){const auto& image=d.image[pick(d.images)];m.texture=image.texture;m.palette=image.palette;}
    else{m.texture=chance(40)?ModelBank::no_texture:pick(9);m.palette=chance(30)?ModelBank::no_texture:pick(6);}
    m.scale_x=signed_in(1<<14);m.scale_y=signed_in(1<<14);m.angle=std::uint16_t(rnd());
    m.translate_x=signed_in(1<<20);m.translate_y=signed_in(1<<20);
    const unsigned roll=pick(100),mode=tidy?(roll<85?0u:1u):roll<70?0u:roll<82?1u:roll<94?2u:3u;
    m.texture_flags=(mode<<30)|(rnd()&0x03ff0000u);
    const unsigned alpha=chance(80)?31u:pick(32),faces=chance(tidy?97:85)?1u+pick(3):0u,polygon_mode=chance(tidy?100:90)?0u:pick(4);
    m.polygon=(chance(tidy?3:5)?0x80000000u:0u)|(alpha<<16)|(faces<<6)|(polygon_mode<<4)|pick(16)|(chance(50)?1u<<11:0u)|
        (chance(tidy?0:5)?0xc000u&rnd():0u);
    m.diffuse_ambient=rnd();m.specular_emission=rnd();
    return m;
}
// The colour role a binding gives a material is one the material has.
game::RuntimeMaterial fitting_runtime(const ModelBindings& d,unsigned source) noexcept {
    auto m=any_runtime(d,true);
    for(unsigned i=0;i<d.materials;++i)if(d.material[i].source_material==source){
        if(d.material[i].colour==ColourSource::emission)m.polygon&=~15u;
        if(d.material[i].colour==ColourSource::diffuse)m.diffuse_ambient|=0x8000u;
        if(d.material[i].normal_uv&&m.texture!=ModelBank::no_texture)m.texture_flags=(m.texture_flags&0x3fffffffu)|(2u<<30);
    }
    return m;
}
unsigned check_ratio() noexcept {
    // Every pair of texture dimensions, each axis: the constructed power of two
    // against the divide it replaces.
    game::RuntimeMaterial source{};source.polygon=0x1f00c0;source.texture=0;source.palette=0;
    game::set_lenient_materials(false);game::set_material_fog(false);reference::carry_fog=false;
    for(unsigned a=3;a<=10;++a)for(unsigned b=3;b<=10;++b)for(unsigned c=3;c<=10;++c)for(unsigned e=3;e<=10;++e){
        const game::Extent basis{1u<<a,1u<<b},sampled{1u<<c,1u<<e};
        LiveMaterial expected=unwritten_material(0),cut=unwritten_material(1),old=unwritten_material(2),scratch=unwritten_material(3);
        CHECK(reference::capture_material(source,{0,0,sampled,true},basis,{0,0},0,4096,expected,9)==Error::none);
        perf::set_cuts(1);CHECK(game::capture_material(source,{0,0,sampled,true},basis,{0,0},0,4096,cut,9)==Error::none);
        CHECK(game::capture_material_scratch(source,{0,0,sampled,true},basis,{0,0},0,4096,scratch,9)==Error::none);
        perf::set_cuts(0);CHECK(game::capture_material(source,{0,0,sampled,true},basis,{0,0},0,4096,old,9)==Error::none);
        CHECK(same(expected,cut)&&same(expected,old)&&same(expected,scratch));
        const float x=float(basis.width)/sampled.width,y=float(basis.height)/sampled.height;
        CHECK(!std::memcmp(&cut.state.uv[0],&x,4)&&!std::memcmp(&cut.state.uv[4],&y,4));
    }
    return 0;
}
unsigned check_materials() noexcept {
    static std::int16_t table[8192];for(auto& value:table)value=std::int16_t(rnd());
    static LiveMaterial out[3][16];static game::RuntimeMaterial runtime[8][16];static BMD_Texture textures[8];
    static BMD_File files[8];static ModelComponents models[8];static reference::Sequences sequences[8];
    hooks::initialize_image_tracking();for(auto& held:sequences)held={};
    for(unsigned slot=0;slot<8;++slot){models[slot]={};models[slot].modelFile=&files[slot];
        models[slot].materials=reinterpret_cast<BMD_Material*>(runtime[slot]);}
    for(unsigned trial=0;trial<3750*effort;++trial){
        const bool lenient=chance(50),fog=chance(30),wild=chance(30);
        game::set_lenient_materials(lenient);game::set_material_fog(fog);reference::carry_fog=fog;
        Shape shape=any_shape(wild);shape.groups=0;const auto& d=define(shape);
        const unsigned slot=pick(8);auto& file=files[slot];auto& model=models[slot];auto& held=sequences[slot];file={};
        file.numMaterials=wild&&chance(3)?d.source_materials+1:d.source_materials;file.numTextures=chance(90)?8:pick(9);file.numPalettes=6;
        for(auto& texture:textures)texture.unk_0c=chance(wild?85:100)?power_of_two(3,10)|(power_of_two(3,10)<<16):rnd();
        file.textures=wild&&chance(2)?nullptr:textures;
        for(auto& material:runtime[slot])material=any_runtime(d,false);
        // Texture sequences of several models live side by side. A refused
        // step poisons its model until an accepted one or a reset.
        if(chance(25)){hooks::reset_image_tracking(model);held={};}
        if(chance(35)){
            std::array<std::uint16_t,4> tex{{std::uint16_t(pick(9)),0,0,0}},pal{{std::uint16_t(pick(7)),0,0,0}};
            std::array<std::uint16_t,6> track{{std::uint16_t(pick(d.source_materials)),0,0,0,1,0}};std::uint16_t zero=0;
            BTP_File animation{};animation.unk_00=1;animation.numTexRecords=animation.numPalRecords=animation.numSeqRecords=1;
            animation.unk_04=reinterpret_cast<char*>(tex.data());animation.unk_0c=reinterpret_cast<char*>(pal.data());
            animation.unk_10=animation.unk_14=animation.unk_18=reinterpret_cast<char*>(&zero);
            animation.unk_20=reinterpret_cast<char*>(track.data());
            __wrap_func_02046bbc(&model,&animation,0);
            // The updater's own admission: indices inside the model file.
            if(tex[0]<file.numTextures&&pal[0]<file.numPalettes&&!(held.poisoned&&false)){
                unsigned at=0;while(at<held.count&&held.choice[at].material!=track[0])++at;
                if(at==held.count&&held.count<8)++held.count;
                if(at<8)held.choice[at]={track[0],tex[0],pal[0]};
                held.poisoned=false;++coverage.sequences;
            }else{held.poisoned=true;++coverage.poisoned;}
        }
        const std::int16_t* trig=chance(90)?table:nullptr;const std::size_t pairs=chance(92)?4096:chance(50)?4095:0;
        const std::uint8_t cutoff=std::uint8_t(rnd());const std::size_t room_for=wild&&chance(2)?d.source_materials-1:16;
        Error error[3];
        for(unsigned mode=0;mode<3;++mode)for(unsigned i=0;i<16;++i)out[mode][i]=unwritten_material(mode);
        error[0]=reference::capture_materials(d,model,trig,pairs,out[0],room_for,cutoff,held);
        perf::set_cuts(1);error[1]=hooks::capture_materials(d,model,trig,pairs,out[1],room_for,cutoff);
        perf::set_cuts(0);error[2]=hooks::capture_materials(d,model,trig,pairs,out[2],room_for,cutoff);
        CHECK(error[0]==error[1]&&error[0]==error[2]);
        ++coverage.material_models;coverage.material_refusals+=error[0]!=Error::none;
        if(error[0]==Error::none)for(unsigned i=0;i<d.source_materials;++i){
            CHECK(same(out[0][i],out[1][i])&&same(out[0][i],out[2][i]));++coverage.materials;
            CHECK(out[0][i].image==LiveMaterial::no_image&&out[2][i].image==LiveMaterial::no_image);
            // The note for the part stage is the first entry with the identity.
            const auto& cut=out[1][i];
            if(cut.visible&&cut.texture!=ModelBank::no_texture)CHECK(cut.image!=LiveMaterial::no_image&&cut.image==first_image(d,cut.texture,cut.palette));
            else if(cut.image!=LiveMaterial::no_image)CHECK(cut.image<d.images);
        }
    }
    hooks::initialize_image_tracking();
    game::set_lenient_materials(false);game::set_material_fog(false);reference::carry_fog=false;
    return 0;
}

// ---------------------------------------------------------------- whole producer
// Actors with their own bones and materials, captured update after update into
// two alternating frame stores, as the game does: once with the cuts, once
// with the BUILD 133 paths, and the skin once more by the reference above.
constexpr unsigned max_actors=40,frame_parts=8192,frame_occurrences=2048,frame_skin=24576,frame_states=2048,frame_lights=128;
struct Actor {
    const ModelBindings* definition=nullptr;ModelComponents model{};BMD_File file{};
    game::Affine bones[32]{};game::RuntimeMaterial runtime[16]{};unsigned char runtime_bones[32*52]{};
    game::Affine view{};Vector3 scale{};bool has_view=false,has_scale=false;std::uint64_t owner=0;
};
Actor actors[max_actors];BMD_Texture actor_textures[8];
Occurrence store_occurrence[2][2][frame_occurrences];PreparedPart store_part[2][2][frame_parts];
LightSet store_light[2][2][frame_lights];PreparedLighting store_state[2][2][frame_states];NormalVertex store_skin[2][2][frame_skin];
hooks::ProducerWorkspace workspace[2];
NormalVertex expected_skin[frame_skin];reference::SkinOccurrence expected_piece[frame_occurrences];
std::int16_t trig[8192];
bool same_frame(const SceneView& a,const SceneView& b) noexcept {
    if(a.occurrences!=b.occurrences||a.parts!=b.parts||a.skin_vertices!=b.skin_vertices||a.scene!=b.scene||a.serial!=b.serial||
       a.shadows!=b.shadows||!same(a.lighting,b.lighting))return false;
    for(std::size_t i=0;i<a.occurrences;++i){const auto& x=a.occurrence[i];const auto& y=b.occurrence[i];
        if(x.file_id!=y.file_id||x.first!=y.first||x.count!=y.count||x.kind!=y.kind||x.state!=y.state||
           x.identity.owner!=y.identity.owner||x.identity.component!=y.identity.component||x.identity.piece!=y.identity.piece)return false;}
    for(std::size_t i=0;i<a.parts;++i)if(!same(a.part[i],b.part[i]))return false;
    return !a.skin_vertices||!std::memcmp(a.skin_vertex,b.skin_vertex,a.skin_vertices*sizeof(NormalVertex));
}
unsigned check_producer(unsigned scenarios,bool crowds) noexcept {
    static SceneFrame frame[2][2]={
        {SceneFrame(store_occurrence[0][0],frame_occurrences,store_part[0][0],frame_parts,store_light[0][0],frame_lights,store_state[0][0],frame_states),
         SceneFrame(store_occurrence[0][1],frame_occurrences,store_part[0][1],frame_parts,store_light[0][1],frame_lights,store_state[0][1],frame_states)},
        {SceneFrame(store_occurrence[1][0],frame_occurrences,store_part[1][0],frame_parts,store_light[1][0],frame_lights,store_state[1][0],frame_states),
         SceneFrame(store_occurrence[1][1],frame_occurrences,store_part[1][1],frame_parts,store_light[1][1],frame_lights,store_state[1][1],frame_states)}};
    static hooks::SceneProducer producer[2]={hooks::SceneProducer(frame[0][0],workspace[0],trig,4096),hooks::SceneProducer(frame[1][0],workspace[1],trig,4096)};
    for(auto& value:trig)value=std::int16_t(rnd());
    for(auto& texture:actor_textures)texture.unk_0c=power_of_two(3,10)|(power_of_two(3,10)<<16);
    hooks::initialize_image_tracking();
    for(unsigned scenario=0;scenario<scenarios;++scenario){
        const bool lenient=chance(85);
        game::set_lenient_materials(lenient);LightingState::set_lenient(lenient);
        hooks::lighting_reset();
        if(chance(90)){std::int32_t basis[9];for(auto& value:basis)value=signed_in(4096);hooks::lighting_view(basis);
            for(unsigned id=0;id<4;++id)if(chance(lenient?50:95)){
                unsigned char source[40]{};const std::int32_t enabled=1,index=std::int32_t(id);
                std::memcpy(source,&index,4);std::memcpy(source+36,&enabled,4);
                for(unsigned i=0;i<3;++i){const std::int16_t value=std::int16_t(signed_in(4095));std::memcpy(source+28+i*2,&value,2);}
                const std::uint16_t colour=std::uint16_t(rnd()&32767u);std::memcpy(source+34,&colour,2);
                __wrap_func_ov007_020c232c(source);}
        }
        // Small skin storage in some scenarios: pieces that do not fit are left out.
        const std::size_t skin_room=chance(12)?30+pick(300):frame_skin;
        for(auto& mode:frame)for(auto& store:mode){store.set_lenient(lenient);}
        for(unsigned mode=0;mode<2;++mode)for(unsigned side=0;side<2;++side)frame[mode][side].set_skin_storage(store_skin[mode][side],skin_room);
        const unsigned count=crowds?12+pick(max_actors-11):1+pick(6);
        for(unsigned i=0;i<count;++i){auto& a=actors[i];
            if(i&&chance(30))a.definition=actors[pick(i)].definition; // several actors of one model
            else{
                Shape shape=any_shape(chance(6));shape.normal_uv=chance(85)?0:12;shape.parts=1+pick(crowds?40:chance(30)?90:14);
                if(shape.normal_uv)shape.billboards=false;
                shape.groups=chance(80)?1+pick(7):0;shape.group_vertices=3*(1+pick(crowds?60:20));
                const unsigned before=used_definitions;const auto* next=&define(shape);
                if(used_definitions<=before)for(unsigned j=0;j<i;++j)actors[j].definition=next; // pools refilled: drop the old ones
                a.definition=next;
            }
        }
        for(unsigned i=0;i<count;++i){auto& a=actors[i];const auto& d=*a.definition;
            a.file={};a.file.numBones=d.bones;a.file.numMaterials=d.source_materials;a.file.numTextures=8;a.file.numPalettes=6;a.file.textures=actor_textures;
            a.model={};a.model.modelFile=&a.file;a.model.materials=reinterpret_cast<BMD_Material*>(a.runtime);
            a.model.bones=reinterpret_cast<BMD_Bone*>(a.runtime_bones);a.model.transforms=reinterpret_cast<Matrix4x3*>(a.bones);
            for(unsigned source=0;source<16;++source)a.runtime[source]=fitting_runtime(d,source);
            for(auto& bone:a.bones)bone=any_bone(true);
            for(unsigned bone=0;bone<32;++bone){const std::uint16_t normalize=chance(50);std::memcpy(a.runtime_bones+bone*52+24,&normalize,2);}
            a.view=any_bone(true);a.has_view=chance(85);a.has_scale=chance(40);
            a.scale.x=4096+signed_in(2048);a.scale.y=4096+signed_in(2048);a.scale.z=4096+signed_in(2048);a.owner=1000+i;
        }
        Matrix projection{};for(auto& row:projection.row)for(auto& value:row)value=float(signed_in(4096))/1024.f;
        for(unsigned update=0;update<10;++update){
            bool submitted[max_actors];
            for(unsigned i=0;i<count;++i){auto& a=actors[i];const auto& d=*a.definition;
                const unsigned roll=pick(100);coverage.repeated_poses+=roll<50||(roll>=86&&roll<96)||roll>=98;
                if(roll>=50&&roll<72)a.bones[pick(d.bones)]=any_bone(true);
                else if(roll<86&&roll>=72)for(unsigned bone=0;bone<d.bones;++bone)a.bones[bone]=any_bone(true);
                else if(roll>=86&&roll<93)a.runtime[pick(d.source_materials)].polygon^=0x80000000u; // shown / hidden
                else if(roll>=93&&roll<96){const unsigned source=pick(d.source_materials);a.runtime[source]=fitting_runtime(d,source);}
                else if(roll>=96&&roll<98){auto& bone=a.bones[pick(d.bones)];bone.value[0]=bone.value[1]=bone.value[2]=0;} // no inverse
                submitted[i]=chance(88);
            }
            SceneView view[2];Error sealed[2];bool accepted[2][max_actors];
            for(unsigned mode=0;mode<2;++mode){
                perf::set_cuts(mode==0);
                auto& target=frame[mode][update&1u];producer[mode].set_frame(target);
                CHECK(target.begin(3,update)==Error::none);producer[mode].begin_frame();
                CHECK(producer[mode].projection(projection)==Error::none);
                for(unsigned i=0;i<count;++i)if(submitted[i]){auto& a=actors[i];
                    producer[mode].owner(a.owner);
                    accepted[mode][i]=hooks::SceneProducer::submit(*a.definition,a.model,a.has_view?reinterpret_cast<Matrix4x3*>(&a.view):nullptr,
                        a.has_scale?&a.scale:nullptr,&producer[mode]);
                }
                sealed[mode]=target.seal(true,0);
                if(sealed[mode]==Error::none)CHECK(target.acquire(view[mode])==Error::none);
            }
            CHECK(sealed[0]==sealed[1]);++coverage.updates;
            for(unsigned i=0;i<count;++i)if(submitted[i]){CHECK(accepted[0][i]==accepted[1][i]);++coverage.models;coverage.model_refusals+=!accepted[0][i];}
            CHECK(frame[0][update&1u].dropped()==frame[1][update&1u].dropped());
            if(sealed[0]!=Error::none)continue;
            CHECK(same_frame(view[0],view[1]));
            // The skin, from the bones and the captured visibility alone.
            std::size_t used=0,expected_pieces=0;
            for(std::size_t at=0;at<view[1].occurrences;++at){const auto& occurrence=view[1].occurrence[at];
                if(occurrence.kind!=Occurrence::Kind::model)continue;
                const Actor* actor=nullptr;
                for(unsigned i=0;i<count;++i)if(reinterpret_cast<std::uintptr_t>(&actors[i].model)==occurrence.identity.component)actor=&actors[i];
                CHECK(actor&&actor->definition->file_id==occurrence.file_id);
                if(actor->definition->skins)reference::skin(*actor->definition,actor->bones,view[1].part+occurrence.first,occurrence.first,
                    expected_skin,skin_room,used,expected_piece,expected_pieces);
            }
            CHECK(used==view[0].skin_vertices&&!std::memcmp(expected_skin,view[0].skin_vertex,used*sizeof(NormalVertex)));
            std::size_t piece=0;
            for(std::size_t at=0;at<view[0].occurrences;++at){const auto& occurrence=view[0].occurrence[at];
                if(occurrence.kind!=Occurrence::Kind::skin)continue;
                CHECK(piece<expected_pieces);const auto& expected=expected_piece[piece++];
                CHECK(occurrence.state==expected.part&&occurrence.first==expected.first&&occurrence.count==expected.count&&occurrence.identity.piece==expected.piece);
            }
            CHECK(piece==expected_pieces);coverage.skin_pieces+=piece;coverage.skin_vertices+=used;coverage.frame_parts+=view[0].parts;
            for(unsigned mode=0;mode<2;++mode)CHECK(frame[mode][update&1u].release()==Error::none);
        }
        for(auto& mode:frame)for(auto& store:mode)if(store.phase()!=SceneFrame::Phase::idle)store.release();
    }
    game::set_lenient_materials(false);LightingState::set_lenient(false);
    return 0;
}
}
extern "C" unsigned producer_cuts_run(unsigned amount,unsigned start){
    effort=amount;seed=start;refill();
    RUN(check_projection());
    RUN(check_skin_marks());
    RUN(check_ratio());
    RUN(check_parts());
    RUN(check_materials());
    RUN(check_producer(50*effort,false));
    RUN(check_producer(5*effort,true));
    perf::set_cuts(-1);
    return 0;
}
#if defined(__3DS__)&&defined(SM64DS_PRODUCER_CUTS_CATALOG)
// Cost of one update of a scene of catalog models, for the emulation harness:
// the producer's own stage clocks (set_stage_profile) read the harness's
// executed-instruction counter through svcGetSystemTick.
extern "C" {
std::uint32_t producer_cuts_scene[16]{};   // file identities of the scene's actors, zero ends the list
std::uint32_t producer_cuts_census[8]{};   // per update: models, parts, skin vertices, light states, lit parts, materials
volatile std::uint32_t producer_cuts_clock=0; // the harness answers reads of this word with its counter
unsigned long long svcGetSystemTick(void){return producer_cuts_clock;}
}
namespace {
constexpr unsigned bench_actors=15,bench_bones=64,bench_materials=128;
struct BenchActor {
    const ModelBindings* definition=nullptr;ModelComponents model{};BMD_File file{};
    game::Affine bones[bench_bones]{};game::RuntimeMaterial runtime[bench_materials]{};unsigned char runtime_bones[bench_bones*52]{};
    game::Affine view{};
};
BenchActor bench[bench_actors];
// A rigid bone: a rotation about each axis and a translation, 20.12.
game::Affine rigid_bone() noexcept {
    float m[9]={1,0,0,0,1,0,0,0,1};
    for(unsigned axis=0;axis<3;++axis){
        const float angle=float(signed_in(3141))/1000.f,c=std::cos(angle),s=std::sin(angle);
        const unsigned a=(axis+1)%3,b=(axis+2)%3;float r[9];
        for(unsigned column=0;column<3;++column){
            r[column*3+axis]=m[column*3+axis];r[column*3+a]=c*m[column*3+a]-s*m[column*3+b];r[column*3+b]=s*m[column*3+a]+c*m[column*3+b];}
        std::memcpy(m,r,sizeof(m));
    }
    game::Affine bone{};for(unsigned i=0;i<9;++i)bone.value[i]=int(m[i]*4096.f);
    for(unsigned i=9;i<12;++i)bone.value[i]=signed_in(1<<16);
    return bone;
}
}
// cuts: 1 the cuts, 0 the BUILD 133 paths. moving: every actor's bones change
// each update (0: they stand still). Returns 0, or the line that refused.
extern "C" unsigned producer_cuts_bench(unsigned cuts,unsigned updates,unsigned moving){
    static SceneFrame frame[2]={
        SceneFrame(store_occurrence[0][0],frame_occurrences,store_part[0][0],frame_parts,store_light[0][0],frame_lights,store_state[0][0],frame_states),
        SceneFrame(store_occurrence[0][1],frame_occurrences,store_part[0][1],frame_parts,store_light[0][1],frame_lights,store_state[0][1],frame_states)};
    static hooks::SceneProducer producer(frame[0],workspace[0],trig,4096);
    seed=0x51ce9e77u;refill();perf::set_cuts(int(cuts));
    game::set_lenient_materials(true);LightingState::set_lenient(true);hooks::initialize_image_tracking();
    hooks::lighting_reset();std::int32_t basis[9]={4096,0,0,0,4096,0,0,0,4096};hooks::lighting_view(basis);
    for(auto& value:trig)value=std::int16_t(rnd());
    for(auto& texture:actor_textures)texture.unk_0c=64u|(64u<<16);
    unsigned count=0;
    for(;count<bench_actors&&producer_cuts_scene[count];++count){auto& a=bench[count];
        a.definition=nullptr;
        for(const auto& model:prepared::models)if(model.file_id==producer_cuts_scene[count])a.definition=&model;
        CHECK(a.definition);const auto& d=*a.definition;
        CHECK(d.bones<=bench_bones&&d.source_materials<=bench_materials);
        a.file={};a.file.numBones=d.bones;a.file.numMaterials=d.source_materials;a.file.numTextures=8;a.file.numPalettes=6;a.file.textures=actor_textures;
        a.model={};a.model.modelFile=&a.file;a.model.materials=reinterpret_cast<BMD_Material*>(a.runtime);
        a.model.bones=reinterpret_cast<BMD_Bone*>(a.runtime_bones);a.model.transforms=reinterpret_cast<Matrix4x3*>(a.bones);
        for(unsigned source=0;source<d.source_materials;++source){auto& m=a.runtime[source];m={};
            // Opaque, both faces, its first prepared image, one light where the model is lit.
            bool lit=false,diffuse=false;unsigned width=0,height=0;
            for(unsigned i=0;i<d.materials;++i)if(d.material[i].source_material==source){
                lit|=d.material[i].colour==ColourSource::lighting;diffuse|=d.material[i].colour==ColourSource::diffuse;
                width=d.material[i].width;height=d.material[i].height;}
            m.texture=m.palette=ModelBank::no_texture;
            for(unsigned i=0;i<d.images;++i)if(d.image[i].use==ImageUse::model&&d.image[i].width==width&&d.image[i].height==height){
                m.texture=d.image[i].texture;m.palette=d.image[i].palette;break;}
            m.scale_x=m.scale_y=4096;m.polygon=0x1f00c0u|(lit?1u:0u);m.diffuse_ambient=0x7fff7fffu|(diffuse?0x8000u:0u);m.specular_emission=0;
        }
        for(auto& bone:a.bones)bone=rigid_bone();
        a.view=rigid_bone();
    }
    CHECK(count);
    for(auto& store:frame){store.set_lenient(true);store.set_skin_storage(store_skin[0][&store-frame],frame_skin);}
    hooks::set_stage_profile(false);
    constexpr unsigned warm=3; // kept marks and vertices settle before the clocks start
    Matrix projection{};projection.row[0][0]=1.2f;projection.row[1][1]=1.6f;projection.row[2][2]=-1.01f;projection.row[2][3]=-0.2f;projection.row[3][2]=-1.f;
    for(unsigned update=0;update<warm+updates;++update){
        if(update==warm){hooks::stage_profile={};hooks::set_stage_profile(true);}
        if(moving)for(unsigned i=0;i<count;++i)for(unsigned bone=0;bone<bench[i].definition->bones;++bone)bench[i].bones[bone]=rigid_bone();
        auto& target=frame[update&1u];producer.set_frame(target);
        CHECK(target.begin(3,update)==Error::none);producer.begin_frame();CHECK(producer.projection(projection)==Error::none);
        for(unsigned i=0;i<count;++i){producer.owner(100+i);
            CHECK(hooks::SceneProducer::submit(*bench[i].definition,bench[i].model,reinterpret_cast<Matrix4x3*>(&bench[i].view),nullptr,&producer));}
        CHECK(target.dropped()==0&&target.seal(true,0)==Error::none);
        SceneView view;CHECK(target.acquire(view)==Error::none);
        unsigned lit=0,materials=0;
        for(std::size_t i=0;i<view.parts;++i)lit+=view.part[i].lighting!=no_lighting;
        for(unsigned i=0;i<count;++i)materials+=bench[i].definition->source_materials;
        producer_cuts_census[0]=count;producer_cuts_census[1]=std::uint32_t(view.parts);producer_cuts_census[2]=std::uint32_t(view.skin_vertices);
        producer_cuts_census[3]=std::uint32_t(view.lighting.state_count);producer_cuts_census[4]=lit;producer_cuts_census[5]=materials;
        // What the scene published, for the harness to compare between the two paths.
        std::uint32_t digest=2166136261u;
        const auto mix=[&](const void* data,std::size_t bytes){const auto* p=static_cast<const unsigned char*>(data);
            for(std::size_t i=0;i<bytes;++i)digest=(digest^p[i])*16777619u;};
        for(std::size_t i=0;i<view.parts;++i){const auto& part=view.part[i];mix(&part.object_to_clip,sizeof(Matrix));
            mix(part.material.uv.data(),sizeof(part.material.uv));mix(&part.material.tint,4);mix(&part.image,4);mix(&part.lighting,2);}
        mix(view.skin_vertex,view.skin_vertices*sizeof(NormalVertex));
        for(std::size_t i=0;i<view.lighting.state_count;++i){mix(view.lighting.state[i].normal.data(),36);mix(&view.lighting.state[i].lights,2);}
        producer_cuts_census[6]=update==warm?digest:producer_cuts_census[6]*31u+digest;
        CHECK(target.release()==Error::none);
    }
    hooks::set_stage_profile(false);perf::set_cuts(-1);
    return 0;
}
#endif
#ifndef __3DS__
int main(){
    for(unsigned pass=0;pass<3;++pass){
        const unsigned line=producer_cuts_run(8,0x2468ace1u+pass*0x9e3779b9u);
        if(line){std::printf("producer cuts: mismatch at tests/native_renderer/producer_cuts.cpp:%u (pass %u)\n",line,pass);return 1;}
    }
    const auto& c=coverage;
    std::printf("Producer cuts == BUILD 133, bit for bit, over 3 passes:\n"
        "  poses              %llu matrices (%llu refused alike)\n"
        "  skin-bone marks    %llu captures (%llu refused alike)\n"
        "  UV ratio           4096 dimension pairs x 3 paths\n"
        "  parts              %llu models, %llu parts (%llu lit), %llu refused alike\n"
        "  materials          %llu models, %llu materials, %llu refused alike, %llu texture-sequence steps (%llu poisoned)\n"
        "  whole producer     %llu updates, %llu model captures (%llu refused alike), %llu parts, %llu skin pieces, %llu skin vertices, %llu captures with unmoved bones\n",
        (unsigned long long)c.poses,(unsigned long long)c.pose_refusals,(unsigned long long)c.marked,(unsigned long long)c.mark_refusals,
        (unsigned long long)c.part_models,(unsigned long long)c.parts,(unsigned long long)c.lit_parts,(unsigned long long)c.part_refusals,
        (unsigned long long)c.material_models,(unsigned long long)c.materials,(unsigned long long)c.material_refusals,
        (unsigned long long)c.sequences,(unsigned long long)c.poisoned,
        (unsigned long long)c.updates,(unsigned long long)c.models,(unsigned long long)c.model_refusals,(unsigned long long)c.frame_parts,
        (unsigned long long)c.skin_pieces,(unsigned long long)c.skin_vertices,(unsigned long long)c.repeated_poses);
    return 0;
}
#endif
