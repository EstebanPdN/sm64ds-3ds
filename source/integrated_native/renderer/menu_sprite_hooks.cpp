#include "menu_sprite_hooks.hpp"
#include "model_components_hooks.hpp"
#include "game_lighting.hpp"
#include <cstring>
namespace sm64ds::render::game_hooks {
namespace {
struct Image {const void* descriptor=nullptr;const ModelBindings* binding=nullptr;const ImageBinding* image=nullptr;};
std::array<Image,64> images{};
SubmitMenuSprite sink=nullptr;void* context=nullptr;MenuSpriteStatistics counters;
SubmitMenuGrid grid_sink=nullptr;void* grid_context=nullptr;
SubmitMenuStroke stroke_sink=nullptr;void* stroke_context=nullptr;
#ifdef SM64DS_FRESH_NATIVE_GAME
std::uint16_t stroke_colour=0;std::uint8_t stroke_alpha=31;
#endif
std::uint64_t (*clock_source)()=nullptr;
}
bool install_menu_sprites(SubmitMenuSprite submit,void* user) noexcept {
    if(sink||!submit)return false;
    sink=submit;context=user;return true;
}
bool install_menu_grid(SubmitMenuGrid submit,void* user) noexcept {
    if(grid_sink||!submit)return false;
    grid_sink=submit;grid_context=user;return true;
}
bool install_menu_strokes(SubmitMenuStroke submit,void* user) noexcept {
    if(stroke_sink||!submit)return false;
    stroke_sink=submit;stroke_context=user;return true;
}
void menu_sprite_clock(std::uint64_t (*clock)()) noexcept {clock_source=clock;}
// bd1c0 frees the BMD immediately after upload, while its descriptors remain
// live. Keep copied logical identities until the descriptor's own destructor;
// never retain a pointer to that released source file or its texture bytes.
void menu_descriptor_retiring(const void* descriptor) noexcept {for(auto& image:images)if(image.descriptor==descriptor)image={};}
const MenuSpriteStatistics& menu_sprite_statistics() noexcept {return counters;}
void menu_sprite_geometry_count(std::size_t source,std::size_t native) noexcept {counters.source_quads+=source;counters.native_quads+=native;}
bool menu_image_diagnostic(unsigned slot,MenuImageDiagnostic& out) noexcept {
    if(slot>=images.size()||!images[slot].descriptor)return false;
    const auto& image=images[slot];out={reinterpret_cast<std::uintptr_t>(image.descriptor),
        image.binding?image.binding->file_id:0xffffffffu,image.image?image.image->image:0xffffffffu};return true;
}
}

