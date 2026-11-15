#include "lighting_palette.hpp"
#include <optional>
namespace sm64ds::render {
namespace {
// BUILD131: one multiply per word instead of FNV-1a over its four bytes
// (72 multiply steps for each lit part's 18-word state). The hash only picks
// the starting bucket: identifiers are handed out in insertion order and
// equality decides a match, so every published id is unchanged.
struct Hash {
    std::uint32_t value=2166136261u;
    void word(std::uint32_t v) noexcept {value=(value^v)*16777619u;value^=value>>15;}
};
bool equal(const LightSet& a,const LightSet& b) noexcept {
    return a.known==b.known&&a.table_known==b.table_known&&a.light==b.light&&
        (!a.table_known||a.shininess==b.shininess)&&a.toon_known==b.toon_known&&(!a.toon_known||a.toon==b.toon);
}
bool equal(const PreparedLighting& a,const PreparedLighting& b) noexcept {
    return a.normal==b.normal&&a.material==b.material&&a.lights==b.lights;
}
std::uint32_t hash(const LightSet& a) noexcept {
    Hash h;h.word(a.known);h.word(a.table_known);
    for(const auto& light:a.light){for(auto v:light.direction)h.word(std::uint32_t(v));
        for(auto v:light.colour)h.word(v);
        h.word(std::uint32_t(light.specular_reciprocal));}
    if(a.table_known)for(auto v:a.shininess)h.word(v);
    h.word(a.toon_known);if(a.toon_known)for(auto v:a.toon)h.word(v);
    return h.value;
}
std::uint32_t hash(const PreparedLighting& a) noexcept {
    Hash h;for(auto v:a.normal)h.word(std::uint32_t(v));const auto& m=a.material;
    h.word(m.diffuse);h.word(m.ambient);h.word(m.specular);h.word(m.emission);
    h.word(m.mask);h.word(m.use_shininess);h.word(m.toon);h.word(m.grey);h.word(a.lights);return h.value;
}
template<class Table,class Values,class Value>
unsigned locate(const Table& table,Values values,const Value& value) noexcept {
    unsigned at=hash(value)&(table.size()-1);
    while(table[at]&&!equal(values[table[at]-1],value))at=(at+1)&(table.size()-1);
    return at;
}
}
Error resolve_lighting(const LightingPaletteView& view,std::uint16_t id,LightingInput& output) noexcept {
    if(!view.state||id>=view.state_count||view.state_count>LightingPalette::max_states||
       view.light_count>LightingPalette::max_lights)return Error::invalid_data;
    const auto& s=view.state[id];LightingInput next{nullptr,s.normal,s.material};
    if(s.material.mask){
        if(!view.lights||s.lights>=view.light_count)return Error::invalid_data;
        next.lights=view.lights+s.lights;
    }else if(s.lights!=no_lighting)return Error::invalid_data;
    const auto error=validate_lighting(next);if(error!=Error::none)return error;
    output=next;return Error::none;
}
LightingPalette::LightingPalette(LightSet* lights,std::size_t nl,PreparedLighting* states,std::size_t ns) noexcept:
    lights_(lights),states_(states),light_capacity_(nl),state_capacity_(ns){}
void LightingPalette::reset() noexcept {
    light_count_=state_count_=0;light_index_.fill(0);state_index_.fill(0);recent_lights_.fill(0);
}
Error LightingPalette::intern(const LightingInput& input,std::uint16_t& output) noexcept {
    const auto error=validate_lighting(input);if(error!=Error::none)return error;
    if(state_capacity_>max_states||light_capacity_>max_lights||!states_||
       (light_capacity_&&!lights_))return Error::invalid_data;
    PreparedLighting state;state.material=input.material;
    std::optional<LightSet> pending_light;unsigned light_slot=0,light_key=0;bool new_light=false;
    if(input.material.mask){
        const bool table=input.material.specular&&input.material.use_shininess;
        const bool toon=input.material.toon||input.material.grey;
        light_key=input.material.mask|(unsigned(table)<<4)|(unsigned(toon)<<5);
        const auto recent=recent_lights_[light_key];bool reuse=recent!=0;
        if(reuse){
            const auto& owned=lights_[recent-1];
            for(unsigned i=0;i<4;++i)if(input.material.mask&(1u<<i))reuse&=owned.light[i]==input.lights->light[i];
            if(table)reuse&=owned.shininess==input.lights->shininess;
            if(toon)reuse&=owned.toon==input.lights->toon;
        }
        if(reuse)state.lights=recent-1;
        else{
            auto& light=pending_light.emplace();light.known=input.material.mask;
            for(unsigned i=0;i<4;++i)if(light.known&(1u<<i))light.light[i]=input.lights->light[i];
            light.table_known=table;if(table)light.shininess=input.lights->shininess;
            light.toon_known=toon;if(toon)light.toon=input.lights->toon;
            light_slot=locate(light_index_,lights_,light);new_light=!light_index_[light_slot];
            if(new_light&&light_count_==light_capacity_)return Error::capacity;
            state.lights=new_light?std::uint16_t(light_count_):std::uint16_t(light_index_[light_slot]-1);
        }
        state.normal=input.normal;
    }
    const unsigned state_slot=locate(state_index_,states_,state);
    if(state_index_[state_slot]){
        if(input.material.mask)recent_lights_[light_key]=state.lights+1;
        output=state_index_[state_slot]-1;return Error::none;
    }
    if(state_count_==state_capacity_)return Error::capacity;
    // Both capacities and complete source validity are known before mutation.
    if(new_light){lights_[light_count_++]=*pending_light;light_index_[light_slot]=std::uint16_t(light_count_);}
    if(input.material.mask)recent_lights_[light_key]=state.lights+1;
    states_[state_count_]=state;output=std::uint16_t(state_count_++);state_index_[state_slot]=std::uint16_t(state_count_);
    return Error::none;
}
void LightingPalette::rollback(Mark mark) noexcept {
    if(mark.lights>light_count_||mark.states>state_count_)return;
    if(mark.lights==light_count_&&mark.states==state_count_)return;
    light_count_=mark.lights;state_count_=mark.states;light_index_.fill(0);state_index_.fill(0);recent_lights_.fill(0);
    for(unsigned i=0;i<light_count_;++i)light_index_[locate(light_index_,lights_,lights_[i])]=std::uint16_t(i+1);
    for(unsigned i=0;i<state_count_;++i)state_index_[locate(state_index_,states_,states_[i])]=std::uint16_t(i+1);
}
}
