// Experimental CPU OBJ overlay over the existing GPU-affine background.
// Appended to the pinned PPU implementation in the derived runtime and replay.
// Call only after the presentation worker has drained: scan_obj uses shared
// per-engine scratch, and the source affine cache is borrowed by the result.
namespace ntr {
std::optional<PpuPreparedCandidates> ppu_prepare_affine_alpha_objects(Engine eng) {
    if(eng!=ENGINE_B)return std::nullopt;
    const auto& em=kEngines[eng];const uint32_t disp=rd32(em.reg_base);
    const unsigned blend=rd16(em.reg_base+0x50),alpha=rd16(em.reg_base+0x52);
    // No geometry, windows, BG first targets, OBJ blend targets or brightness
    // are reinterpreted by this slice. Unsupported inputs keep the full PPU.
    if(!(disp&(1u<<12)) || (disp&0xe080u) || ((blend>>6)&3)!=1 ||
       (blend&0x1fu) || (blend&0x1000u) || rd16(em.reg_base+0x6c))return std::nullopt;
    ppu_sync_vram_views();
    auto background=ppu_prepare_background_affine(eng);
    if(!background || !background->gpu_affine)return std::nullopt;
    const auto& layer=*background->gpu_affine;
    const unsigned overlays=((disp>>8)&15u)&~(1u<<layer.layer);
    if((blend>>8)&overlays)return std::nullopt;
    const auto* oam=static_cast<const uint8_t*>(host_ptr(0x07000400));
    bool semi=false;
    for(unsigned i=0;i<128;++i){
        const unsigned a0=oam[i*8]|(unsigned(oam[i*8+1])<<8);
        if(!(a0&0x100) && (a0&0x200))continue;
        if(((a0>>10)&3)>1 || (a0&0x1000))return std::nullopt;
        int x0,x1,y0,y1;if(!obj_box(oam,i,x0,x1,y0,y1))continue;
        const unsigned a2=oam[i*8+4]|(unsigned(oam[i*8+5])<<8);
        if(((a2>>10)&3)>layer.priority)return std::nullopt;
        semi|=((a0>>10)&3)==1;
    }
    if(!semi)return std::nullopt;
    struct Storage {
        HostVector<uint32_t> objects,overlay[2];HostVector<uint8_t> priority;
        PpuGpuAffineLayer layer[2]{};unsigned current=0;uint64_t generation=0;
    };
    static Storage storage;auto& s=storage;const unsigned target=s.current^1u;
    constexpr unsigned pixels=SCREEN_W*SCREEN_H;
    try{s.objects.resize(pixels);s.priority.resize(pixels);s.overlay[target].resize(pixels);}
    catch(const std::bad_alloc&){return std::nullopt;}
    std::fill(s.objects.begin(),s.objects.end(),0u);
    std::fill(s.priority.begin(),s.priority.end(),uint8_t(0xff));
    // Same OAM order, integer affine mapping and palette decoding as the full
    // CPU PPU; only its winning OBJ pixel is needed (OBJ is one DS layer).
    scan_obj(eng,[&](int x,int y,uint32_t color,unsigned priority,unsigned object_alpha){
        const unsigned i=unsigned(y)*SCREEN_W+unsigned(x);
        if(priority<=(s.priority[i]&0x7f)){
            s.objects[i]=color;s.priority[i]=uint8_t(priority|(object_alpha?0x80:0));
        }
    },false,false,NoObjBox{});
    for(unsigned y=0;y<SCREEN_H;++y)for(unsigned x=0;x<SCREEN_W;++x){
        const unsigned i=y*SCREEN_W+x;
        uint32_t out=layer.overlay?layer.overlay[i]:0u;
        const unsigned p=s.priority[i]&0x7f;
        // OBJ wins a priority tie. Admission guarantees it is never behind
        // the affine BG; a front overlay can still occlude it.
        if((s.objects[i]>>24) && (!(out>>24) || p<=((out>>29)-2u))){
            uint32_t color=s.objects[i];
            if((s.priority[i]&0x80) && !(out>>24)){
                int px=(layer.ref_x+int32_t(x)*layer.pa+int32_t(y)*layer.pb)>>8;
                int py=(layer.ref_y+int32_t(x)*layer.pc+int32_t(y)*layer.pd)>>8;
                uint32_t back=0;
                if(layer.wrap){px&=layer.width-1;py&=layer.height-1;}
                if(unsigned(px)<unsigned(layer.width) && unsigned(py)<unsigned(layer.height))
                    back=layer.raster[unsigned(py)*unsigned(layer.width)+unsigned(px)];
                const unsigned behind=(back>>24)?layer.layer:5u;
                if(!(back>>24))back=layer.backdrop;
                if((blend>>(8+behind))&1u)color=alpha_blend(color,back,alpha&31u,(alpha>>8)&31u);
            }
            out=(color&0xffffffu)|(uint32_t(32u*(p+2u))<<24);
        }
        s.overlay[target][i]=out;
    }
    s.layer[target]=layer;s.layer[target].overlay=s.overlay[target].data();
    s.layer[target].overlay_generation=++s.generation;s.current=target;
    auto out=*background;out.gpu_affine=&s.layer[target];
    out.background_identity=reinterpret_cast<const PpuBackgroundPlanes*>(&s.layer[target]);
    out.background_generation=s.generation;
    return out;
}
} // namespace ntr
