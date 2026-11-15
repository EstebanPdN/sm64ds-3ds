#include "lighting_uniforms.hpp"
namespace sm64ds::render {
namespace {
void pack_normal(const LightingState::Basis& normal,std::array<std::array<float,4>,12>& rows) noexcept {
    for(unsigned digit=0;digit<4;++digit)for(unsigned row=0;row<3;++row)for(unsigned axis=0;axis<3;++axis)
        rows[digit*3+row][axis]=float((std::uint32_t(normal[axis*3+row])>>(digit*6))&(digit==3?31u:63u));
}
// In place: store the value and report whether the row held another one.
// The comparison is the float comparison the reference applies to whole banks.
inline void put(float& held,float value,bool& changed) noexcept {if(held!=value){held=value;changed=true;}}
bool repack_normal(const LightingState::Basis& normal,std::array<std::array<float,4>,12>& rows) noexcept {
    bool changed=false;
    for(unsigned digit=0;digit<4;++digit)for(unsigned row=0;row<3;++row)for(unsigned axis=0;axis<3;++axis)
        put(rows[digit*3+row][axis],float((std::uint32_t(normal[axis*3+row])>>(digit*6))&(digit==3?31u:63u)),changed);
    return changed;
}
}
Error validate_lighting(const LightingInput& input) noexcept {
    const auto& m=input.material;
    if(m.mask>15||m.diffuse>32767||m.ambient>32767||m.specular>32767||m.emission>32767)return Error::invalid_data;
    if(m.mask&&(!input.lights||(input.lights->known&m.mask)!=m.mask))return Error::unsupported;
    const bool table=m.mask&&m.specular&&m.use_shininess;
    if(table&&!input.lights->table_known)return Error::unsupported;
    // One uniform bank serves shininess or toon lookup. Simultaneous custom
    // shininess and toon needs a separate shader variant, not an approximation.
    if(m.toon&&(table||!m.mask||!input.lights||!input.lights->toon_known))return Error::unsupported;
    if(m.grey&&(m.toon||table||!m.mask||!input.lights||!input.lights->toon_known))return Error::unsupported;
    for(unsigned slot=0;slot<4;++slot){
        if(!(m.mask&(1u<<slot)))continue;
        const auto& source=input.lights->light[slot];const auto r=source.specular_reciprocal;
        if(r<-262144||r>262144||(r>131072&&r!=262144))return Error::invalid_data;
        for(unsigned i=0;i<3;++i)
            if(source.direction[i]<-1024||source.direction[i]>1023||source.colour[i]>31)return Error::invalid_data;
    }
    return Error::none;
}
Error pack_lighting(const LightingInput& input,LightingUniforms& output) noexcept {
    const auto error=validate_lighting(input);if(error!=Error::none)return error;
    const auto& m=input.material;const bool table=m.mask&&m.specular&&m.use_shininess;
    LightingUniforms next;next.specular=m.specular!=0;next.table=table;next.toon=m.toon;next.grey=m.grey;
    // Only coefficient bits0..22 affect a shift12/signed11 result. Digits
    // stay unsigned; final reduction supplies the original signed result.
    pack_normal(input.normal,next.normal);
    for(unsigned slot=0;slot<4;++slot){
        if(!(m.mask&(1u<<slot)))continue;
        const auto& source=input.lights->light[slot];const auto r=source.specular_reciprocal;
        auto& direction=next.direction[next.count];auto& colour=next.colour[next.count];
        for(unsigned i=0;i<3;++i){direction[i]=float(source.direction[i]);colour[i]=float(source.colour[i]);}
        // Negative reciprocals always select shine0 before an optional table.
        direction[3]=float(r>0?r:0);++next.count;
    }
    const std::uint16_t colours[4]={m.diffuse,m.ambient,m.specular,m.emission};
    for(unsigned row=0;row<4;++row)for(unsigned i=0;i<3;++i)next.material[row][i]=float((colours[row]>>(i*5))&31);
    if(table)for(unsigned i=0;i<128;++i)next.shininess[i/4][i%4]=float(input.lights->shininess[i]);
    if(m.toon)for(unsigned i=0;i<32;++i)for(unsigned c=0;c<3;++c)next.shininess[i][c]=float((input.lights->toon[i]>>(c*5))&31);
    output=next;return Error::none;
}
bool LightingCache::matches(const LightingInput& input) const noexcept {
    const auto& m=input.material;
    if(!prepared_||!(material_==m))return false;
    if(!m.mask)return true;
    if(normal_!=input.normal||!input.lights||(input.lights->known&m.mask)!=m.mask)return false;
    for(unsigned slot=0;slot<4;++slot)
        if((m.mask&(1u<<slot))&&!(lights_.light[slot]==input.lights->light[slot]))return false;
    return (!uniforms_.table||(input.lights->table_known&&lights_.shininess==input.lights->shininess))&&
        (!uniforms_.toon||(input.lights->toon_known&&lights_.toon==input.lights->toon));
}
bool LightingCache::same_source(const LightingInput& input) const noexcept {
    // matches() without its normal basis: the reference passes the cached one.
    const auto& m=input.material;
    if(!prepared_||!(material_==m))return false;
    if(!m.mask)return true;
    if(!input.lights||(input.lights->known&m.mask)!=m.mask)return false;
    for(unsigned slot=0;slot<4;++slot)
        if((m.mask&(1u<<slot))&&!(lights_.light[slot]==input.lights->light[slot]))return false;
    return (!uniforms_.table||(input.lights->table_known&&lights_.shininess==input.lights->shininess))&&
        (!uniforms_.toon||(input.lights->toon_known&&lights_.toon==input.lights->toon));
}
Error LightingCache::prepare(const LightingInput& input,LightingChanges& changes,bool palette) noexcept {
    if(reference_)return prepare_reference(input,changes);
    // The rows are compared or uploaded below: make them the packing of the
    // cached basis again, as they always are in the reference.
    if(!palette&&normal_rows_stale_){pack_normal(normal_,uniforms_.normal);normal_rows_stale_=false;}
    LightingChanges next;
    if(same_source(input)){
        if(input.material.mask&&normal_!=input.normal){
            if(palette)normal_rows_stale_=true;
            else next.normal=repack_normal(input.normal,uniforms_.normal);
            normal_=input.normal;next.packed=true;
        }
        if(!resident_)next.normal=next.direction=next.colour=next.material=next.shininess=next.switches=next.count=true;
    }else{
        const auto error=validate_lighting(input);if(error!=Error::none)return error;
        const auto& m=input.material;const bool table=m.mask&&m.specular&&m.use_shininess;
        const bool had_lights=uniforms_.count!=0,had_bank=uniforms_.table||uniforms_.toon;
        auto& u=uniforms_;
        if(palette)normal_rows_stale_=true;
        else next.normal=repack_normal(input.normal,u.normal)||!resident_||!had_lights;
        unsigned count=0;bool direction=false,colour=false,material=false,bank=false;
        for(unsigned slot=0;slot<4;++slot){
            if(!(m.mask&(1u<<slot)))continue;
            const auto& source=input.lights->light[slot];const auto r=source.specular_reciprocal;
            for(unsigned i=0;i<3;++i){put(u.direction[count][i],float(source.direction[i]),direction);put(u.colour[count][i],float(source.colour[i]),colour);}
            // Negative reciprocals always select shine0 before an optional table.
            put(u.direction[count][3],float(r>0?r:0),direction);put(u.colour[count][3],0.f,colour);++count;
            lights_.light[slot]=source;
        }
        for(unsigned row=count;row<4;++row)for(unsigned i=0;i<4;++i){put(u.direction[row][i],0.f,direction);put(u.colour[row][i],0.f,colour);}
        const std::uint16_t colours[4]={m.diffuse,m.ambient,m.specular,m.emission};
        for(unsigned row=0;row<4;++row){
            for(unsigned i=0;i<3;++i)put(u.material[row][i],float((colours[row]>>(i*5))&31),material);
            put(u.material[row][3],0.f,material);
        }
        if(table){
            for(unsigned i=0;i<128;++i)put(u.shininess[i/4][i%4],float(input.lights->shininess[i]),bank);
            lights_.shininess=input.lights->shininess;
        }else if(m.toon){
            for(unsigned i=0;i<32;++i){
                for(unsigned c=0;c<3;++c)put(u.shininess[i][c],float((input.lights->toon[i]>>(c*5))&31),bank);
                put(u.shininess[i][3],0.f,bank);
            }
            lights_.toon=input.lights->toon;
        }
        next.direction=direction||!resident_||!had_lights;
        next.colour=colour||!resident_||!had_lights;
        next.material=material||!resident_;
        next.shininess=bank||!resident_||!had_bank;
        next.switches=!resident_||u.specular!=(m.specular!=0)||u.table!=table||bool(u.count)!=bool(count)||u.toon!=m.toon||u.grey!=m.grey;
        next.count=!resident_||u.count!=count;
        u.count=count;u.specular=m.specular!=0;u.table=table;u.toon=m.toon;u.grey=m.grey;
        normal_=input.normal;material_=m;prepared_=true;next.packed=true;
    }
    next.normal=!palette&&next.normal&&uniforms_.count;
    next.direction=next.direction&&uniforms_.count;
    next.colour=next.colour&&uniforms_.count;
    next.shininess=next.shininess&&(uniforms_.table||uniforms_.toon);
    resident_=true;changes=next;return Error::none;
}
Error LightingCache::prepare_reference(const LightingInput& input,LightingChanges& changes) noexcept {
    LightingChanges next;
    // Bones commonly change only the normal basis while sharing material and
    // lights. Compare the owned lighting source once, then repack just those
    // 12 rows; do not clear/copy the 32-row lookup bank for every bone.
    const bool same_source=matches({input.lights,normal_,input.material});
    if(same_source){
        if(input.material.mask&&normal_!=input.normal){
            auto rows=uniforms_.normal;pack_normal(input.normal,rows);
            next.normal=rows!=uniforms_.normal;uniforms_.normal=rows;
            normal_=input.normal;next.packed=true;
        }
        if(!resident_)next.normal=next.direction=next.colour=next.material=next.shininess=next.switches=next.count=true;
    }else{
        LightingUniforms packed;
        const auto error=pack_lighting(input,packed);
        if(error!=Error::none)return error;
        next.normal=!resident_||!uniforms_.count||uniforms_.normal!=packed.normal;
        next.direction=!resident_||!uniforms_.count||uniforms_.direction!=packed.direction;
        next.colour=!resident_||!uniforms_.count||uniforms_.colour!=packed.colour;
        next.material=!resident_||uniforms_.material!=packed.material;
        next.shininess=!resident_||!(uniforms_.table||uniforms_.toon)||uniforms_.shininess!=packed.shininess;
        next.switches=!resident_||uniforms_.specular!=packed.specular||uniforms_.table!=packed.table||
            bool(uniforms_.count)!=bool(packed.count)||uniforms_.toon!=packed.toon||uniforms_.grey!=packed.grey;
        next.count=!resident_||uniforms_.count!=packed.count;
        uniforms_=packed;normal_=input.normal;material_=input.material;
        lights_=input.lights?*input.lights:LightSet{};prepared_=true;next.packed=true;
    }
    // Unused uniforms may retain another draw's values. They become dirty
    // again when count/table changes from disabled to enabled.
    next.normal=next.normal&&uniforms_.count;
    next.direction=next.direction&&uniforms_.count;
    next.colour=next.colour&&uniforms_.count;
    next.shininess=next.shininess&&(uniforms_.table||uniforms_.toon);
    resident_=true;changes=next;return Error::none;
}
}
