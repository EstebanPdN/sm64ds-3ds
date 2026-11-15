"""Patch only the generated sseqplayer segment mixer: ARMv6 inner loops.

SoundPool::GenerateSamples (PLAYER_AUDIO_SEGMENT_VOICES=1, the shipped path)
spends ~85% of its ARM11 instructions in the per-voice sample loop (Unicorn
profile of the devkitARM build on SM64DS sequences, 2026-11-09). The loop
moves to source/integrated_native/options/audio_mixer_kernels.hpp, whose
ARMv6 version (SMLAWB/SSAT/PKHBT) is bit-identical to the original; also only
the samples of a segment are cleared instead of six 128-sample arrays.

Call prepare(output) after prepare_integrated_audio (it also adds the kernel
choice to the NDSP output). It accepts only the pinned Player.cpp; the
pinned vendor is never changed.
tools/test_integrated_mixer_armv6.py runs both builds in Unicorn.
"""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
HEADER = 'audio_mixer_kernels.hpp'

BLOCK_ARRAYS = '''  int32_t left[audio_blocks::MaxBlock]{},right[audio_blocks::MaxBlock]{};
  int32_t directLeft[2][audio_blocks::MaxBlock]{},directRight[2][audio_blocks::MaxBlock]{};
  // Sound-timer ticks of each output sample (the same accumulator as raw PCM
  // and captures, committed after the block).
  unsigned cycles[audio_blocks::MaxBlock];
  std::uint64_t blockRemainder=rawClockRemainder;
  for(unsigned n=0;n<span;++n){blockRemainder+=sampleClock.numerator;cycles[n]=sampleClock.reduce(blockRemainder);}
'''
BLOCK_ARRAYS_NEW = '''  // Only this segment's samples are cleared (was 3 KiB per call); the
  // ch1/ch3 direct outputs only when an output selector reads them.
  sm64ds_mixer::Pair acc[audio_blocks::MaxBlock];std::memset(acc,0,span*sizeof acc[0]);
  const bool directRead=leftOutput||rightOutput;
  int32_t directLeft[2][audio_blocks::MaxBlock],directRight[2][audio_blocks::MaxBlock];
  if(directRead)for(unsigned i=0;i<2;++i){std::memset(directLeft[i],0,span*4);std::memset(directRight[i],0,span*4);}
  // Sound-timer ticks of each output sample (the same accumulator as raw PCM
  // and captures, committed after the block); uniform at the DS-native rate.
  unsigned cycles[audio_blocks::MaxBlock];bool uniform=true;
  std::uint64_t blockRemainder=rawClockRemainder;
  if(sampleClock.powerOfTwo&&!(sampleClock.numerator&(sampleClock.denominator-1))&&blockRemainder<sampleClock.denominator){
   // DS-native rate (1024/2): reduce() yields numerator>>shift every sample
   // and leaves the remainder unchanged.
   const unsigned step=unsigned(sampleClock.numerator>>sampleClock.shift);
   for(unsigned n=0;n<span;++n)cycles[n]=step;
  }else
  for(unsigned n=0;n<span;++n){blockRemainder+=sampleClock.numerator;cycles[n]=sampleClock.reduce(blockRemainder);uniform=uniform&&cycles[n]==cycles[0];}
'''
VOICE_LOOP = '''   const uint32_t reload=c.reg.timer,total=c.reg.totalLength,loop=c.reg.loopStart;
   const bool repeat=c.reg.repeatMode==1;
   uint32_t timer=c.reg.timerCounter;int32_t pos=c.reg.samplePosition;bool ended=false;
   for(unsigned n=0;n<span;++n) {
    timer+=cycles[n];
    while(timer>=0x10000){
     timer=timer-0x10000+reload;
     if(++pos>=int32_t(total)){if(repeat)pos=int32_t(loop);else{ended=true;break;}}
    }
    if(ended)break;
    AUDIO_BLOCK_COUNT(++audio_blocks::counters.samples);
    if(!audible)continue;
    // Same as DsPan: (sample<<shift)*volume*(128-p or p) >> 10.
    const int32_t sample=pos<0?0:data[pos];
    const int32_t l=int32_t((int64_t(sample)*gl)>>10),r=int32_t((int64_t(sample)*gr)>>10);
    if(id==1||id==3){directLeft[id/2][n]=l;directRight[id/2][n]=r;}
    if(!excluded){left[n]+=l;right[n]+=r;}
   }
   c.reg.timerCounter=timer;c.reg.samplePosition=pos;
   if(ended)c.Kill();
'''
VOICE_LOOP_NEW = '''   // Same as DsPan: (sample<<shift)*volume*(128-p or p) >> 10 (audio_mixer_kernels.hpp).
   sm64ds_mixer::Voice voice{data,c.reg.timer,c.reg.totalLength,c.reg.loopStart,c.reg.repeatMode==1,gl,gr,
                             c.reg.timerCounter,c.reg.samplePosition,false};
   const bool direct=(id==1||id==3)&&directRead;
   const unsigned played=sm64ds_mixer::mix_voice(voice,cycles,uniform,span,audible,excluded?nullptr:acc,
                                                 direct?directLeft[id/2]:nullptr,direct?directRight[id/2]:nullptr);
   (void)played;AUDIO_BLOCK_COUNT(audio_blocks::counters.samples+=played);
   c.reg.timerCounter=voice.timer;c.reg.samplePosition=voice.pos;
   if(voice.ended)c.Kill();
'''
OUTPUT_LOOP = '''  for(unsigned n=0;n<span;++n) {
   int32_t l=left[n],r=right[n];
'''
OUTPUT_LOOP_NEW = '''#ifndef SM64DS_AUDIO_TRACE
  // No live channel, capture unit or direct output: clamp and pack only.
  if(!liveCount&&!captures[0].enabled&&!captures[1].enabled&&!directRead){
   sm64ds_mixer::store_block(acc,span,&buf[offset]);offset+=4*span;
  }else
#endif
  for(unsigned n=0;n<span;++n) {
   int32_t l=acc[n].l,r=acc[n].r;
'''
OUTPUT_CLAMPS = '''   const int32_t cl=DsCapture(l),cr=DsCapture(r);
   SM64DS_TRACE_MIX(cl,cr);
   captures[0].run(tick,cl);captures[1].run(tick,cr);
   if(leftOutput)l=((leftOutput&1)?directLeft[0][n]:0)+((leftOutput&2)?directLeft[1][n]:0);
   if(rightOutput)r=((rightOutput&1)?directRight[0][n]:0)+((rightOutput&2)?directRight[1][n]:0);
   const int16_t out[2]={int16_t(DsSpeaker(l)),int16_t(DsSpeaker(r))};
'''
OUTPUT_CLAMPS_NEW = '''   const int32_t cl=sm64ds_mixer::capture(l),cr=sm64ds_mixer::capture(r);
   SM64DS_TRACE_MIX(cl,cr);
   captures[0].run(tick,cl);captures[1].run(tick,cr);
   if(leftOutput)l=((leftOutput&1)?directLeft[0][n]:0)+((leftOutput&2)?directLeft[1][n]:0);
   if(rightOutput)r=((rightOutput&1)?directRight[0][n]:0)+((rightOutput&2)?directRight[1][n]:0);
   const int16_t out[2]={int16_t(sm64ds_mixer::capture(l)),int16_t(sm64ds_mixer::capture(r))};
'''
INCLUDE = '#include "../../audio/sample_clock.h"\n'


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Mixer integration anchor changed: ' + old[:90])
    return text.replace(old, new, 1)


