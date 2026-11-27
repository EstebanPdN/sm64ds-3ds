"""Patch only generated NDSP output: gain continuity and bounded diagnostics.

Call prepare(output) after copying the local options headers. It accepts the
pinned source or the exact legacy block-gain patch used by render60; no other
source shape is accepted. The pinned vendor is never changed.
"""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
LEGACY_INCLUDE = '#include "../platform/3ds/enhance_settings.h"\n#include "../platform/3ds/audio_gain.hpp"\n'
LEGACY_GAIN = '''        const unsigned volume=enhance::live().audio_volume.load(std::memory_order_relaxed);
        if(volume!=100)for(unsigned sample=0;sample<Frames*2;++sample)
            buffer.data_pcm16[sample]=enhance::audio_gain(buffer.data_pcm16[sample],volume);
'''


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Audio integration anchor changed: ' + old[:90])
    return text.replace(old, new, 1)


def patch_output(text):
    if text.count('constexpr unsigned Chunk=128;') != 1:
        raise ValueError('Audio render chunk contract changed')
    # Give game commands a lock handoff every ~2 ms instead of ~4 ms.
    # Mixer/sample rate and submitted buffer duration stay unchanged.
    text = once(text,'constexpr unsigned Chunk=128;','constexpr unsigned Chunk=64;')
    if text.startswith(LEGACY_INCLUDE):
        text = text[len(LEGACY_INCLUDE):]
        text = once(text, LEGACY_GAIN, '')
    text = once(text,
        'struct Sm64dsSoundOutputState {s16* samples;ndspWaveBuf buffers[3];bool owned[3];};',
        '''struct Sm64dsSoundOutputState {
    s16* samples=nullptr;ndspWaveBuf buffers[4]{};bool owned[4]{}; // BUILD137: Sm64dsSoundOutput::Count
    enhance::AudioGainRamp gain;
    enhance::audio_diagnostics::Snapshot metrics;
};''')
    text = once(text,
        'state=static_cast<Sm64dsSoundOutputState*>(std::calloc(1,sizeof(Sm64dsSoundOutputState)));',
        '''state=static_cast<Sm64dsSoundOutputState*>(std::malloc(sizeof(Sm64dsSoundOutputState)));
    if(state)::new(state) Sm64dsSoundOutputState;''')
    text = once(text, '    state->samples=static_cast<s16*>(linearAlloc(Count*Frames*2*sizeof(s16)));',
        '''    static_assert(sizeof(state->buffers)/sizeof(state->buffers[0])==Count&&sizeof(state->owned)==Count,"one NDSP buffer per queue slot");
    state->samples=static_cast<s16*>(linearAlloc(Count*Frames*2*sizeof(s16)));''')
    text = once(text, '    service=&commands;', '''    service=&commands;
    state->gain.reset(enhance::live().audio_volume.load(std::memory_order_relaxed));
    enhance::audio_diagnostics::publish(state->metrics);''')
    text = once(text, '''    for(unsigned i=0;i<Count;++i){
        auto& buffer=state->buffers[i];
        if(state->owned[i]&&buffer.status==NDSP_WBUF_DONE){state->owned[i]=false;++completed;}
        if(state->owned[i])continue;''', '''    // Snapshot completion before refilling. A queue exhausted since its
    // previous submission is a deadline miss; initial prefill is excluded.
    unsigned previously_owned=0,completed_now=0;
    bool gap_counted=false; // BUILD137 underrun counter
    for(unsigned i=0;i<Count;++i){
        if(!state->owned[i])continue;
        ++previously_owned;
        if(state->buffers[i].status==NDSP_WBUF_DONE){
            state->owned[i]=false;++completed;++completed_now;
        }
    }
    if(enhance::audio_diagnostics::queue_drained(previously_owned,completed_now))
        ++state->metrics.queue_drains;
    for(unsigned i=0;i<Count;++i){
        auto& buffer=state->buffers[i];
        // Preserve the pinned pump's catch-up behavior when another DSP
        // buffer completes while an earlier replacement is being rendered.
        if(state->owned[i]&&buffer.status==NDSP_WBUF_DONE){state->owned[i]=false;++completed;}
        if(state->owned[i])continue;''')
    text = once(text, 'while(sm64ds_sound_game_waiting)',
                'while(__atomic_load_n(&sm64ds_sound_game_waiting,__ATOMIC_RELAXED))')
    text = once(text, '        DSP_FlushDataCache(buffer.data_pcm16,Frames*2*sizeof(s16));',
        '''        const bool collect_metrics=enhance::live().developer_overlay.load(std::memory_order_relaxed);
        state->metrics.detailed_enabled=unsigned(collect_metrics);
        const auto stats=state->gain.process(buffer.data_pcm16,Frames,
            enhance::live().audio_volume.load(std::memory_order_relaxed),collect_metrics);
        state->metrics.clipped_samples+=stats.clipped_samples;
        if(stats.peak_input>state->metrics.peak_input)state->metrics.peak_input=stats.peak_input;
        if(stats.peak_output>state->metrics.peak_output)state->metrics.peak_output=stats.peak_output;
        DSP_FlushDataCache(buffer.data_pcm16,Frames*2*sizeof(s16));''')
    text = once(text, '''        ndspChnWaveBufAdd(0,&buffer);state->owned[i]=true;++submitted;
    }
    return true;''', '''        // BUILD137: the channel already stopped (every queued buffer played)
        // when a buffer is added after the prefill: an audible gap.
        // At most one per pump pass: NDSP updates the playing flag a DSP frame
        // later, so the next buffers of the same refill still see it stopped.
        if(!gap_counted&&submitted>=Count&&!ndspChnIsPlaying(0)){gap_counted=true;enhance::audio_diagnostics::timing.underruns.fetch_add(1,std::memory_order_relaxed);}
        ndspChnWaveBufAdd(0,&buffer);state->owned[i]=true;++submitted;
    }
    state->metrics.submitted=submitted;state->metrics.completed=completed;
    enhance::audio_diagnostics::publish(state->metrics);
    return true;''')
    # Keep the pinned libc allocator domain. Global operator new in this
    # port can route into the original game heap; only construction changes.
    text = once(text, 'std::free(state);state=nullptr;',
                'state->~Sm64dsSoundOutputState();std::free(state);state=nullptr;')
    # Last: the metrics anchor above still names the original flush line.
    text = once(text,'        DSP_FlushDataCache(buffer.data_pcm16,Frames*2*sizeof(s16));','        // Clean the PCM lines for the DSP directly: dsp::DSP FlushDataCache is\n        // an IPC per buffer on the 30%-quota audio core of an Old 3DS.\n        if(R_FAILED(svcStoreProcessDataCache(CUR_PROCESS_HANDLE,u32(reinterpret_cast<uintptr_t>(buffer.data_pcm16)),Frames*2*sizeof(s16))))\n            DSP_FlushDataCache(buffer.data_pcm16,Frames*2*sizeof(s16));')
    text = output_address_patch(text)
    text = timing_patch(text)
    return (LEGACY_INCLUDE + '#include "../platform/3ds/audio_diagnostics.hpp"\n#include <new>\n' + text)


