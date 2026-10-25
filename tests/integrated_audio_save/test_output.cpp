#include "sound_output_3ds.h"
#include "enhance_settings.h"
#include "audio_diagnostics.hpp"
#include <3ds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" { volatile int sm64ds_sound_game_waiting = 0; }
static std::vector<ndspWaveBuf*> queue;
static unsigned render_calls = 0, exits = 0;
static bool render_ok = true, joined = false, allocated = false;
static bool complete_during_render = false;
Result ndspInit() { joined = false; return 0; }
void ndspExit() { assert(queue.empty()); joined = true; ++exits; }
void ndspChnReset(int) {}
void ndspChnSetInterp(int, int) {}
void ndspChnSetRate(int, float) {}
void ndspChnSetFormat(int, int) {}
void ndspChnSetMix(int, float*) {}
void ndspChnWaveBufAdd(int, ndspWaveBuf* b) {
    assert(std::find(queue.begin(), queue.end(), b) == queue.end());
    b->status = 1; queue.push_back(b);
}
void ndspChnWaveBufClear(int) { queue.clear(); }
void* linearAlloc(size_t n) { allocated = true; return std::malloc(n); }
void linearFree(void* p) { assert(joined && allocated); std::free(p); allocated = false; }
void DSP_FlushDataCache(void*, size_t) {}
void RecursiveLock_Lock(RecursiveLock*) {}
void RecursiveLock_Unlock(RecursiveLock*) {}
void svcSleepThread(long long) { assert(false); }
Sm64dsSoundCommands::~Sm64dsSoundCommands() = default;
bool Sm64dsSoundCommands::render(int16_t* pcm, unsigned frames) {
    assert(frames == 128); ++render_calls;
    if (complete_during_render) {
        assert(!queue.empty()); queue.front()->status = NDSP_WBUF_DONE;
        queue.erase(queue.begin()); complete_during_render = false;
    }
    std::fill(pcm, pcm + frames*2, int16_t(23000)); return render_ok;
}
static void complete(unsigned n) {
    for (unsigned i = 0; i < n; ++i) queue[i]->status = NDSP_WBUF_DONE;
    queue.erase(queue.begin(), queue.begin() + n);
}
int main() {
    Sm64dsSoundCommands service;
    Sm64dsSoundOutput output;
    auto settings = enhance::Settings::safe(); settings.developer_overlay = true; enhance::publish(settings);
    assert(output.open(service)); assert(!output.open(service));
    assert(output.pump()); assert(render_calls == 12 && queue.size() == 3);
    auto stats = enhance::audio_diagnostics::snapshot();
    assert(stats.submitted == 3 && stats.completed == 0 && stats.queue_drains == 0);
    assert(queue[0]->data_pcm16[0] == 23000);
    assert(output.pump() && render_calls == 12); // DSP still owns every buffer
    complete(1); settings.audio_volume = 0; enhance::publish(settings);
    assert(output.pump() && render_calls == 16 && queue.size() == 3);
    assert(queue.back()->data_pcm16[0] >= 22820 && queue.back()->data_pcm16[0] < 23000);
    assert(queue.back()->data_pcm16[254] == 0 && queue.back()->data_pcm16[1023] == 0);
    stats = enhance::audio_diagnostics::snapshot(); assert(stats.queue_drains == 0 && stats.completed == 1);
    complete(3); assert(output.pump());
    stats = enhance::audio_diagnostics::snapshot(); assert(stats.queue_drains == 1 && stats.completed == 4);
    complete(1); settings.audio_volume = 200; enhance::publish(settings);
    assert(output.pump());
    stats = enhance::audio_diagnostics::snapshot();
    assert(stats.clipped_samples > 0 && stats.peak_input == 23000 && stats.peak_output == 32767);
    assert(stats.detailed_enabled == 1);
    // Production failure path joins NDSP before freeing descriptor/PCM state.
    complete(1); render_ok = false; assert(!output.pump());
    assert(joined && !allocated && exits == 1 && !output.pump());
    render_ok = true; settings.developer_overlay = false; enhance::publish(settings);
    assert(output.open(service)); assert(output.pump());
    stats = enhance::audio_diagnostics::snapshot(); assert(stats.queue_drains == 0 && stats.submitted == 3);
    assert(stats.detailed_enabled == 0 && stats.clipped_samples == 0 && stats.peak_input == 0);
    assert(queue[0]->data_pcm16[0] == 32767); // saturation still applied without detailed counters
    complete(1); complete_during_render = true; assert(output.pump());
    stats = enhance::audio_diagnostics::snapshot();
    assert(stats.completed == 2 && stats.submitted == 5 && queue.size() == 3);
    output.close(); assert(exits == 2);
    std::puts("PASS generated NDSP output: ownership, 128-frame chunks, continuous volume, queue-drain evidence, clipping counters, failure shutdown and restart");
}
