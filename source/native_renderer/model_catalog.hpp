#pragma once
#include "model_instance.hpp"

namespace sm64ds::render {
// Borrowed immutable catalog, sorted by original file identity. Construction
// happens on load; render lookups allocate nothing and never compare pixels.
class ModelCatalog {
public:
    static Error open(const ModelBindings*,std::size_t count,ModelCatalog&,
                      const std::uint16_t* source_files=nullptr,std::size_t source_count=0) noexcept;
    const ModelBindings* find(std::uint32_t file_id) const noexcept;
    const ModelBindings* at(std::size_t index) const noexcept { return index<size_?models_+index:nullptr; }
    std::size_t size() const noexcept { return size_; }
    bool source_model(std::uint32_t file_id) const noexcept;
private:
    const ModelBindings* models_=nullptr;
    std::size_t size_=0;
    const std::uint16_t* source_files_=nullptr;
    std::size_t source_count_=0;
};
ModelCatalog prepared_model_catalog() noexcept;
}
