// BUILD141: text BG layers drawn by the GPU over the native map.
// The opening's bottom screen scrolls two 512x512 cloud layers (BG0, BG1)
// over the rotating map. Merging them into the 256x192 overlay on the CPU,
// copying that into the foreground and uploading 192 KB took 20-30 ms per
// refresh on the Old 3DS (prueba139), so the opening stayed near 22 FPS.
// When the consumer can take them (ppu_prepare_native_affine with a behind
// plane), the layers are published instead: a copy of each layer raster
// (alpha 255, or EVA/16 for the one blended layer) that the GPU samples with
// the scroll offsets, like the map raster (512x512 RGBA8, repeat: register
// values the console has run). A copy is made only when the raster or its
// blend changes, into the other of two buffers (the worker may still upload
// from the current one); the layers stay on the CPU path for the call in
// which they changed, so an animated layer never pays a copy per update.
static bool native_affine_layers=false;
void* (*ppu_text_alloc)(size_t)=nullptr;
void (*ppu_text_free)(void*)=nullptr;
unsigned ppu_affine_scroll_hits=0; // BUILD142: scroll-only descriptor updates (tests)
uint32_t ppu_affine_memo_miss=0; // BUILD142: first byte that missed the memo last (diagnostics)
namespace {
struct TextSnapshot {
    uint32_t* buffer[2]{};size_t bytes[2]{};uint64_t source[2]{},key[2]{};uint8_t alpha[2]{};unsigned current=0;
};
TextSnapshot text_snapshots[4];
uint64_t text_snapshot_serial=0,text_snapshot_epoch=1;
bool text_retry=false; // a call fell back only because a layer had just changed
void* text_alloc(size_t n){return ppu_text_alloc?ppu_text_alloc(n):std::malloc(n);}
void text_free(void* p){if(!p)return;if(ppu_text_free)ppu_text_free(p);else std::free(p);}
const uint32_t* text_snapshot(unsigned bg,const TextLayerRaster& r,int width,int height,uint8_t alpha,uint64_t& key){
    auto& t=text_snapshots[bg];const size_t count=size_t(width)*size_t(height),bytes=count*4;
    if(r.px.size()!=count)return nullptr;
    const unsigned c=t.current;
    if(t.buffer[c]&&t.bytes[c]==bytes&&t.source[c]==r.generation&&t.alpha[c]==alpha){key=t.key[c];return t.buffer[c];}
    const unsigned n=c^1u;
    if(t.buffer[n]&&t.bytes[n]!=bytes){text_free(t.buffer[n]);t.buffer[n]=nullptr;t.bytes[n]=0;}
    if(!t.buffer[n]){t.buffer[n]=static_cast<uint32_t*>(text_alloc(bytes));if(!t.buffer[n])return nullptr;t.bytes[n]=bytes;}
    const uint32_t* source=r.px.data();uint32_t* out=t.buffer[n];const uint32_t a=uint32_t(alpha)<<24;
    for(size_t i=0;i<count;++i){const uint32_t v=source[i];out[i]=(v>>24)?((v&0xffffffu)|a):0u;}
    t.source[n]=r.generation;t.alpha[n]=alpha;t.key[n]=++text_snapshot_serial;t.current=n;key=t.key[n];
    // A copy costs 1 MiB of writes and the GPU upload: they must stay rare.
    static unsigned copies=0;++copies;
    if(copies<=16||!(copies&63))std::printf("PPU text layer copy: bg=%u %dx%d alpha=%u generation=%llu copies=%u\n",bg,width,height,unsigned(alpha),(unsigned long long)r.generation,copies);
    return out;
}
bool text_layers_equal(const PpuGpuAffineLayer& a,const PpuGpuAffineLayer& b){
    if(a.text_count!=b.text_count||a.text_priority!=b.text_priority)return false;
    for(unsigned k=0;k<a.text_count;++k){const auto& x=a.text[k];const auto& y=b.text[k];
        if(x.raster!=y.raster||x.key!=y.key||x.width!=y.width||x.height!=y.height||x.hofs!=y.hofs||x.vofs!=y.vofs)return false;}
    return true;
}
}
// Frees every layer copy (scene change; the caller has drained the render
// worker). Descriptors that named them are invalidated by the epoch.
unsigned ppu_release_text_snapshots(){
    unsigned released=0;
    for(auto& t:text_snapshots)for(unsigned i=0;i<2;++i)if(t.buffer[i]){text_free(t.buffer[i]);t.buffer[i]=nullptr;t.bytes[i]=0;t.source[i]=0;++released;}
    if(released){++text_snapshot_epoch;std::printf("PPU text layers released: buffers=%u\n",released);}
    return released;
}