def timing_patch(text):
    """2026-11-09: mixer time in the test reports (audio_diagnostics::Timing).

    The BUILD128 hardware log measured audioLoad=27.8% of a core for the
    pump, but no report said how much of it is mixing, waiting for the game
    or NDSP, nor whether the game thread waits for the mixer. Two system
    ticks per 64-frame chunk on the audio thread; no I/O, lock or allocation."""
    text = once(text, '''bool Sm64dsSoundOutput::pump(){
    if(!initialized){std::snprintf(failure,sizeof(failure),"sound output is closed");return false;}
''', '''bool Sm64dsSoundOutput::pump(){
    if(!initialized){std::snprintf(failure,sizeof(failure),"sound output is closed");return false;}
    const u64 pumpStart=svcGetSystemTick();
''')
    text = once(text, '''            if(lock){
                while(__atomic_load_n(&sm64ds_sound_game_waiting,__ATOMIC_RELAXED))svcSleepThread(50000);
                RecursiveLock_Lock(lock);
            }
            const bool rendered=service->render(buffer.data_pcm16+done*2,Chunk);
            if(lock)RecursiveLock_Unlock(lock);
''', '''            const u64 waitStart=svcGetSystemTick();
            if(lock){
                while(__atomic_load_n(&sm64ds_sound_game_waiting,__ATOMIC_RELAXED))svcSleepThread(50000);
                RecursiveLock_Lock(lock);
            }
            const u64 mixStart=svcGetSystemTick();
            const bool rendered=service->render(buffer.data_pcm16+done*2,Chunk);
            const u64 mixEnd=svcGetSystemTick();
            if(lock)RecursiveLock_Unlock(lock);
            enhance::audio_diagnostics::record_chunk(mixStart-waitStart,mixEnd-mixStart);
''')
    return once(text, '''    state->metrics.submitted=submitted;state->metrics.completed=completed;
    enhance::audio_diagnostics::publish(state->metrics);
    return true;
''', '''    state->metrics.submitted=submitted;state->metrics.completed=completed;
    enhance::audio_diagnostics::publish(state->metrics);
    enhance::audio_diagnostics::record_pump(svcGetSystemTick()-pumpStart);
    return true;
''')


