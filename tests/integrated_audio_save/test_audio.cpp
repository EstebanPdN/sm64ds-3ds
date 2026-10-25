#include "audio_gain.hpp"
#include "audio_diagnostics.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
    enhance::AudioGainRamp ramp;
    // Exhaustive PCM16 boundaries at every supported steady gain, including
    // mute, unity, clipping and the negative full-scale asymmetry.
    std::vector<int16_t> samples(65536 * 2);
    std::vector<int16_t> without_metrics(samples.size());
    for (unsigned gain = 0; gain <= 200; ++gain) {
        ramp.reset(gain);
        uint32_t clipped = 0;
        for (int input = -32768; input <= 32767; ++input) {
            const size_t i = size_t(input + 32768) * 2;
            samples[i] = samples[i + 1] = int16_t(input);
        }
        without_metrics = samples;
        const auto stats = ramp.process(samples.data(), 65536, gain, true);
        enhance::AudioGainRamp fast; fast.reset(gain);
        const auto no_stats = fast.process(without_metrics.data(), 65536, gain);
        assert(without_metrics == samples && no_stats.clipped_samples == 0 && no_stats.peak_input == 0);
        for (int input = -32768; input <= 32767; ++input) {
            const int scaled = input * int(gain) / 100;
            const int expected = std::max(-32768, std::min(32767, scaled));
            const size_t i = size_t(input + 32768) * 2;
            assert(samples[i] == expected && samples[i + 1] == expected);
            assert(enhance::audio_gain(int16_t(input), gain) == expected);
            if (scaled != expected) clipped += 2;
        }
        assert(stats.clipped_samples == clipped && stats.peak_input == 32768);
    }
    std::array<int16_t, 512 * 2> whole, split;
    whole.fill(23000); split = whole;
    enhance::AudioGainRamp a, b;
    a.reset(100); b.reset(100);
    a.process(whole.data(), 512, 0);
    b.process(split.data(), 17, 0);
    b.process(split.data() + 34, 95, 0);
    b.process(split.data() + 224, 400, 0);
    assert(whole == split);
    int previous = 23000, max_step = 0;
    for (size_t i = 0; i < 512; ++i) {
        assert(whole[2*i] == whole[2*i+1]);
        max_step = std::max(max_step, std::abs(previous - int(whole[2*i])));
        assert(whole[2*i] <= previous);
        if (i >= 127) assert(whole[2*i] == 0);
        previous = whole[2*i];
    }
    assert(max_step <= 180); // legacy block switch was a 23000-unit step
    // Repeated changes start at the current gain, including mid-ramp reversal.
    a.reset(0);
    std::array<int16_t, 64 * 2> partial;
    partial.fill(12000); a.process(partial.data(), 64, 200);
    assert(partial.back() == 12000);
    partial.fill(12000); a.process(partial.data(), 64, 0);
    assert(partial.front() < 12000 && partial.front() >= 11900);
    a.reset(999); partial.fill(20000);
    assert(a.process(partial.data(), 64, 999, true).clipped_samples == 128);
    // Disabled metrics preserve the bit-exact O(1) steady-unity path.
    // A zero-length target may be null; the unity fast path never touches it.
    a.reset(100); assert(a.process(nullptr, 0, 100).peak_input == 0);
    partial.fill(-23456); const auto unchanged = partial;
    assert(a.process(partial.data(), 64, 100).peak_input == 0 && partial == unchanged);
    a.reset(0); partial.fill(12000); a.process(partial.data(), 64, 100);
    assert(partial.front() < 12000 && partial.back() == 6000); // fast path must not skip transition
    partial.fill(12000); a.process(partial.data(), 64, 100);
    assert(partial.front() > 6000 && partial.back() == 12000);
    assert(!enhance::audio_diagnostics::queue_drained(0, 0));
    assert(!enhance::audio_diagnostics::queue_drained(3, 2));
    assert(enhance::audio_diagnostics::queue_drained(3, 3));
    std::printf("PASS 13,172,736 PCM/gain combinations; stereo ramp independent of chunking; mute step 23000 -> <=%d in 128 frames\n", max_step);
}