#ifdef SM64DS_FRESH_NATIVE_GAME
// Exact original32-bit sprite/descriptor ABI, verified against c421c,c42f8,
// c4684,c49bc and b2308/c162c. This seam reads typed source objects, never
// command buffers or emulated VRAM addresses. No GPU/SD work occurs here.
extern "C" {
void* __real_func_ov007_020b2308(void*,int);
void __real_func_ov007_020b22e0(void*,void*,int);
int __real_func_ov007_020c1620(void*);
void __real_func_ov007_020c421c(void*);
void __real_func_ov007_020ca5f0(void*,void*,const std::int32_t*,int,int);
void func_ov007_020c3abc(std::int32_t*,int,int,int);
extern std::int32_t data_ov007_0210325c;
}
namespace sm64ds::render::game_hooks {
namespace {
static_assert(sizeof(void*)==4,"Original menu sprite ABI requires32-bit game pointers");
template<class T>T field(const void* object,std::size_t offset) noexcept {T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(value));return value;}
std::int32_t word(std::uint32_t bits) noexcept {std::int32_t v;std::memcpy(&v,&bits,4);return v;}
std::int32_t add(std::int32_t a,std::int32_t b) noexcept {return word(std::uint32_t(a)+std::uint32_t(b));}
std::int32_t rounded(std::int32_t a,std::int32_t b) noexcept {return word(std::uint32_t(std::uint64_t(std::int64_t(a)*b+2048)>>12));}
bool failed(const char* issue,std::uint32_t detail=0) noexcept {counters.last_issue=issue;counters.last_detail=detail;return false;}
Image* image_for(const void* descriptor) noexcept {
    // Descriptors are eleven load-time objects in the reviewed menu. Keep the
    // lookup bounded; no pixel comparison, asset decoding or allocations.
    for(auto& image:images)if(image.descriptor==descriptor)return &image;
    return nullptr;
}
bool grid(const void* object,const void* style,const std::int32_t* translation,int depth_scale,int textured) noexcept {
    if(!object||!style||!translation||!grid_sink||!textured)return failed("unsupported procedural grid");
    const auto* descriptor=field<const void*>(style,0);
    const auto* image=image_for(descriptor);
    if(!image||!image->binding||!image->image)return failed("grid image not prepared");
    const auto* dimensions=field<const std::int32_t*>(object,4);
    if(!dimensions||dimensions[0]<2||dimensions[1]<2)return failed("invalid grid dimensions");
    MenuGridDraw draw;auto& g=draw.geometry;
    g.columns=unsigned(dimensions[0]-1);g.rows=unsigned(dimensions[1]-1);
    g.positions=field<const void*>(object,0);g.normals=field<const void*>(object,0x2c);
    g.image_width=image->image->width;g.image_height=image->image->height;
    g.u=field<std::int32_t>(style,4);g.v=field<std::int32_t>(style,8);
    g.du=std::int32_t(std::int64_t(g.image_width)*field<std::int32_t>(style,0x14)/g.columns);
    g.dv=std::int32_t(std::int64_t(g.image_height)*field<std::int32_t>(style,0x18)/g.rows);
    g.normal_u=rounded(field<std::int16_t>(object,0x24),field<std::int32_t>(style,0xc));
    g.normal_v=rounded(field<std::int16_t>(object,0x26),field<std::int32_t>(style,0x10));
    const auto alpha=field<std::uint32_t>(style,0x24),mode=field<std::uint32_t>(style,0x2c);
    if(alpha>31||(mode!=0&&mode!=2))return failed("unsupported grid material",mode);
    if(!alpha)return true;
    g.alpha=std::uint8_t(alpha);
    draw.file_id=image->binding->file_id;draw.image=std::uint16_t(image->image->image);
    draw.object=game::unit_affine();
    draw.object.value[0]=draw.object.value[4]=64*4096;
    draw.object.value[8]=depth_scale*64;
    for(unsigned i=0;i<3;++i)draw.object.value[9+i]=translation[i];
    const auto repeat=field<std::uint32_t>(style,0x1c),format=field<std::uint32_t>(descriptor,4);
    draw.material.cull=Cull::none;draw.material.blend=alpha<31||format==1||format==6?Blend::alpha:Blend::opaque;
    draw.material.depth_write=true; // producer sets translucent depth update
    draw.material.wrap_s=(repeat&1)?Wrap::repeat:Wrap::clamp;draw.material.wrap_t=(repeat&2)?Wrap::repeat:Wrap::clamp;
    draw.material.sample_alpha=!(format>=2&&format<=4)||field<std::uint32_t>(style,0x20);
    draw.lighting.diffuse=field<std::uint16_t>(style,0x28)&32767;draw.lighting.ambient=field<std::uint16_t>(style,0x2a)&32767;draw.lighting.mask=1;
    // The sole caller is the file-selector grid (bee14), whose setup bd1c0
    // selects highlight shading: textured grey base plus the toon-table pass.
    draw.lighting.grey=mode==2;
    if(capture_lighting(1,false,draw.lights)!=Error::none)return failed("grid lighting");
    return grid_sink(draw,grid_context)||failed("grid frame capture");
}
void observe_descriptor(const void* file,int index,const void* descriptor) noexcept {
    if(!descriptor||!tracking_enabled())return;
    Image* slot=image_for(descriptor);if(!slot)for(auto& image:images)if(!image.descriptor){slot=&image;break;}
    if(!slot){failed("descriptor capacity");report_image_failure();++counters.failures;return;}
    const auto* binding=binding_for_file(file);const auto* image=binding&&index>=0?sprite_image(*binding,unsigned(index),unsigned(index)):nullptr;
    *slot={descriptor,binding,image};++counters.descriptors;
}
bool draw(const void* object,const game::Affine& base,unsigned colour,unsigned alpha,unsigned id,unsigned flags,const std::int16_t* offset) noexcept {
    if(!alpha)return true;
    if(!sink)return failed("missing sink");
    if(alpha>31||id>63||colour>32767)return failed("invalid colour/alpha/id",(alpha<<16)|(id<<8));
    MenuSpriteDraw draw;draw.geometry.alpha=std::uint8_t(alpha);draw.polygon_id=std::uint8_t(id);
    const auto polygon=field<std::uint32_t>(object,0x54);
    // Equality-depth/special polygon modes have no native pixel policy yet.
    if(polygon&~std::uint32_t((1u<<6)|(1u<<7)|(1u<<11)|(1u<<15)))return failed("unsupported pixel policy",polygon);
    draw.fog=bool(polygon&(1u<<15));draw.material.cull=(polygon&64)?Cull::none:Cull::back;
    draw.material.blend=alpha<31?Blend::alpha:Blend::opaque;draw.material.depth_write=alpha==31||bool(polygon&(1u<<11));
    auto& g=draw.geometry;g.direct_quad=bool(flags&3);g.corner_colours=bool(field<std::uint32_t>(object,0)&2);
    g.repeat_s=field<std::uint16_t>(object,0x60);g.repeat_t=field<std::uint16_t>(object,0x62);
    g.offset_s=field<std::int16_t>(object,0x70);g.offset_t=field<std::int16_t>(object,0x72);g.colour.fill(std::uint16_t(colour));
    if(g.direct_quad&&g.corner_colours)for(unsigned i=0;i<4;++i)g.colour[i]=field<std::uint16_t>(object,0x46+i*2);
    const auto count=field<std::uint16_t>(object,0x58),index=field<std::uint16_t>(object,0x5a);
    const auto* table=field<const void* const*>(object,0x5c);if(!table||index>=count)return failed("descriptor index",(std::uint32_t(count)<<16)|index);
    const auto* descriptor=table[index];
    if(descriptor){
        auto* image=image_for(descriptor);
        if(!image)return failed("descriptor not observed",std::uint32_t(reinterpret_cast<std::uintptr_t>(descriptor)));
        if(!image->binding)return failed("unassociated source file",index);
        if(!image->image)return failed("sprite bank not prepared",image->binding->file_id);
        const auto width=field<std::uint32_t>(descriptor,0x14),height=field<std::uint32_t>(descriptor,0x18),format=field<std::uint32_t>(descriptor,4);
        if(width>7||height>7||format<1||format>7)return failed("descriptor format",format);
        g.image_width=8u<<width;g.image_height=8u<<height;
        if(image->image->width!=g.image_width||image->image->height!=g.image_height)return failed("descriptor dimensions");
        g.sample_width=g.repeat_s==1?field<std::uint16_t>(object,0x0c):g.image_width;
        g.sample_height=g.repeat_t==1?field<std::uint16_t>(object,0x0e):g.image_height;
        draw.file_id=image->binding->file_id;draw.image=std::uint16_t(image->image->image);
        const auto repeat=field<std::uint32_t>(object,0x64),flip=field<std::uint32_t>(object,0x68),zero=field<std::uint32_t>(object,0x6c);
        if(repeat>3||flip>3||zero>1)return failed("sampler properties");
        draw.material.wrap_s=(repeat&1)?((flip&1)?Wrap::mirror:Wrap::repeat):Wrap::clamp;
        draw.material.wrap_t=(repeat&2)?((flip&2)?Wrap::mirror:Wrap::repeat):Wrap::clamp;
        draw.material.sample_alpha=!(format>=2&&format<=4)||zero;
        // A3I5 (1) and A5I3 (6) carry per-texel alpha: the DS blends those
        // texels even when the polygon alpha is 31 (menu wallpaper pattern).
        if(format==1||format==6){draw.material.blend=Blend::alpha;draw.material.depth_write=bool(polygon&(1u<<11));}
    }else g.sample_width=g.sample_height=0;
    if(flags&8){g.tiles=field<const std::uint16_t*>(object,0x84);if(g.tiles)g.tile_count=std::size_t(g.sample_width/8)*(g.sample_height/8);}
    const auto w=field<std::int32_t>(object,0x20),h=field<std::int32_t>(object,0x24);
    auto centre=game::unit_affine();centre.value[9]=add(std::int32_t(-std::int64_t(w)/2),offset[0]);
    centre.value[10]=add(std::int32_t(-std::int64_t(h)/2),offset[1]);centre.value[11]=offset[2];
    auto extent=game::unit_affine();extent.value[0]=rounded(w,data_ov007_0210325c);extent.value[4]=rounded(h,data_ov007_0210325c);
    draw.object=game::compose(extent,game::compose(centre,base));
    const bool result=sink(draw,context);if(result)++counters.draws;else failed("frame capture");return result;
}
void produce(const void* object) noexcept {
    ++counters.calls;
    if(!object){report_image_failure();++counters.failures;return;}
    const auto flags=field<std::uint32_t>(object,0);const auto opacity=field<std::int32_t>(object,0x50)>>12;
    const int shadow=opacity-field<std::uint8_t>(object,0x7c);
    // The observed menu calls three sprite producers but draws only two.
    // c4684 returns immediately for alpha0; skip its otherwise unused rotation
    // and affine composition when both main and shadow produce no geometry.
    if(!(unsigned(opacity)&255u)&&(!(flags&4)||!(unsigned(shadow>0?shadow:0)&255u)))return;
    auto base=game::unit_affine();
    func_ov007_020c3abc(base.value,field<std::uint16_t>(object,0x34),field<std::uint16_t>(object,0x36),field<std::uint16_t>(object,0x38));
    for(unsigned i=0;i<3;++i)base.value[9+i]=add(field<std::int32_t>(object,0x14+i*4),field<std::int32_t>(object,0x28+i*4));
    auto scale=game::unit_affine();scale.value[0]=field<std::int32_t>(object,0x3c);scale.value[4]=field<std::int32_t>(object,0x40);
    base=game::compose(scale,base);
    const std::int16_t zero[3]={};bool okay=draw(object,base,field<std::uint16_t>(object,0x44),unsigned(opacity)&255u,field<std::uint8_t>(object,0x4e),flags,zero);
    if(okay&&(flags&4)){
        scale.value[0]=field<std::int32_t>(object,0x74);scale.value[4]=field<std::int32_t>(object,0x78);
        std::int16_t offset[3];std::memcpy(offset,static_cast<const char*>(object)+0x7e,sizeof(offset));
        okay=draw(object,game::compose(scale,base),0,unsigned(shadow>0?shadow:0)&255u,field<std::uint8_t>(object,0x7d),0,offset);
    }
    if(!okay){report_image_failure();++counters.failures;}
}
}
}
extern "C" void* __wrap_func_ov007_020b2308(void* file,int index){
    void* descriptor=__real_func_ov007_020b2308(file,index);
    sm64ds::render::game_hooks::observe_descriptor(file,index,descriptor);return descriptor;
}
extern "C" void __wrap_func_ov007_020b22e0(void* descriptor,void* file,int index){
    __real_func_ov007_020b22e0(descriptor,file,index);
    sm64ds::render::game_hooks::observe_descriptor(file,index,descriptor);
}
extern "C" int __wrap_func_ov007_020c1620(void* descriptor){
    if(sm64ds::render::game_hooks::tracking_enabled())sm64ds::render::game_hooks::menu_descriptor_retiring(descriptor);
    return __real_func_ov007_020c1620(descriptor);
}
extern "C" void __wrap_func_ov007_020c421c(void* object){
    if(!sm64ds::render::game_hooks::native_active()){__real_func_ov007_020c421c(object);return;}
    const auto clock=sm64ds::render::game_hooks::clock_source;const auto start=clock?clock():0;
    sm64ds::render::game_hooks::produce(object);
    if(clock)sm64ds::render::game_hooks::counters.capture_ticks+=clock()-start;
}
extern "C" void __wrap_func_ov007_020ca5f0(void* object,void* style,const std::int32_t* translation,int depth_scale,int textured){
    using namespace sm64ds::render::game_hooks;
    if(!native_active()){__real_func_ov007_020ca5f0(object,style,translation,depth_scale,textured);return;}
    ++counters.calls;
    if(grid(object,style,translation,depth_scale,textured))++counters.draws;
    else {++counters.failures;report_image_failure();}
}
#endif

