#include "scene_capture.hpp"
#include <cstdarg>
#include <cmath>
#include <type_traits>

namespace sm64ds::render {
namespace {
bool valid_scene(const SceneView& s) noexcept {
    // Matches the game capture workspace, bounded independently of callers.
    if(s.occurrences>256||s.parts>2048||s.quad_vertices>8192||s.quad_states>256||s.skin_vertices>4095*3||s.skin_vertices%3||
       s.lighting.light_count>LightingPalette::max_lights||s.lighting.state_count>LightingPalette::max_states||
       (s.occurrences&&!s.occurrence)||(s.parts&&!s.part)||
       (s.quad_vertices&&!s.quad_vertex)||(s.quad_states&&!s.quad_state)||(s.skin_vertices&&!s.skin_vertex)||
       (s.lighting.light_count&&!s.lighting.lights)||(s.lighting.state_count&&!s.lighting.state)||
       s.clear.polygon_id>63)return false;
    OrderedLayers order;
    if(order_layers(s.image_layer,s.image_layers,s.scene_screen,s.scene_order,order)!=Error::none)return false;
    for(std::size_t i=0;i<s.parts;++i){const auto& p=s.part[i];
        if(!finite(p.object_to_clip)||!valid(p.material)||p.material.texture)return false;
        for(float v:p.material.uv)if(!std::isfinite(v))return false;
        if(p.lighting!=no_lighting&&p.lighting>=s.lighting.state_count)return false;
    }
    for(std::size_t i=0;i<s.quad_states;++i){
        if(!valid(s.quad_state[i]))return false;
        for(float v:s.quad_state[i].material.uv)if(!std::isfinite(v))return false;
    }
    if(s.quad_vertices&&validate_quads(s.quad_vertex,s.quad_vertices)!=Error::none)return false;
    for(std::size_t i=0;i<s.skin_vertices;++i){const auto& v=s.skin_vertex[i];
        if(!std::isfinite(v.base.x)||!std::isfinite(v.base.y)||!std::isfinite(v.base.z)||
           !std::isfinite(v.base.u)||!std::isfinite(v.base.v)||v.x < -512||v.x>511||
           v.y < -512||v.y>511||v.z < -512||v.z>511||v.uv_source<0||v.uv_source>2)return false;
    }
    for(std::size_t i=0;i<s.lighting.light_count;++i){const auto& lights=s.lighting.lights[i];
        if(lights.known>15)return false;
        LightingInput input;input.lights=&lights;input.material.mask=lights.known;
        if(validate_lighting(input)!=Error::none)return false;
    }
    for(std::size_t i=0;i<s.lighting.state_count;++i){LightingInput input;
        if(resolve_lighting(s.lighting,std::uint16_t(i),input)!=Error::none)return false;
    }
    std::size_t parts=0,vertices=0,states=0,skin=0;
    std::array<bool,max_frame_quads> covered{};
    for(std::size_t i=0;i<s.occurrences;++i){const auto& o=s.occurrence[i];
        if(o.kind==Occurrence::Kind::model){
            if(o.file_id>65535||o.first!=parts||o.count>s.parts-parts)return false;
            parts+=o.count;
        }else if(o.kind==Occurrence::Kind::quads){
            if(o.first%4||o.first>s.quad_vertices||!o.count||o.count%4||o.count>s.quad_vertices-o.first||
               o.state!=states||states>=s.quad_states)return false;
            const auto& q=s.quad_state[states++];
            if(q.image==QuadState::no_image?o.file_id!=untextured_quads:o.file_id>65535)return false;
            for(std::size_t q=o.first/4;q<(o.first+o.count)/4;++q){if(covered[q])return false;covered[q]=true;}
            vertices+=o.count;
        }else if(o.kind==Occurrence::Kind::skin||o.kind==Occurrence::Kind::mesh){
            if(o.first!=skin||!o.count||o.count%3||o.count>s.skin_vertices-skin)return false;
            if(o.kind==Occurrence::Kind::skin){
                if(o.file_id>65535||o.state>=parts)return false;
            }else{
                if(o.state!=parts||parts>=s.parts)return false;
                if(s.part[parts].image==ModelBank::no_texture?o.file_id!=untextured_quads:o.file_id>65535)return false;
                ++parts;
            }
            skin+=o.count;
        }else return false;
    }
    for(std::size_t i=0;i<s.image_layers;++i){const auto& image=s.image_layer[i];if(!image.vertices)continue;
        if(image.first_vertex>s.quad_vertices||image.vertices>s.quad_vertices-image.first_vertex)return false;
        for(std::size_t q=image.first_vertex/4;q<(image.first_vertex+image.vertices)/4;++q){if(covered[q])return false;covered[q]=true;}
        vertices+=image.vertices;
    }
    return parts==s.parts&&vertices==s.quad_vertices&&states==s.quad_states&&skin==s.skin_vertices;
}
struct Json {
    FILE* file;bool okay=true;
    void out(const char* format,...) noexcept {
        if(!okay)return;
        va_list args;va_start(args,format);okay=std::vfprintf(file,format,args)>=0;va_end(args);
    }
    template<class T> void integer(T value) noexcept {
        if constexpr(std::is_signed_v<T>)out("%lld",static_cast<long long>(value));
        else out("%llu",static_cast<unsigned long long>(value));
    }
    template<class Range> void integers(const Range& values) noexcept {
        out("[");bool first=true;for(auto v:values){out(first?"":",");first=false;integer(v);}out("]");
    }
    template<class Range> void floats(const Range& values) noexcept {
        out("[");bool first=true;for(auto v:values){out(first?"":",");first=false;out("%.9g",double(v));}out("]");
    }
    void matrix(const Matrix& value) noexcept {
        out("[");for(unsigned i=0;i<4;++i){if(i)out(",");floats(value.row[i]);}out("]");
    }
    void material(const Material& m) noexcept {
        out("{\"blend\":%u,\"cull\":%u,\"wrap\":[%u,%u],\"depth\":[%u,%u],\"sample_alpha\":%u,\"alpha_cutoff\":%u,\"tint\":%lu,\"uv_mode\":%u,\"uv\":",
            unsigned(m.blend),unsigned(m.cull),unsigned(m.wrap_s),unsigned(m.wrap_t),unsigned(m.depth_test),unsigned(m.depth_write),
            unsigned(m.sample_alpha),unsigned(m.alpha_cutoff),static_cast<unsigned long>(m.tint),unsigned(m.uv_mode));
        floats(m.uv);out(",\"brightness\":%d,\"fixed_uv\":{\"coefficient\":",int(m.brightness));integers(m.fixed_uv.coefficient);
        out(",\"dimensions\":[%u,%u],\"offset\":",unsigned(m.fixed_uv.width),unsigned(m.fixed_uv.height));
        integers(m.fixed_uv.offset);out("},\"shadow_id\":%u}",unsigned(m.shadow_id));
    }
    void lighting_material(const LightingMaterial& m) noexcept {
        out("[%u,%u,%u,%u,%u,%u,%u,%u]",unsigned(m.diffuse),unsigned(m.ambient),unsigned(m.specular),unsigned(m.emission),unsigned(m.mask),unsigned(m.use_shininess),unsigned(m.toon),unsigned(m.grey));
    }
};
}
bool write_component_capture(FILE* file,const SceneView& s,const ComponentCaptureInfo& info) noexcept {
    if(!file||!valid_scene(s))return false;
    Json j{file};
    j.out("{\"format\":\"SM64_NATIVE_COMPONENT_SCENE_5\",\"scope\":\"native-component\",\"original_scene_accepted\":false,\"physical_capture\":false,\"frame\":");j.integer(info.frame);
    j.out(",\"scene\":");j.integer(s.scene);j.out(",\"serial\":");j.integer(s.serial);
    j.out(",\"legacy_words\":");j.integer(info.legacy_words);
    j.out(",\"clear_known\":%s,\"unmigrated_2d\":%s,\"clear\":[%lu,%u,%u],\"occurrences\":[",
        info.clear_known?"true":"false",info.unmigrated_2d?"true":"false",static_cast<unsigned long>(s.clear.rgba),unsigned(s.clear.polygon_id),unsigned(s.clear.fog_mark));
    for(std::size_t i=0;i<s.occurrences;++i){const auto& o=s.occurrence[i];
        j.out(i?",[":"[");j.integer(o.file_id);j.out(",%u,%lu,%lu,%u]",unsigned(o.kind),static_cast<unsigned long>(o.first),static_cast<unsigned long>(o.count),unsigned(o.state));
    }
    j.out("],\"parts\":[");
    for(std::size_t i=0;i<s.parts;++i){const auto& p=s.part[i];if(i)j.out(",");
        j.out("{\"matrix\":");j.matrix(p.object_to_clip);j.out(",\"material\":");j.material(p.material);
        j.out(",\"image\":%lu,\"visible\":%s,\"lighting\":%u,\"highlight\":%s}",static_cast<unsigned long>(p.image),p.visible?"true":"false",unsigned(p.lighting),p.highlight?"true":"false");
    }
    j.out("],\"skin_vertices\":[");
    for(std::size_t i=0;i<s.skin_vertices;++i){const auto& v=s.skin_vertex[i];const auto& b=v.base;
        j.out(i?",[":"[");j.out("%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%u,%d,%d,%d,%d]",
            double(b.x),double(b.y),double(b.z),double(b.u),double(b.v),unsigned(b.r),unsigned(b.g),unsigned(b.b),unsigned(b.a),
            int(v.x),int(v.y),int(v.z),int(v.uv_source));
    }
    j.out("],\"quad_vertices\":[");
    for(std::size_t i=0;i<s.quad_vertices;++i){const auto& v=s.quad_vertex[i];
        j.out(i?",[":"[");j.out("%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%u]",double(v.x),double(v.y),double(v.z),double(v.u),double(v.v),unsigned(v.r),unsigned(v.g),unsigned(v.b),unsigned(v.a));
    }
    j.out("],\"quad_states\":[");
    for(std::size_t i=0;i<s.quad_states;++i){const auto& q=s.quad_state[i];if(i)j.out(",");
        j.out("{\"matrix\":");j.matrix(q.object_to_clip);j.out(",\"material\":");j.material(q.material);
        j.out(",\"image\":%u,\"polygon_id\":%u,\"fog\":%s,\"bank\":%u}",unsigned(q.image),unsigned(q.polygon_id),q.fog?"true":"false",unsigned(q.bank));
    }
    j.out("],\"scene_screen\":%u,\"scene_order\":%u,\"image_layers\":[",unsigned(s.scene_screen),unsigned(s.scene_order));
    for(std::size_t i=0;i<s.image_layers;++i){const auto& image=s.image_layer[i];if(i)j.out(",");
        j.out("{\"bank\":%lu,\"image\":%u,\"screen\":%u,\"order\":%u,\"source\":%u,\"part\":%u,\"rectangle\":[%.9g,%.9g,%.9g,%.9g],\"material\":",
            static_cast<unsigned long>(image.bank_id),unsigned(image.image),unsigned(image.screen),unsigned(image.order),
            unsigned(image.source),unsigned(image.part),double(image.rectangle.x),double(image.rectangle.y),double(image.rectangle.width),double(image.rectangle.height));
        j.material(image.material);j.out(",\"clip\":[%u,%u,%u,%u],\"first_vertex\":%lu,\"vertices\":%lu,\"tone_target\":%u}",unsigned(image.clip.left),unsigned(image.clip.top),unsigned(image.clip.right),unsigned(image.clip.bottom),
            static_cast<unsigned long>(image.first_vertex),static_cast<unsigned long>(image.vertices),unsigned(image.tone_target));
    }
    j.out("],\"light_sets\":[");
    for(std::size_t i=0;i<s.lighting.light_count;++i){const auto& set=s.lighting.lights[i];if(i)j.out(",");
        j.out("{\"known\":%u,\"generation\":",unsigned(set.known));j.integer(set.generation);
        j.out(",\"table_known\":%s,\"shininess\":",set.table_known?"true":"false");j.integers(set.shininess);
        j.out(",\"toon_known\":%s,\"toon\":",set.toon_known?"true":"false");j.integers(set.toon);j.out(",\"lights\":[");
        for(unsigned n=0;n<4;++n){if(n)j.out(",");const auto& l=set.light[n];
            j.out("{\"direction\":");j.integers(l.direction);j.out(",\"colour\":");j.integers(l.colour);
            j.out(",\"specular_reciprocal\":%ld}",static_cast<long>(l.specular_reciprocal));
        }j.out("]}");
    }
    j.out("],\"lighting_states\":[");
    for(std::size_t i=0;i<s.lighting.state_count;++i){const auto& l=s.lighting.state[i];if(i)j.out(",");
        j.out("{\"normal\":");j.integers(l.normal);j.out(",\"material\":");j.lighting_material(l.material);
        j.out(",\"lights\":%u}",unsigned(l.lights));
    }
    j.out("]}\n");return j.okay&&!std::ferror(file);
}
}
