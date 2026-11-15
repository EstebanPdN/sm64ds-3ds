// BUILD134 cost cuts: same outputs, fewer instructions. Console A/B switch,
// the same file as sm64ds::perf::cuts() (perf_switch.hpp):
// sdmc:/3ds/sm64ds/sin-recortes.txt present = the BUILD 133 paths. Read once.
// PPU_NO_COST_CUTS=1 selects them too (host comparisons).
[[maybe_unused]] static bool ppu_cost_cuts(){
    static const bool value=[]{
        if(std::getenv("PPU_NO_COST_CUTS"))return false;
#if defined(PPU_COST_CUTS_OFF) // host comparison: this build never cuts
        return false;
#elif defined(__3DS__)
        struct stat st;return stat("sdmc:/3ds/sm64ds/sin-recortes.txt",&st)!=0;
#else
        return true;
#endif
    }();
    return value;
}
// The affine memo's planned compare (ppu_affine_memo.inc) relies on
// update_text_layer's incremental tile update.
[[maybe_unused]] static bool ppu_memo_cut(){
    static const bool value=ppu_cost_cuts() && std::getenv("PPU_NO_TILE_INCREMENTAL")==nullptr;
    return value;
}
// Host comparison coverage (tests/native_renderer/ppu_cost_cuts.cpp): how
// often each cut ran. Not compiled into the game.
#if defined(PPU_COST_CUTS_STATS)
}
// 0 planned memo compares, 1 of them equal (hit or affine-only), 2 plans built,
// 3 OBJ passes kept, 4 drawn by the row raster, 5 drawn by scan_obj, 6 engine-A word scans.
unsigned long long ppu_cost_cut_stats[8];
namespace {
#define PPU_CUT_COUNT(slot) (++ppu_cost_cut_stats[slot])
#else
#define PPU_CUT_COUNT(slot) ((void)0)
#endif
static inline uint32_t ppu_word(const uint8_t* p){uint32_t w;std::memcpy(&w,p,4);return w;}
// memcmp(a,b,n)==0 by words, one 32-byte cache line of each side per step
// with the following lines requested ahead (the snapshots compared here are
// far larger than the ARM11's 16 KB data cache).
[[maybe_unused]] static bool ppu_equal_bytes(const void* left,const void* right,size_t n){
    const auto* a=static_cast<const uint8_t*>(left);const auto* b=static_cast<const uint8_t*>(right);
    for(;n>=32;a+=32,b+=32,n-=32){
        __builtin_prefetch(a+96);__builtin_prefetch(b+96);
        if((ppu_word(a)^ppu_word(b))|(ppu_word(a+4)^ppu_word(b+4))|(ppu_word(a+8)^ppu_word(b+8))|(ppu_word(a+12)^ppu_word(b+12))|
           (ppu_word(a+16)^ppu_word(b+16))|(ppu_word(a+20)^ppu_word(b+20))|(ppu_word(a+24)^ppu_word(b+24))|(ppu_word(a+28)^ppu_word(b+28)))return false;
    }
    for(;n>=4;a+=4,b+=4,n-=4)if(ppu_word(a)!=ppu_word(b))return false;
    for(;n;++a,++b,--n)if(*a!=*b)return false;
    return true;
}
// Any byte of p[0..n) with bit 7 set, by words.
[[maybe_unused]] static bool ppu_any_bit7(const uint8_t* p,size_t n){
    PPU_CUT_COUNT(6);
    for(;n>=32;p+=32,n-=32){
        __builtin_prefetch(p+96);
        if((ppu_word(p)|ppu_word(p+4)|ppu_word(p+8)|ppu_word(p+12)|ppu_word(p+16)|ppu_word(p+20)|ppu_word(p+24)|ppu_word(p+28))&0x80808080u)return true;
    }
    for(;n;++p,--n)if(*p&0x80)return true;
    return false;
}
