#include "game_model_pose.hpp"
#include "ds_math_unit.h"
#include "perf_switch.hpp"
#include <cstring>
// Host builds model the console's arithmetic: its VFPv2 has no fused multiply-add.
#if !defined(__3DS__)&&defined(__clang__)
#pragma clang fp contract(off)
#elif !defined(__3DS__)&&defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#ifdef SM64DS_NATIVE_AFFINE_ASM
extern "C" void sm64ds_affine_compose_arm11(const std::int32_t*,const std::int32_t*,std::int32_t*) noexcept;
#endif

namespace sm64ds::render::game {
namespace {
#ifdef SM64DS_NATIVE_AFFINE_ASM
bool affine_assembly=true;
#endif
std::int32_t word(std::uint32_t bits) noexcept { std::int32_t result;std::memcpy(&result,&bits,4);return result; }
std::int32_t fixed(std::uint64_t sum) noexcept { return word(std::uint32_t(sum>>12)); }
std::uint64_t product(std::int32_t a,std::int32_t b) noexcept { return std::uint64_t(std::int64_t(a)*b); }
Affine scaled(Affine pose,const std::int32_t* scale) noexcept {
    for(unsigned column=0;column<3;++column)for(unsigned row=0;row<3;++row)
        pose.value[column*3+row]=fixed(product(pose.value[column*3+row],scale[column]));
    return pose;
}
// BUILD131: floor(sqrt(n)) and 2^56 / sq, the two steps of the DS vector
// normalisation, through the exact VFP helpers the game's own math already
// uses (ds_math_unit.h) instead of a 32-step restoring loop and libgcc's
// shift-subtract 64-bit divide (ARMv6K has no integer divide). Every lit or
// environment-mapped bone with the normalise flag and every billboard runs
// them three (or one) times per image. Same integers, bit for bit:
// tests/native_renderer/normal_basis.cpp and tools/test_native_normalize_arm.py.
std::uint64_t normalization_count=0;
std::uint64_t root(std::uint64_t n) noexcept {++normalization_count;return sm64ds_ds_math_isqrt64(n);}
std::uint64_t reciprocal56(std::uint64_t sq) noexcept {
    std::int64_t quotient,remainder;
    // Declined for sq < 8 (the quotient leaves the exact double range).
    if(sm64ds_ds_math_divide(std::int64_t(1)<<56,std::int64_t(sq),&quotient,&remainder))return std::uint64_t(quotient);
    return (std::uint64_t(1)<<56)/sq;
}
bool normalize_xy(std::int32_t* v) noexcept {
    // Bound the original signed products/root to a defined numeric domain.
    // Wider real pose coverage must be established before widening admission.
    for(unsigned i=0;i<2;++i)if(v[i]<-0x1000000 || v[i]>0x1000000)return false;
    const std::uint64_t sq=product(v[0],v[0])+product(v[1],v[1]);
    if(!sq){v[0]=v[1]=0;return true;}
    const std::uint64_t reciprocal=reciprocal56(sq);
    const std::uint64_t scale=reciprocal*root(sq<<2);
    for(unsigned i=0;i<2;++i){
        const auto bits=scale*std::uint64_t(std::int64_t(v[i]))+(std::uint64_t(1)<<44);
        const auto shifted=std::uint32_t(bits>>45)|((bits>>63)?(0xffffffffu<<19):0u);
        v[i]=word(shifted);
    }
    return true;
}
std::int32_t rounded_product(std::int32_t a,std::int32_t b) noexcept { return fixed(product(a,b)+0x800u); }
bool billboard_pose(const Affine& bone,const Affine* view,const std::int32_t* scale,Affine& output) noexcept {
    const Affine oriented=view?compose(bone,*view):bone;
    Affine translated=bone;
    if(view){Affine size=unit_affine();size.value[0]=scale[0];size.value[4]=scale[1];size.value[8]=scale[2];
        translated=compose(bone,compose(size,*view));}
    output={};output.value[8]=scale[2];
    for(unsigned i=0;i<3;++i)output.value[9+i]=translated.value[9+i];
    std::int32_t axis[2];
    const auto length=word(std::uint32_t(product(oriented.value[3],oriented.value[3])+product(oriented.value[4],oriented.value[4])));
    if(length>0x4000){
        axis[0]=oriented.value[3];axis[1]=oriented.value[4];if(!normalize_xy(axis))return false;
        const auto x=rounded_product(axis[0],scale[0]),y=rounded_product(axis[1],scale[1]);
        output.value[3]=x;output.value[4]=y;output.value[0]=y;output.value[1]=word(0u-std::uint32_t(x));
    }else{
        axis[0]=oriented.value[0];axis[1]=oriented.value[1];if(!normalize_xy(axis))return false;
        const auto x=rounded_product(axis[0],scale[0]),y=rounded_product(axis[1],scale[1]);
        output.value[0]=x;output.value[1]=y;output.value[4]=x;output.value[3]=word(0u-std::uint32_t(y));
    }
    return true;
}
bool normalize_columns(Affine& normal) noexcept {
    for(unsigned column=0;column<3;++column){
        auto* vector=normal.value+column*3;std::uint64_t sq=0;
        for(unsigned axis=0;axis<3;++axis){const auto v=vector[axis];if(v<-0x1000000||v>0x1000000)return false;sq+=product(v,v);}
        if(!sq){for(unsigned axis=0;axis<3;++axis)vector[axis]=0;continue;}
        const auto factor=reciprocal56(sq)*root(sq<<2);
        for(unsigned axis=0;axis<3;++axis){const auto value=factor*std::uint64_t(std::int64_t(vector[axis]))+(std::uint64_t(1)<<44);
            vector[axis]=word(std::uint32_t(value>>45)|((value>>63)?(0xffffffffu<<19):0u));}
    }
    return true;
}
Matrix projected(const Affine& affine,const Matrix& projection) noexcept {
    Matrix result{};
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column){
        float value=column==3?projection.row[row][3]:0;
        for(unsigned k=0;k<3;++k)value+=projection.row[row][k]*(float(affine.value[column*3+k])/4096.f);
        result.row[row][column]=value;
    }
    return result;
}
// BUILD134 (exact cut): the same matrix, element for element, as projected().
// Each of the twelve fixed values is converted and scaled once instead of once
// per row (48 conversions), and the sixteen sums advance together, one term
// at a time, instead of one after another: on the ARM11's VFP every add waits
// for the previous add of the same sum, so sixteen independent sums fill the
// time one sum spends waiting. Each element still is
//   ((start + P[row][0]*a[c*3+0]) + P[row][1]*a[c*3+1]) + P[row][2]*a[c*3+2]
// with start = 0 or P[row][3]: the same operations in the same order, so the
// same bits under any rounding mode (tests/native_renderer/producer_cuts.cpp,
// tools/test_native_producer_cuts_arm.py). `out` may not alias the inputs.
void project(const Affine& affine,const Matrix& projection,Matrix& out) noexcept {
    const std::int32_t* v=affine.value;
    const float a0=float(v[0])/4096.f,a1=float(v[1])/4096.f,a2=float(v[2])/4096.f,
                a3=float(v[3])/4096.f,a4=float(v[4])/4096.f,a5=float(v[5])/4096.f,
                a6=float(v[6])/4096.f,a7=float(v[7])/4096.f,a8=float(v[8])/4096.f,
                a9=float(v[9])/4096.f,a10=float(v[10])/4096.f,a11=float(v[11])/4096.f;
    const float (*p)[4]=projection.row;
    float r00=0,r01=0,r02=0,r03=p[0][3],r10=0,r11=0,r12=0,r13=p[1][3],
          r20=0,r21=0,r22=0,r23=p[2][3],r30=0,r31=0,r32=0,r33=p[3][3];
    float k=p[0][0];r00+=k*a0;r01+=k*a3;r02+=k*a6;r03+=k*a9;
    k=p[1][0];r10+=k*a0;r11+=k*a3;r12+=k*a6;r13+=k*a9;
    k=p[2][0];r20+=k*a0;r21+=k*a3;r22+=k*a6;r23+=k*a9;
    k=p[3][0];r30+=k*a0;r31+=k*a3;r32+=k*a6;r33+=k*a9;
    k=p[0][1];r00+=k*a1;r01+=k*a4;r02+=k*a7;r03+=k*a10;
    k=p[1][1];r10+=k*a1;r11+=k*a4;r12+=k*a7;r13+=k*a10;
    k=p[2][1];r20+=k*a1;r21+=k*a4;r22+=k*a7;r23+=k*a10;
    k=p[3][1];r30+=k*a1;r31+=k*a4;r32+=k*a7;r33+=k*a10;
    k=p[0][2];r00+=k*a2;r01+=k*a5;r02+=k*a8;r03+=k*a11;
    k=p[1][2];r10+=k*a2;r11+=k*a5;r12+=k*a8;r13+=k*a11;
    k=p[2][2];r20+=k*a2;r21+=k*a5;r22+=k*a8;r23+=k*a11;
    k=p[3][2];r30+=k*a2;r31+=k*a5;r32+=k*a8;r33+=k*a11;
    float (*o)[4]=out.row;
    o[0][0]=r00;o[0][1]=r01;o[0][2]=r02;o[0][3]=r03;o[1][0]=r10;o[1][1]=r11;o[1][2]=r12;o[1][3]=r13;
    o[2][0]=r20;o[2][1]=r21;o[2][2]=r22;o[2][3]=r23;o[3][0]=r30;o[3][1]=r31;o[3][2]=r32;o[3][3]=r33;
}
// BUILD134 (exact cut): the bones a model's skin vertices name never change
// (definitions are immutable catalog entries), yet every capture read each
// skin vertex again only to mark them: up to 32 KB of vertex records per
// update, twice the ARM11's data cache. The marks of the last skinned models
// are kept here, on the game thread, tied to the definition they were made
// from (address, arrays, counts, every group and the outer vertices).
struct SkinBones {
    const ModelBindings* owner=nullptr;const SkinGroup* group=nullptr;const SkinVertex* vertex=nullptr;
    std::uint32_t file_id=0,bones=0,skins=0,signature=0;bool valid=false;
    std::array<std::uint32_t,ModelBank::max_matrices/32> mask{};
};
constexpr unsigned skin_bone_entries=64,skin_bone_window=4;
std::array<SkinBones,skin_bone_entries> skin_bones{};
unsigned skin_bone_victim=0;
std::uint32_t skin_signature(const ModelBindings& bindings) noexcept {
    std::uint32_t h=2166136261u;
    for(unsigned g=0;g<bindings.skins;++g){const auto& group=bindings.skin[g];
        h=(h^group.part)*16777619u;h=(h^group.first)*16777619u;h=(h^group.count)*16777619u;}
    const auto& first=bindings.skin[0];const auto& last=bindings.skin[bindings.skins-1];
    if(first.count)h=(h^bindings.skin_vertex[first.first].bone)*16777619u;
    if(last.count)h=(h^bindings.skin_vertex[last.first+last.count-1].bone)*16777619u;
    return h;
}
// False: a skin vertex names a bone the model does not have.
bool mark_skin_bones(const ModelBindings& bindings,std::array<std::uint32_t,ModelBank::max_matrices/32>& plain) noexcept {
    SkinBones* entry=nullptr;std::uint32_t signature=0;
    if(perf::cuts()){
        const auto address=reinterpret_cast<std::uintptr_t>(&bindings);
        const unsigned start=unsigned((address>>4)^(address>>10)^bindings.file_id);
        signature=skin_signature(bindings);
        for(unsigned i=0;i<skin_bone_window;++i){auto& e=skin_bones[(start+i)%skin_bone_entries];
            if(e.owner==&bindings&&e.group==bindings.skin&&e.vertex==bindings.skin_vertex&&
               e.file_id==bindings.file_id&&e.bones==bindings.bones&&e.skins==bindings.skins&&e.signature==signature){
                if(!e.valid)return false;
                for(unsigned word=0;word<plain.size();++word)plain[word]|=e.mask[word];
                return true;
            }
            if(!entry&&!e.owner)entry=&e;
        }
        if(!entry)entry=&skin_bones[(start+skin_bone_victim++%skin_bone_window)%skin_bone_entries];
    }
    std::array<std::uint32_t,ModelBank::max_matrices/32> mask{};bool valid=true;
    for(unsigned g=0;g<bindings.skins&&valid;++g){const auto& group=bindings.skin[g];
        for(std::uint32_t v=0;v<group.count;++v){const auto bone=bindings.skin_vertex[group.first+v].bone;
            if(bone>=bindings.bones){valid=false;break;}
            mask[bone/32]|=1u<<(bone%32);}
    }
    if(entry)*entry={&bindings,bindings.skin,bindings.skin_vertex,bindings.file_id,bindings.bones,bindings.skins,signature,valid,mask};
    if(!valid)return false;
    for(unsigned word=0;word<plain.size();++word)plain[word]|=mask[word];
    return true;
}
}
std::uint64_t normalizations() noexcept {return normalization_count;}
Affine unit_affine() noexcept { Affine result{};result.value[0]=result.value[4]=result.value[8]=4096;return result; }
Error project_pose(const Affine& pose,const Matrix& projection,Matrix& out) noexcept {
    if(!finite(projection))return Error::invalid_data;
    const auto next=projected(pose,projection);if(!finite(next))return Error::invalid_data;
    out=next;return Error::none;
}
void forget_skin_bones() noexcept {for(auto& entry:skin_bones)entry={};}
void use_affine_assembly(bool value) noexcept {
#ifdef SM64DS_NATIVE_AFFINE_ASM
    affine_assembly=value;
#else
    (void)value;
#endif
}
Affine compose(const Affine& local,const Affine& parent) noexcept {
#ifdef SM64DS_NATIVE_AFFINE_ASM
    if(affine_assembly){Affine output;sm64ds_affine_compose_arm11(local.value,parent.value,output.value);return output;}
#endif
    Affine output{};
    for(unsigned column=0;column<4;++column)for(unsigned row=0;row<3;++row){
        std::uint64_t sum=0;
        for(unsigned k=0;k<3;++k)sum+=product(local.value[column*3+k],parent.value[k*3+row]);
        const std::uint32_t value=std::uint32_t(fixed(sum))+(column==3?std::uint32_t(parent.value[9+row]):0u);
        output.value[column*3+row]=word(value);
    }
    return output;
}
Error build_normal_bases(const ModelBindings& bindings,const void* transforms,const void* runtime_bones,
                         std::size_t count,const Affine* view,NormalBasis* output,std::size_t capacity,const LiveMaterial* materials) noexcept {
    if(!bindings.part||!bindings.material||bindings.parts>ModelBank::max_parts||bindings.bones>ModelBank::max_matrices)
        return Error::invalid_data;
    std::array<std::uint32_t,ModelBank::max_matrices/32> visited{};
    for(unsigned i=0;i<bindings.parts;++i){const auto& part=bindings.part[i];
        if(part.material>=bindings.materials)return Error::invalid_data;
        if(!bindings.material[part.material].normal_uv)continue;
        if(materials){const auto source=bindings.material[part.material].source_material;
            if(source>=bindings.source_materials)return Error::invalid_data;
            if(!materials[source].visible)continue;}
        const auto bone=part.normal_bone;
        if(part.billboard)return Error::unsupported; // original billboard path inherits another texture matrix
        if(bone>=bindings.bones||bone>=count||bone>=capacity||!transforms||!runtime_bones||!output)return Error::invalid_data;
        const auto bit=1u<<(bone%32);if(visited[bone/32]&bit)continue;visited[bone/32]|=bit;
        Affine normal;std::memcpy(&normal,static_cast<const char*>(transforms)+bone*sizeof(Affine),sizeof(normal));
        std::uint16_t normalize;std::memcpy(&normalize,static_cast<const char*>(runtime_bones)+bone*52+24,2);
        if(normalize&&!normalize_columns(normal))return Error::unsupported;
        const auto combined=compose(normal,view?*view:unit_affine());
        std::memcpy(output[bone].value,combined.value,sizeof(output[bone].value));
    }
    return Error::none;
}
Error build_lighting_bases(const ModelBindings& bindings,const void* transforms,const void* runtime_bones,
                          std::size_t count,const Affine* view,const std::int32_t* scale_input,
                          const LiveMaterial* materials,NormalBasis* ordinary,NormalBasis* billboard,std::size_t capacity,
                          const NormalBasis* prepared_texture) noexcept {
    if(!bindings.part||!bindings.material||!materials||bindings.parts>ModelBank::max_parts||bindings.bones>ModelBank::max_matrices)
        return Error::invalid_data;
    std::array<std::uint32_t,ModelBank::max_matrices/32> plain{},facing{};
    std::array<std::uint32_t,ModelBank::max_matrices/32> texture{};
    // Ordinary texture normals and lighting use the same normalized palette
    // basis/view, even when their selected bones differ. The producer supplies
    // only bases computed by build_normal_bases for this very capture.
    if(prepared_texture)for(unsigned i=0;i<bindings.parts;++i){const auto& p=bindings.part[i];
        if(p.material>=bindings.materials)return Error::invalid_data;
        const auto& m=bindings.material[p.material];
        if(m.source_material>=bindings.source_materials)return Error::invalid_data;
        if(m.normal_uv&&materials[m.source_material].visible&&!p.billboard){
            if(p.normal_bone>=bindings.bones||p.normal_bone>=capacity)return Error::invalid_data;
            texture[p.normal_bone/32]|=1u<<(p.normal_bone%32);
        }
    }
    const std::int32_t unit[3]={4096,4096,4096};const auto* scale=scale_input?scale_input:unit;
    for(unsigned i=0;i<bindings.parts;++i){const auto& part=bindings.part[i];
        if(part.material>=bindings.materials)return Error::invalid_data;
        const auto& m=bindings.material[part.material];
        if(m.source_material>=bindings.source_materials)return Error::invalid_data;
        if(m.colour!=ColourSource::lighting||!materials[m.source_material].visible||!materials[m.source_material].lighting.mask)continue;
        const auto bone=part.lighting_bone;
        if(bone>=bindings.bones||bone>=count||bone>=capacity||!transforms)return Error::invalid_data;
        auto& visited=part.billboard?facing:plain;const auto bit=1u<<(bone%32);
        if(visited[bone/32]&bit)continue;
        visited[bone/32]|=bit;
        if(!part.billboard&&(texture[bone/32]&bit)){
            if(!ordinary)return Error::invalid_data;
            ordinary[bone]=prepared_texture[bone];continue;
        }
        Affine source;std::memcpy(&source,static_cast<const char*>(transforms)+bone*sizeof(Affine),sizeof(source));
        Affine normal;
        if(part.billboard){
            if(!billboard)return Error::invalid_data;
            if(!billboard_pose(source,view,scale,normal)||!normalize_columns(normal))return Error::unsupported;
        }else{
            if(!runtime_bones||!ordinary)return Error::invalid_data;
            std::uint16_t normalize;std::memcpy(&normalize,static_cast<const char*>(runtime_bones)+bone*52+24,2);
            if(normalize&&!normalize_columns(source))return Error::unsupported;
            normal=compose(source,view?*view:unit_affine());
        }
        std::memcpy((part.billboard?billboard:ordinary)[bone].value,normal.value,sizeof(NormalBasis));
    }
    return Error::none;
}
Error build_poses_bytes(const ModelBindings& bindings,const void* bones,std::size_t bone_count,const Affine* view,
                  const std::int32_t* scale_input,const Matrix& projection,Matrix* ordinary,Matrix* billboard,std::size_t capacity) noexcept {
    if(!bindings.bones || bindings.bones>ModelBank::max_matrices || !bones || bone_count<bindings.bones ||
       capacity<bindings.bones || !bindings.part || !bindings.parts || bindings.parts>ModelBank::max_parts ||
       !finite(projection))return Error::invalid_data;
    std::array<std::uint32_t,ModelBank::max_matrices/32> plain{},facing{};
    for(unsigned i=0;i<bindings.parts;++i){
        const auto& part=bindings.part[i];if(part.bone>=bindings.bones)return Error::invalid_data;
        (part.billboard?facing:plain)[part.bone/32]|=1u<<(part.bone%32);
    }
    if(bindings.skins&&bindings.skin&&bindings.skin_vertex&&!mark_skin_bones(bindings,plain))return Error::invalid_data;
    const std::int32_t unit[3]={4096,4096,4096};const auto* scale=scale_input?scale_input:unit;
    const Affine parent=view?*view:unit_affine();
    const Affine ordinary_parent=scale_input?scaled(parent,scale):parent;
    const bool cut=perf::cuts();const Matrix view_to_clip=projection; // owned: outputs cannot alias it
    for(unsigned bone=0;bone<bindings.bones;++bone){
        const auto mask=1u<<(bone%32);
        if(!((plain[bone/32]|facing[bone/32])&mask))continue;
        Affine source;std::memcpy(&source,static_cast<const char*>(bones)+bone*sizeof(Affine),sizeof(source));
        if(plain[bone/32]&(1u<<(bone%32))){
            if(!ordinary)return Error::invalid_data;
            if(cut)project(compose(source,ordinary_parent),view_to_clip,ordinary[bone]);
            else ordinary[bone]=projected(compose(source,ordinary_parent),projection);
            if(!finite(ordinary[bone]))return Error::invalid_data;
        }
        if(facing[bone/32]&(1u<<(bone%32))){
            if(!billboard)return Error::invalid_data;
            Affine pose;if(!billboard_pose(source,view,scale,pose))return Error::invalid_data;
            if(cut)project(pose,view_to_clip,billboard[bone]);
            else billboard[bone]=projected(pose,projection);
            if(!finite(billboard[bone]))return Error::invalid_data;
        }
    }
    return Error::none;
}
Error build_poses(const ModelBindings& bindings,const Affine* bones,std::size_t bone_count,const Affine* view,
                  const std::int32_t* scale,const Matrix& projection,Matrix* ordinary,Matrix* billboard,std::size_t capacity) noexcept {
    return build_poses_bytes(bindings,bones,bone_count,view,scale,projection,ordinary,billboard,capacity);
}
}
