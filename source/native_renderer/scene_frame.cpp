#include "scene_frame.hpp"
#include <limits>
#include <cstring>
#include <cmath>
namespace sm64ds::render {
SceneFrame::SceneFrame(Occurrence* occurrences,std::size_t nc,PreparedPart* parts,std::size_t np,
                       LightSet* lights,std::size_t nl,PreparedLighting* lighting,std::size_t ns,QuadStorage quads,
                       ImageLayer* images,std::size_t ni) noexcept:
    occurrences_(occurrences),parts_(parts),occurrence_capacity_(nc),part_capacity_(np),lighting_(lights,nl,lighting,ns),quads_(quads),images_(images),image_capacity_(ni){}
Error SceneFrame::begin(std::uint64_t scene,std::uint64_t serial,SceneClear clear) noexcept {
    if(phase_!=Phase::idle)return Error::invalid_phase;
    if(!occurrences_||!parts_||!occurrence_capacity_||!part_capacity_||part_capacity_>std::numeric_limits<std::uint32_t>::max())
        return Error::invalid_data;
    if(clear.polygon_id>63)return Error::invalid_data;
    if((quads_.vertices&&!quads_.vertex)||(quads_.states&&!quads_.state)||
       quads_.vertices>max_frame_quads*4||quads_.states>65536)return Error::invalid_data;
    if((image_capacity_&&!images_)||image_capacity_>ImageLayer::max_layers)return Error::invalid_data;
    used_quad_vertices_=used_quad_states_=used_images_=used_skin_=0;used_shadows_=0;fog_={};scene_order_=0;scene_screen_=Screen::top;
    scene_=scene;serial_=serial;clear_=clear;used_occurrences_=used_parts_=0;lighting_.reset();failure_=Error::none;phase_=Phase::capturing;return Error::none;
}
Error SceneFrame::clear(SceneClear value) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(value.polygon_id>63)return Error::invalid_data;
    clear_=value;return Error::none;
}
void SceneFrame::invalidate(Error error) noexcept {
    if(phase_!=Phase::capturing||error==Error::none)return;
    if(lenient_){++dropped_;return;}
    if(failure_==Error::none)failure_=error;
}
Error SceneFrame::append(const ModelBindings& bindings,const InstanceInputs& input,InstanceIdentity identity) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(failure_!=Error::none)return failure_;
    if(bindings.file_id>65535){invalidate(Error::invalid_data);return Error::invalid_data;}
    if(used_occurrences_==occurrence_capacity_||bindings.parts>part_capacity_-used_parts_){invalidate(Error::capacity);return Error::capacity;}
    const auto result=capture_prepared(bindings,input,parts_+used_parts_,part_capacity_-used_parts_,&lighting_,CaptureOutput::unpublished);
    if(result!=Error::none){invalidate(result);return result;}
    occurrences_[used_occurrences_++]={bindings.file_id,std::uint32_t(used_parts_),bindings.parts,Occurrence::Kind::model,0,identity};
    used_parts_+=bindings.parts;return Error::none;
}
Error SceneFrame::append_quads(std::uint32_t file_id,const QuadState& state,const Vertex* vertex,std::size_t count) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(failure_!=Error::none)return failure_;
    if(!valid(state)||(state.image==QuadState::no_image?file_id!=untextured_quads:file_id>65535)){
        invalidate(Error::invalid_data);return Error::invalid_data;
    }
    const auto error=validate_quads(vertex,count);
    if(error!=Error::none){invalidate(error);return error;}
    const bool merge=used_occurrences_&&occurrences_[used_occurrences_-1].kind==Occurrence::Kind::quads&&
        occurrences_[used_occurrences_-1].first+occurrences_[used_occurrences_-1].count==used_quad_vertices_&&
        occurrences_[used_occurrences_-1].file_id==file_id&&same(quads_.state[occurrences_[used_occurrences_-1].state],state);
    if(count>quads_.vertices-used_quad_vertices_||(!merge&&
       (used_occurrences_==occurrence_capacity_||used_quad_states_==quads_.states))){invalidate(Error::capacity);return Error::capacity;}
    if(vertex!=quads_.vertex+used_quad_vertices_)std::memmove(quads_.vertex+used_quad_vertices_,vertex,count*sizeof(Vertex));
    if(merge)occurrences_[used_occurrences_-1].count+=std::uint32_t(count);
    else{
        const auto slot=std::uint16_t(used_quad_states_);quads_.state[used_quad_states_++]=state;
        occurrences_[used_occurrences_++]={file_id,std::uint32_t(used_quad_vertices_),std::uint32_t(count),Occurrence::Kind::quads,slot};
    }
    used_quad_vertices_+=count;return Error::none;
}
SceneFrame::QuadWrite SceneFrame::quad_workspace() noexcept {
    if(phase_!=Phase::capturing||failure_!=Error::none||!quads_.vertex)return {};
    return {quads_.vertex+used_quad_vertices_,quads_.vertices-used_quad_vertices_};
}
Error SceneFrame::append_image(const ImageLayer& image) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(failure_!=Error::none)return failure_;
    if(!valid(image)||image.vertices||image.first_vertex){invalidate(Error::invalid_data);return Error::invalid_data;}
    if(used_images_==image_capacity_){invalidate(Error::capacity);return Error::capacity;}
    images_[used_images_++]=image;return Error::none;
}
Error SceneFrame::append_image_quads(const ImageLayer& image,const Vertex* vertex,std::size_t count) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(failure_!=Error::none)return failure_;
    if(!valid(image)||image.source!=LayerSource::images||image.vertices||image.first_vertex){
        invalidate(Error::invalid_data);return Error::invalid_data;
    }
    const auto error=validate_quads(vertex,count);
    if(error!=Error::none){invalidate(error);return error;}
    if(count>quads_.vertices-used_quad_vertices_||used_images_==image_capacity_){
        invalidate(Error::capacity);return Error::capacity;
    }
    if(vertex!=quads_.vertex+used_quad_vertices_)std::memmove(quads_.vertex+used_quad_vertices_,vertex,count*sizeof(Vertex));
    auto& captured=images_[used_images_++];captured=image;
    captured.first_vertex=std::uint32_t(used_quad_vertices_);captured.vertices=std::uint32_t(count);
    used_quad_vertices_+=count;return Error::none;
}
Error SceneFrame::append_skin(std::uint32_t file_id,std::uint32_t part,const NormalVertex* vertex,std::size_t count,InstanceIdentity identity) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(failure_!=Error::none)return failure_;
    if(!count||count%3||part>=used_parts_||part>65535||file_id>65535||count>skin_capacity_-used_skin_||used_occurrences_==occurrence_capacity_){
        invalidate(Error::capacity);return Error::capacity;}
    if(vertex!=skin_+used_skin_)std::memmove(skin_+used_skin_,vertex,count*sizeof(NormalVertex));
    occurrences_[used_occurrences_++]={file_id,std::uint32_t(used_skin_),std::uint32_t(count),Occurrence::Kind::skin,std::uint16_t(part),identity};
    used_skin_+=count;return Error::none;
}
Error SceneFrame::presentation(Screen screen,std::uint16_t order) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(!screen_width(screen))return Error::invalid_data;
    scene_order_=order;scene_screen_=screen;return Error::none;
}
Error SceneFrame::append_mesh(std::uint32_t file_id,const PreparedPart& source,const LightingInput* light,const NormalVertex* vertex,std::size_t count) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(failure_!=Error::none)return failure_;
    if(!vertex||!count||count%3||!valid(source.material)||source.material.texture||!finite(source.object_to_clip)||
       (source.image==ModelBank::no_texture?file_id!=untextured_quads:file_id>65535)){
        invalidate(Error::invalid_data);return Error::invalid_data;
    }
    if(!skin_||count>skin_capacity_-used_skin_||used_parts_==part_capacity_||used_parts_>65535||used_occurrences_==occurrence_capacity_){
        invalidate(Error::capacity);return Error::capacity;
    }
    for(std::size_t i=0;i<count;++i){const auto& v=vertex[i];
        if(!std::isfinite(v.base.x)||!std::isfinite(v.base.y)||!std::isfinite(v.base.z)||
           !std::isfinite(v.base.u)||!std::isfinite(v.base.v)||v.x < -512||v.x>511||v.y < -512||v.y>511||v.z < -512||v.z>511||v.uv_source!=2){
            invalidate(Error::invalid_data);return Error::invalid_data;
        }
    }
    auto part=source;part.lighting=no_lighting;
    if(light){const auto error=lighting_.intern(*light,part.lighting);if(error!=Error::none){invalidate(error);return error;}}
    if(vertex!=skin_+used_skin_)std::memmove(skin_+used_skin_,vertex,count*sizeof(NormalVertex));
    parts_[used_parts_]=part;
    occurrences_[used_occurrences_++]={file_id,std::uint32_t(used_skin_),std::uint32_t(count),Occurrence::Kind::mesh,std::uint16_t(used_parts_++)};
    used_skin_+=count;return Error::none;
}
Error SceneFrame::seal(bool producers_ok,std::uint64_t legacy_writers) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(!lenient_){
        if(!producers_ok)invalidate(Error::invalid_data);
        if(legacy_writers)invalidate(Error::unsupported);
    }
    if(failure_!=Error::none){phase_=Phase::idle;used_occurrences_=used_parts_=used_quad_vertices_=used_quad_states_=used_images_=used_skin_=0;return failure_;}
    phase_=Phase::sealed;return Error::none;
}
Error SceneFrame::acquire(SceneView& output) noexcept {
    if(phase_!=Phase::sealed)return Error::invalid_phase;
    output={occurrences_,parts_,used_occurrences_,used_parts_,scene_,serial_,lighting_.view(),clear_,
        quads_.vertex,quads_.state,used_quad_vertices_,used_quad_states_,images_,used_images_,scene_order_,scene_screen_,skin_,used_skin_,
        shadows_.data(),used_shadows_,shadow_projection_,fog_,fog_projection_};phase_=Phase::consuming;return Error::none;
}
Error SceneFrame::release() noexcept {
    if(phase_!=Phase::consuming&&phase_!=Phase::sealed)return Error::invalid_phase;
    phase_=Phase::idle;used_occurrences_=used_parts_=used_quad_vertices_=used_quad_states_=used_images_=used_skin_=0;used_shadows_=0;return Error::none;
}
Error SceneFrame::fog(const DsFog& fog,const Matrix& view_to_clip) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(!finite(view_to_clip)||fog.shift>15)return Error::invalid_data;
    fog_=fog;fog_projection_=view_to_clip;return Error::none;
}
Error SceneFrame::append_shadow(const ShadowVolume& shadow,const Matrix& view_to_clip) noexcept {
    if(phase_!=Phase::capturing)return Error::invalid_phase;
    if(!shadow.opacity||shadow.opacity>31||!finite(view_to_clip))return Error::invalid_data;
    for(float value:shadow.pose)if(!std::isfinite(value))return Error::invalid_data;
    if(used_shadows_==shadows_.size()){++dropped_;return Error::none;}
    shadows_[used_shadows_++]=shadow;shadow_projection_=view_to_clip;return Error::none;
}
}