def patch_player(text):
    text = once(text, INCLUDE, INCLUDE + '#include "../../platform/3ds/' + HEADER + '"\n')
    text = once(text, BLOCK_ARRAYS, BLOCK_ARRAYS_NEW)
    text = once(text, VOICE_LOOP, VOICE_LOOP_NEW)
    text = once(text, OUTPUT_LOOP, OUTPUT_LOOP_NEW)
    return once(text, OUTPUT_CLAMPS, OUTPUT_CLAMPS_NEW)


SELECT = '''    service=&commands;
'''
SELECT_NEW = '''    service=&commands;
    // Mixer kernel (prepare_integrated_mixer.py): ARMv6 unless
    // sdmc:/3ds/sm64ds/mezclador-portable.txt exists, for console A/B timing
    // (both kernels produce the same PCM, so this never changes the sound).
    {struct stat st;const char* env=std::getenv("SM64DS_MIXER_PORTABLE");
     sm64ds_mixer::set_portable((env&&*env=='1')||stat("sdmc:/3ds/sm64ds/mezclador-portable.txt",&st)==0);
     std::printf("Native sound mixer: kernel=%s\\n",sm64ds_mixer::kernel_name());}
'''


def patch_output(text):
    text = once(text, SELECT, SELECT_NEW)
    return '#include "../platform/3ds/' + HEADER + '"\n#include <sys/stat.h>\n' + text


def prepare(output):
    output = Path(output).resolve()
    if output.is_relative_to((ROOT / 'third_party').resolve()):
        raise ValueError('Pinned vendor tree is immutable')
    path = output / 'port/third_party/sseqplayer/Player.cpp'
    sound = output / 'port/audio/sound_output_3ds.cpp'
    header = output / 'port/platform/3ds' / HEADER
    for target in (path, sound, header):
        if not target.resolve().is_relative_to(output):
            raise ValueError('Generated mixer source must not escape output')
    text = patch_player(path.read_text())  # Check all anchors before any write.
    sound_text = patch_output(sound.read_text())
    header.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'source/integrated_native/options' / HEADER, header)
    path.write_text(text)
    sound.write_text(sound_text)
    return ['port/third_party/sseqplayer/Player.cpp', 'port/audio/sound_output_3ds.cpp', 'port/platform/3ds/' + HEADER]
