#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace enhance {
// PCM16 gain: 100% is bit-exact. Clamp before narrowing, never wrap a peak.
inline int16_t audio_gain(int16_t sample,unsigned percent){
    if(percent>200)percent=200;
    const int scaled=int(sample)*int(percent)/100;
    return int16_t(scaled>32767?32767:scaled< -32768?-32768:scaled);
}

// One state per output stream, owned by its render thread. A 128-frame
// transition (~3.9 ms at the DS/DSP rate) avoids a block-edge gain step.
// No allocation, locking, floating point, or division by a variable in PCM.
struct AudioGainStats {
    uint32_t clipped_samples = 0, peak_input = 0, peak_output = 0;
};
class AudioGainRamp {
    static constexpr int Scale = 12800;
    int current_ = Scale, origin_ = Scale, target_ = Scale;
    unsigned position_ = 128;
public:
    void reset(unsigned percent) {
        if (percent > 200) percent = 200;
        current_ = origin_ = target_ = int(percent) * 128;
        position_ = 128;
    }
    AudioGainStats process(int16_t* stereo, size_t frames, unsigned percent, bool collect_metrics = false) {
        if (percent > 200) percent = 200;
        const int target = int(percent) * 128;
        if (target != target_) {
            origin_ = current_; target_ = target; position_ = 0;
        }
        AudioGainStats stats;
        // Normal playback at unity retains the old no-PCM-work path.
        // Metrics are an explicit Developer/test opt-in, not a steady tax.
        if (!collect_metrics && position_ == 128) {
            if (current_ == Scale) return stats;
            if (current_ == 0) {
                if (frames) std::memset(stereo, 0, frames * 2 * sizeof(int16_t));
                return stats;
            }
            // Preserve the vectorizable legacy integer-gain loop at every
            // other steady setting; no per-frame ramp/peak bookkeeping.
            for (size_t sample = 0; sample < frames * 2; ++sample)
                stereo[sample] = audio_gain(stereo[sample], percent);
            return stats;
        }
        for (size_t frame = 0; frame < frames; ++frame) {
            if (position_ < 128)
                current_ = origin_ + (target_ - origin_) * int(++position_) / 128;
            // Once a ramp reaches unity, the untouched remainder already
            // contains the exact output. Never bypass an active transition.
            if (!collect_metrics && position_ == 128 && current_ == Scale) break;
            // Advance once per stereo frame: both ears use identical gain.
            for (unsigned channel = 0; channel < 2; ++channel) {
                const int input = stereo[frame * 2 + channel];
                const int scaled = current_ == Scale ? input : input * current_ / Scale;
                const int output = scaled > 32767 ? 32767 : scaled < -32768 ? -32768 : scaled;
                if (collect_metrics) {
                    if (scaled != output) ++stats.clipped_samples;
                    const unsigned in_peak = unsigned(input < 0 ? -input : input);
                    const unsigned out_peak = unsigned(output < 0 ? -output : output);
                    if (in_peak > stats.peak_input) stats.peak_input = in_peak;
                    if (out_peak > stats.peak_output) stats.peak_output = out_peak;
                }
                stereo[frame * 2 + channel] = int16_t(output);
            }
        }
        return stats;
    }
};
}
