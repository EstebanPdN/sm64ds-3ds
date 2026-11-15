#include "model_catalog.hpp"
#ifdef SM64DS_NATIVE_CATALOG_HEADER
#include SM64DS_NATIVE_CATALOG_HEADER
#endif

namespace sm64ds::render {
Error ModelCatalog::open(const ModelBindings* models,std::size_t count,ModelCatalog& output,const std::uint16_t* files,std::size_t source_count) noexcept {
    if(count>65536 || (count&&!models))return Error::invalid_data;
    if(source_count>65536||(source_count&&!files))return Error::invalid_data;
    for(std::size_t i=1;i<source_count;++i)if(files[i]<=files[i-1])return Error::invalid_data;
    for(std::size_t i=0;i<count;++i){
        const auto& m=models[i];
        if(m.file_id>65535 || (i&&m.file_id<=models[i-1].file_id) || !m.bones || m.bones>ModelBank::max_matrices ||
           !m.source_materials || m.source_materials>65536 || !m.material || !m.materials || m.materials>ModelBank::max_materials ||
           !m.part || !m.parts || m.parts>ModelBank::max_parts || m.images>ModelBank::max_images ||
           (m.images&&!m.image))return Error::invalid_data;
    }
    ModelCatalog next;next.models_=models;next.size_=count;next.source_files_=files;next.source_count_=source_count;
    for(std::size_t i=0;i<count&&source_count;++i)if(!next.source_model(models[i].file_id))return Error::invalid_data;
    output=next;return Error::none;
}
bool ModelCatalog::source_model(std::uint32_t id) const noexcept {
    if(!source_count_)return find(id)!=nullptr;
    std::size_t lo=0,hi=source_count_;
    while(lo<hi){const auto m=lo+(hi-lo)/2;if(source_files_[m]<id)lo=m+1;else hi=m;}
    return lo<source_count_&&source_files_[lo]==id;
}
const ModelBindings* ModelCatalog::find(std::uint32_t id) const noexcept {
    std::size_t lo=0,hi=size_;
    while(lo<hi){const auto mid=lo+(hi-lo)/2;if(models_[mid].file_id<id)lo=mid+1;else hi=mid;}
    return lo<size_&&models_[lo].file_id==id?models_+lo:nullptr;
}
ModelCatalog prepared_model_catalog() noexcept {
    ModelCatalog result;
#ifdef SM64DS_NATIVE_CATALOG_HEADER
    ModelCatalog::open(prepared::models,sizeof(prepared::models)/sizeof(prepared::models[0]),result,
        prepared::source_model_files,sizeof(prepared::source_model_files)/sizeof(prepared::source_model_files[0]));
#endif
    return result;
}
}