GUARD = '''struct Guard {RecursiveLock* l;explicit Guard(RecursiveLock* lock):l(lock){
 __sync_fetch_and_add(&sm64ds_sound_game_waiting,1);RecursiveLock_Lock(l);__sync_fetch_and_sub(&sm64ds_sound_game_waiting,1);}
'''


def patch_native(text):
    """2026-11-09: time the game thread's waits for the mixer lock.

    A free lock is taken at once without the announcement (the mixer only
    reads it before its next chunk); a contended wait on the game thread is
    timed into audio_diagnostics (test reports: juego_espera_ms)."""
    text = once(text, GUARD, '''struct Guard {RecursiveLock* l;explicit Guard(RecursiveLock* lock):l(lock){
 if(!RecursiveLock_TryLock(l))return;
 const bool game=!sm64ds_on_present_worker();const u64 start=game?svcGetSystemTick():0;
 __sync_fetch_and_add(&sm64ds_sound_game_waiting,1);RecursiveLock_Lock(l);__sync_fetch_and_sub(&sm64ds_sound_game_waiting,1);
 if(game)enhance::audio_diagnostics::record_game_wait(svcGetSystemTick()-start);}
''')
    return '#include "../platform/3ds/audio_diagnostics.hpp"\n' + deferral_patch(text)


DEFERRAL = '''// BUILD134: a command batch that finds the mixer rendering is kept (game
// thread only) and submitted after this update's image went to the renderer,
// still on the game thread and before the next update. The BUILD 133 console
// tour measured these mid-update waits at up to 76 ms (the mixer descheduled
// under its lock by the core-1 quota); each one delays the image. The game's
// protocol is asynchronous already (it polls completedBatches, as with the
// DS ARM7). Anything that needs the queue in order drains it first.
// sdmc:/3ds/sm64ds/audio-espera.txt restores the blocking submit.
namespace {
unsigned int deferredHead=0;
bool deferral(){static const bool on=[](){struct stat st;return stat("sdmc:/3ds/sm64ds/audio-espera.txt",&st)!=0;}();return on;}
void drain_deferred(bool late){
 if(!deferredHead)return;
 Guard guard(&backend->lock,late);const auto head=deferredHead;deferredHead=0;
 if(!backend->queue.submit(head))fatal(backend->queue.error());
}
}
'''

DEFERRAL_SUBMIT = ''' if(payload&&game&&deferral()){
  if(RecursiveLock_TryLock(&backend->lock)){
   deferredHead=payload;enhance::audio_diagnostics::timing.deferred_batches.fetch_add(1,std::memory_order_relaxed);return 0;
  }
  const bool ok=backend->queue.submit(payload);RecursiveLock_Unlock(&backend->lock);
  if(!ok)fatal(backend->queue.error());
  return 0;
 }
'''

def deferral_patch(text):
    """BUILD134: keep a command batch that finds the mixer rendering.

    The BUILD 133 console tour measured the game thread waiting on the mixer
    lock 1.75 ms per update (single waits up to 76 ms). The batch is kept on
    the game thread and submitted after the image went to the renderer;
    sdmc:/3ds/sm64ds/audio-espera.txt restores the blocking submit."""
    text = once(text, '#include <3ds.h>\n', '#include <3ds.h>\n#include <sys/stat.h>\n')
    text = once(text, 'explicit Guard(RecursiveLock* lock):l(lock){\n if(!RecursiveLock_TryLock(l))return;',
        'explicit Guard(RecursiveLock* lock,bool late=false):l(lock){\n if(!RecursiveLock_TryLock(l))return;')
    text = once(text, ' if(game)enhance::audio_diagnostics::record_game_wait(svcGetSystemTick()-start);}\n',
        ''' if(game){if(late)enhance::audio_diagnostics::record_late_wait(svcGetSystemTick()-start);
  else enhance::audio_diagnostics::record_game_wait(svcGetSystemTick()-start);}}
''')
    text = once(text, 'extern "C" int IPCSend(', DEFERRAL + 'extern "C" int IPCSend(')
    text = once(text, ' if(!backend)fatal("audio service not initialized");\n',
        ''' if(!backend)fatal("audio service not initialized");
 const bool game=!sm64ds_on_present_worker();
 if(game)drain_deferred(false); // keeps batch order and one prepared set per batch
''')
    text = once(text, ' if(payload)backend->queue.prepare(payload);\n',
        ' if(payload)backend->queue.prepare(payload);\n' + DEFERRAL_SUBMIT)
    text = once(text, '''bool sm64ds_native_sound_register(Sm64dsSoundBuffer resource){
 if(!backend)return false;Guard guard(&backend->lock);return backend->service.registerResource(resource);
}''', '''// Game thread, after the presentation of each display event.
void sm64ds_native_sound_flush_deferred(){if(backend)drain_deferred(true);}
bool sm64ds_native_sound_register(Sm64dsSoundBuffer resource){
 if(!backend)return false;if(!sm64ds_on_present_worker())drain_deferred(false);
 Guard guard(&backend->lock);return backend->service.registerResource(resource);
}''')
    text = once(text, ' if(backend){Guard guard(&backend->lock);backend->service.unregisterResource(resource);}\n',
        ''' if(backend){if(!sm64ds_on_present_worker())drain_deferred(false);
  Guard guard(&backend->lock);backend->service.unregisterResource(resource);}
''')
    text = once(text, ''' // announces itself and waits at most one 128-frame render chunk.
 if(RecursiveLock_TryLock(&backend->lock))return true;
''', ''' // announces itself and waits at most one 64-frame render chunk.
 // BUILD134: the worker's game-heap frees do not need the mixer lock; return
 // them even when busy (their queue is bounded and the caller may now retry
 // on later frames instead of blocking).
 if(RecursiveLock_TryLock(&backend->lock)){hal_flush_deferred_frees();return true;}
''')
    return once(text, 'void sm64ds_native_sound_shutdown(){\n', 'void sm64ds_native_sound_shutdown(){\n deferredHead=0;\n')


