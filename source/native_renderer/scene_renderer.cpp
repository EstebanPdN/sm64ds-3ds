#include "scene_renderer.hpp"
#include "shadow_volumes.hpp"
#include "perf_switch.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
namespace sm64ds::render {
Error SceneRenderer::init(ModelCatalog catalog,const char* directory,const void* shader,std::size_t bytes,std::size_t budget,bool timings,bool prefer_affine,ImageCatalog images,CanvasCatalog canvases,bool antialias) noexcept {
    if(ready_||c3d_owned_)return Error::invalid_phase;
    if((!catalog.size()&&!images.size()&&!canvases.size())||!directory||!directory[0]||std::strlen(directory)>=sizeof(directory_))return Error::invalid_data;
    if(!C3D_Init(256*1024))return Error::gpu_failure;
    c3d_owned_=true;
    auto error=backend_.init(shader,bytes,budget,timings,prefer_affine);
    if(error!=Error::none){C3D_Fini();c3d_owned_=false;return error;}
    scene_scale_=antialias?2u:1u;
    error=backend_.create_surface(256,512*scene_scale_,true,scene_surface_,antialias);
    if(error==Error::none)error=compositor_.init(backend_);
    if(error!=Error::none){
        if(scene_surface_)backend_.destroy(scene_surface_);
        scene_surface_={};backend_.shutdown();C3D_Fini();c3d_owned_=false;return error;
    }
    target_=backend_.surface(scene_surface_);
    const auto* trace=std::getenv("SM64DS_NATIVE_LOAD_TRACE");load_trace_=trace&&*trace=='1';
    std::strcpy(directory_,directory);catalog_=catalog;images_=images;canvases_=canvases;timings_=timings;ready_=true;
    const auto* batches=std::getenv("SM64DS_NATIVE_BATCHES");batches_=backend_.batches_ready()&&(!batches||*batches=='1');
    std::printf("Native resident palette: enabled=%u vertices=%u spans=%u\n",unsigned(batches_),unsigned(NativeBatchPlan::max_vertices),unsigned(NativeBatchPlan::max_spans));
    const auto* cache=std::getenv("SM64DS_NATIVE_BATCH_CACHE");batch_cache_=!cache||*cache!='0';
    cuts_=perf::cuts();find_hint_={};
    have_scene_=completed_target_=false;preload_count_=trace_frames_=0;metrics_={};interface_metrics_={};interface_={};gfxSet3D(false);return Error::none;
}
Error SceneRenderer::preload_model(std::uint32_t id) noexcept {
    return queue_preload(id,PreloadKind::model);
}
Error SceneRenderer::preload_model_images(std::uint32_t id) noexcept {
    return queue_preload(id,PreloadKind::model_images);
}
Error SceneRenderer::preload_image_bank(std::uint32_t id) noexcept {
    return queue_preload(id,PreloadKind::image_bank);
}
Error SceneRenderer::preload_canvas(std::uint32_t id) noexcept {
    return queue_preload(id,PreloadKind::canvas);
}
Error SceneRenderer::queue_preload(std::uint32_t id,PreloadKind kind) noexcept {
    if(!ready_||!backend_.between_frames())return Error::invalid_phase;
    if(kind==PreloadKind::image_bank?!images_.find(id):kind==PreloadKind::canvas?!canvases_.find(id):!catalog_.find(id))return Error::invalid_data;
    for(unsigned i=0;i<preload_count_;++i)if(preload_[i].id==id&&preload_[i].kind==kind)return Error::none;
    if(preload_count_==preload_.size())return Error::capacity;
    preload_[preload_count_++]={id,kind};return Error::none;
}
Error SceneRenderer::interface_image(unsigned channel,std::uint32_t revision,const InterfaceImage& image) noexcept {
    if(!ready_||!backend_.between_frames())return Error::invalid_phase;
    if(channel>=interface_.size()||!revision||validate(image)!=Error::none||
       image.width!=(channel?256u:320u)||image.height!=(channel?16u:240u))return Error::invalid_data;
    auto& item=interface_[channel];
    if(item.revision==revision){item.visible=!item.crop.empty();++interface_metrics_.reuses;return Error::none;}
    auto error=backend_.begin(false);if(error!=Error::none)return error;
    error=upload_interface(channel,revision,image);
    const auto ended=backend_.end();return error==Error::none?ended:error;
}
Error SceneRenderer::upload_interface(unsigned channel,std::uint32_t revision,const InterfaceImage& image) noexcept {
    auto& item=interface_[channel];
    if(item.revision==revision){item.visible=!item.crop.empty();++interface_metrics_.reuses;return Error::none;}
    const auto start=timings_?svcGetSystemTick():0;
    item.visible=false;item.revision=0;
    const auto error=backend_.upload_interface(image,item.texture,item.crop);
    if(error==Error::none){
        item.revision=revision;item.visible=!item.crop.empty();++interface_metrics_.publications;
        if(item.visible)++interface_metrics_.uploads;
        interface_metrics_.scan_pixels+=std::size_t(image.width)*image.height;
        interface_metrics_.upload_bytes+=std::size_t(item.crop.texture_width)*item.crop.texture_height*4;
    }
    if(timings_)interface_metrics_.upload_ticks+=svcGetSystemTick()-start;
    return error;
}
void SceneRenderer::hide_interface(unsigned channel) noexcept {
    if(channel<interface_.size())interface_[channel].visible=false;
}
SceneRenderer::Resident* SceneRenderer::find(std::uint32_t id) noexcept {
    if(cuts_){
        // An identity is resident in at most one slot (a slot is only bound
        // after this search fails), so a checked hint is the walk's answer.
        auto& hint=find_hint_[id&63u];auto& hinted=resident_[hint];
        if(hinted.binding&&hinted.binding->file_id==id)return &hinted;
        for(auto& item:resident_)if(item.binding&&item.binding->file_id==id){hint=std::uint8_t(&item-resident_.data());return &item;}
        return nullptr;
    }
    for(auto& item:resident_)if(item.binding&&item.binding->file_id==id)return &item;
    return nullptr;
}
SceneRenderer::ImageResident* SceneRenderer::find_images(std::uint32_t id) noexcept {
    for(auto& item:image_resident_)if(item.binding&&item.binding->bank_id==id)return &item;
    return nullptr;
}
SceneRenderer::CanvasResident* SceneRenderer::find_canvas(std::uint32_t id) noexcept {
    for(auto& item:canvas_resident_)if(item.binding&&item.binding->bank_id==id)return &item;
    return nullptr;
}
bool SceneRenderer::evict() noexcept {
    Resident* model=nullptr;ImageResident* images=nullptr;CanvasResident* canvas=nullptr;
    for(auto& item:resident_)if(item.binding&&!item.pinned&&(!model||item.used<model->used))model=&item;
    for(auto& item:image_resident_)if(item.binding&&!item.pinned&&(!images||item.used<images->used))images=&item;
    for(auto& item:canvas_resident_)if(item.binding&&!item.pinned&&(!canvas||item.used<canvas->used))canvas=&item;
    if(canvas&&(!model||canvas->used<model->used)&&(!images||canvas->used<images->used)){canvas->model.reset();canvas->binding=nullptr;}
    else if(model&&(!images||model->used<=images->used)){model->model.reset();model->binding=nullptr;}
    else if(images){images->images.reset();images->binding=nullptr;}
    else return false;
    ++metrics_.evictions;return true;
}
std::size_t SceneRenderer::batch_cache_budget() const noexcept {return batches_&&batch_cache_?1024*1024:0;}
Error SceneRenderer::load(Resident& item,const ModelBindings& binding) noexcept {
    const auto start=timings_?svcGetSystemTick():0;
    char path[224];const int length=std::snprintf(path,sizeof(path),"%s/%05lu.nmd",directory_,static_cast<unsigned long>(binding.file_id));
    if(length<0||std::size_t(length)>=sizeof(path))return Error::invalid_data;
    if(timings_)std::printf("Native bank begin: model=%lu serial=%llu linearFree=%u appFree=%u\n",
        (unsigned long)binding.file_id,(unsigned long long)serial_,unsigned(linearSpaceFree()),unsigned(osGetMemRegionFree(MEMREGION_APPLICATION)));
    FILE* file=std::fopen(path,"rb");if(!file)return Error::invalid_data;
    bool okay=std::fseek(file,0,SEEK_END)==0;const long size=okay?std::ftell(file):-1;
    // Bounded transient bank allocation. Larger assets require streaming;
    // format validity alone does not promise Old3DS residency.
    if(size<128||size>8*1024*1024){std::fclose(file);return Error::budget;}
    void* source=std::malloc(std::size_t(size));if(!source){std::fclose(file);return Error::out_of_memory;}
    okay=std::fseek(file,0,SEEK_SET)==0&&std::fread(source,1,std::size_t(size),file)==std::size_t(size);
    const bool closed=std::fclose(file)==0;ModelBank bank;
    auto error=okay&&closed?ModelBank::open(source,std::size_t(size),bank):Error::invalid_data;
    if(error==Error::none)error=validate(bank,binding);
    if(error==Error::none){
        std::array<bool,ModelBank::max_images> required{};
        // Retain previously requested images when extending a sprite bank.
        // Reload/promotion is load-time work after the GPU fence; ordinary
        // resident frames neither reread banks nor allocate/upload resources.
        for(unsigned i=0;i<bank.images();++i)required[i]=bool(item.required_images[i/32]&(1u<<(i%32)))||bool(item.model.image(i));
        // Sprite-only extensions preserve resident handles and upload just the
        // missing images. A geometry promotion still replaces the whole asset.
        if(item.required_geometry)item.model.reset();
        for(;;){
            error=item.required_geometry?item.model.upload(backend_,bank,batch_cache_budget()):item.model.upload_images(backend_,bank,required);
            if(error!=Error::budget&&error!=Error::capacity)break;
            if(!evict())break;
        }
    }
    std::free(source);metrics_.source_peak_bytes=std::max(metrics_.source_peak_bytes,std::size_t(size));
    if(timings_)metrics_.load_ticks+=svcGetSystemTick()-start;
    if(error!=Error::none)return error;
    item.binding=&binding;item.used=serial_;item.pinned=true;++metrics_.loads;return Error::none;
}
Error SceneRenderer::load(ImageResident& item,const ImageBankBinding& binding) noexcept {
    const auto start=timings_?svcGetSystemTick():0;
    const auto trace=[&](const char* phase){if(load_trace_)std::printf("Native image I/O: bank=%lu serial=%llu phase=%s reads=%llu hits=%llu cacheBytes=%u\n",
        (unsigned long)binding.bank_id,(unsigned long long)serial_,phase,
        (unsigned long long)metrics_.image_source_reads,(unsigned long long)metrics_.image_source_hits,unsigned(metrics_.image_source_bytes));};
    trace("begin");
    ImageBank bank;void* source=nullptr;bool retained=false;
    Error error=Error::none;
    if(image_source_binding_==&binding){bank=image_source_bank_;retained=true;++metrics_.image_source_hits;trace("cached");}
    else{
        std::free(image_source_);image_source_=nullptr;image_source_binding_=nullptr;image_source_bank_={};metrics_.image_source_bytes=0;
        char path[224];const int length=std::snprintf(path,sizeof(path),"%s/%05lu.nim",directory_,static_cast<unsigned long>(binding.bank_id));
        if(length<0||std::size_t(length)>=sizeof(path))return Error::invalid_data;
        FILE* file=std::fopen(path,"rb");if(!file)return Error::invalid_data;trace("opened");
        bool okay=std::fseek(file,0,SEEK_END)==0;const long size=okay?std::ftell(file):-1;
        if(size<128||size>8*1024*1024){std::fclose(file);return Error::budget;}
        source=std::malloc(std::size_t(size));if(!source){std::fclose(file);return Error::out_of_memory;}trace("allocated");
        okay=std::fseek(file,0,SEEK_SET)==0&&std::fread(source,1,std::size_t(size),file)==std::size_t(size);
        const bool closed=std::fclose(file)==0;++metrics_.image_source_reads;trace("read");
        error=okay&&closed?ImageBank::open(source,std::size_t(size),bank):Error::invalid_data;
        if(error==Error::none)error=validate(bank,binding);
        trace("validated");
        metrics_.source_peak_bytes=std::max(metrics_.source_peak_bytes,std::size_t(size));
        if(error==Error::none&&size<=512*1024){
            image_source_=source;image_source_binding_=&binding;image_source_bank_=bank;
            metrics_.image_source_bytes=std::size_t(size);retained=true;
        }
    }
    if(error==Error::none)for(;;){
        error=item.images.upload(backend_,bank,item.required);
        if((error!=Error::budget&&error!=Error::capacity)||!evict())break;
    }
    trace(error==Error::none?"uploaded":"failed");
    if(!retained)std::free(source);
    if(timings_)metrics_.load_ticks+=svcGetSystemTick()-start;
    if(error!=Error::none)return error;
    item.binding=&binding;item.used=serial_;item.pinned=true;++metrics_.loads;return Error::none;
}
Error SceneRenderer::load(CanvasResident& item,const CanvasBinding& binding) noexcept {
    const auto start=timings_||load_trace_?svcGetSystemTick():0;
    const auto trace=[&](const char* phase){if(load_trace_)std::printf(
        "Native canvas I/O: bank=%lu serial=%llu phase=%s elapsedMs=%.3f linearFree=%u\n",
        (unsigned long)binding.bank_id,(unsigned long long)serial_,phase,
        double(svcGetSystemTick()-start)*1000/SYSCLOCK_ARM11,unsigned(linearSpaceFree()));};
    char path[224];const auto n=std::snprintf(path,sizeof(path),"%s/%05lu.nmd",directory_,static_cast<unsigned long>(binding.bank_id));
    if(n<0||std::size_t(n)>=sizeof(path))return Error::invalid_data;
    trace("begin");
    FILE* file=std::fopen(path,"rb");if(!file)return Error::invalid_data;
    trace("opened");
    bool okay=std::fseek(file,0,SEEK_END)==0;const auto size=okay?std::ftell(file):-1;
    if(size<128||size>8*1024*1024){std::fclose(file);return Error::budget;}
    void* bytes=std::malloc(std::size_t(size));if(!bytes){std::fclose(file);return Error::out_of_memory;}
    trace("allocated");
    okay=std::fseek(file,0,SEEK_SET)==0&&std::fread(bytes,1,std::size_t(size),file)==std::size_t(size);
    trace("read");
    const bool closed=std::fclose(file)==0;ModelBank bank;
    trace("closed");
    auto error=okay&&closed?ModelBank::open(bytes,std::size_t(size),bank):Error::invalid_data;
    if(error==Error::none)error=validate(bank,binding);
    trace("validated");
    if(error==Error::none){item.model.reset();
        // The compositor draws canvas spans directly; it never consumes the
        // world's matrix-palette copy. Keep just the geometry it actually uses.
        for(;;){error=item.model.upload(backend_,bank,0,load_trace_);
            if((error!=Error::budget&&error!=Error::capacity)||!evict())break;
        }
    }
    trace(error==Error::none?"uploaded":"failed");
    std::free(bytes);metrics_.source_peak_bytes=std::max(metrics_.source_peak_bytes,std::size_t(size));
    if(timings_)metrics_.load_ticks+=svcGetSystemTick()-start;
    if(error==Error::none){item.binding=&binding;item.used=serial_;item.pinned=true;++metrics_.loads;}
    return error;
}
Error SceneRenderer::prepare(const SceneView& scene) noexcept {
    skip_.fill(false);
    for(auto& item:resident_){item.pinned=false;item.required_geometry=false;item.required_images={};}
    for(auto& item:image_resident_){item.pinned=false;item.required={};}
    for(auto& item:canvas_resident_)item.pinned=false;
    for(std::size_t i=0;i<scene.image_layers;++i){const auto& layer=scene.image_layer[i];
        if(layer.source==LayerSource::images){if(auto* item=find_images(layer.bank_id))item->pinned=true;}
        else if(layer.source==LayerSource::canvas){if(auto* item=find_canvas(layer.bank_id))item->pinned=true;}
    }
    // Protect both namespaces before selecting slots or evicting anything.
    for(std::size_t i=0;i<scene.occurrences;++i){const auto& o=scene.occurrence[i];
        if(o.kind==Occurrence::Kind::quads&&scene.quad_state[o.state].bank==QuadState::Bank::images){
            if(auto* item=find_images(o.file_id))item->pinned=true;
        }else if(auto* item=find(o.file_id))item->pinned=true;
    }
    for(std::size_t i=0;i<scene.occurrences;++i){const auto& occurrence=scene.occurrence[i];
        if((occurrence.kind==Occurrence::Kind::quads||occurrence.kind==Occurrence::Kind::mesh)&&occurrence.file_id==untextured_quads)continue;
        if(occurrence.kind==Occurrence::Kind::quads&&scene.quad_state[occurrence.state].bank==QuadState::Bank::images){
            const auto error=require_image(occurrence.file_id,scene.quad_state[occurrence.state].image);
            if(error!=Error::none){if(lenient_&&i<skip_.size()){skip_[i]=true;continue;}return error;}
            continue;
        }
        const auto* binding=catalog_.find(occurrence.file_id);
        if(!binding){if(lenient_&&i<skip_.size()){skip_[i]=true;continue;}return Error::invalid_data;}
        Resident* slot=find(occurrence.file_id);
        if(!slot){
            for(auto& item:resident_)if(!item.pinned&&(!slot||!item.binding||item.used<slot->used)){
                slot=&item;if(!item.binding)break;
            }
            if(!slot){if(lenient_&&i<skip_.size()){skip_[i]=true;continue;}return Error::capacity;}
            if(slot->binding){slot->model.reset();++metrics_.evictions;}
            slot->binding=binding;slot->required_images={};slot->required_geometry=false;
        }
        slot->pinned=true;slot->used=serial_;
        if(occurrence.kind==Occurrence::Kind::model||occurrence.kind==Occurrence::Kind::skin)slot->required_geometry=true;
        else{
            const auto image=occurrence.kind==Occurrence::Kind::mesh?scene.part[occurrence.state].image:scene.quad_state[occurrence.state].image;
            if(image>=binding->images){if(lenient_&&i<skip_.size()){skip_[i]=true;continue;}return Error::invalid_data;}
            slot->required_images[image/32]|=1u<<(image%32);
        }
    }
    for(std::size_t i=0;i<scene.image_layers;++i){const auto& image=scene.image_layer[i];
        if(image.source==LayerSource::images){const auto error=require_image(image.bank_id,image.image);if(error!=Error::none)return error;}
        else if(image.source==LayerSource::canvas){
            const auto* binding=canvases_.find(image.bank_id);if(!binding||image.part>=binding->parts)return Error::invalid_data;
            auto* slot=find_canvas(image.bank_id);
            if(!slot){for(auto& item:canvas_resident_)if(!item.pinned&&(!slot||!item.binding||item.used<slot->used)){
                    slot=&item;if(!item.binding)break;
                }
                if(!slot)return Error::capacity;
                if(slot->binding){slot->model.reset();++metrics_.evictions;}
                slot->binding=binding;
            }
            slot->pinned=true;slot->used=serial_;
        }
    }
    for(auto& item:resident_)if(item.pinned){
        bool missing=item.required_geometry&&!item.model.geometry_ready();
        for(unsigned i=0;!missing&&i<item.binding->images;++i)
            if((item.required_images[i/32]&(1u<<(i%32)))&&!item.model.image(i))missing=true;
        if(missing){const auto error=load(item,*item.binding);
            if(error!=Error::none){
                record_load_failure(item.binding->file_id,error);
                if(!lenient_)return error;
                // Playable mode: drop every occurrence of the failed file.
                for(std::size_t i=0;i<scene.occurrences&&i<skip_.size();++i)
                    if(scene.occurrence[i].file_id==item.binding->file_id)skip_[i]=true;
                item.model.reset();item.binding=nullptr;item.pinned=false;
            }
        }
    }
    for(auto& item:image_resident_)if(item.pinned){
        bool missing=false;
        for(unsigned i=0;!missing&&i<item.binding->images;++i)missing=item.required[i]&&!item.images.image(i);
        if(missing){const auto error=load(item,*item.binding);
            if(error!=Error::none){
                record_load_failure(item.binding->bank_id,error);
                if(!lenient_)return error;
                for(std::size_t i=0;i<scene.occurrences&&i<skip_.size();++i){const auto& o=scene.occurrence[i];
                    if(o.kind==Occurrence::Kind::quads&&scene.quad_state[o.state].bank==QuadState::Bank::images&&o.file_id==item.binding->bank_id)skip_[i]=true;}
                item.images.reset();item.binding=nullptr;item.pinned=false;
            }
        }
    }
    for(auto& item:canvas_resident_)if(item.pinned&&!item.model.geometry_ready()){
        const auto error=load(item,*item.binding);if(error!=Error::none){record_load_failure(item.binding->bank_id,error);return error;}
    }
    return Error::none;
}
void SceneRenderer::record_load_failure(std::uint32_t bank,Error error) noexcept {
    const bool changed=metrics_.failed_bank!=bank||metrics_.load_error!=error;
    ++metrics_.load_failures;metrics_.failed_bank=bank;metrics_.load_error=error;
    if(changed||metrics_.load_failures<=3)std::printf("Native bank FAILED: bank=%lu error=%s failures=%llu serial=%llu linearFree=%u appFree=%u\n",
        (unsigned long)bank,describe(error),(unsigned long long)metrics_.load_failures,(unsigned long long)serial_,
        unsigned(linearSpaceFree()),unsigned(osGetMemRegionFree(MEMREGION_APPLICATION)));
}
Error SceneRenderer::require_image(std::uint32_t id,std::uint16_t image) noexcept {
    const auto* binding=images_.find(id);if(!binding||image>=binding->images)return Error::invalid_data;
    auto* slot=find_images(id);
    if(!slot){
        for(auto& item:image_resident_)if(!item.pinned&&(!slot||!item.binding||item.used<slot->used)){
            slot=&item;if(!item.binding)break;
        }
        if(!slot)return Error::capacity;
        if(slot->binding){slot->images.reset();++metrics_.evictions;}
        slot->binding=binding;slot->required={};
    }
    slot->pinned=true;slot->used=serial_;slot->required[image]=true;return Error::none;
}
Error SceneRenderer::layer_storage(std::size_t count) noexcept {
    if(count<=layer_capacity_)return Error::none;
    std::size_t capacity=2;while(capacity<count)capacity*=2;
    auto* next=static_cast<ScreenLayer*>(std::malloc(capacity*sizeof(ScreenLayer)));
    if(!next)return Error::out_of_memory;
    for(std::size_t i=0;i<capacity;++i)new(next+i)ScreenLayer{};
    std::free(layers_);layers_=next;layer_capacity_=capacity;
    metrics_.composition_bytes=capacity*sizeof(ScreenLayer);
    metrics_.composition_peak_bytes=std::max(metrics_.composition_peak_bytes,metrics_.composition_bytes);
    return Error::none;
}
Error SceneRenderer::compose(const SceneView& scene,Screen output,const OrderedLayers& order) noexcept {
    ScreenLayer geometry;geometry.rectangle={0,0,float(screen_width(output)),240};
    geometry.material.texture=scene_surface_;
    geometry.material.brightness=std::int8_t(geometry_tone_);
    geometry.material.uv={0,-240.f/256,240.f/256,-float(screen_width(output))/512,0,float(screen_width(output))/512};
    ScreenCanvas top,bottom;top.brightness=screen_tone_[0];bottom.brightness=screen_tone_[1];
    if(!scene.image_layers&&!interface_[0].visible&&!interface_[1].visible){
        auto& selected=output==Screen::top?top:bottom;selected.layer=&geometry;selected.layers=1;
        return compositor_.draw(top,bottom);
    }
    std::size_t cursor=0;
    for(unsigned side=0;side<2;++side){
      auto& canvas=side?bottom:top;canvas.layer=layers_+cursor;
      const auto count=side?order.bottom:order.top;
      const auto offset=side?order.top:0;
      for(std::size_t j=0;j<count;++j){const auto id=order.index[offset+j];
        auto& layer=layers_[cursor++];++canvas.layers;
        if(id==OrderedLayers::scene)layer=geometry;
        else{const auto error=screen_layer(scene.image_layer[id],layer);if(error!=Error::none)return error;}
      }
      const auto channel=side?0u:1u;const auto& ui=interface_[channel];
      if(ui.visible){auto& layer=layers_[cursor++];layer={};++canvas.layers;
        const auto& crop=ui.crop;
        layer.rectangle={float(crop.x+(channel?140:0)),float(crop.y+(channel?26:0)),float(crop.width),float(crop.height)};
        layer.material.texture=ui.texture;layer.material.blend=Blend::alpha;
        layer.material.uv={float(crop.width)/crop.texture_width,0,0,0,float(crop.height)/crop.texture_height,0};
      }
    }
    return compositor_.draw(top,bottom);
}
Error SceneRenderer::screen_layer(const ImageLayer& image,ScreenLayer& layer) noexcept {
    layer={};layer.rectangle=image.rectangle;layer.material=image.material;layer.clip=image.clip;
    if(image.source==LayerSource::images){const auto* resident=find_images(image.bank_id);
        if(!resident)return Error::invalid_data;
        layer.material.texture=resident->images.image(image.image);
        if(image.vertices){layer.mesh=quad_stream_;layer.first_index=image.first_vertex/4*6;layer.index_count=image.vertices/4*6;}
    }else if(image.source==LayerSource::canvas){
        const auto* resident=find_canvas(image.bank_id);if(!resident)return Error::invalid_data;
        NativeModel::DrawSpan span;const auto error=resident->model.span(image.part,span);if(error!=Error::none)return error;
        layer.mesh=span.mesh;layer.first_index=span.first;layer.index_count=span.count;layer.material.texture=span.material.texture;
    }
    return image.source!=LayerSource::solid&&!layer.material.texture?Error::invalid_data:Error::none;
}
Error SceneRenderer::upload_plane(unsigned index,const std::uint32_t* argb,Texture& texture,bool sharp,bool* changed,std::uint32_t candidates) noexcept {
    // Without memory the upload simply stays a full re-tile (the old path).
    constexpr std::size_t bytes=std::size_t(256)*192*4;
    auto*& shadow=frame_shadow_[index];
    if(!shadow){shadow=static_cast<std::uint32_t*>(std::malloc(bytes));frame_shadow_valid_[index]=false;}
    // Bands are compared only against a shadow that a complete upload of
    // this plane filled. Until then: the full upload, then remember it.
    const bool valid=shadow&&frame_shadow_valid_[index];
    const auto error=backend_.upload_frame_image(argb,texture,sharp,valid?shadow:nullptr,changed,candidates);
    if(shadow&&!valid&&error==Error::none){std::memcpy(shadow,argb,bytes);frame_shadow_valid_[index]=true;}
    return error;
}
Error SceneRenderer::initialize_map_surfaces() noexcept {
    if(map_sharp_)return Error::none;
    map_drawn_=false;map_dirty_=true;
    auto error=backend_.create_surface(256,256,false,map_surface_);
    if(error==Error::none)error=backend_.create_surface(512,512,false,map_sharp_,true);
    if(error!=Error::none){
        if(map_sharp_)backend_.destroy(map_sharp_);
        if(map_surface_)backend_.destroy(map_surface_);
        map_sharp_=map_surface_={};
    }
    return error;
}
Error SceneRenderer::ensure_screen_quad() noexcept {
    if(map_quad_)return Error::none;
    constexpr Vertex vertices[]={{0,0,0,0,0,255,255,255,255},{1,0,0,1,0,255,255,255,255},
        {1,1,0,1,1,255,255,255,255},{0,1,0,0,1,255,255,255,255}};
    constexpr std::uint16_t indices[]={0,1,2,0,2,3};
    return backend_.create_mesh({vertices,4,indices,6},map_quad_);
}
Error SceneRenderer::upload_shadows(const SceneView& scene) noexcept {
    // Upload phase. Every volume shares the projection, so their vertices are
    // placed in view space here (at most 64 per volume) and one stream holds
    // them all: two draws per image instead of two per shadow.
    shadow_vertex_count_=0;
    if(!scene.shadows)return Error::none;
    if(!shadow_vertices_)shadow_vertices_=static_cast<Vertex*>(std::malloc(max_shadow_vertices*sizeof(Vertex)));
    if(!shadow_vertices_)return Error::out_of_memory;
    if(!shadow_stream_){const auto error=backend_.create_quad_stream(max_shadow_vertices/4,shadow_stream_,false);if(error!=Error::none)return error;}
    const auto count=build_shadow_quads(scene.shadow,scene.shadows,shadow_vertices_,max_shadow_vertices);
    if(!count)return Error::none;
    const auto error=backend_.update_quad_stream(shadow_stream_,shadow_vertices_,count);
    if(error==Error::none)shadow_vertex_count_=count;
    return error;
}
Error SceneRenderer::draw_shadows(const SceneView& scene) noexcept {
    // After every occurrence, as the DS draws its translucent polygons after
    // the opaque ones: the mask over all far faces, then the darkening over
    // all near faces.
    if(!shadow_vertex_count_||!shadow_stream_)return Error::none;
    auto error=backend_.draw_shadow(shadow_stream_,scene.shadow_projection,true,1);
    if(error==Error::none)error=backend_.draw_shadow(shadow_stream_,scene.shadow_projection,false,1);
    return lenient_?Error::none:error;
}
Error SceneRenderer::initialize_bridge_surfaces() noexcept {
    // Render targets exist outside frames only; decide each plane once.
    const std::uint32_t* planes[4]={bridge_.back,bridge_.front,bridge_.other,bridge_.sky};
    static const bool gpu=[](){const char* e=std::getenv("SM64DS_NATIVE_GPU_PLANES");return !e||*e!='0';}();
    for(unsigned k=0;k<4;++k){
        if(!planes[k]||bridge_path_[k]!=BridgePath::undecided)continue;
        bridge_path_[k]=BridgePath::cpu;
        if(!gpu||k>1)continue;
        // 1 MiB surface plus a margin, and the map's two surfaces if absent.
        const std::size_t need=(std::size_t(1)<<20)+(std::size_t(1)<<18)+(map_sharp_?0:(std::size_t(5)<<18));
        if(vramSpaceFree()<need)continue;
        if(backend_.create_surface(512,512,false,bridge_texture_[k],true,true)!=Error::none){bridge_texture_[k]={};continue;}
        bridge_path_[k]=BridgePath::gpu;bridge_surface_ready_[k]=false;bridge_expand_[k]=true;
    }
    if(gpu){static bool reported=false;if(!reported&&bridge_path_[0]!=BridgePath::undecided){reported=true;
        std::printf("Native 2D planes: back=%s front=%s vramFree=%lu\n",bridge_path_[0]==BridgePath::gpu?"gpu":"cpu",
            bridge_path_[1]==BridgePath::gpu?"gpu":bridge_path_[1]==BridgePath::cpu?"cpu":"later",(unsigned long)vramSpaceFree());}}
    return Error::none;
}
Error SceneRenderer::expand_bridge_planes() noexcept {
    // Same transform as draw_map's sharp step: the nearest 2x image lands in
    // the high texture rows the compositor samples. The map reaches its sharp
    // surface in two passes and each pass mirrors the rows; a plane takes one
    // pass from its CPU-tiled base, so its rows are sampled mirrored here.
    // BUILD114-130 left them as drawn: on the Old 3DS the whole top-screen
    // HUD (and any 2D sky) came out upside down.
    const Citro3DBackend::Viewport sharp_area{0,0,512,384};
    const Matrix pose{{{2,0,0,-1},{0,-2,0,1},{0,0,0,-.5f},{0,0,0,1}}};
    const std::uint32_t* planes[4]={bridge_.back,bridge_.front,bridge_.other,bridge_.sky};
    for(unsigned k=0;k<4;++k){
        if(bridge_path_[k]!=BridgePath::gpu||!planes[k]||!bridge_expand_[k]||!bridge_base_[k]||!bridge_texture_[k])continue;
        // The front plane is mostly transparent and the alpha test drops
        // those texels: without a clear, what the HUD drew before stays.
        auto error=backend_.pass(backend_.surface(bridge_texture_[k]),k==1||!bridge_surface_ready_[k],0,&sharp_area);
        auto material=screen_material();material.texture=bridge_base_[k];material.blend=Blend::opaque;
        material.uv={1,0,0,0,-192.f/256,192.f/256};
        if(error==Error::none)error=backend_.draw(map_quad_,material,pose);
        if(error!=Error::none)return error;
        bridge_surface_ready_[k]=true;bridge_expand_[k]=false;
    }
    return Error::none;
}
Error SceneRenderer::upload_map(const AffineImage& image) noexcept {
    Error error=ensure_screen_quad();
    if(error!=Error::none)return error;
    if(map_identity_!=image.pixels||map_generation_!=image.generation){
        // BUILD123: a large map raster (512x512, 1 MiB) can meet a full GPU
        // budget in a heavy level. Evict unpinned resources like a bank load
        // does (prepare() has pinned this frame's), instead of failing the
        // frame: on hardware that left level 24's map on the CPU 2D path
        // (25 ms preparation, 786 KB upload per moving image).
        for(;;){
            error=backend_.upload_map_image(image.pixels,image.width,image.height,map_raster_);
            if((error!=Error::budget&&error!=Error::capacity&&error!=Error::out_of_memory)||!evict())break;
        }
        if(error==Error::none){map_identity_=image.pixels;map_generation_=image.generation;map_dirty_=true;}
    }
    // BUILD129: over the same base, the new foreground can differ from the
    // published one only inside the OBJ bands of either; every other band is
    // equal by construction (ppu_native_affine_bands) and skips its compare.
    std::uint32_t candidates=~0u;
    if(map_hint_valid_&&map_foreground_&&image.foreground_bands!=~0u&&map_hint_bands_!=~0u&&
       map_hint_base_==image.foreground_base&&map_hint_generation_==image.foreground_base_generation)
        candidates=map_hint_bands_|image.foreground_bands;
    bool foreground_changed=true;
    if(error==Error::none)error=upload_plane(4,image.foreground,map_foreground_,false,&foreground_changed,candidates);
    if(error!=Error::none||foreground_changed)map_dirty_=true;
    map_hint_valid_=error==Error::none;
    map_hint_base_=image.foreground_base;map_hint_generation_=image.foreground_base_generation;map_hint_bands_=image.foreground_bands;
    return error;
}
Error SceneRenderer::draw_map(const AffineImage& image) noexcept {
    const auto& last=map_drawn_image_;
    if(map_drawn_&&!map_dirty_&&map_sharp_&&last.pixels==image.pixels&&last.generation==image.generation&&
       last.width==image.width&&last.height==image.height&&last.x==image.x&&last.y==image.y&&
       last.xx==image.xx&&last.xy==image.xy&&last.yx==image.yx&&last.yy==image.yy&&
       last.backdrop==image.backdrop&&last.wrap==image.wrap)return Error::none;
    map_drawn_=false;
    const auto error=draw_map_passes(image);
    if(error==Error::none){map_drawn_=true;map_dirty_=false;map_drawn_image_=image;}
    return error;
}
Error SceneRenderer::draw_map_passes(const AffineImage& image) noexcept {
    // PICA places a short viewport in the high physical texture rows itself.
    const Citro3DBackend::Viewport base_area{0,0,256,192},sharp_area{0,0,512,384};
    const auto backdrop=(image.backdrop<<8)|(image.backdrop>>24);
    auto error=backend_.pass(backend_.surface(map_surface_),true,backdrop,&base_area);
    Matrix pose{{{2,0,0,-1},{0,-2,0,1},{0,0,0,-.5f},{0,0,0,1}}};
    auto material=screen_material();material.texture=map_raster_;material.blend=Blend::alpha;
    material.wrap_s=material.wrap_t=image.wrap?Wrap::repeat:Wrap::transparent_border;
    material.uv=affine_image_uv(image);
    if(error==Error::none)error=backend_.draw(map_quad_,material,pose);
    material=screen_material();material.texture=map_foreground_;material.blend=Blend::alpha;
    material.uv={1,0,0,0,192.f/256,0};
    if(error==Error::none)error=backend_.draw(map_quad_,material,pose);
    // Preserve the existing sharp display filter: make the nearest 2x image
    // on the GPU, then sample it linearly at the LCD's 1.25x scale.
    if(error==Error::none)error=backend_.pass(backend_.surface(map_sharp_),true,0,&sharp_area);
    material.texture=map_surface_;material.blend=Blend::opaque;
    if(error==Error::none)error=backend_.draw(map_quad_,material,pose);
    return error;
}
Error SceneRenderer::compose_bridge(const SceneView& scene,Screen output,const OrderedLayers& order) noexcept {
    // PLAYABLE MODE temporary 2D bridge. DS 256x192 planes are scaled to 4:3
    // (top: 320x240 centred, bottom: full). Native 3D keeps its own aspect.
    const auto error=layer_storage(16+scene.image_layers);if(error!=Error::none)return error;
    auto plane=[&](ScreenLayer& layer,Texture texture,Screen screen,bool blend){
        layer={};layer.material.texture=texture;layer.material.blend=blend?Blend::alpha:Blend::opaque;
        layer.rectangle=screen==Screen::top?ScreenRect{40,0,320,240}:ScreenRect{0,0,320,240};
        layer.material.uv={1,0,0,0,192.f/256,0}; // same convention as interface crops
    };
    ScreenCanvas top,bottom;std::size_t cursor=0;
    auto native_layers=[&](Screen side){
        const auto offset=side==Screen::top?0:order.top,count=side==Screen::top?order.top:order.bottom;
        for(std::size_t i=0;i<count;++i){const auto index=order.index[offset+i];if(index==OrderedLayers::scene)continue;
            const auto result=screen_layer(scene.image_layer[index],layers_[cursor]);if(result!=Error::none)return result;++cursor;
        }
        return Error::none;
    };
    auto& three=output==Screen::top?top:bottom;auto& other=output==Screen::top?bottom:top;
    three.layer=layers_;
    // Opaque planes on the 400-wide top LCD: extend their edge columns into the
    // side bars (sky/background continues instead of black borders).
    auto extend=[&](Texture texture,bool blend=false){
        for(unsigned side=0;side<2;++side){auto& layer=layers_[cursor++];layer={};layer.material.texture=texture;
            layer.material.blend=blend?Blend::alpha:Blend::opaque;
            layer.rectangle=side?ScreenRect{360,0,40,240}:ScreenRect{0,0,40,240};
            layer.material.uv={0,0,side?511.f/512:1.f/512,0,192.f/256,0};}
    };
    if(bridge_.back&&bridge_texture_[0]){plane(layers_[cursor++],bridge_texture_[0],output,false);if(output==Screen::top&&bridge_.extend_back)extend(bridge_texture_[0]);}
    if(bridge_.geometry){auto& geometry=layers_[cursor++];geometry={};
        geometry.rectangle={0,0,float(screen_width(output)),240};
        geometry.material.texture=scene_surface_;geometry.material.blend=bridge_.back?Blend::alpha:Blend::opaque;
        geometry.material.uv={0,-240.f/256,240.f/256,-float(screen_width(output))/512,0,float(screen_width(output))/512};
        if(bridge_.geometry_tone_clip.enabled()){
            auto& toned=layers_[cursor++];toned=geometry;toned.clip=bridge_.geometry_tone_clip;
            toned.material.brightness=bridge_.geometry_tone;
        }
    }
    if(bridge_.front&&bridge_texture_[1]){
        plane(layers_[cursor++],bridge_texture_[1],output,true);
        if(output==Screen::top&&bridge_.extend_front)extend(bridge_texture_[1],true);
    }
    const auto native_three=native_layers(output);if(native_three!=Error::none)return native_three;
    const auto ui_three=output==Screen::top?1u:0u;
    for(unsigned channel=0;channel<2;++channel){const auto& ui=interface_[channel];if(!ui.visible)continue;
        const bool on_three=channel==ui_three;if(!on_three)continue;
        auto& layer=layers_[cursor++];layer={};const auto& crop=ui.crop;
        layer.rectangle={float(crop.x+(channel?140:0)),float(crop.y+(channel?26:0)),float(crop.width),float(crop.height)};
        layer.material.texture=ui.texture;layer.material.blend=Blend::alpha;
        layer.material.uv={float(crop.width)/crop.texture_width,0,0,0,float(crop.height)/crop.texture_height,0};
    }
    three.layers=cursor;
    other.layer=layers_+cursor;const auto start=cursor;
    if(output==Screen::bottom&&bridge_.sky&&bridge_texture_[3]){
        for(unsigned side=0;side<2;++side){auto& layer=layers_[cursor++];layer={};
            layer.material.texture=bridge_texture_[3];
            layer.rectangle={side?360.f:0.f,0,40,240};
            layer.material.uv={32.f/256,0,side?32.f/256:0.f,0,192.f/256,0};
        }
    }
    if(bridge_.other_affine)plane(layers_[cursor++],map_sharp_,output==Screen::top?Screen::bottom:Screen::top,false);
    else if(bridge_.other&&bridge_texture_[2]){plane(layers_[cursor++],bridge_texture_[2],output==Screen::top?Screen::bottom:Screen::top,false);
        if(output!=Screen::top&&bridge_.extend_other)extend(bridge_texture_[2]);}
    const auto native_other=native_layers(output==Screen::top?Screen::bottom:Screen::top);if(native_other!=Error::none)return native_other;
    for(unsigned channel=0;channel<2;++channel){const auto& ui=interface_[channel];if(!ui.visible||channel==ui_three)continue;
        auto& layer=layers_[cursor++];layer={};const auto& crop=ui.crop;
        layer.rectangle={float(crop.x+(channel?140:0)),float(crop.y+(channel?26:0)),float(crop.width),float(crop.height)};
        layer.material.texture=ui.texture;layer.material.blend=Blend::alpha;
        layer.material.uv={float(crop.width)/crop.texture_width,0,0,0,float(crop.height)/crop.texture_height,0};
    }
    other.layers=cursor-start;
    three.brightness=bridge_.brightness_three;other.brightness=bridge_.brightness_other;
    return compositor_.draw(top,bottom);
}
Error SceneRenderer::draw(const SceneView& scene,Screen output) noexcept {
    return draw_frame(scene,output,nullptr);
}
Error SceneRenderer::draw(const SceneView& scene,Screen output,const std::array<InterfacePublication,2>& images) noexcept {
    return draw_frame(scene,output,&images);
}
Error SceneRenderer::draw_second(const SceneView& scene,Screen output,bool fence,int raised_priority,int normal_priority) noexcept {
    second_=true;second_fence_=fence;second_raised_=raised_priority;second_normal_=normal_priority;
    const auto error=draw_frame(scene,output,nullptr);
    second_=false;return error;
}
Error SceneRenderer::draw_frame(const SceneView& scene,Screen output,const std::array<InterfacePublication,2>* images) noexcept {
    if(!ready_)return Error::invalid_phase;
    static const bool profile=[](){const auto* value=std::getenv("SM64DS_NATIVE_STAGE_PROFILE");return value&&*value=='1';}();
    const bool sample=profile&&scene.serial%240==0;
    // BUILD121: every frame keeps its stage times (10 tick reads) for the
    // test reports; the log line stays opt-in.
    std::array<std::uint64_t,10> stage{};unsigned stages=0;stage_ticks_.fill(0);
    const auto mark=[&](){if(stages<stage.size())stage[stages++]=svcGetSystemTick();};
    mark();
    completed_target_=false;
    if(!screen_width(output))return Error::invalid_data;
    if(images)for(unsigned c=0;c<2;++c){const auto& p=(*images)[c];if(!p.image.pixels)continue;
        if(!p.revision||validate(p.image)!=Error::none||p.image.width!=(c?256u:320u)||p.image.height!=(c?16u:240u))return Error::invalid_data;
    }
    OrderedLayers order;auto error=order_layers(scene.image_layer,scene.image_layers,output,scene.scene_order,order);
    if(error!=Error::none)return error;
    if(order.top+unsigned(interface_[1].visible)>ScreenCanvas::max_layers||
       order.bottom+unsigned(interface_[0].visible)>ScreenCanvas::max_layers)return Error::capacity;
    // LCD composition exists, but original transparent-clear/backdrop and
    // polygon policies still need their semantic producers. Never substitute
    // an opaque background silently in an original scene.
    if(!lenient_&&((scene.clear.rgba&255u)!=255u||scene.clear.polygon_id!=63))return Error::unsupported;
    if((scene.occurrences&&!scene.occurrence)||(scene.parts&&!scene.part))return Error::invalid_data;
    if(scene.quad_states&&!scene.quad_state)return Error::invalid_data;
    // BUILD134: take the UV domain here; the quad stream then copies the
    // vertices without reading them all a second time to validate them.
    // BUILD135: an overlapped second image is the endpoint of the pair whose
    // interpolated image was drawn a moment ago from the same leased frame;
    // PoseHistory::midpoint replaces only the part matrices and the skin
    // positions. Quads, shadows, the map and the 2D planes are therefore the
    // bytes just validated and uploaded; they are not read or written again.
    if(scene.quad_vertices&&!second_){const auto error=validate_quads(scene.quad_vertex,scene.quad_vertices,cuts_?&quad_domain_:nullptr);if(error!=Error::none)return error;}
    for(std::size_t i=0;i<scene.image_layers;++i){const auto& image=scene.image_layer[i];
        if(image.vertices&&(image.first_vertex>scene.quad_vertices||image.vertices>scene.quad_vertices-image.first_vertex))return Error::invalid_data;
    }
    for(std::size_t i=0;i<scene.quad_states;++i){const auto& state=scene.quad_state[i];
        if(!valid(state))return Error::invalid_data;
        if(!lenient_&&(state.polygon_id!=255||state.fog))return Error::unsupported;
    }
    for(std::size_t i=0;i<scene.occurrences;++i){const auto& o=scene.occurrence[i];
        if(o.kind==Occurrence::Kind::model){if(o.file_id>65535||o.first>scene.parts||o.count>scene.parts-o.first)return Error::invalid_data;}
        else if(o.kind==Occurrence::Kind::skin){
            if(o.file_id>65535||o.state>=scene.parts||!o.count||o.count%3||o.first>scene.skin_vertices||o.count>scene.skin_vertices-o.first)return Error::invalid_data;}
        else if(o.kind==Occurrence::Kind::mesh){
            if(o.state>=scene.parts||!o.count||o.count%3||o.first>scene.skin_vertices||o.count>scene.skin_vertices-o.first)return Error::invalid_data;
            if(scene.part[o.state].image==ModelBank::no_texture?o.file_id!=untextured_quads:o.file_id>65535)return Error::invalid_data;}
        else if(o.kind==Occurrence::Kind::quads){
            if(o.state>=scene.quad_states||o.first%4||!o.count||o.count%4||o.first>scene.quad_vertices||o.count>scene.quad_vertices-o.first)
                return Error::invalid_data;
            if(scene.quad_state[o.state].image==QuadState::no_image?o.file_id!=untextured_quads:o.file_id>65535)return Error::invalid_data;
        }else return Error::invalid_data;
    }
    if(lenient_&&scene.occurrences>skip_.size())return Error::capacity;
    if(bridge_active_&&bridge_.other_affine){
        if(!valid(*bridge_.other_affine))return Error::invalid_data;
        error=initialize_map_surfaces();if(error!=Error::none)return error;
    }
    if(bridge_active_){error=initialize_bridge_surfaces();if(error!=Error::none)return error;}
    if(scene.lighting.state_count>LightingPalette::max_states)return Error::invalid_data;
    for(unsigned i=0;i<scene.lighting.state_count;++i){LightingInput input;
        const auto result=resolve_lighting(scene.lighting,std::uint16_t(i),input);if(result!=Error::none)return result;}
    if(scene.image_layers||images||interface_[0].visible||interface_[1].visible){
        error=layer_storage(scene.image_layers+3);
        if(error!=Error::none)return error;
    }
    // The game loop already paces 30 Hz; waiting for another VBlank here cost
    // up to a full display frame per update. FrameBegin(0) keeps the GPU fence.
    if(trace_frames_)std::printf("Native resource frame: serial=%llu phase=fence-begin\n",(unsigned long long)scene.serial);
    mark();error=second_?backend_.begin_overlapped():backend_.begin(pace_);if(error!=Error::none)return error;mark();
    if(second_)std::swap(skin_stream_,skin_stream_second_);
    if(trace_frames_)std::printf("Native resource frame: serial=%llu phase=fence-done\n",(unsigned long long)scene.serial);
    // Detach only after begin drained the previous queue. A failed submission
    // must never transfer its partly drawn target to the LCD.
    // (An overlapped second image keeps the links; nothing transfers it but
    // submit_overlapped, and relinking outside a C3D frame waits for the GPU.)
    if(!second_){C3D_RenderTargetSetOutput(nullptr,GFX_TOP,GFX_LEFT,0);
        C3D_RenderTargetSetOutput(nullptr,GFX_BOTTOM,GFX_LEFT,0);}
    target_->used=false;
    if(images)for(unsigned c=0;c<2&&error==Error::none;++c){const auto& p=(*images)[c];
        if(p.image.pixels)error=upload_interface(c,p.revision,p.image);else hide_interface(c);
    }
    if(!have_scene_||scene_!=scene.scene){
        for(auto& item:resident_){item.model.reset();item.binding=nullptr;item.pinned=false;}
        for(auto& item:image_resident_){item.images.reset();item.binding=nullptr;item.pinned=false;}
        for(auto& item:canvas_resident_){item.model.reset();item.binding=nullptr;item.pinned=false;}
        scene_=scene.scene;have_scene_=true;
    }
    serial_=scene.serial;
    const auto loads_before=metrics_.loads;
    if(error==Error::none)error=prepare(scene);
    if(load_trace_&&metrics_.loads!=loads_before)trace_frames_=2;
    const bool trace_frame=trace_frames_!=0;
    const auto trace_frame_stage=[&](const char* phase){if(trace_frame)std::printf(
        "Native resource frame: serial=%llu phase=%s status=%s quads=%u skinVertices=%u layers=%u\n",
        (unsigned long long)serial_,phase,describe(error),unsigned(scene.quad_vertices/4),
        unsigned(scene.skin_vertices),unsigned(scene.image_layers));};
    trace_frame_stage("prepared");
    if(error==Error::none&&preload_count_){
        // prepare() has protected every resource needed by this visible frame.
        // Run on the GPU owner only; Old's sole core1 app slot remains audio's.
        for(unsigned i=0;i<preload_count_;++i){
            const auto request=preload_[i];const auto id=request.id;
            if(request.kind==PreloadKind::canvas){
                auto* slot=find_canvas(id);
                if(slot&&slot->model.geometry_ready())continue;
                if(!slot)for(auto& item:canvas_resident_)if(!item.binding){slot=&item;break;}
                if(!slot){std::printf("Native preload skipped: canvas=%lu no free slot\n",(unsigned long)id);continue;}
                const auto* binding=canvases_.find(id);const bool pinned=slot->pinned;slot->pinned=true;
                const auto loaded=load(*slot,*binding);
                if(loaded!=Error::none){slot->model.reset();slot->binding=nullptr;}
                slot->pinned=pinned;
                std::printf("Native preload: canvas=%lu serial=%llu status=%s\n",(unsigned long)id,(unsigned long long)serial_,describe(loaded));
                continue;
            }
            if(request.kind==PreloadKind::image_bank){
                auto* slot=find_images(id);
                if(!slot)for(auto& item:image_resident_)if(!item.binding){slot=&item;break;}
                if(!slot){std::printf("Native preload skipped: images=%lu no free slot\n",(unsigned long)id);continue;}
                const auto* binding=images_.find(id);const bool resident=slot->binding!=nullptr;
                const bool pinned=slot->pinned;slot->pinned=true;slot->required.fill(false);
                bool missing=false;
                for(unsigned n=0;n<binding->images;++n){slot->required[n]=true;missing|=!slot->images.image(n);}
                const auto loaded=missing?load(*slot,*binding):Error::none;
                // NativeImages stages additions; speculative failure cannot
                // destroy textures already referenced by this visible frame.
                if(loaded!=Error::none&&!resident){slot->images.reset();slot->binding=nullptr;}
                slot->pinned=pinned;
                std::printf("Native preload: images=%lu serial=%llu status=%s\n",(unsigned long)id,(unsigned long long)serial_,describe(loaded));
                continue;
            }
            auto* slot=find(id);
            if(request.kind==PreloadKind::model_images){
                if(!slot)for(auto& item:resident_)if(!item.binding){slot=&item;break;}
                if(!slot){std::printf("Native preload skipped: modelImages=%lu no free slot\n",(unsigned long)id);continue;}
                const auto* binding=catalog_.find(id);const bool resident=slot->binding!=nullptr;
                const bool pinned=slot->pinned,geometry=slot->required_geometry;
                slot->pinned=true;slot->required_geometry=false;slot->required_images={};
                bool missing=false;
                for(unsigned n=0;n<binding->images;++n){slot->required_images[n/32]|=1u<<(n%32);missing|=!slot->model.image(n);}
                const auto loaded=missing?load(*slot,*binding):Error::none;
                if(loaded!=Error::none&&!resident){slot->model.reset();slot->binding=nullptr;}
                slot->pinned=pinned;slot->required_geometry=geometry;
                std::printf("Native preload: modelImages=%lu serial=%llu status=%s\n",(unsigned long)id,(unsigned long long)serial_,describe(loaded));
                continue;
            }
            if(slot&&slot->model.geometry_ready())continue;
            // A visible image-only bank must not be destructively promoted by
            // speculation: failure would lose textures needed by this frame.
            if(slot&&slot->pinned){std::printf("Native preload skipped: model=%lu visible image bank\n",(unsigned long)id);continue;}
            if(!slot)for(auto& item:resident_)if(!item.binding){slot=&item;break;}
            if(!slot){std::printf("Native preload skipped: model=%lu no free slot\n",(unsigned long)id);continue;}
            const auto* binding=catalog_.find(id);slot->required_geometry=true;slot->pinned=true;
            const auto loaded=load(*slot,*binding);
            std::printf("Native preload: model=%lu serial=%llu status=%s\n",(unsigned long)id,(unsigned long long)serial_,describe(loaded));
            if(loaded!=Error::none){slot->model.reset();slot->binding=nullptr;slot->pinned=false;}
        }
        preload_count_=0;
    }
    mark();
    if(error==Error::none&&batches_)dynamic_ranges_.prepare(scene);
    if(error==Error::none&&scene.quad_vertices&&!second_){
        const auto required=scene.quad_vertices/4;
        if(required>quad_capacity_){
            if(quad_stream_){backend_.destroy(quad_stream_);quad_stream_={};quad_capacity_=0;}
            std::size_t capacity=1;while(capacity<required)capacity*=2;
            error=backend_.create_quad_stream(capacity,quad_stream_,batches_);
            if(error==Error::none)quad_capacity_=capacity;
        }
        if(error==Error::none)error=backend_.update_quad_stream(quad_stream_,scene.quad_vertex,scene.quad_vertices,batches_?dynamic_ranges_.quads():nullptr,
            cuts_?&quad_domain_:nullptr);
    }
    if(error==Error::none&&scene.skin_vertices){
        if(!skin_stream_)error=backend_.create_skin_stream(4095*3,skin_stream_,batches_);
        if(error==Error::none)error=backend_.update_skin_stream(skin_stream_,scene.skin_vertex,scene.skin_vertices,batches_?dynamic_ranges_.skin():nullptr);
        if(error!=Error::none&&lenient_){error=Error::none;for(std::size_t i=0;i<scene.occurrences&&i<skip_.size();++i)if(scene.occurrence[i].kind==Occurrence::Kind::skin||scene.occurrence[i].kind==Occurrence::Kind::mesh)skip_[i]=true;}
    }
    trace_frame_stage("dynamic");
    mark();
    // Validate all occurrence ranges before any draw or framebuffer clear.
    if(error==Error::none)for(std::size_t i=0;i<scene.occurrences;++i){const auto& o=scene.occurrence[i];
        if(lenient_&&i<skip_.size()&&skip_[i])continue;
        Error bad=Error::none;
        if(o.kind==Occurrence::Kind::model){const auto* item=find(o.file_id);
            if(!item||o.count!=item->model.parts())bad=Error::invalid_data;
            // Every lighting state of this image resolved above (BUILD134).
            else if(batches_)bad=item->model.validate_prepared(scene.part+o.first,o.count,scene.lighting,cuts_);
        }else if(o.kind==Occurrence::Kind::skin){const auto* item=find(o.file_id);if(!item||!item->model.geometry_ready())bad=Error::invalid_data;
        }else if(o.kind==Occurrence::Kind::mesh){const auto& p=scene.part[o.state];const auto* item=find(o.file_id);
            if(p.image!=ModelBank::no_texture&&(!item||!item->model.image(p.image)))bad=Error::invalid_data;
        }else if(o.file_id!=untextured_quads){const auto& state=scene.quad_state[o.state];
            const auto* images=state.bank==QuadState::Bank::images?find_images(o.file_id):nullptr;
            const auto* model=state.bank==QuadState::Bank::images?nullptr:find(o.file_id);
            const auto texture=images?images->images.image(state.image):model?model->model.image(state.image):Texture{};
            if(!texture)bad=Error::invalid_data;
        }
        if(bad!=Error::none){if(lenient_&&i<skip_.size()){skip_[i]=true;continue;}error=bad;break;}
    }
    if(error==Error::none&&bridge_active_){
        const std::uint32_t* planes[4]={bridge_.back,bridge_.front,bridge_.other,bridge_.sky};
        const bool dirty[4]={bridge_.back_dirty,bridge_.front_dirty,bridge_.other_dirty,bridge_.sky_dirty};
        if(planes[0]||planes[1]||planes[2]||planes[3])error=ensure_screen_quad();
        for(unsigned k=0;k<4&&error==Error::none;++k)
            if(!planes[k])continue;
            else if(bridge_path_[k]==BridgePath::gpu){
                if(dirty[k]||!bridge_base_[k]){
                    bool changed=false;
                    error=upload_plane(k,planes[k],bridge_base_[k],false,&changed);
                    bridge_expand_[k]=bridge_expand_[k]||changed;
                }
            }else if(dirty[k]||!bridge_texture_[k])error=upload_plane(k,planes[k],bridge_texture_[k],true);
        if(error==Error::none&&bridge_.other_affine&&!second_)error=upload_map(*bridge_.other_affine);
    }
    if(error==Error::none&&!second_){
        // A refused upload leaves the image without shadows.
        const auto uploaded=upload_shadows(scene);if(uploaded!=Error::none&&!lenient_)error=uploaded;
    }
    trace_frame_stage("preflight");
    mark();const bool batch_frame=error==Error::none&&batches_&&prepare_batches(scene);mark();
    trace_frame_stage("planned");
    const Citro3DBackend::Viewport area{0,0,240,screen_width(output)*scene_scale_};
    if(error==Error::none)error=backend_.pass(target_,true,scene.clear.rgba,&area);
    if(error==Error::none){
        // Fog experiment (off unless the frame carries enabled fog registers).
        const auto depth=fog_depth(scene.fog_projection);
        if(fog_drawable(scene.fog,depth)){
            if(!fog_table_valid_||!same(fog_source_,scene.fog)||fog_depth_.a!=depth.a||fog_depth_.b!=depth.b){
                build_fog_lut(scene.fog,depth,fog_table_.data());fog_source_=scene.fog;fog_depth_=depth;fog_table_valid_=true;
            }
            backend_.fog(fog_table_.data(),fog_colour(scene.fog));
        }
    }
    if(error==Error::none)for(std::size_t i=0,n=batch_frame?batch_commands_:scene.occurrences;i<n;++i){
        const auto occurrence=batch_frame?batch_command_[i].occurrence:i;
        if(lenient_&&occurrence<skip_.size()&&skip_[occurrence]){++skipped_total_;continue;}
        error=batch_frame&&batch_command_[i].batch?backend_.draw_batch(batch_plan_,batch_command_[i].span):
            draw_occurrence(scene,occurrence,batch_frame?batch_command_[i].part:-1);
        if(error!=Error::none){if(lenient_){error=Error::none;++skipped_total_;continue;}break;}
    }
    if(error==Error::none)error=draw_shadows(scene);
    trace_frame_stage("geometry");
    if(error==Error::none&&bridge_active_&&bridge_.other_affine)error=draw_map(*bridge_.other_affine);
    if(error==Error::none&&bridge_active_)error=expand_bridge_planes();
    if(second_)compositor_.present_only(output==Screen::top?0:1);
    mark();if(error==Error::none)error=bridge_active_?compose_bridge(scene,output,order):compose(scene,output,order);mark();
    compositor_.present_only(-1);
    trace_frame_stage("composed");
    const auto ended=second_?backend_.end_overlapped():backend_.end();if(error==Error::none)error=ended;
    if(second_){
        std::swap(skin_stream_,skin_stream_second_);
        if(error==Error::none)error=backend_.submit_overlapped(compositor_.target(output),second_fence_,second_raised_,second_normal_);
        else backend_.discard_overlapped();
    }
    mark();
    trace_frame_stage("submitted");
    if(trace_frames_)--trace_frames_;
    if(stages==stage.size())for(unsigned i=0;i+1<stage.size();++i)stage_ticks_[i]=std::uint32_t(stage[i+1]-stage[i]);
    if(sample){
        std::printf("Native render stages: serial=%llu scene=%llu",(unsigned long long)scene.serial,(unsigned long long)scene.scene);
        constexpr const char* names[]={"validate","wait","resources","dynamic","preflight","plan","geometry","compose","finish"};
        for(unsigned i=0;i<9;++i)std::printf(" %sMs=%.3f",names[i],double(stage[i+1]-stage[i])*1000/SYSCLOCK_ARM11);
        std::printf(" status=%s\n",describe(error));
    }
    if(error==Error::none){++metrics_.frames;completed_target_=true;}else ++metrics_.refused;
    return error;
}
bool SceneRenderer::prepare_batches(const SceneView& scene) noexcept {
    const auto start=timings_?svcGetSystemTick():0;batch_commands_=0;batch_plan_.reset();
    Error result=Error::none;unsigned source_draws=0;
    const auto command=[&](std::size_t occurrence,int part,bool batch,unsigned span){
        if(batch&&batch_commands_&&batch_command_[batch_commands_-1].batch&&batch_command_[batch_commands_-1].span==span)return true;
        if(batch_commands_==batch_command_.size())return false;
        batch_command_[batch_commands_++]={std::uint32_t(occurrence),part,span,batch};return true;
    };
    const auto append=[&](std::size_t occurrence,int part,BatchRange range,const Material& material,
        const Matrix& matrix,const LightingInput* lighting,bool highlight){
        // The plan borrows the matrix and light set: both live in the leased frame.
        unsigned span=0;auto e=range&&!highlight?batch_plan_.append(range,material,&matrix,lighting,span):Error::unsupported;
        if(e==Error::unsupported){batch_plan_.barrier();return command(occurrence,part,false,0)?Error::none:Error::capacity;}
        if(e!=Error::none)return e;
        ++source_draws;return command(occurrence,part,true,span)?Error::none:Error::capacity;
    };
    const auto prepared=[&](std::size_t occurrence,int part,BatchRange range,const PreparedPart& p,Texture texture){
        auto material=p.material;material.texture=texture;LightingInput input;
        if(p.lighting!=no_lighting){const auto& light=scene.lighting.state[p.lighting];
            input={light.material.mask?scene.lighting.lights+light.lights:nullptr,light.normal,light.material};}
        return append(occurrence,part,range,material,p.object_to_clip,p.lighting==no_lighting?nullptr:&input,p.highlight);
    };
    for(std::size_t i=0;i<scene.occurrences&&result==Error::none;++i){const auto& o=scene.occurrence[i];
        if(lenient_&&skip_[i]){batch_plan_.barrier();if(!command(i,-1,false,0))result=Error::capacity;continue;}
        if(o.kind==Occurrence::Kind::model){const auto& model=find(o.file_id)->model;
            for(unsigned p=0;p<o.count&&result==Error::none;++p){const auto& state=scene.part[o.first+p];
                if(!state.visible||model.empty_part(p))continue;
                const auto range=model.resident_range(p);
                result=prepared(i,int(p),range,state,state.image==ModelBank::no_texture?Texture{}:model.image(state.image));
            }
        }else if(o.kind==Occurrence::Kind::skin||o.kind==Occurrence::Kind::mesh){const auto& p=scene.part[o.state];
            if(!p.visible)continue;
            const auto texture=p.image==ModelBank::no_texture?Texture{}:find(o.file_id)->model.image(p.image);
            if(p.image!=ModelBank::no_texture&&!texture)continue;
            const auto range=dynamic_ranges_.eligible(unsigned(i))?
                BatchRange{skin_stream_,o.first,o.count,std::uint8_t(dynamic_ranges_.slot(unsigned(i)))}:BatchRange{};
            result=prepared(i,-1,range,p,texture);
        }else{const auto& state=scene.quad_state[o.state];auto material=state.material;material.fog=state.fog&&scene.fog.enabled;
            if(state.image!=QuadState::no_image)material.texture=state.bank==QuadState::Bank::images?
                find_images(o.file_id)->images.image(state.image):find(o.file_id)->model.image(state.image);
            const auto range=dynamic_ranges_.eligible(unsigned(i))?
                BatchRange{quad_stream_,o.first/4*6,o.count/4*6,std::uint8_t(dynamic_ranges_.slot(unsigned(i)))}:BatchRange{};
            result=append(i,-1,range,material,state.object_to_clip,nullptr,false);
        }
    }
    if(result==Error::none)result=batch_plan_.seal();
    const auto ticks=timings_?svcGetSystemTick()-start:0;metrics_.batch_prepare_ticks+=ticks;
    if(result!=Error::none){++metrics_.batch_fallbacks;
        if(metrics_.batch_fallbacks<=3)std::printf("Native resident palette fallback: serial=%llu reason=%s\n",(unsigned long long)serial_,describe(result));
        return false;
    }
    if(!source_draws)return false;
    ++metrics_.batch_frames;
    if(timings_&&(metrics_.batch_frames==1||serial_%240==0))std::printf(
        "Native resident palette: serial=%llu sources=%u spans=%u geometryUpload=0 prepareMs=%.3f\n",
        (unsigned long long)serial_,source_draws,unsigned(batch_plan_.spans()),double(ticks)*1000/SYSCLOCK_ARM11);
    return true;
}
Error SceneRenderer::draw_occurrence(const SceneView& scene,std::size_t index,int part) noexcept {
    const auto& o=scene.occurrence[index];
    if(o.kind==Occurrence::Kind::model){const auto& model=find(o.file_id)->model;
        return part<0?model.draw_prepared(scene.part+o.first,o.count,scene.lighting):
            model.draw_prepared_part(unsigned(part),scene.part[o.first+unsigned(part)],scene.lighting);
    }
    if(o.kind==Occurrence::Kind::skin||o.kind==Occurrence::Kind::mesh){const auto& p=scene.part[o.state];
        if(!p.visible)return Error::none;
        auto material=p.material;material.texture=p.image==ModelBank::no_texture?Texture{}:find(o.file_id)->model.image(p.image);
        if(p.image!=ModelBank::no_texture&&!material.texture)return Error::none;
        if(material.uv_mode==UvMode::fixed&&!backend_.fixed_uv_compatible(skin_stream_,material.fixed_uv)){material.uv_mode=UvMode::affine;material.uv={{1,0,0,0,1,0}};}
        if(material.uv_mode==UvMode::normal&&!backend_.normal_uv_compatible(skin_stream_,material.normal_uv())){material.uv_mode=UvMode::affine;material.uv={{1,0,0,0,1,0}};}
        LightingInput input;const bool lit=p.lighting!=no_lighting;
        if(lit){const auto& light=scene.lighting.state[p.lighting];input={light.material.mask?scene.lighting.lights+light.lights:nullptr,light.normal,light.material};}
        auto error=backend_.draw(skin_stream_,material,p.object_to_clip,o.first,o.count,lit?&input:nullptr);
        if(error==Error::none&&p.highlight&&lit){
            input.material.grey=false;input.material.toon=true;
            material.texture={};material.blend=Blend::highlight;material.depth_write=false;
            error=backend_.draw(skin_stream_,material,p.object_to_clip,o.first,o.count,&input);
        }
        return error;
    }
    const auto& state=scene.quad_state[o.state];auto material=state.material;material.fog=state.fog&&scene.fog.enabled;
    if(state.image!=QuadState::no_image)material.texture=state.bank==QuadState::Bank::images?
        find_images(o.file_id)->images.image(state.image):find(o.file_id)->model.image(state.image);
    return backend_.draw(quad_stream_,material,state.object_to_clip,o.first/4*6,o.count/4*6);
}
Error SceneRenderer::shutdown() noexcept {
    if(!ready_)return Error::invalid_phase;
    auto error=backend_.begin(false);if(error!=Error::none)return error;
    for(auto& item:resident_){item.model.reset();item.binding=nullptr;}
    for(auto& item:image_resident_){item.images.reset();item.binding=nullptr;}
    for(auto& item:canvas_resident_){item.model.reset();item.binding=nullptr;}
    for(auto& item:interface_){if(item.texture){error=backend_.destroy(item.texture);if(error!=Error::none){backend_.end();return error;}}item={};}
    if(quad_stream_){error=backend_.destroy(quad_stream_);if(error!=Error::none){backend_.end();return error;}}
    if(shadow_stream_){error=backend_.destroy(shadow_stream_);if(error!=Error::none){backend_.end();return error;}shadow_stream_={};}
    std::free(shadow_vertices_);shadow_vertices_=nullptr;shadow_vertex_count_=0;
    if(skin_stream_){error=backend_.destroy(skin_stream_);if(error!=Error::none){backend_.end();return error;}skin_stream_={};}
    if(skin_stream_second_){error=backend_.destroy(skin_stream_second_);if(error!=Error::none){backend_.end();return error;}skin_stream_second_={};}
    for(auto& texture:bridge_base_)if(texture){error=backend_.destroy(texture);if(error!=Error::none){backend_.end();return error;}texture={};}
    for(unsigned k=0;k<4;++k)if(bridge_path_[k]!=BridgePath::gpu&&bridge_texture_[k]){
        error=backend_.destroy(bridge_texture_[k]);if(error!=Error::none){backend_.end();return error;}bridge_texture_[k]={};}
    for(auto* texture:{&map_raster_,&map_foreground_})if(*texture){error=backend_.destroy(*texture);if(error!=Error::none){backend_.end();return error;}*texture={};}
    if(map_quad_){error=backend_.destroy(map_quad_);if(error!=Error::none){backend_.end();return error;}map_quad_={};}
    quad_stream_={};quad_capacity_=0;
    error=backend_.end();if(error!=Error::none)return error;
    error=compositor_.shutdown();if(error!=Error::none)return error;
    for(auto* texture:{&map_sharp_,&map_surface_})if(*texture){error=backend_.destroy(*texture);if(error!=Error::none)return error;*texture={};}
    for(auto& texture:bridge_texture_)if(texture){error=backend_.destroy(texture);if(error!=Error::none)return error;texture={};}
    bridge_expand_={};bridge_surface_ready_={};bridge_path_={};
    map_identity_=nullptr;map_generation_=0;
    error=backend_.destroy(scene_surface_);if(error!=Error::none)return error;
    scene_surface_={};target_=nullptr;
    error=backend_.shutdown();if(error!=Error::none)return error;
    std::free(layers_);layers_=nullptr;layer_capacity_=0;metrics_.composition_bytes=0;
    for(auto*& shadow:frame_shadow_){std::free(shadow);shadow=nullptr;}
    for(auto& valid:frame_shadow_valid_)valid=false;
    std::free(image_source_);image_source_=nullptr;image_source_binding_=nullptr;image_source_bank_={};metrics_.image_source_bytes=0;
    C3D_Fini();ready_=c3d_owned_=false;return Error::none;
}
Error SceneRenderer::capture_rgb8(void* destination,std::size_t bytes) noexcept {
    return capture_rgb8(Screen::top,destination,bytes);
}
Error SceneRenderer::capture_rgb8(Screen screen,void* destination,std::size_t bytes) noexcept {
    if(!ready_||!completed_target_)return Error::invalid_phase;
    return compositor_.capture(screen,destination,bytes);
}
}

