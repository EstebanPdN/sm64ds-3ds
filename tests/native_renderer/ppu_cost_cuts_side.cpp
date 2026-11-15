// One PPU build behind the plain interface of ppu_cost_cuts.hpp. Compiled once
// per build under comparison with -Dntr=<its namespace> and
// -DHARNESS_SIDE=<its accessor>; the PPU source is compiled the same way.
#include "ppu_cost_cuts.hpp"
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include <cstdlib>
#include <cstring>

namespace ntr {
uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
uint64_t io_read(uint32_t a,unsigned size){uint64_t v=0;std::memcpy(&v,host_ptr(a),size);return v;}
void gx_render(Framebuffer&){std::abort();}
VramBanks* io_vram_banks(){return nullptr;}
#if defined(HARNESS_STATS)
extern unsigned long long ppu_cost_cut_stats[8];
#endif
namespace {
void init(uint8_t** regions){for(unsigned i=0;i<8;++i)g_region[i]=regions[i];}
HarnessLayer plain(const PpuGpuAffineLayer& l){
    HarnessLayer out{};
    out.raster=l.raster;out.width=l.width;out.height=l.height;out.raster_generation=l.raster_generation;
    out.ref_x=l.ref_x;out.ref_y=l.ref_y;out.pa=l.pa;out.pb=l.pb;out.pc=l.pc;out.pd=l.pd;out.wrap=l.wrap;
    out.backdrop=l.backdrop;out.layer=l.layer;out.priority=l.priority;
    out.base_generation=l.base_generation;out.dirty_bands[0]=l.dirty_bands[0];out.dirty_bands[1]=l.dirty_bands[1];
    out.overlay=l.overlay;out.overlay_generation=l.overlay_generation;
    out.effect_mode=l.effect_mode;out.effect_factor=l.effect_factor;out.effect_layer=l.effect_layer;out.effect_backdrop=l.effect_backdrop;
    return out;
}
void plain(const PpuPreparedCandidates& v,HarnessView& out){
    out=HarnessView{};
    const auto& s=v.state;
    out.blend=s.blend;out.alpha=s.alpha;out.brightness=s.brightness;out.master=s.master;out.geometry_priority=s.geometry_priority;
    out.blank=s.blank;out.has_geometry=s.has_geometry;out.resolved=s.resolved;out.has_windows=s.has_windows;out.has_obj_alpha=s.has_obj_alpha;
    out.semi_geometry=s.semi_geometry;out.obj_first_blend=s.obj_first_blend;out.semi_back=s.semi_back;out.bg_first_blend=s.bg_first_blend;
    if(s.window_cut){out.has_cut=true;out.cut_color=s.window_cut->color;out.cut_count=s.window_cut->count;out.cut_signature=s.window_cut->signature;
        out.cut_rects=s.window_cut->rects;out.cut_rect_bytes=unsigned(s.window_cut->count*sizeof(PpuWindowCut::Rect));}
    out.front=&v.front.px[0][0];out.back=&v.back.px[0][0];
    out.front_layer=v.front_layer();out.front_priority=v.front_priority();out.back_layer=v.back_layer();out.back_priority=v.back_priority();
    out.window_mask=v.window_mask();out.front_obj_alpha=v.front_obj_alpha();
    out.has_identity=v.background_identity!=nullptr;out.background_generation=v.background_generation;out.front_alpha_encoded=v.front_alpha_encoded;
    if(v.gpu_affine){out.has_affine=true;out.affine=plain(*v.gpu_affine);}
}
int native(uint32_t* foreground,HarnessLayer* layer){
    PpuGpuAffineLayer map{};
    const bool ok=ppu_prepare_native_affine(ENGINE_B,*reinterpret_cast<Framebuffer*>(foreground),map);
    *layer=plain(map);
    return ok;
}
uint32_t bands(const uint32_t* foreground,const uint32_t** base,uint64_t* generation){
    return ppu_native_affine_bands(*reinterpret_cast<const Framebuffer*>(foreground),base,generation);
}
void candidates(int engine,HarnessView* view,uint32_t* composed){
    static Framebuffer empty;
    const auto prepared=ppu_prepare_candidates_cached(Engine(engine));
    plain(prepared,*view);
    // A GPU-affine view has no planes to compose on the CPU.
    if(composed && !prepared.gpu_affine)ppu_compose_candidates(prepared,empty,*reinterpret_cast<Framebuffer*>(composed));
}
int background(int engine,HarnessView* view){
    const auto prepared=ppu_prepare_background_candidates_cached(Engine(engine));
    if(!prepared){*view=HarnessView{};return 0;}
    plain(*prepared,*view);
    return 1;
}
int unchanged(int engine){return ppu_prepared_inputs_unchanged(Engine(engine));}
void pin(int pinned){ppu_pin_candidate_ways(pinned!=0);}
}
}
extern "C" const HarnessSide* HARNESS_SIDE(){
    static const HarnessSide side{ntr::init,ntr::native,ntr::bands,ntr::candidates,ntr::background,ntr::unchanged,ntr::pin,
#if defined(HARNESS_STATS)
        ntr::ppu_cost_cut_stats
#else
        nullptr
#endif
    };
    return &side;
}
