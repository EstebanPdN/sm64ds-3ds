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
    if text.startswith(LEGACY_INCLUDE):
        text = text[len(LEGACY_INCLUDE):]
        text = once(text, LEGACY_GAIN, '')
    text = once(text,
        'struct Sm64dsSoundOutputState {s16* samples;ndspWaveBuf buffers[3];bool owned[3];};',
        '''struct Sm64dsSoundOutputState {
    s16* samples=nullptr;ndspWaveBuf buffers[3]{};bool owned[3]{};
    enhance::AudioGainRamp gain;
    enhance::audio_diagnostics::Snapshot metrics;
};''')
    text = once(text,
        'state=static_cast<Sm64dsSoundOutputState*>(std::calloc(1,sizeof(Sm64dsSoundOutputState)));',
        '''state=static_cast<Sm64dsSoundOutputState*>(std::malloc(sizeof(Sm64dsSoundOutputState)));
    if(state)::new(state) Sm64dsSoundOutputState;''')
    text = once(text, '    service=&commands;', '''    service=&commands;
    state->gain.reset(enhance::live().audio_volume.load(std::memory_order_relaxed));
    enhance::audio_diagnostics::publish(state->metrics);''')
    text = once(text, '''    for(unsigned i=0;i<Count;++i){
        auto& buffer=state->buffers[i];
        if(state->owned[i]&&buffer.status==NDSP_WBUF_DONE){state->owned[i]=false;++completed;}
        if(state->owned[i])continue;''', '''    // Snapshot completion before refilling. A queue exhausted since its
    // previous submission is a deadline miss; initial prefill is excluded.
    unsigned previously_owned=0,completed_now=0;
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
    return true;''', '''        ndspChnWaveBufAdd(0,&buffer);state->owned[i]=true;++submitted;
    }
    state->metrics.submitted=submitted;state->metrics.completed=completed;
    enhance::audio_diagnostics::publish(state->metrics);
    return true;''')
    # Keep the pinned libc allocator domain. Global operator new in this
    # port can route into the original game heap; only construction changes.
    text = once(text, 'std::free(state);state=nullptr;',
                'state->~Sm64dsSoundOutputState();std::free(state);state=nullptr;')
    return (LEGACY_INCLUDE + '#include "../platform/3ds/audio_diagnostics.hpp"\n#include <new>\n' + text)


def prepare(output):
    output = Path(output).resolve()
    if output.is_relative_to((ROOT / 'third_party').resolve()):
        raise ValueError('Pinned vendor tree is immutable')
    path = output / 'port/audio/sound_output_3ds.cpp'
    if not path.resolve().is_relative_to(output):
        raise ValueError('Generated audio source must not escape output')
    text = patch_output(path.read_text())  # Check all anchors before any write.
    base = output / 'port/platform/3ds'
    headers = ['audio_gain.hpp', 'audio_diagnostics.hpp']
    for name in headers:
        if not (base / name).resolve().is_relative_to(output):
            raise ValueError('Generated audio header must not escape output')
    base.mkdir(parents=True, exist_ok=True)
    for name in headers:
        shutil.copyfile(ROOT / 'source/integrated_native/options' / name, base / name)
    path.write_text(text)
    return ['port/audio/sound_output_3ds.cpp',
            *['port/platform/3ds/' + name for name in headers]]
