// BUILD134 CPU cost cuts of the render worker against the BUILD 133 code
// they replace: same results, same bytes for the GPU.
//  1. LightingCache::prepare packed in place, against its reference path:
//     the same changes and, over a model of the uploaded uniform rows, the
//     same rows after every draw.
//  2. NativeBatchPlan with borrowed matrices/light sets, against the plan
//     that copied them (kept here): same spans, same values.
//  3. The single-pass skin upload against validate() followed by the copy.
//  4. validate_quads with and without the UV domain.
//  5. validate_prepared's shortcut: in a palette whose every state resolved,
//     resolve_lighting can refuse a part only for its index.
#include "native_batch.hpp"
#include "lighting_palette.hpp"
#include "native_quads.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
using namespace sm64ds::render;
namespace {
std::uint32_t random_word(){static std::uint32_t v=0x6a09e667;v^=v<<13;v^=v>>17;v^=v<<5;return v;}

// ---------------------------------------------------------------- lighting
// What Citro3DBackend::draw_impl sends for a lit draw.
struct Rows {
    std::array<std::array<float,4>,12> normal{};
    std::array<std::array<float,4>,4> direction{},colour{},material{};
    std::array<std::array<float,4>,32> shininess{};
    unsigned count=~0u;bool lights=false,specular=false,table=false,toon=false,grey=false;
    bool operator==(const Rows& b) const {
        return normal==b.normal&&direction==b.direction&&colour==b.colour&&material==b.material&&shininess==b.shininess&&
            count==b.count&&lights==b.lights&&specular==b.specular&&table==b.table&&toon==b.toon&&grey==b.grey;
    }
};
void upload(Rows& gpu,const LightingCache& cache,const LightingChanges& c,bool palette){
    const auto& u=cache.uniforms();
    if(!palette&&c.normal)gpu.normal=u.normal;
    if(c.direction)gpu.direction=u.direction;
    if(c.colour)gpu.colour=u.colour;
    if(c.material)gpu.material=u.material;
    if(c.shininess)gpu.shininess=u.shininess;
    if(c.switches){gpu.lights=bool(u.count);gpu.specular=u.specular;gpu.table=u.table;gpu.toon=u.toon;gpu.grey=u.grey;}
    if(c.count)gpu.count=u.count;
}
void lighting(){
    std::array<LightSet,6> sets{};
    const auto fill=[&](LightSet& s){
        for(auto& light:s.light){for(auto& d:light.direction)d=std::int16_t(int(random_word()%2048)-1024);
            for(auto& c:light.colour)c=std::uint8_t(random_word()%32);
            light.specular_reciprocal=random_word()%4?std::int32_t(random_word()%131073):random_word()%2?262144:-std::int32_t(random_word()%262145);}
        for(auto& v:s.shininess)v=std::uint8_t(random_word());for(auto& v:s.toon)v=std::uint16_t(random_word()&0x7fff);
        s.known=random_word()%8?15:std::uint8_t(random_word()&15);s.table_known=random_word()%8!=0;s.toon_known=random_word()%8!=0;
    };
    for(auto& s:sets)fill(s);
    std::array<LightingState::Basis,5> bases{};
    for(unsigned b=1;b<bases.size();++b)for(auto& v:bases[b])v=std::int32_t(random_word());
    LightingCache reference,cut;reference.set_reference(true);
    Rows gpu_reference,gpu_cut;
    unsigned accepted=0,refused=0,palette_draws=0,unchanged=0;LightingInput last;
    for(unsigned step=0;step<400000;++step){
        const auto op=random_word()%64;
        if(op==0){reference.invalidate();cut.invalidate();continue;} // a pass or a program switch
        if(op==1){fill(sets[random_word()%sets.size()]);continue;}   // the next image's lights
        if(op==2){for(auto& v:bases[1+random_word()%(bases.size()-1)])v=std::int32_t(random_word());continue;}
        LightingInput input;auto& m=input.material;
        // Mostly the materials of a real image: a few masks, sometimes the
        // table, toon or grey; rarely values that validation must refuse.
        m.mask=std::uint8_t(random_word()%6?(random_word()&1?1:random_word()&15):0);
        m.diffuse=std::uint16_t(random_word()%4?0x7fff:random_word()&0x7fff);m.ambient=std::uint16_t(random_word()%4?0x2108:random_word()&0x7fff);
        m.specular=std::uint16_t(random_word()%3?0:random_word()&0x7fff);m.emission=std::uint16_t(random_word()%4?0:random_word()&0x7fff);
        m.use_shininess=random_word()%4==0;m.toon=random_word()%12==0;m.grey=random_word()%16==0;
        if(random_word()%512==0)m.diffuse=0x8000;
        if(random_word()%512==0)m.mask=16;
        input.lights=m.mask||random_word()%4==0?&sets[random_word()%(random_word()%3?2:sets.size())]:nullptr;
        if(random_word()%256==0)input.lights=nullptr;
        // The usual run: the parts of one model share material and lights and
        // differ in their bone's basis, or in nothing.
        if(random_word()%4){input.lights=last.lights;input.material=last.material;}
        // A batch draw passes no basis; allow any, the contract does not care.
        const bool palette=random_word()%4==0;
        input.normal=palette&&random_word()%4?bases[0]:random_word()%3?last.normal:bases[random_word()%bases.size()];
        last=input;
        LightingChanges a,b;a.packed=b.packed=true;
        const auto ea=reference.prepare(input,a,palette),eb=cut.prepare(input,b,palette);
        assert(ea==eb);
        if(ea!=Error::none){assert(a.packed&&b.packed);++refused;continue;} // untouched
        ++accepted;palette_draws+=palette;
        assert(a.direction==b.direction&&a.colour==b.colour&&a.material==b.material&&a.shininess==b.shininess&&
            a.switches==b.switches&&a.count==b.count&&a.packed==b.packed);
        if(palette)assert(!b.normal);else assert(a.normal==b.normal);
        unchanged+=!b.direction&&!b.colour&&!b.material&&!b.shininess&&!b.switches&&!b.count;
        const auto& ua=reference.uniforms();const auto& ub=cut.uniforms();
        assert(ua.count==ub.count&&ua.specular==ub.specular&&ua.table==ub.table&&ua.toon==ub.toon&&ua.grey==ub.grey);
        assert(ua.direction==ub.direction&&ua.colour==ub.colour&&ua.material==ub.material);
        if(!palette)assert(ua.normal==ub.normal);
        if(ua.table||ua.toon)assert(ua.shininess==ub.shininess);
        upload(gpu_reference,reference,a,palette);upload(gpu_cut,cut,b,palette);
        // A palette draw never sends the normal rows: the reference's flag
        // for them is dropped by the backend, the cut does not compute it.
        assert(gpu_reference==gpu_cut);
    }
    assert(accepted>200000&&refused>1000&&palette_draws>50000&&unchanged>20000);
    std::printf("PASS lighting cache: %u draws (%u palette, %u refused): same changes, same uploaded rows\n",accepted,palette_draws,refused);
}

// -------------------------------------------------------------- batch plan
// NativeBatchPlan of BUILD 133: every span owns copies.
struct OwnedSpan {
    Mesh mesh{};Material material{};std::array<Matrix,8> palette{};std::array<BatchBasis,8> basis{};
    LightSet lights{};LightingMaterial lighting{};std::uint32_t first=0,count=0;std::uint8_t used=0;bool lit=false;
};
struct OwnedPlan {
    std::vector<OwnedSpan> span=std::vector<OwnedSpan>(NativeBatchPlan::max_spans);
    std::size_t spans=0;bool open=false,sealed=false;Error error=Error::invalid_phase;
    void reset(){spans=0;open=sealed=false;error=Error::none;}
    void barrier(){open=false;}
    static bool same_lighting(const OwnedSpan& span,const LightingInput* input){
        if(span.lit!=bool(input))return false;
        if(!input)return true;
        if(!(span.lighting==input->material))return false;
        for(unsigned i=0;i<4;++i)
            if((input->material.mask&(1u<<i))&&!(span.lights.light[i]==input->lights->light[i]))return false;
        return true;
    }
    Error append(BatchRange range,const Material& material,const Matrix& matrix,const LightingInput* lighting,unsigned& output){
        if(sealed||error!=Error::none)return error==Error::none?Error::invalid_phase:error;
        if(!range||range.slot>=8||range.first%3||range.count%3||range.count>std::numeric_limits<std::uint32_t>::max()-range.first)
            return error=Error::invalid_data;
        if(material.uv_mode!=UvMode::affine||!NativeBatchPlan::supports(lighting))return Error::unsupported;
        BatchBasis basis{};
        if(lighting&&lighting->material.mask)for(unsigned row=0;row<3;++row)for(unsigned axis=0;axis<3;++axis)
            basis[row][axis]=std::int16_t(lighting->normal[axis*3+row]);
        const auto bit=std::uint8_t(1u<<range.slot);
        bool compatible=open&&span[spans-1].mesh==range.mesh;
        if(compatible){const auto& group=span[spans-1];
            compatible=group.first+group.count==range.first&&group.material==material&&same_lighting(group,lighting)&&
                (!(group.used&bit)||(!std::memcmp(&group.palette[range.slot],&matrix,sizeof(matrix))&&group.basis[range.slot]==basis));
        }
        if(!compatible){
            if(spans==NativeBatchPlan::max_spans)return error=Error::capacity;
            auto& group=span[spans++];group.mesh=range.mesh;group.material=material;
            group.lit=bool(lighting);group.lighting=lighting?lighting->material:LightingMaterial{};
            group.lights=lighting&&lighting->lights?*lighting->lights:LightSet{};
            group.first=range.first;group.count=0;group.used=0;open=true;
        }
        auto& group=span[spans-1];group.palette[range.slot]=matrix;group.basis[range.slot]=basis;
        group.used|=bit;group.count+=range.count;output=unsigned(spans-1);return Error::none;
    }
    Error seal(){
        if(sealed)return Error::invalid_phase;
        open=false;
        if(error!=Error::none){spans=0;return error;}
        sealed=true;return Error::none;
    }
};
void batches(){
    static NativeBatchPlan plan;OwnedPlan owned;
    int domain;const Mesh meshes[3]={{1,1,&domain},{2,2,&domain},{3,1,&domain}};
    // The leased frame: a few distinct poses, each stored at several addresses.
    std::array<Matrix,24> matrix;std::array<LightSet,6> lights{};std::array<LightingInput,12> lit{};
    std::array<Material,5> material{};
    unsigned frames=0,appends=0,spans=0,shared=0;
    for(unsigned frame=0;frame<4000;++frame){
        for(unsigned i=0;i<matrix.size();++i){matrix[i]=identity();matrix[i].row[0][3]=float(i%8);matrix[i].row[2][1]=float(frame%5);}
        for(unsigned i=0;i<lights.size();++i){lights[i]={};lights[i].known=15;lights[i].light[0].colour[0]=std::uint8_t(i%3);
            lights[i].light[1].direction[2]=std::int16_t(i%2);lights[i].light[3].specular_reciprocal=std::int32_t(random_word());}
        for(unsigned i=0;i<lit.size();++i){auto& input=lit[i];input={};
            input.material.mask=std::uint8_t(i%4==3?0:1u<<(i%3));input.material.diffuse=std::uint16_t(i%2?0x7fff:0x1234);
            input.material.toon=i==11;input.material.use_shininess=i==10;input.material.specular=std::uint16_t(i==10?31:0);
            input.lights=input.material.mask?&lights[i%lights.size()]:i%2?&lights[0]:nullptr;
            for(auto& v:input.normal)v=std::int32_t(random_word()%8==0?random_word():(random_word()%5)-2);}
        for(unsigned i=0;i<material.size();++i){material[i]={};material[i].tint=0xffffff00u|i;material[i].blend=i%2?Blend::alpha:Blend::opaque;
            material[i].texture={i/2+1u,std::uint16_t(i/2),&domain};}
        material[4].uv_mode=UvMode::fixed;
        plan.reset();owned.reset();
        std::uint32_t next[3]={0,0,0};
        for(unsigned n=random_word()%400;n--;){
            if(random_word()%24==0){plan.barrier();owned.barrier();continue;}
            const unsigned mesh=random_word()%8?0:random_word()%3;
            BatchRange range{meshes[mesh],next[mesh],(random_word()%4+1)*3,std::uint8_t(random_word()%8)};
            if(random_word()%16==0)range.first+=3;              // a hidden part's hole
            if(random_word()%2048==0)range.slot=8;              // invalid: poisons both plans
            if(random_word()%2048==0)range.count=0;             // an empty range
            const auto& m=material[random_word()%16?random_word()%2:random_word()%material.size()];
            const auto* pose=&matrix[random_word()%matrix.size()];
            const LightingInput* input=random_word()%3?&lit[random_word()%lit.size()]:nullptr;
            unsigned a=~0u,b=~0u;
            const auto ea=owned.append(range,m,*pose,input,a),eb=plan.append(range,m,pose,input,b);
            assert(ea==eb&&a==b);++appends;
            if(ea==Error::none)next[mesh]=range.first+range.count;
            else if(ea==Error::unsupported&&random_word()&1){plan.barrier();owned.barrier();} // the consumer's ordinary draw
        }
        const auto ea=owned.seal(),eb=plan.seal();assert(ea==eb);
        if(ea!=Error::none){assert(!plan.ready()&&plan.spans()==0&&owned.spans==0);continue;}
        ++frames;assert(plan.spans()==owned.spans);
        for(unsigned i=0;i<owned.spans;++i){const auto& a=owned.span[i];const auto& b=*plan.span(i);++spans;
            assert(a.mesh==b.mesh&&a.material==b.material&&a.first==b.first&&a.count==b.count&&a.used==b.used&&a.lit==b.lit&&a.lighting==b.lighting);
            for(unsigned slot=0;slot<8;++slot)if(a.used&(1u<<slot)){
                assert(!std::memcmp(&a.palette[slot],b.palette[slot],sizeof(Matrix))&&a.basis[slot]==b.basis[slot]);
                shared+=b.palette[slot]>=matrix.data()&&b.palette[slot]<matrix.data()+matrix.size();
            }
            // What draw_batch hands to the lighting cache.
            assert(bool(a.lighting.mask)==bool(b.lights));
            for(unsigned l=0;l<4;++l)if(a.lighting.mask&(1u<<l))assert(a.lights.light[l]==b.lights->light[l]);
        }
    }
    assert(frames>3000&&appends>500000&&spans>100000&&shared>=spans);
    std::printf("PASS batch plan: %u plans, %u appends, %u spans equal to the copying plan (%zu -> %zu bytes per span)\n",
        frames,appends,spans,sizeof(OwnedSpan),sizeof(BatchSpan));
}

// ------------------------------------------------------------ skin + quads
bool equal(const UvDomain& a,const UvDomain& b){ // bit patterns, not padding
    return !std::memcmp(a.low,b.low,sizeof(a.low))&&!std::memcmp(a.high,b.high,sizeof(a.high))&&
        a.fractional_bits[0]==b.fractional_bits[0]&&a.fractional_bits[1]==b.fractional_bits[1];
}
void streams(){
    std::vector<NormalVertex> source(3*400);std::vector<BatchVertex> packed(source.size()),expected(source.size());
    std::vector<NormalVertex> copied(source.size());std::vector<std::uint8_t> selectors(source.size());
    unsigned valid_runs=0,invalid_runs=0;
    for(unsigned run=0;run<20000;++run){
        const std::size_t count=(random_word()%401)*3;
        for(std::size_t i=0;i<count;++i){auto& v=source[i];
            v.base={float(int(random_word()%4096)-2048)/16,float(int(random_word()%4096)-2048)/16,float(int(random_word()%4096)-2048)/16,
                float(int(random_word()%65536)-32768)/float(1u<<(random_word()%12)),float(int(random_word()%65536)-32768)/float(1u<<(random_word()%12)),
                std::uint8_t(random_word()),std::uint8_t(random_word()),std::uint8_t(random_word()),std::uint8_t(random_word())};
            v.x=std::int16_t(int(random_word()%1024)-512);v.y=std::int16_t(int(random_word()%1024)-512);v.z=std::int16_t(int(random_word()%1024)-512);
            v.uv_source=std::int16_t(random_word()%3);selectors[i]=std::uint8_t(random_word()%8);
        }
        if(count&&random_word()%3==0){auto& v=source[random_word()%count];
            switch(random_word()%8){
            case 0:v.base.x=std::numeric_limits<float>::infinity();break;
            case 1:v.base.y=std::numeric_limits<float>::quiet_NaN();break;
            case 2:v.base.z=-std::numeric_limits<float>::infinity();break;
            case 3:v.base.u=std::numeric_limits<float>::quiet_NaN();break;
            case 4:v.base.v=std::numeric_limits<float>::infinity();break;
            case 5:v.x=512;break;
            case 6:v.z=-513;break;
            default:v.uv_source=std::int16_t(random_word()&1?3:-1);break;}
        }
        const auto* selector=random_word()%4?selectors.data():nullptr;
        // BUILD 133 update_skin_stream.
        UvDomain reference;bool reference_valid=true;
        if(count&&validate(source.data(),count,&reference)!=Error::none){reference={};reference_valid=false;}
        std::memset(static_cast<void*>(expected.data()),0xa5,expected.size()*sizeof(BatchVertex));
        for(std::size_t i=0;i<count;++i){expected[i].source=source[i];
            const auto rows=packed_palette_rows(selector?selector[i]:0);std::memcpy(expected[i].rows.data(),&rows,sizeof(rows));}
        std::memset(static_cast<void*>(packed.data()),0xa5,packed.size()*sizeof(BatchVertex));
        UvDomain domain;bool valid=pack_skin_vertices(source.data(),count,selector,packed.data(),domain);if(!valid)domain={};
        assert(valid==reference_valid&&equal(domain,reference));
        assert(!std::memcmp(static_cast<const void*>(packed.data()),static_cast<const void*>(expected.data()),packed.size()*sizeof(BatchVertex)));
        std::memset(static_cast<void*>(copied.data()),0x5a,copied.size()*sizeof(NormalVertex));
        UvDomain plain;valid=copy_skin_vertices(source.data(),count,copied.data(),plain);if(!valid)plain={};
        assert(valid==reference_valid&&equal(plain,reference));
        assert(!count||!std::memcmp(static_cast<const void*>(copied.data()),static_cast<const void*>(source.data()),count*sizeof(NormalVertex)));
        for(std::size_t i=count*sizeof(NormalVertex);i<copied.size()*sizeof(NormalVertex);++i)
            assert(reinterpret_cast<const unsigned char*>(copied.data())[i]==0x5a);
        (reference_valid?valid_runs:invalid_runs)++;
    }
    assert(valid_runs>5000&&invalid_runs>3000);
    std::vector<Vertex> quads(4*64);unsigned accepted=0,refused=0;
    for(unsigned run=0;run<20000;++run){
        const std::size_t count=(random_word()%64+1)*4;UvDomain expected_domain;
        for(std::size_t i=0;i<count;++i){auto& v=quads[i];
            v={float(random_word()%512),float(random_word()%512),0,float(int(random_word()%65536)-32768)/float(1u<<(random_word()%12)),
               float(int(random_word()%65536)-32768)/float(1u<<(random_word()%12)),255,255,255,255};}
        if(random_word()%4==0)quads[random_word()%count].u=std::numeric_limits<float>::quiet_NaN();
        for(std::size_t i=0;i<count;++i)expected_domain.include(quads[i].u,quads[i].v); // BUILD 133's unconditional loop
        UvDomain domain;domain.low[0]=-77;domain.high[1]=91;domain.fractional_bits[0]=13;const auto untouched=domain;
        const auto without=validate_quads(quads.data(),count),with=validate_quads(quads.data(),count,&domain);
        assert(without==with);
        if(with==Error::none){assert(equal(domain,expected_domain));++accepted;}
        else{assert(equal(domain,untouched));++refused;}
    }
    assert(accepted>10000&&refused>3000);
    std::printf("PASS dynamic streams: %u valid and %u lenient skin uploads byte-equal to validate+copy; quad domain equal\n",valid_runs,invalid_runs);
}

// ----------------------------------------------------- resolved palettes
void palettes(){
    std::array<LightSet,8> lights{};std::array<PreparedLighting,64> states{};
    unsigned resolved=0,refused=0,probes=0;
    for(unsigned run=0;run<60000;++run){
        for(auto& s:lights){s={};s.known=random_word()%32?15:std::uint8_t(random_word()&15);s.table_known=random_word()%16!=0;s.toon_known=random_word()%16!=0;
            for(auto& light:s.light){for(auto& d:light.direction)d=std::int16_t(int(random_word()%2048)-1024);
                for(auto& c:light.colour)c=std::uint8_t(random_word()%32);light.specular_reciprocal=std::int32_t(random_word()%131073);}}
        LightingPaletteView view;view.lights=lights.data();view.state=states.data();
        view.light_count=random_word()%(lights.size()+1);view.state_count=random_word()%(states.size()+1);
        for(std::size_t i=0;i<view.state_count;++i){auto& s=states[i];s={};
            s.material.mask=std::uint8_t(random_word()%4?random_word()&15:0);s.material.diffuse=std::uint16_t(random_word()&0x7fff);
            s.material.specular=std::uint16_t(random_word()%4?0:31);s.material.use_shininess=random_word()%8==0;s.material.toon=random_word()%64==0;
            s.lights=s.material.mask?std::uint16_t(random_word()%(view.light_count?view.light_count:1)):no_lighting;
            if(random_word()%128==0)s.lights=std::uint16_t(random_word()%12);}
        if(random_word()%64==0)view.state=nullptr;
        if(random_word()%64==0)view.light_count=LightingPalette::max_lights+1;
        // SceneRenderer::draw_frame: every state, before any model is validated.
        bool all=view.state_count<=LightingPalette::max_states;
        for(std::size_t i=0;all&&i<view.state_count;++i){LightingInput input;all=resolve_lighting(view,std::uint16_t(i),input)==Error::none;}
        if(!all){++refused;continue;}
        ++resolved;
        for(unsigned probe=0;probe<24;++probe){
            const std::uint16_t id=probe<2?std::uint16_t(view.state_count+probe):probe==2?std::uint16_t(0xfffe):std::uint16_t(random_word()%(states.size()+8));
            LightingInput input;const auto error=resolve_lighting(view,id,input);++probes;
            // NativeModel::validate_prepared(...,palette_resolved=true).
            assert(error==(id>=view.state_count?Error::invalid_data:Error::none));
        }
    }
    assert(resolved>5000&&refused>5000);
    std::printf("PASS resolved palettes: %u palettes, %u part indices: the index decides\n",resolved,probes);
}
}
int main(){
    lighting();batches();streams();palettes();
}
