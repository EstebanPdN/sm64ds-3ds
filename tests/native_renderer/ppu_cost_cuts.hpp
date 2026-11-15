// Shared between the two PPU builds compared by ppu_cost_cuts.cpp. Each build
// is compiled with its own `ntr` namespace (-Dntr=...), so the comparison
// talks to both through these plain types only.
#pragma once
#include <cstdint>

struct HarnessLayer { // ntr::PpuGpuAffineLayer
    const uint32_t* raster;int width,height;uint64_t raster_generation;
    int32_t ref_x,ref_y;int16_t pa,pb,pc,pd;bool wrap;uint32_t backdrop;uint8_t layer,priority;
    uint64_t base_generation,dirty_bands[2];
    const uint32_t* overlay;uint64_t overlay_generation;
    uint8_t effect_mode,effect_factor;bool effect_layer,effect_backdrop;
};
struct HarnessView { // ntr::PpuPreparedCandidates
    uint16_t blend,alpha,brightness,master;uint8_t geometry_priority;
    bool blank,has_geometry,resolved,has_windows,has_obj_alpha,semi_geometry,obj_first_blend,semi_back,bg_first_blend;
    bool has_cut;uint32_t cut_color;unsigned cut_count;uint64_t cut_signature;const void* cut_rects;unsigned cut_rect_bytes;
    const uint32_t *front,*back;
    const uint8_t *front_layer,*front_priority,*back_layer,*back_priority,*window_mask,*front_obj_alpha;
    bool has_identity;uint64_t background_generation;bool front_alpha_encoded;
    bool has_affine;HarnessLayer affine;
};
struct HarnessSide {
    void (*init)(uint8_t** regions);
    // ppu_prepare_native_affine(ENGINE_B, foreground[192*256], layer)
    int (*native)(uint32_t* foreground,HarnessLayer* layer);
    uint32_t (*bands)(const uint32_t* foreground,const uint32_t** base,uint64_t* generation);
    // ppu_prepare_candidates_cached(engine), composed over empty geometry.
    void (*candidates)(int engine,HarnessView* view,uint32_t* composed);
    // ppu_prepare_background_candidates_cached(engine)
    int (*background)(int engine,HarnessView* view);
    int (*unchanged)(int engine);
    void (*pin)(int pinned);
    // Coverage counters of the cuts (null when this build has none).
    const unsigned long long* stats;
};