def patch_native_header(text):
    return once(text, 'bool sm64ds_native_sound_try_maintenance(bool& done);\n',
        '''bool sm64ds_native_sound_try_maintenance(bool& done);
// BUILD134: submit a command batch deferred while the mixer was rendering.
void sm64ds_native_sound_flush_deferred();
''')


def output_address_patch(text):
    """BUILD115: log where the NDSP output lives (state block, PCM buffers).

    A hardware crash (BUILD114, Old 3DS, castle pause) was a core-1 data
    abort in the mixer's memcpy into these PCM buffers at 0x141c9e00: a valid
    old-mapping linear address, unmapped because the process was aborting
    (libctru frees the linear heap while other threads still run). The exit
    path now stops this thread first; the addresses let a hardware log tell
    a stale buffer from a corrupted pointer."""
    return once(text, '''    if(!state->samples){std::snprintf(failure,sizeof(failure),"sound linear allocation failed");close();return false;}''',
        '''    if(!state->samples){std::snprintf(failure,sizeof(failure),"sound linear allocation failed");close();return false;}
    std::printf("Native sound output: state=%p samples=%p bytes=%u\\n",static_cast<void*>(state),static_cast<void*>(state->samples),unsigned(Count*Frames*2*sizeof(s16)));''')


def patch_output_header(text):
    # BUILD137: four 512-sample buffers (62.5 ms) instead of three (47 ms).
    return once(text, 'static constexpr unsigned Frames=512, Count=3;',
        '''// BUILD137: four 512-sample buffers (62.5 ms) instead of three (47 ms):
    // the Old 3DS core-1 mixer outlasted the shorter queue (see player_present).
    static constexpr unsigned Frames=512, Count=4;''')


def prepare(output):
    output = Path(output).resolve()
    if output.is_relative_to((ROOT / 'third_party').resolve()):
        raise ValueError('Pinned vendor tree is immutable')
    path = output / 'port/audio/sound_output_3ds.cpp'
    if not path.resolve().is_relative_to(output):
        raise ValueError('Generated audio source must not escape output')
    text = patch_output(path.read_text())  # Check all anchors before any write.
    native = output / 'port/audio/sound_native.cpp'
    if not native.resolve().is_relative_to(output):
        raise ValueError('Generated audio source must not escape output')
    native_text = patch_native(native.read_text())
    header = output / 'port/audio/sound_native.h'
    if not header.resolve().is_relative_to(output):
        raise ValueError('Generated audio source must not escape output')
    header_text = patch_native_header(header.read_text())
    output_header = output / 'port/audio/sound_output_3ds.h'
    if not output_header.resolve().is_relative_to(output):
        raise ValueError('Generated audio source must not escape output')
    output_header_text = patch_output_header(output_header.read_text())
    base = output / 'port/platform/3ds'
    headers = ['audio_gain.hpp', 'audio_diagnostics.hpp']
    for name in headers:
        if not (base / name).resolve().is_relative_to(output):
            raise ValueError('Generated audio header must not escape output')
    base.mkdir(parents=True, exist_ok=True)
    for name in headers:
        shutil.copyfile(ROOT / 'source/integrated_native/options' / name, base / name)
    path.write_text(text)
    native.write_text(native_text)
    header.write_text(header_text)
    output_header.write_text(output_header_text)
    return ['port/audio/sound_output_3ds.cpp', 'port/audio/sound_output_3ds.h', 'port/audio/sound_native.cpp', 'port/audio/sound_native.h',
            *['port/platform/3ds/' + name for name in headers]]
