#pragma once
#include "sm64ds/native_2d.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace sm64ds {
// The first cache deliberately compares physical bytes instead of relying on
// write notifications: the original SDK and display capture retain writable
// native_video_pointer aliases. Call only at the synchronous scanout boundary.
class Native2DScanoutCache {
public:
    struct Inputs {
        const std::uint8_t* registers; // 0x2000 owned bytes
        const std::uint8_t* vram;      // all 0xa4000 physical bytes, including tail
        const std::uint8_t* palette;   // 0x800 bytes, both engines
        const std::uint8_t* oam;       // 0x800 bytes, attributes and affine params
        bool complete() const {return registers && vram && palette && oam;}
    };
    static constexpr std::size_t input_bytes=676020;
    struct Stats {
        std::uint64_t checks=0,hits=0,misses=0,recompositions=0,invalidations=0;
        // Sum of spans passed to memcmp (which may exit early), not measured
        // DRAM traffic. A hit requests exactly input_bytes bytes of comparison.
        std::uint64_t bytes_compared=0,baseline_bytes_copied=0;
    };
    const Stats& stats() const {return stats_;}
    void reset_stats() {stats_={};}
    void invalidate() {
        valid_=false;output_=nullptr;revision_=0;++stats_.invalidations;
        // Preserve serial across reset/init/exit so different bytes cannot
        // accidentally acquire the prior renderer's nonzero revision token.
    }
    std::uint64_t revision() const {return valid_?revision_:0;}
    bool matches(const Inputs& inputs,const Native2DFrame& output) {
        ++stats_.checks;
        if(valid_ && output_==&output && inputs.complete() && equal(inputs)) {
            ++stats_.hits;return true;
        }
        ++stats_.misses;return false;
    }
    // The caller must establish successful scanout before committing. Neither
    // a failed read nor an unavailable video state may promote this baseline.
    bool commit(const Inputs& inputs,const Native2DFrame& output) {
        if(!inputs.complete()) {invalidate();return false;}
        std::size_t at=0;
        visit(inputs,[&](const std::uint8_t* bytes,std::size_t length) {
            std::memcpy(baseline_.data()+at,bytes,length);at+=length;
        });
        stats_.baseline_bytes_copied+=at;++stats_.recompositions;
        output_=&output;valid_=true;
        // Exhaustion falls back to the renderer's token-zero CRC route.
        revision_=serial_==std::numeric_limits<std::uint64_t>::max()?0:++serial_;
        return true;
    }
private:
    // Current compositor reads: DISPCNT; BGxCNT/scroll/affine; WIN; BLDCNT,
    // BLDALPHA, BLDY; MASTER_BRIGHT. MOSAIC/reserved bytes inside 08..55 are
    // included conservatively. DISPSTAT/VCOUNT, IRQ/GX and DISPCAPCNT belong
    // to other services and do not determine ordinary CPU scanout pixels.
    // VRAMCNT determines all logical aliases and POWCNT determines screen swap.
    template<class F> static void visit(const Inputs& inputs,F fn) {
        for(const auto base:{0u,0x1000u}) {
            fn(inputs.registers+base,4);
            fn(inputs.registers+base+8,0x4e);
            fn(inputs.registers+base+0x6c,2);
        }
        fn(inputs.registers+0x240,10);
        fn(inputs.registers+0x304,2);
        fn(inputs.vram,0xa4000);fn(inputs.palette,0x800);fn(inputs.oam,0x800);
    }
    bool equal(const Inputs& inputs) {
        std::size_t at=0;bool same=true;
        visit(inputs,[&](const std::uint8_t* bytes,std::size_t length) {
            if(same) {
                stats_.bytes_compared+=length;
                same=std::memcmp(baseline_.data()+at,bytes,length)==0;
            }
            at+=length;
        });
        return same;
    }
    std::array<std::uint8_t,input_bytes> baseline_{};
    const Native2DFrame* output_=nullptr;
    Stats stats_{};
    std::uint64_t serial_=0,revision_=0;
    bool valid_=false;
};

// Only ordinary presentation is cached. Source A capture keeps its independent
// compositor route before display selection and MASTER_BRIGHT. Output belongs
// exclusively to this cache/compositor until invalidation.
bool native_2d_scanout_cached(Native2DFrame& frame,Native2DScanoutCache& cache);
} // namespace sm64ds
