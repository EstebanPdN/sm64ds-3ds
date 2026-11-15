#pragma once
#include "model_bank.hpp"
namespace sm64ds::render {
// Native prepared2D geometry uses its own identity namespace, separate from
// original model IDs and image-only packs. It reuses the strict NMD container.
struct CanvasBinding {
    std::uint32_t bank_id;
    std::array<std::uint8_t,32> source_sha256;
    std::uint16_t parts,images;
};
class CanvasCatalog {
public:
    static Error open(const CanvasBinding*,std::size_t,CanvasCatalog&) noexcept;
    const CanvasBinding* find(std::uint32_t) const noexcept;
    std::size_t size() const noexcept {return size_;}
private:
    const CanvasBinding* banks_=nullptr;std::size_t size_=0;
};
Error validate(const ModelBank&,const CanvasBinding&) noexcept;
CanvasCatalog prepared_canvas_catalog() noexcept;
}
