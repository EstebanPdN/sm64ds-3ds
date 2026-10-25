// Informational host microbenchmark. No hardware/FPS inference or pass gate.
#include "audio_gain.hpp"
#include "audio_diagnostics.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>

static volatile unsigned checksum = 0;
using Clock = std::chrono::steady_clock;
static double measure(bool ramped, unsigned gain, bool metrics = false) {
    constexpr unsigned Iterations = 10000;
    std::array<int16_t, 1024> input, pcm;
    for (unsigned i = 0; i < input.size(); ++i) input[i] = int16_t(int(i * 67u % 65536u) - 32768);
    enhance::AudioGainRamp ramp;
    ramp.reset(gain);
    enhance::audio_diagnostics::Snapshot counters;
    counters.detailed_enabled = unsigned(metrics);
    const auto start = Clock::now();
    for (unsigned i = 0; i < Iterations; ++i) {
        std::memcpy(pcm.data(), input.data(), sizeof pcm);
        if (ramped) {
            const auto stats = ramp.process(pcm.data(), 512, gain, metrics);
            counters.clipped_samples += stats.clipped_samples;
            counters.peak_input = std::max(counters.peak_input, stats.peak_input);
            counters.peak_output = std::max(counters.peak_output, stats.peak_output);
            ++counters.submitted;
            enhance::audio_diagnostics::publish(counters);
        } else if (gain != 100) {
            for (auto& sample : pcm) sample = enhance::audio_gain(sample, gain);
        }
        checksum += unsigned(pcm[i % pcm.size()]);
    }
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / Iterations;
}
int main() {
    std::puts("Informational host median ns per 512 stereo frames (identical input copy; new paths include seven relaxed counter stores per block):");
    for (unsigned gain : {0u, 100u, 200u}) {
        std::array<double, 5> old_time, new_time, detailed_time;
        for (unsigned repeat = 0; repeat < 5; ++repeat) {
            old_time[repeat] = measure(false, gain);
            new_time[repeat] = measure(true, gain);
            detailed_time[repeat] = measure(true, gain, true);
        }
        std::sort(old_time.begin(), old_time.end());
        std::sort(new_time.begin(), new_time.end());
        std::sort(detailed_time.begin(), detailed_time.end());
        std::printf("gain=%u legacy=%.1f ramp_metrics_off=%.1f ramp_metrics_on=%.1f\n",
                    gain, old_time[2], new_time[2], detailed_time[2]);
    }
    return 0;
}
