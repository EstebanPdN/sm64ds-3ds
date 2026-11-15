#include "particle_hooks.hpp"
#include "model_components_hooks.hpp"
#include <cstring>

namespace sm64ds::render::game_hooks {
namespace {
struct File {const void* file=nullptr;const ImageBankBinding* binding=nullptr;};
struct Manager {const void* manager=nullptr;const ImageBankBinding* binding=nullptr;};
std::array<File,8> files{};
std::array<Manager,8> managers{};
ImageCatalog catalog;
SubmitParticle sink=nullptr;void* context=nullptr;
std::uint64_t (*clock_source)()=nullptr;
ParticleStatistics counters;
}
bool install_particles(ImageCatalog images,SubmitParticle submit,void* user) noexcept {
    if(sink||!images.size()||!submit)return false;
    catalog=images;sink=submit;context=user;return true;
}
void particle_clock(std::uint64_t (*clock)()) noexcept {clock_source=clock;}
void particle_file_loaded(const void* file,std::uint32_t bank_id) noexcept {
    if(!sink||!file)return;
    const auto* binding=catalog.find(bank_id);
    File* slot=nullptr;
    for(auto& item:files)if(item.file==file){slot=&item;break;}
    if(!slot)for(auto& item:files)if(!item.file){slot=&item;break;}
    if(!slot){++counters.failures;counters.last_issue="file capacity";report_image_failure();return;}
    if(slot->file==file&&slot->binding==binding)return;
    if(slot->file&&slot->binding!=binding)binding=nullptr; // missed retirement cannot silently change an identity
    *slot={file,binding};++counters.resource_loads;
}
void particle_file_retiring(const void* file) noexcept {for(auto& item:files)if(item.file==file)item={};}
void particle_manager_retiring(const void* manager) noexcept {for(auto& item:managers)if(item.manager==manager)item={};}
const ParticleStatistics& particle_statistics() noexcept {return counters;}
}

