#pragma once
// Inner loops of the sseqplayer segment mixer (SoundPool::GenerateSamples),
// copied into the generated tree by tools/prepare_integrated_mixer.py.
//
// Two implementations with bit-identical results:
// - portable: the original loop (64-bit products, branchy clamps);
// - ARMv6 (ARM11 MPCore): SMLAWB for (sample*gain)>>10, SSAT for the
//   DS capture/speaker clamp, PKHBT to pack a stereo frame, and loops
//   specialised for the loop-invariant voice flags.
// SMLAWB(gain<<6, s, acc) = acc + floor(s*(gain<<6) / 2^16) = acc + ((s*gain)>>10)
// exactly, because gain<<6 <= 2^24 and the product keeps 48 bits.
//
// Build with -DSM64DS_MIXER_PORTABLE to compile only the portable loop.
// At run time the portable loop is selected by sm64ds_mixer::set_portable()
// (sdmc:/3ds/sm64ds/mezclador-portable.txt) for console A/B timing; both
// produce the same PCM and state, so switching is always safe.
#include <cstdint>
#include <cstring>
#if defined(__arm__) && defined(__ARM_FEATURE_DSP) && defined(__ARM_FEATURE_SIMD32) && \
    !defined(__thumb__) && !defined(SM64DS_MIXER_PORTABLE)
#include <arm_acle.h>
#define SM64DS_MIXER_ARMV6 1
#else
#define SM64DS_MIXER_ARMV6 0
#endif

