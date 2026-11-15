#include "scene_producer.hpp"
#ifdef __3DS__
extern "C" unsigned long long svcGetSystemTick(void);
// libctru; 3ds.h clashes with game types.h
#endif
#include "game_materials.hpp"
#include "game_lighting.hpp"
#include "perf_switch.hpp"
#include "ModelBase.h"
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
// Host builds model the console's arithmetic: its VFPv2 has no fused multiply-add.
#if !defined(__3DS__)&&defined(__clang__)
#pragma clang fp contract(off)
#elif !defined(__3DS__)&&defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
namespace sm64ds::render::game_hooks {
static_assert(sizeof(Matrix4x3)==sizeof(game::Affine));
namespace {CaptureFailures failures;Error fail(unsigned stage,Error error) noexcept{
    if(error!=Error::none){++failures.stage[stage];failures.last[stage]=error;}return error;}}
const CaptureFailures& capture_failures() noexcept {return failures;}
std::uint64_t skin_ticks=0;
StageProfile stage_profile;
namespace {bool profile_stages=false;}
void set_stage_profile(bool enabled) noexcept {profile_stages=enabled;}
static_assert(sizeof(Vector3)==3*sizeof(std::int32_t));
namespace {void note_skin_update() noexcept;}
SceneProducer::SceneProducer(SceneFrame& frame,ProducerWorkspace& workspace,const std::int16_t* table,std::size_t pairs) noexcept:
    frame_(&frame),workspace_(workspace),table_(table),pairs_(pairs){}
void SceneProducer::begin_frame() noexcept {projection_valid_=false;note_skin_update();}
Error SceneProducer::projection(const Matrix& native) noexcept {
    if(frame_->phase()!=SceneFrame::Phase::capturing)return Error::invalid_phase;
    if(!finite(native)){frame_->invalidate(Error::invalid_data);return Error::invalid_data;}
    projection_=native;projection_valid_=true;return Error::none;
}
Error SceneProducer::capture(const ModelBindings& bindings,const ModelComponents& model,const Matrix4x3* matrix,const Vector3* scale) noexcept {
    if(frame_->phase()!=SceneFrame::Phase::capturing)return Error::invalid_phase;
    if(frame_->failure()!=Error::none)return frame_->failure();
    if(!model.modelFile||model.modelFile->numBones!=bindings.bones)return fail(7,Error::invalid_data);
    auto& w=workspace_;
    {   // PLAYABLE diagnostics: live material dump for one model id.
        static long trace=-2;static unsigned calls=0;
        if(trace==-2){const char* v=std::getenv("SM64DS_PLAYABLE_MATERIAL_TRACE");trace=v?std::atol(v):-1;}
        if(trace==long(bindings.file_id)&&(calls++%300)==0)for(unsigned i=0;i<bindings.source_materials;++i){
            game::RuntimeMaterial m;std::memcpy(&m,reinterpret_cast<const char*>(model.materials)+i*sizeof(m),sizeof(m));
            std::printf("Material trace file=%lu mat=%u tex=%lu pal=%lu uvmode=%lu scale=%ld,%ld angle=%u translate=%ld,%ld polygon=%08lx\n",
                (unsigned long)bindings.file_id,i,(unsigned long)m.texture,(unsigned long)m.palette,(unsigned long)(m.texture_flags>>30),
                (long)m.scale_x,(long)m.scale_y,unsigned(m.angle),(long)m.translate_x,(long)m.translate_y,(unsigned long)m.polygon);
        }
    }
#ifdef __3DS__
    std::uint64_t mark=profile_stages?svcGetSystemTick():0;
    const auto lap=[&](unsigned stage){if(profile_stages){const auto now=svcGetSystemTick();stage_profile.ticks[stage]+=now-mark;mark=now;}};
#else
    const auto lap=[](unsigned){};
#endif
    const auto material_error=capture_materials(bindings,model,table_,pairs_,w.material.data(),w.material.size());
    if(material_error!=Error::none)return fail(0,material_error);
    lap(0);
    bool visible=false,needs_normals=false,needs_lighting=false,needs_table=false;unsigned light_mask=0;
    for(unsigned i=0;i<bindings.materials;++i){const auto source=bindings.material[i].source_material;
        if(source>=bindings.source_materials)return fail(0,Error::invalid_data);
        visible|=w.material[source].visible;needs_normals|=bindings.material[i].normal_uv&&w.material[source].visible;
        if(bindings.material[i].colour==ColourSource::lighting&&w.material[source].visible){
            const auto& l=w.material[source].lighting;needs_lighting|=l.mask!=0;light_mask|=l.mask;
            needs_table|=l.mask&&l.specular&&l.use_shininess;
        }
    }
    if(!visible)return Error::none; // hidden models need no pose, storage or resource lease
    if(visible){
        if(!projection_valid_)return fail(1,Error::invalid_data);
        game::Affine view;std::int32_t xyz[3];
        if(matrix)std::memcpy(&view,matrix,sizeof(view));
        if(scale)std::memcpy(xyz,scale,sizeof(xyz));
        const auto pose_error=game::build_poses_bytes(bindings,model.transforms,model.modelFile->numBones,
            matrix?&view:nullptr,scale?xyz:nullptr,projection_,w.ordinary.data(),w.billboard.data(),w.ordinary.size());
        if(pose_error!=Error::none)return fail(2,pose_error);
        lap(1);
        const auto normal_error=needs_normals?game::build_normal_bases(bindings,model.transforms,model.bones,model.modelFile->numBones,
            matrix?&view:nullptr,w.normal.data(),w.normal.size(),w.material.data()):Error::none;
        if(normal_error!=Error::none)return fail(3,normal_error);
        lap(2);
        const auto lighting_error=needs_lighting?game::build_lighting_bases(bindings,model.transforms,model.bones,model.modelFile->numBones,
            matrix?&view:nullptr,scale?xyz:nullptr,w.material.data(),w.light_ordinary.data(),w.light_billboard.data(),w.light_ordinary.size(),
            needs_normals?w.normal.data():nullptr):Error::none;
        if(lighting_error!=Error::none)return fail(4,lighting_error);
        if(needs_lighting){const auto error=capture_lighting(light_mask,needs_table,w.lights);if(error!=Error::none)return fail(5,error);}
        lap(3);
    }
    const InstanceIdentity identity{owner_,reinterpret_cast<std::uintptr_t>(&model),0};
    const auto appended=fail(6,frame_->append(bindings,{visible?w.ordinary.data():nullptr,visible?w.billboard.data():nullptr,
        visible?bindings.bones:0,w.material.data(),bindings.source_materials,nullptr,0,w.normal.data(),
        w.light_ordinary.data(),w.light_billboard.data(),needs_lighting?&w.lights:nullptr},identity));
    lap(4);
    if(appended==Error::none&&bindings.skins)append_skin(bindings,model,identity);
    lap(5);++stage_profile.models;
    return appended;
}
namespace {
using Mat=SkinMatrix; // columns c0,c1,c2 (3x3) + translation, DS/4096 units
Mat load(const void* transforms,unsigned bone) noexcept {
    std::int32_t raw[12];std::memcpy(raw,static_cast<const char*>(transforms)+bone*48,48);
    Mat out;for(unsigned i=0;i<12;++i)out.m[i]=float(raw[i])/4096.f;return out;
}
void apply(const Mat& a,const float* v,float w,float* out) noexcept {
    for(unsigned k=0;k<3;++k)out[k]=a.m[k]*v[0]+a.m[3+k]*v[1]+a.m[6+k]*v[2]+(w?a.m[9+k]:0.f);
}
bool inverse(const Mat& a,Mat& out) noexcept {
    const float* c=a.m; // column-major 3x3: element(row k, col j)=c[j*3+k]
    const float e00=c[0],e10=c[1],e20=c[2],e01=c[3],e11=c[4],e21=c[5],e02=c[6],e12=c[7],e22=c[8];
    const float det=e00*(e11*e22-e12*e21)-e01*(e10*e22-e12*e20)+e02*(e10*e21-e11*e20);
    if(!(det>1e-12f||det<-1e-12f))return false;
    const float s=1.f/det;
    float i[9]={(e11*e22-e12*e21)*s,(e12*e20-e10*e22)*s,(e10*e21-e11*e20)*s,
                (e02*e21-e01*e22)*s,(e00*e22-e02*e20)*s,(e01*e20-e00*e21)*s,
                (e01*e12-e02*e11)*s,(e02*e10-e00*e12)*s,(e00*e11-e01*e10)*s};
    // i is column-major inverse: element(row k,col j)=i[j*3+k]
    for(unsigned n=0;n<9;++n)out.m[n]=i[n];
    for(unsigned k=0;k<3;++k)out.m[9+k]=-(i[k]*c[9]+i[3+k]*c[10]+i[6+k]*c[11]);
    return true;
}
// Live bone transforms of the model being skinned, converted on first use.
struct SkinPoses {
    ProducerWorkspace& w;const void* transforms;
    const Mat& pose(unsigned bone) noexcept {
        if(!(w.skin_pose_state[bone]&1u)){
            w.skin_pose[bone]=load(transforms,bone);
            w.skin_pose_state[bone]|=1u;
        }
        return w.skin_pose[bone];
    }
    bool invert(unsigned bone) noexcept {
        auto& state=w.skin_pose_state[bone];
        if(!(state&6u))state|=inverse(pose(bone),w.skin_inverse[bone])?2u:4u;
        return bool(state&2u);
    }
};
// The BUILD 133 vertex loop, kept whole: the comparison path of the console
// switch (sin-recortes.txt) and the reference the cuts below are tested against.
void skin_group_reference(const ModelBindings& bindings,const SkinGroup& group,unsigned target,unsigned normal_target,
                          const Mat& to_target,const Mat& to_normal,SkinPoses& poses,NormalVertex* out) noexcept {
    for(std::uint32_t v=0;v<group.count;++v){const auto& s=bindings.skin_vertex[group.first+v];
        auto& o=out[v];const float p[3]={s.x,s.y,s.z};float world[3],local[3];
        if(s.bone==target){o.base.x=s.x;o.base.y=s.y;o.base.z=s.z;}
        else{const auto& bone=poses.pose(s.bone<bindings.bones?s.bone:target);
            apply(bone,p,1,world);apply(to_target,world,1,local);o.base.x=local[0];o.base.y=local[1];o.base.z=local[2];}
        o.base.u=s.u;o.base.v=s.v;o.base.r=s.r;o.base.g=s.g;o.base.b=s.b;o.base.a=255;
        if(s.bone==normal_target){o.x=s.nx;o.y=s.ny;o.z=s.nz;}
        else{const float n[3]={float(s.nx),float(s.ny),float(s.nz)};
            const auto& bone=poses.pose(s.bone<bindings.bones?s.bone:normal_target);
            apply(bone,n,0,world);apply(to_normal,world,0,local);
            const float len=std::sqrt(local[0]*local[0]+local[1]*local[1]+local[2]*local[2]);
            const float scale=len>1e-6f?std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2])/len:0.f;
            auto clamp10=[](float x){return std::int16_t(x>511.f?511:x<-512.f?-512:int(x+(x>=0?.5f:-.5f)));};
            o.x=clamp10(local[0]*scale);o.y=clamp10(local[1]*scale);o.z=clamp10(local[2]*scale);}
        o.uv_source=s.uv_source;
    }
}
// BUILD134 (exact cuts). The vertices of a skin group are a triangle list: a
// vertex shared by several triangles is stored once per triangle, and the
// loop above transformed every copy (four 3x3 products, two roots, one
// divide). Here
//  - a vertex with the bone, position and normal of an earlier vertex of the
//    group copies that vertex's result (the same inputs through the same code);
//  - the position and normal sums of a vertex advance together instead of one
//    sum, one term at a time: on the ARM11's VFP each add waits for the
//    previous add of its own sum, so the other sums fill that wait.
// Every element is computed by the operations of apply(), in apply()'s order:
//   ((m[k]*v0 + m[3+k]*v1) + m[6+k]*v2) + (m[9+k] or 0.f)
// then the length, divide and rounding expressions of the loop above, written
// the same way. The root of the source normal is named before the transform
// (the compiler may still place it after): same operand, an exact integer
// sum, so the same root. No product is merged, reordered or contracted, so
// the bits are the same under any rounding mode
// (tests/native_renderer/producer_cuts.cpp on the host;
// tools/test_native_producer_cuts_arm.py with the console compiler).
inline std::int16_t clamp10(float x) noexcept {return std::int16_t(x>511.f?511:x<-512.f?-512:int(x+(x>=0?.5f:-.5f)));}
inline void skin_position(const Mat& bone,const Mat& to,const SkinVertex& s,NormalVertex& o) noexcept {
    const float* b=bone.m;const float* t=to.m;const float x=s.x,y=s.y,z=s.z;
    float w0=b[0]*x,w1=b[1]*x,w2=b[2]*x;
    w0+=b[3]*y;w1+=b[4]*y;w2+=b[5]*y;
    w0+=b[6]*z;w1+=b[7]*z;w2+=b[8]*z;
    w0+=b[9];w1+=b[10];w2+=b[11];
    float l0=t[0]*w0,l1=t[1]*w0,l2=t[2]*w0;
    l0+=t[3]*w1;l1+=t[4]*w1;l2+=t[5]*w1;
    l0+=t[6]*w2;l1+=t[7]*w2;l2+=t[8]*w2;
    l0+=t[9];l1+=t[10];l2+=t[11];
    o.base.x=l0;o.base.y=l1;o.base.z=l2;
}
inline void skin_normal(const Mat& bone,const Mat& to,const SkinVertex& s,NormalVertex& o) noexcept {
    const float* b=bone.m;const float* t=to.m;const float n0=float(s.nx),n1=float(s.ny),n2=float(s.nz);
    const float source=std::sqrt(n0*n0+n1*n1+n2*n2);
    float w0=b[0]*n0,w1=b[1]*n0,w2=b[2]*n0;
    w0+=b[3]*n1;w1+=b[4]*n1;w2+=b[5]*n1;
    w0+=b[6]*n2;w1+=b[7]*n2;w2+=b[8]*n2;
    w0+=0.f;w1+=0.f;w2+=0.f;
    float l0=t[0]*w0,l1=t[1]*w0,l2=t[2]*w0;
    l0+=t[3]*w1;l1+=t[4]*w1;l2+=t[5]*w1;
    l0+=t[6]*w2;l1+=t[7]*w2;l2+=t[8]*w2;
    l0+=0.f;l1+=0.f;l2+=0.f;
    const float len=std::sqrt(l0*l0+l1*l1+l2*l2);
    const float scale=len>1e-6f?source/len:0.f;
    o.x=clamp10(l0*scale);o.y=clamp10(l1*scale);o.z=clamp10(l2*scale);
}
// skin_position and skin_normal in one block, so their six sums interleave.
inline void skin_both(const Mat& position_bone,const Mat& position_to,const Mat& normal_bone,const Mat& normal_to,
                      const SkinVertex& s,NormalVertex& o) noexcept {
    const float* b=position_bone.m;const float* c=normal_bone.m;
    const float x=s.x,y=s.y,z=s.z,n0=float(s.nx),n1=float(s.ny),n2=float(s.nz);
    const float source=std::sqrt(n0*n0+n1*n1+n2*n2);
    float w0=b[0]*x,w1=b[1]*x,w2=b[2]*x,v0=c[0]*n0,v1=c[1]*n0,v2=c[2]*n0;
    w0+=b[3]*y;w1+=b[4]*y;w2+=b[5]*y;v0+=c[3]*n1;v1+=c[4]*n1;v2+=c[5]*n1;
    w0+=b[6]*z;w1+=b[7]*z;w2+=b[8]*z;v0+=c[6]*n2;v1+=c[7]*n2;v2+=c[8]*n2;
    w0+=b[9];w1+=b[10];w2+=b[11];v0+=0.f;v1+=0.f;v2+=0.f;
    const float* t=position_to.m;const float* u=normal_to.m;
    float l0=t[0]*w0,l1=t[1]*w0,l2=t[2]*w0,m0=u[0]*v0,m1=u[1]*v0,m2=u[2]*v0;
    l0+=t[3]*w1;l1+=t[4]*w1;l2+=t[5]*w1;m0+=u[3]*v1;m1+=u[4]*v1;m2+=u[5]*v1;
    l0+=t[6]*w2;l1+=t[7]*w2;l2+=t[8]*w2;m0+=u[6]*v2;m1+=u[7]*v2;m2+=u[8]*v2;
    l0+=t[9];l1+=t[10];l2+=t[11];m0+=0.f;m1+=0.f;m2+=0.f;
    const float len=std::sqrt(m0*m0+m1*m1+m2*m2);
    const float scale=len>1e-6f?source/len:0.f;
    o.base.x=l0;o.base.y=l1;o.base.z=l2;
    o.x=clamp10(m0*scale);o.y=clamp10(m1*scale);o.z=clamp10(m2*scale);
}
static_assert(offsetof(SkinVertex,x)==0&&offsetof(SkinVertex,y)==4&&offsetof(SkinVertex,z)==8);
// Transform inputs of a vertex: bone, position bits, normal. Bit patterns, so
// -0 and 0 or two NaNs are only equal when the old loop saw the same bytes.
inline std::uint32_t skin_hash(const SkinVertex& s) noexcept {
    std::uint32_t p[3];std::memcpy(p,reinterpret_cast<const unsigned char*>(&s),12);
    std::uint32_t h=p[0]^((p[1]<<11)|(p[1]>>21))^((p[2]<<22)|(p[2]>>10));
    h^=std::uint32_t(s.bone)<<3;h^=std::uint32_t(std::uint16_t(s.nx))<<6;
    h^=std::uint32_t(std::uint16_t(s.ny))<<16;h^=std::uint32_t(std::uint16_t(s.nz))<<26;
    return h*2654435761u;
}
inline bool skin_same(const SkinVertex& a,const SkinVertex& b) noexcept {
    return a.bone==b.bone&&a.nx==b.nx&&a.ny==b.ny&&a.nz==b.nz&&
        std::memcmp(reinterpret_cast<const unsigned char*>(&a),reinterpret_cast<const unsigned char*>(&b),12)==0;
}
constexpr unsigned skin_slots=512;
// `slots` is caller scratch (skin_slots entries); a slot names the last vertex
// of this group with its hash, plus one.
void skin_group(const ModelBindings& bindings,const SkinGroup& group,unsigned target,unsigned normal_target,
                const Mat& to_target,const Mat& to_normal,SkinPoses& poses,NormalVertex* out,std::uint16_t* slots) noexcept {
    if(group.count>65535u){skin_group_reference(bindings,group,target,normal_target,to_target,to_normal,poses,out);return;}
    unsigned size=16,shift=28;
    while(size<group.count*2u&&size<skin_slots){size<<=1;--shift;}
    std::memset(slots,0,size*sizeof(std::uint16_t));
    const SkinVertex* const source=bindings.skin_vertex+group.first;
    for(std::uint32_t v=0;v<group.count;++v){const auto& s=source[v];auto& o=out[v];
        const bool move_position=s.bone!=target,move_normal=s.bone!=normal_target;
        if(!move_position&&!move_normal){
            o.base.x=s.x;o.base.y=s.y;o.base.z=s.z;o.x=s.nx;o.y=s.ny;o.z=s.nz;
        }else{
            auto& slot=slots[skin_hash(s)>>shift];
            if(slot&&skin_same(s,source[slot-1u])){
                const auto& twin=out[slot-1u];
                std::memcpy(&o.base.x,&twin.base.x,4);std::memcpy(&o.base.y,&twin.base.y,4);std::memcpy(&o.base.z,&twin.base.z,4);
                o.x=twin.x;o.y=twin.y;o.z=twin.z;
            }else{
                slot=std::uint16_t(v+1u);
                if(move_position&&move_normal)
                    skin_both(poses.pose(s.bone<bindings.bones?s.bone:target),to_target,
                              poses.pose(s.bone<bindings.bones?s.bone:normal_target),to_normal,s,o);
                else if(move_position){
                    skin_position(poses.pose(s.bone<bindings.bones?s.bone:target),to_target,s,o);
                    o.x=s.nx;o.y=s.ny;o.z=s.nz;
                }else{
                    o.base.x=s.x;o.base.y=s.y;o.base.z=s.z;
                    skin_normal(poses.pose(s.bone<bindings.bones?s.bone:normal_target),to_normal,s,o);
                }
            }
        }
        o.base.u=s.u;o.base.v=s.v;o.base.r=s.r;o.base.g=s.g;o.base.b=s.b;o.base.a=255;
        o.uv_source=s.uv_source;
    }
}
// BUILD134 (exact cut): a skinned actor whose bones did not move produces the
// vertices of its previous capture: the output of the loops above depends
// only on the definition, the visible groups and the bone transforms. Kept
// here, in memory only the game thread touches (never a frame store, which
// the render worker may still be reading):
//  - the bone transforms each skinned actor was last captured with;
//  - once they repeat, the vertices they produced.
// A later capture with byte-identical transforms copies those vertices. Full
// storage forgets everything; the next captures compute as usual.
struct SkinKept {
    const ModelBindings* bindings=nullptr;const void* component=nullptr;
    std::uint32_t pose=0,pose_bytes=0,vertex=0,vertices=0,used=0;
    std::uint64_t groups=0; // groups whose vertices are kept
    bool recorded=false;
};
constexpr unsigned skin_kept_actors=32,skin_kept_vertices=2048,skin_kept_pose_bytes=16384,skin_kept_groups=64;
std::array<SkinKept,skin_kept_actors> skin_kept{};
std::array<NormalVertex,skin_kept_vertices> skin_kept_vertex{};
alignas(4) std::array<unsigned char,skin_kept_pose_bytes> skin_kept_pose{};
std::uint32_t skin_kept_used=0,skin_kept_vertex_used=0,skin_kept_pose_used=0,skin_update=0;
void skin_forget() noexcept {skin_kept_used=skin_kept_vertex_used=skin_kept_pose_used=0;}
// Storage is full. An actor that stopped being captured (another level, a
// removed object) frees it all; while every kept actor is live, nothing moves
// and the newcomer simply keeps computing.
bool skin_reclaim() noexcept {
    for(unsigned i=0;i<skin_kept_used;++i)if(skin_update-skin_kept[i].used>4u){skin_forget();return true;}
    return false;
}
// The kept vertices of this actor when its bones are those of its previous
// capture (null otherwise). Records the bones it is called with.
SkinKept* skin_repeated(const ModelBindings& bindings,const void* component,const void* transforms) noexcept {
    const std::size_t pose_bytes=std::size_t(bindings.bones)*48u;
    if(bindings.skins>skin_kept_groups||pose_bytes>skin_kept_pose_bytes)return nullptr;
    SkinKept* kept=nullptr;
    for(unsigned i=0;i<skin_kept_used&&!kept;++i)
        if(skin_kept[i].component==component&&skin_kept[i].bindings==&bindings)kept=&skin_kept[i];
    if(kept&&kept->pose_bytes!=pose_bytes){skin_forget();kept=nullptr;} // not the definition it was made from
    if(!kept){
        if((skin_kept_used==skin_kept_actors||pose_bytes>skin_kept_pose_bytes-skin_kept_pose_used)&&!skin_reclaim())return nullptr;
        kept=&skin_kept[skin_kept_used++];*kept={};kept->bindings=&bindings;kept->component=component;
        kept->pose=skin_kept_pose_used;kept->pose_bytes=std::uint32_t(pose_bytes);skin_kept_pose_used+=std::uint32_t(pose_bytes);
        kept->used=skin_update;std::memcpy(skin_kept_pose.data()+kept->pose,transforms,pose_bytes);
        return nullptr;
    }
    kept->used=skin_update;
    if(std::memcmp(skin_kept_pose.data()+kept->pose,transforms,pose_bytes)){
        std::memcpy(skin_kept_pose.data()+kept->pose,transforms,pose_bytes);
        kept->groups=0;kept->recorded=false; // its vertex span waits for the next repeated pose
        return nullptr;
    }
    if(!kept->recorded){
        // Second capture with these bones: keep what this one produces.
        std::uint32_t total=0;
        for(unsigned g=0;g<bindings.skins;++g){
            if(bindings.skin[g].count>skin_kept_vertices-total)return nullptr;
            total+=bindings.skin[g].count;}
        if(kept->vertices!=total){
            if(total>skin_kept_vertices-skin_kept_vertex_used){skin_reclaim();return nullptr;}
            kept->vertex=skin_kept_vertex_used;kept->vertices=total;skin_kept_vertex_used+=total;
        }
        kept->groups=0;kept->recorded=true;
    }
    return kept;
}
void note_skin_update() noexcept {++skin_update;}
}
void forget_skin_reuse() noexcept {skin_forget();}
void SceneProducer::append_skin(const ModelBindings& bindings,const ModelComponents& model,InstanceIdentity identity) noexcept {
    // PLAYABLE MODE: triangles spanning bones. Re-express each vertex from its
    // own bone into the space of the part's bone with the LIVE bone transforms
    // (exact for the current pose), normals into the part's lighting bone. The
    // renderer then draws them with the part's own material/lighting/UV state.
    const auto* parts=frame_->last_model_parts();if(!parts||!model.transforms)return;
    const auto first_part=frame_->last_model_first_part();
#ifdef __3DS__
    // Two clock system calls for each skinned model: only when timings are on,
    // like every other producer clock (set_stage_profile).
    struct Timer{bool on;std::uint64_t s;explicit Timer(bool enabled):on(enabled),s(enabled?svcGetSystemTick():0){}
        ~Timer(){if(on)skin_ticks+=svcGetSystemTick()-s;}} timer(profile_stages);
#endif
    auto& w=workspace_;
    w.skin_pose_state.fill(0);
    SkinPoses poses{w,model.transforms};
    const bool cut=sm64ds::perf::cuts();
    // Null, or this actor's kept vertices for exactly these bones.
    SkinKept* const kept=cut?skin_repeated(bindings,&model,model.transforms):nullptr;
    std::uint32_t kept_at=0;
    std::uint16_t slots[skin_slots];
    for(unsigned g=0;g<bindings.skins;kept_at+=bindings.skin[g].count,++g){const auto& group=bindings.skin[g];
        if(group.part>=bindings.parts||group.count%3||!parts[group.part].visible)continue;
        const auto& binding=bindings.part[group.part];if(binding.billboard)continue;
        const unsigned target=binding.bone,normal_target=binding.lighting_bone!=0xffffu?binding.lighting_bone:target;
        if(target>=bindings.bones||normal_target>=bindings.bones)continue;
        const bool reuse=kept&&((kept->groups>>g)&1u);
        // A kept group passed these inversions with the same bones.
        if(!reuse&&(!poses.invert(target)||!poses.invert(normal_target)))continue;
        std::size_t capacity=0;auto* out=frame_->skin_workspace(capacity);
        if(!out||capacity<group.count)return;
        if(reuse)std::memcpy(static_cast<void*>(out),skin_kept_vertex.data()+kept->vertex+kept_at,group.count*sizeof(NormalVertex));
        else{
            const auto& to_target=w.skin_inverse[target];
            const auto& to_normal=w.skin_inverse[normal_target];
            if(cut)skin_group(bindings,group,target,normal_target,to_target,to_normal,poses,out,slots);
            else skin_group_reference(bindings,group,target,normal_target,to_target,to_normal,poses,out);
            if(kept){
                std::memcpy(static_cast<void*>(skin_kept_vertex.data()+kept->vertex+kept_at),out,group.count*sizeof(NormalVertex));
                kept->groups|=std::uint64_t(1)<<g;
            }
        }
        identity.piece=g;
        frame_->append_skin(bindings.file_id,first_part+group.part,out,group.count,identity);
    }
}
Error SceneProducer::capture_shadow(const ModelComponents& model,const Matrix4x3* matrix,unsigned shape) noexcept {
    if(frame_->phase()!=SceneFrame::Phase::capturing)return Error::invalid_phase;
    if(!model.materials||!model.transforms||!matrix)return Error::invalid_data;
    // The material's POLYGON_ATTR as RenderAll left it for this call: polygon
    // ID 0 is the mask draw, any other the render draw. One volume per pair.
    std::uint32_t polygon;std::memcpy(&polygon,reinterpret_cast<const char*>(model.materials)+0x24,4);
    const unsigned id=(polygon>>24)&63u,opacity=(polygon>>16)&31u;
    if(!id||!opacity)return Error::none;
    if(!projection_valid_)return fail(1,Error::invalid_data);
    game::Affine view,bone;std::memcpy(&view,matrix,sizeof view);std::memcpy(&bone,model.transforms,sizeof bone);
    ShadowVolume volume;volume.opacity=std::uint8_t(opacity);
    volume.shape=shape?ShadowVolume::Shape::cuboid:ShadowVolume::Shape::cylinder;
    const auto pose=game::compose(bone,view);
    for(unsigned i=0;i<12;++i)volume.pose[i]=float(pose.value[i])/4096.f;
    return fail(6,frame_->append_shadow(volume,projection_));
}
bool SceneProducer::submit_shadow(const ModelComponents& model,const Matrix4x3* matrix,unsigned shape,void* user) noexcept {
    if(!user)return false;
    auto& producer=*static_cast<SceneProducer*>(user);
    // A volume that cannot be drawn costs its shadow, not the image.
    producer.capture_shadow(model,matrix,shape);return true;
}
bool SceneProducer::submit(const ModelBindings& bindings,const ModelComponents& model,const Matrix4x3* matrix,const Vector3* scale,void* user) noexcept {
    if(!user)return false;
    auto& producer=*static_cast<SceneProducer*>(user);
    const auto error=producer.capture(bindings,model,matrix,scale);producer.frame_->invalidate(error);return error==Error::none;
}
}