#ifdef SM64DS_FRESH_NATIVE_GAME
// Original32-bit manager/descriptor/node ABI, reviewed against a17c,a5c8,
// af3c,be40,bbd8 and their owning loaders/destructors. No graphics packets or
// emulated VRAM addresses are read. Original simulation/list traversal stays.
extern "C" {
void* __real_func_0204a4c8(void* (*)(int),int,int,int,unsigned short);
void __real_func_0204a17c(void*,void*);
void __real_func_0204a5c8(void*);
void __real_func_0204af3c(void*);
void __real_func_0204be40(void*,void*);
void __real_func_0204bbd8(void*,void*);
void __real_func_ov007_020b7764();
void __real__ZN8Particle10SysTrackerD1Ev(void*);
void __real_func_020190b8();
int func_02019018();
void MulVec3Mat4x3(void*,const void*,void*);
extern std::int16_t data_02082214[];
extern unsigned char data_02075f14[];
extern void* data_ov007_0210344c;
extern unsigned char data_0209d464;
}
namespace sm64ds::render::game_hooks {
namespace {
static_assert(sizeof(void*)==4,"Original particle ABI requires32-bit pointers");
template<class T>T field(const void* object,std::size_t offset) noexcept {T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(value));return value;}
template<class T>void store(void* object,std::size_t offset,T value) noexcept {std::memcpy(static_cast<char*>(object)+offset,&value,sizeof(value));}
std::int32_t word(std::uint32_t bits) noexcept {std::int32_t value;std::memcpy(&value,&bits,4);return value;}
void failed(const char* issue,std::uint32_t detail=0) noexcept {
    ++counters.failures;counters.last_issue=issue;counters.last_detail=detail;report_image_failure();
}
Manager* manager_for(const void* pointer) noexcept {
    for(auto& item:managers)if(item.manager==pointer)return &item;
    return nullptr;
}
void observe_manager(void* manager,const void* file) noexcept {
    if(!sink||!tracking_enabled()||!manager)return;
    particle_manager_retiring(manager);Manager* slot=nullptr;
    for(auto& item:managers)if(!item.manager){slot=&item;break;}
    if(!slot){failed("manager capacity");return;}
    const ImageBankBinding* binding=nullptr;
    if(file==data_02075f14)binding=catalog.find(0); // reviewed embedded core resource
    else for(const auto& item:files)if(item.file==file){binding=item.binding;break;}
    // Resource bytes may disappear after original uploads. Save only this
    // compiled identity; never keep source texture-record/pixel pointers.
    if(binding&&field<std::uint16_t>(manager,0x26)!=binding->images)binding=nullptr;
    *slot={manager,binding};++counters.manager_loads;
}
const void* current_manager=nullptr;
struct Selection {
    const void* manager=nullptr;
    const ImageBankBinding* binding=nullptr;
    unsigned image=0,format=0;
    std::uint32_t parameters=0;
    std::uint16_t width=0,height=0;
} selected;
void bind(const void* descriptor) noexcept {
    ++counters.bindings;selected={};
    const auto* manager=current_manager;const auto* record=manager_for(manager);
    if(!descriptor||!manager||!record||!record->binding)return;
    const auto* table=field<const unsigned char*>(manager,0x20);
    const auto count=field<std::uint16_t>(manager,0x26);
    const auto base=reinterpret_cast<std::uintptr_t>(table),address=reinterpret_cast<std::uintptr_t>(descriptor);
    if(!table||address<base||address-base>=std::size_t(count)*20||(address-base)%20)return;
    const auto index=unsigned((address-base)/20);
    if(index>=record->binding->images)return;
    const auto parameters=field<std::uint32_t>(descriptor,12),format=parameters&15u;
    const auto ws=(parameters>>4)&15u,hs=(parameters>>8)&15u;
    const auto& expected=record->binding->image[index];
    if(ws>7||hs>7||format<1||format>7||format==5||expected.width!=(8u<<ws)||expected.height!=(8u<<hs)||
       (expected.alpha==ImageBank::Alpha::palette_zero_mask)!=(format>=2&&format<=4))return;
    selected={manager,record->binding,index,format,parameters,field<std::uint16_t>(descriptor,16),field<std::uint16_t>(descriptor,18)};
}
void produce(void* manager,const void* node,bool child) noexcept {
    ++counters.calls;
    if(!manager||!node){failed("null particle object");return;}
    auto* system=field<void*>(manager,0x34);if(!system){failed("null particle system");return;}
    const auto opacity=field<std::uint16_t>(node,0x40);
    const unsigned alpha=((opacity&31)*(((opacity>>5)&31)+1))>>5;
    auto counter=field<std::uint32_t>(system,0x74);
    const auto old_count=(counter>>12)&63u;
    const auto polygon=field<std::uint32_t>(manager,0x30)|0xc0u|(old_count<<24)|(alpha<<16);
    auto next=(old_count+1)&63u;
    if(next>((counter>>6)&63u))next=counter&63u;
    counter=(counter&~(63u<<12))|(next<<12);store(system,0x74,counter);
    // Even invisible particles consume a polygon ID. Preserve this before
    // early-returning; no identity/pose work is needed for their missing draw.
    // A dry update (image not shown) keeps the counter and draws nothing.
    if(!alpha||dry())return;
    const auto* association=manager_for(manager);
    if(!sink||selected.manager!=manager||!selected.binding||!association||association->binding!=selected.binding){failed("unassociated particle image");return;}
    const auto unsupported=polygon&~std::uint32_t(0x3f1f88c0u);
    if(unsupported){failed("unsupported particle pixel policy",polygon);return;}
    const auto* resource=field<const void*>(system,0x18);
    const auto* definition=resource?field<const void*>(resource,0):nullptr;
    const auto* view=field<const void*>(manager,0x38);
    if(!definition||!view){failed("missing particle definition/view");return;}
    game::ParticleBillboard input;
    for(unsigned i=0;i<3;++i)input.position[i]=word(std::uint32_t(field<std::int32_t>(node,0x14+i*4))+
        std::uint32_t(field<std::int32_t>(node,8+i*4)));
    MulVec3Mat4x3(input.position.data(),view,input.position.data()); // original centroid transform, not per-vertex rendering
    input.scale=field<std::int32_t>(node,0x30);input.multiplier=field<std::int16_t>(node,0x34);
    input.aspect=field<std::int16_t>(definition,0x20);
    const auto index=field<std::uint16_t>(node,0x36)>>4;
    input.sine=data_02082214[index*2];input.cosine=data_02082214[index*2+1];
    input.colour=field<std::uint16_t>(node,0x3a)&32767u;input.alpha=std::uint8_t((polygon>>16)&31);
    if(child){
        const auto* child_definition=field<const void*>(resource,0x14);
        if(!child_definition){failed("missing child particle definition");return;}
        const auto repeat=field<std::uint32_t>(child_definition,0x10);
        input.repeat_s=std::uint8_t(repeat&3);input.repeat_t=std::uint8_t((repeat>>2)&3);
    }else{
        const auto repeat=field<std::uint32_t>(definition,0x34);
        input.repeat_s=std::uint8_t((repeat>>24)&3);input.repeat_t=std::uint8_t((repeat>>26)&3);
    }
    const auto& image=selected.binding->image[selected.image];input.image_width=image.width;input.image_height=image.height;
    input.sample_width=selected.width;input.sample_height=selected.height;
    ParticleDraw draw;const auto error=game::particle_billboard(input,draw.geometry);
    if(error!=Error::none){failed("particle geometry",unsigned(error));return;}
    draw.bank_id=selected.binding->bank_id;draw.image=std::uint16_t(selected.image);
    draw.polygon_id=std::uint8_t((polygon>>24)&63);draw.fog=bool(polygon&(1u<<15));
    auto& material=draw.material;material.cull=Cull::none;
    material.blend=(input.alpha<31||selected.format==1||selected.format==6)?Blend::alpha:Blend::opaque;
    material.depth_write=input.alpha==31||bool(polygon&(1u<<11));
    const auto repeat=(selected.parameters>>12)&3,flip=(selected.parameters>>14)&3;
    material.wrap_s=(repeat&1)?((flip&1)?Wrap::mirror:Wrap::repeat):Wrap::clamp;
    material.wrap_t=(repeat&2)?((flip&2)?Wrap::mirror:Wrap::repeat):Wrap::clamp;
    material.sample_alpha=image.alpha!=ImageBank::Alpha::palette_zero_mask||bool(selected.parameters&(1u<<16));
    if(!sink(draw,context)){failed("particle frame capture");return;}
    ++counters.draws;
}
}
}
extern "C" void* __wrap_func_0204a4c8(void* (*allocate)(int),int a,int b,int c,unsigned short d){
    void* manager=__real_func_0204a4c8(allocate,a,b,c,d);
    if(sm64ds::render::game_hooks::tracking_enabled())sm64ds::render::game_hooks::particle_manager_retiring(manager);
    return manager;
}
extern "C" void __wrap_func_0204a17c(void* manager,void* file){
    __real_func_0204a17c(manager,file);sm64ds::render::game_hooks::observe_manager(manager,file);
}
extern "C" void __wrap_func_0204a5c8(void* manager){
    using namespace sm64ds::render::game_hooks;
    if(!native_active()){__real_func_0204a5c8(manager);return;}
    const auto* previous=current_manager;const auto before=selected;
    const auto start=clock_source?clock_source():0;
    current_manager=manager;selected={};__real_func_0204a5c8(manager);current_manager=previous;selected=before;
    if(clock_source)counters.capture_ticks+=clock_source()-start;
}
extern "C" void __wrap_func_0204af3c(void* descriptor){
    if(!sm64ds::render::game_hooks::native_active()){__real_func_0204af3c(descriptor);return;}
    sm64ds::render::game_hooks::bind(descriptor);
}
extern "C" void __wrap_func_0204be40(void* manager,void* node){
    using namespace sm64ds::render::game_hooks;
    if(!native_active()){__real_func_0204be40(manager,node);return;}
    produce(manager,node,false);
}
extern "C" void __wrap_func_0204bbd8(void* manager,void* node){
    using namespace sm64ds::render::game_hooks;
    if(!native_active()){__real_func_0204bbd8(manager,node);return;}
    produce(manager,node,true);
}
extern "C" void __wrap_func_ov007_020b7764(){
    if(sm64ds::render::game_hooks::tracking_enabled())sm64ds::render::game_hooks::particle_manager_retiring(data_ov007_0210344c);
    __real_func_ov007_020b7764();
}
extern "C" void __wrap__ZN8Particle10SysTrackerD1Ev(void* object){
    if(sm64ds::render::game_hooks::tracking_enabled()&&object)
        sm64ds::render::game_hooks::particle_manager_retiring(sm64ds::render::game_hooks::field<const void*>(object,4));
    __real__ZN8Particle10SysTrackerD1Ev(object);
}
extern "C" void __wrap_func_020190b8(){
    if(!sm64ds::render::game_hooks::native_active()){__real_func_020190b8();return;}
    if(!func_02019018())return;
    data_0209d464=1;++sm64ds::render::game_hooks::counters.flushes;
}
#endif