namespace sm64ds_mixer {
// Stereo accumulator of one output sample; 8-byte aligned so the ARMv6 loop
// reads and writes a pair with one LDRD/STRD (stack storage only).
struct alignas(8) Pair { std::int32_t l, r; };

inline bool portable_selected = !SM64DS_MIXER_ARMV6;
inline void set_portable(bool portable) { portable_selected = portable || !SM64DS_MIXER_ARMV6; }
inline bool armv6_active() { return !portable_selected; }
inline const char* kernel_name() { return portable_selected ? "portable" : "armv6"; }

// One independent PCM voice for one segment. Volume and pan are constant
// inside a segment; timer/position/ended are the channel's live state.
struct Voice {
    const std::int16_t* data;
    std::uint32_t reload, total, loop;
    bool repeat;
    std::int32_t gl, gr;    // gain*(128-pan), gain*pan (<= 2^18)
    std::uint32_t timer;
    std::int32_t pos;
    bool ended;
};

// The original per-sample loop. acc==nullptr: excluded from the mix;
// direct==nullptr: channel 1/3 direct output not requested. Returns the
// number of samples played before the voice ended (diagnostic counter).
inline unsigned mix_voice_portable(Voice& v, const unsigned* cycles, unsigned span, bool audible,
                                   Pair* acc, std::int32_t* directLeft, std::int32_t* directRight) {
    std::uint32_t timer = v.timer; std::int32_t pos = v.pos; bool ended = false; unsigned played = 0;
    for (unsigned n = 0; n < span; ++n) {
        timer += cycles[n];
        while (timer >= 0x10000) {
            timer = timer - 0x10000 + v.reload;
            if (++pos >= std::int32_t(v.total)) { if (v.repeat) pos = std::int32_t(v.loop); else { ended = true; break; } }
        }
        if (ended) break;
        ++played;
        if (!audible) continue;
        const std::int32_t sample = pos < 0 ? 0 : v.data[pos];
        const std::int32_t l = std::int32_t((std::int64_t(sample) * v.gl) >> 10), r = std::int32_t((std::int64_t(sample) * v.gr) >> 10);
        if (directLeft) { directLeft[n] = l; directRight[n] = r; }
        if (acc) { acc[n].l += l; acc[n].r += r; }
    }
    v.timer = timer; v.pos = pos; v.ended = ended;
    return played;
}

// DS capture unit / speaker input: sum>>8 clamped to signed 16 bits.
inline std::int32_t capture_portable(std::int32_t sum) { sum >>= 8; return sum < -0x8000 ? -0x8000 : sum > 0x7FFF ? 0x7FFF : sum; }

#if SM64DS_MIXER_ARMV6
// Packs two signed 16-bit values (already saturated) into one stereo word:
// low half = left, high half = right (little-endian int16_t[2]).
__attribute__((always_inline)) inline std::uint32_t pack_lr(std::int32_t left, std::int32_t right) {
    std::uint32_t out;
    __asm__("pkhbt %0, %1, %2, lsl #16" : "=r"(out) : "r"(left), "r"(right));
    return out;
}

// One output sample of a mixed voice (audible, not a ch1/ch3 direct output)
// whose position is, and stays, non-negative after the start delay.
// remaining = total - pos, so `++pos >= total` is `--remaining <= 0`, and the
// sample is *p == data[pos]. Out of line: inlined into GenerateSamples the
// loop spilled 8 values per sample. Silent and direct voices keep the
// portable loop, which measured faster for them (Unicorn instruction counts).
template <bool Uniform>
__attribute__((noinline)) unsigned mix_voice_armv6(Voice& v, const unsigned* cycles, unsigned span, Pair* acc) {
    std::uint32_t timer = v.timer; std::int32_t pos = v.pos;
    const std::uint32_t reloadDelta = v.reload - 0x10000u;   // timer - 0x10000 + reload (mod 2^32)
    const std::uint32_t step0 = cycles[0];
    const std::int32_t total = std::int32_t(v.total), loop = std::int32_t(v.loop);
    const bool repeat = v.repeat;
    const std::int32_t gl = v.gl << 6, gr = v.gr << 6;
    unsigned n = 0;
    // Start delay: a negative position reads silence (adds nothing).
    for (; n < span; ++n) {
        timer += Uniform ? step0 : cycles[n];
        while (timer >= 0x10000) {
            timer += reloadDelta;
            if (++pos >= total) { if (!repeat) { v.timer = timer; v.pos = pos; v.ended = true; return n; } pos = loop; }
        }
        if (pos >= 0) break;
    }
    if (n == span) { v.timer = timer; v.pos = pos; v.ended = false; return n; }
    const std::int16_t* p = v.data + pos;
    std::int32_t remaining = total - pos;
    // Loop restart: data+loop and total-loop, or nullptr for a one-shot.
    const struct { const std::int16_t* p; std::int32_t remaining; } restart{repeat ? v.data + loop : nullptr, total - loop};
    if (!Uniform) {
        // Sample n already advanced its timer above.
        for (;;) {
            const std::int32_t sample = *p;
            Pair a = acc[n]; a.l = __smlawb(gl, sample, a.l); a.r = __smlawb(gr, sample, a.r); acc[n] = a;
            if (++n == span) break;
            timer += cycles[n];
            while (timer >= 0x10000) {
                timer += reloadDelta; ++p;
                if (--remaining <= 0) {
                    if (!restart.p) { v.timer = timer; v.pos = total - remaining; v.ended = true; return n; }
                    p = restart.p; remaining = restart.remaining;
                }
            }
        }
        v.timer = timer; v.pos = total - remaining; v.ended = false;
        return span;
    }
    // Every ordinary voice at the DS-native rate. GCC kept spilling a gain
    // and splitting the pair access (22 instructions per voice sample); this
    // is 14 with one timer step and 5 per extra step. The timer is biased by
    // -0x10000 so the add's sign flag is the overflow test (timer stays far
    // below 2^31). C equivalent, entered at 1 (sample n already advanced):
    // 1: s=*p; a->l=SMLAWB(gl,s,a->l); a->r=SMLAWB(gr,s,a->r); if(++a==end)done;
    //    if((T+=step)<0)goto 1;
    // 2: ++p; if(--remaining<=0){T+=delta; if(!restart.p)ended;
    //                            p=restart.p; remaining=restart.remaining; goto check;}
    //    if((T+=delta)>=0)goto 2; goto 1;
    // LDRD/STRD only on acc: 8-byte aligned stack Pairs.
    Pair* a = acc + n;
    Pair* const end = acc + span;
    std::int32_t biased = std::int32_t(timer - 0x10000u);
    const std::int32_t delta = std::int32_t(reloadDelta), step = std::int32_t(step0);
    register std::int32_t l asm("r4");
    register std::int32_t r asm("r5");
    std::int32_t sample;
    __asm__ volatile(
        "b 1f\n"
        "2: add %[p], %[p], #2\n"
        "subs %[rem], %[rem], #1\n"
        "ble 5f\n"
        "adds %[t], %[t], %[delta]\n"
        "bpl 2b\n"
        "1: ldrsh %[s], [%[p]]\n"
        "ldrd %[l], %[r], [%[a]]\n"
        "smlawb %[l], %[gl], %[s], %[l]\n"
        "smlawb %[r], %[gr], %[s], %[r]\n"
        "strd %[l], %[r], [%[a]], #8\n"
        "cmp %[a], %[end]\n"
        "beq 9f\n"
        "adds %[t], %[t], %[step]\n"
        "bpl 2b\n"
        "b 1b\n"
        // End of the sample data: this step's timer update, then restart or end.
        "5: add %[t], %[t], %[delta]\n"
        "ldr %[s], [%[rs]]\n"
        "cmp %[s], #0\n"
        "beq 9f\n"
        "mov %[p], %[s]\n"
        "ldr %[rem], [%[rs], #4]\n"
        "cmp %[t], #0\n"
        "bpl 2b\n"
        "b 1b\n"
        "9:\n"
        : [s] "=&r"(sample), [l] "=&r"(l), [r] "=&r"(r), [a] "+r"(a), [t] "+r"(biased), [p] "+r"(p), [rem] "+r"(remaining)
        : [end] "r"(end), [step] "r"(step), [delta] "r"(delta), [gl] "r"(gl), [gr] "r"(gr), [rs] "r"(&restart)
        : "cc", "memory");
    // A one-shot ended during sample a's timer step (a != end).
    const bool ended = a != end;
    v.timer = std::uint32_t(biased) + 0x10000u; v.pos = total - remaining; v.ended = ended;
    return ended ? unsigned(a - acc) : span;
}
#endif

// uniform: every cycles[n] equals cycles[0] (always true at the DS-native rate).
inline unsigned mix_voice(Voice& v, const unsigned* cycles, bool uniform, unsigned span, bool audible,
                          Pair* acc, std::int32_t* directLeft, std::int32_t* directRight) {
#if SM64DS_MIXER_ARMV6
    // A loop start beyond INT32_MAX reads silence after a restart, and loops
    // under 8 samples restart so often that the portable loop is faster.
    if (!portable_selected && span && audible && acc && !directLeft && std::int32_t(v.loop) >= 0 &&
        (!v.repeat || std::int64_t(v.total) - std::int64_t(v.loop) >= 8))
        return uniform ? mix_voice_armv6<true>(v, cycles, span, acc) : mix_voice_armv6<false>(v, cycles, span, acc);
#else
    (void)uniform;
#endif
    return mix_voice_portable(v, cycles, span, audible, acc, directLeft, directRight);
}

inline std::int32_t capture(std::int32_t sum) {
#if SM64DS_MIXER_ARMV6
    if (!portable_selected) return __ssat(sum >> 8, 16);
#endif
    return capture_portable(sum);
}

// Speaker output of a block without live channels, captures or direct
// outputs: out[n] = {capture(acc[n].l), capture(acc[n].r)}. dst may be
// unaligned (single-word stores only).
inline void store_block(const Pair* acc, unsigned span, std::uint8_t* dst) {
#if SM64DS_MIXER_ARMV6
    if (!portable_selected) {
        for (unsigned n = 0; n < span; ++n) {
            const Pair p = acc[n];
            const std::uint32_t word = pack_lr(__ssat(p.l >> 8, 16), __ssat(p.r >> 8, 16));
            std::memcpy(dst + 4 * n, &word, 4);
        }
        return;
    }
#endif
    for (unsigned n = 0; n < span; ++n) {
        const std::int16_t out[2] = {std::int16_t(capture_portable(acc[n].l)), std::int16_t(capture_portable(acc[n].r))};
        std::memcpy(dst + 4 * n, out, 4);
    }
}
}