namespace sm64ds::render {
Error SceneRenderer::capture_geometry(Screen screen,std::uint32_t* argb,std::size_t pixels) noexcept {
    if(!ready_||!completed_target_||!backend_.between_frames())return Error::invalid_phase;
    if(!argb||pixels!=256*192)return Error::invalid_data;
    const unsigned width=256,height=512*scene_scale_,bytes=width*height*4;
    auto* scratch=static_cast<std::uint32_t*>(linearMemAlign(bytes,128));
    if(!scratch)return Error::out_of_memory;
    auto error=finish();
    if(error==Error::none){
        // Discard CPU cache lines before the transfer as well as afterwards:
        // dirty allocator metadata must never overwrite freshly written pixels.
        GSPGPU_InvalidateDataCache(scratch,bytes);
        const auto flags=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
        C3D_SyncDisplayTransfer(static_cast<u32*>(target_->frameBuf.colorBuf),GX_BUFFER_DIM(width,height),scratch,GX_BUFFER_DIM(width,height),flags);
        GSPGPU_InvalidateDataCache(scratch,bytes);
        const auto lcd_width=screen_width(screen);
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
            const unsigned sx=(2*x+1)*lcd_width*scene_scale_/512,sy=(2*y+1)*240/384;
            const auto rgba=scratch[(height-lcd_width*scene_scale_+sx)*width+(239-sy)];
            argb[y*256+x]=(rgba>>8)|(rgba<<24);
        }
    }
    linearFree(scratch);return error;
}
}