#ifdef SM64DS_FRESH_NATIVE_GAME
extern "C" {
void __real_func_ov007_020c4fcc(int,int);
void __real_func_ov007_020c19cc(int,const sm64ds::render::game::SketchPoint*,std::int16_t,int);
int func_020538b8(int,int);
extern std::int16_t data_02082214[];
void __wrap_func_ov007_020c4fcc(int colour,int alpha){
    using namespace sm64ds::render::game_hooks;
    if(!native_active()){__real_func_ov007_020c4fcc(colour,alpha);return;}
    stroke_colour=std::uint16_t(colour)&32767;stroke_alpha=std::uint8_t(alpha)&31;
}
void __wrap_func_ov007_020c19cc(int count,const sm64ds::render::game::SketchPoint* points,std::int16_t half,int progress){
    using namespace sm64ds::render::game_hooks;
    if(!native_active()){__real_func_ov007_020c19cc(count,points,half,progress);return;}
    ++counters.calls;
    if(count<=0||!stroke_alpha)return;
    const sm64ds::render::game::SketchStroke stroke{points,std::size_t(count),half,progress,stroke_colour,stroke_alpha,func_020538b8,data_02082214};
    if(stroke_sink&&stroke_sink(stroke,stroke_context))++counters.draws;
    else {failed("sketch frame capture");++counters.failures;report_image_failure();}
}
}
#endif
