#!/usr/bin/env python3
"""Bit-exact gate for the ARMv6 sseqplayer mixer (prepare_integrated_mixer.py).

Builds three bare-metal ARM11 programs with devkitARM and the game's flags:
  base  - the pinned Player.cpp (third_party/sm64ds-3ds/port, never changed);
  armv6 - the generated patch with SMLAWB/SSAT/PKHBT loops;
  port  - the same patch compiled with -DSM64DS_MIXER_PORTABLE.
Runs them in Unicorn (UC_CPU_ARM_11MPCORE, VFP enabled through CPACR/FPEXC)
and requires identical PCM and identical player/pool state after every call:
  1. the whole mixer on the game's own sequences (sound_data.sdat), with
     random and extreme perturbations (volume, pan, timer reload/counter,
     start delay, loop, end of one-shots, mutes, excluded channels, ch1/ch3
     direct outputs, surround, raw PCM channels, capture units), several
     sample rates and interpolation modes, odd chunk sizes and unaligned
     output offsets;
  2. the voice kernel itself (portable vs ARMv6 in one binary) on random,
     extreme and unaligned inputs;
  3. the clamp/pack store on extreme sums and unaligned destinations.
Reports executed ARM instructions (block hook) for the base and ARMv6 mixers.

Needs devkitARM ($DEVKITARM or --devkitarm), unicorn and pyelftools
(build/python has both). Without --sdat only steps 2-3 run.
"""
import argparse, os, random, shutil, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_integrated_mixer import patch_player, HEADER

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--output', type=Path, default=ROOT / 'build/mixer-armv6-test')
ap.add_argument('--devkitarm', type=Path, default=os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM'))
ap.add_argument('--sdat', type=Path, help='sound_data.sdat extracted from the user ROM')
ap.add_argument('--seed', type=int, default=1)
ap.add_argument('--kernel-cases', type=int, default=4000)
ap.add_argument('--seconds', type=float, default=1.0, help='audio per sequence for the timing pass')
ap.add_argument('--quick', action='store_true')
a = ap.parse_args()
try:
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK
    from unicorn.arm_const import (UC_CPU_ARM_11MPCORE, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_SP,
                                   UC_ARM_REG_LR, UC_ARM_REG_R0)
except ImportError as error:
    sys.exit('needs unicorn and pyelftools (build/python/bin/python): %s' % error)
gcc, gxx = a.devkitarm / 'bin/arm-none-eabi-gcc', a.devkitarm / 'bin/arm-none-eabi-g++'
if not gxx.exists():
    sys.exit('needs devkitARM: pass --devkitarm or set DEVKITARM')

HARNESS = r'''
#include "../third_party/sseqplayer/SDAT.h"
#include "../third_party/sseqplayer/Player.h"
#include <cstring>
#include <cstdlib>
#include <vector>
#ifdef HAS_KERNELS
#include "../platform/3ds/audio_mixer_kernels.hpp"
#endif
extern "C" char** environ;
static char segment_env[]="PLAYER_AUDIO_SEGMENT_VOICES=1";
static char* envp[]={segment_env,nullptr};
extern "C" {
uint32_t hArgs[16];
uint32_t hSdat,hSdatSize,hSnapLen;
uint8_t hOut[4*4096+64] __attribute__((aligned(32)));
uint8_t hSnap[32768] __attribute__((aligned(32)));
uint8_t hKernel[32768] __attribute__((aligned(32)));
void __libc_init_array(void);
extern unsigned sm64ds_unicorn_null_sources;
}
static std::vector<uint8_t>* bytes;static SDAT* archive;static Player* player;static std::vector<uint8_t>* pcm;
alignas(4) static int16_t rawSource[2][96];
alignas(4) static uint8_t rings[2][256];
static uint32_t rng;
static uint32_t next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint8_t* snap;
template<class T> static void put(const T& v){std::memcpy(snap,&v,sizeof v);snap+=sizeof v;}
static void snapshot(){
 snap=hSnap;SoundPool& pool=player->pool;
 put(pool.rawClockRemainder);put(pool.secondsIntoPlayback);put(pool.secondsUntilNextClock);
 put(pool.leftOutput);put(pool.rightOutput);put(pool.excludeChannel1);put(pool.excludeChannel3);put(pool.surroundAmount);
 put(player->localTickCount);put(player->tempoCount);put(player->nTracks);
 for(auto v:player->localVariables)put(v);
 for(auto v:pool.globalVariables)put(v);
 for(const auto& c:pool.channels){
  put(c.state);put(c.trackId);put(c.prio);put(c.ampl);put(c.noteLength);put(uint32_t(c.flags.to_ulong()));
  put(c.reg.samplePosition);put(c.reg.timerCounter);put(c.reg.timer);put(c.reg.loopStart);put(c.reg.repeatMode);
  put(c.reg.volumeMul);put(c.reg.volumeDiv);put(c.reg.panning);put(c.reg.format);
  put(c.reg.psgX);put(c.reg.psgLastCount);put(c.reg.psgLast);put(c.reg.length);put(c.reg.totalLength);
  put(c.sampleHistoryPtr);for(auto v:c.sampleHistory)put(v);
 }
 for(const auto& raw:pool.rawPcm){
  put(raw.enabled);put(raw.keyOn);put(raw.position);put(raw.sample);put(raw.read);put(raw.write);
  put(raw.available);put(raw.sourceOffset);put(raw.elapsed);for(auto v:raw.fifo)put(v);
 }
 for(const auto& cap:pool.captures){
  put(cap.enabled);put(cap.period);put(cap.elapsed);put(cap.position);put(cap.offset);
  put(cap.read);put(cap.write);put(cap.available);for(auto v:cap.fifo)put(v);
 }
 std::memcpy(snap,rings,sizeof rings);snap+=sizeof rings;
 put(sm64ds_unicorn_null_sources);
 hSnapLen=uint32_t(snap-hSnap);
}
static uint32_t pick(const uint32_t* v,unsigned n){return v[next()%n];}
static void perturb(uint32_t seed,uint32_t level){
 rng=seed|1;SoundPool& pool=player->pool;
 for(auto& c:pool.channels){
  if(next()%4)continue;
  if(next()%2)c.reg.volumeMul=uint8_t(next()%3?next()%128:127);
  if(next()%2)c.reg.volumeDiv=uint8_t(next()%4);
  if(next()%2)c.reg.panning=uint8_t(next()%5?next()%128:(next()%2?0:127));
  if(next()%3==0){static const uint32_t reloads[]={0xFE00,0xFF00,0xFC00,0xF000,0x8000,0xFFF0,0x0000,0xFE01,0xFDFF};
   c.reg.timer=uint16_t(level&&next()%8==0?0xFFFF:next()%2?pick(reloads,9):next()&0xFFFF);}
  if(next()%3==0)c.reg.timerCounter=next()%2?next()%0x10000:0xFFFF-next()%600;
  const uint32_t total=c.reg.totalLength;
  if(total&&c.reg.source&&c.reg.totalLength<=c.reg.source->data.size()){
   if(next()%3==0){const int32_t positions[]={-3,-2,-1,0,int32_t(total)-1,int32_t(total)-2,int32_t(total)-40,int32_t(next()%total)};
    c.reg.samplePosition=positions[next()%8];if(c.reg.samplePosition<-3)c.reg.samplePosition=0;}
   if(next()%3==0)c.reg.loopStart=next()%3?next()%total:(next()%2?0:total-1);
   if(next()%4==0)c.reg.repeatMode=uint8_t(next()%3);
  }
 }
 if(next()%3==0)player->mutes=std::bitset<16>(next()%2?0:next()&next()&0xFFFF);
 if(next()%4==0){pool.excludeChannel1=next()%2;pool.excludeChannel3=next()%2;}
 if(next()%4==0){pool.leftOutput=next()%2?0:next()%4;pool.rightOutput=next()%2?0:next()%4;}
 if(next()%4==0)pool.surroundAmount=next()%2?0:next()%0x8000;
 if(level>1&&next()%3==0){
  for(unsigned i=0;i<2;++i){
   if(next()%2)continue;
   auto& raw=pool.rawPcm[1+2*i];raw={};
   for(auto& s:rawSource[i])s=int16_t(next());
   raw.source=reinterpret_cast<const uint8_t*>(rawSource[i]);raw.format=next()%2;raw.repeat=1+next()%2;
   raw.lengthBytes=sizeof rawSource[i]-16;raw.loopBytes=16;raw.period=next()%2?512:300+next()%900;
   raw.volume=next()%128;raw.divider=next()%4;raw.pan=next()%128;raw.start();
   auto& cap=pool.captures[i];cap={};cap.destination=rings[i];cap.bytes=sizeof rings[i];cap.period=next()%2?512:200+next()%1200;
   cap.pcm8=next()%2;cap.oneShot=next()%4==0;cap.start();
  }
 }
}
extern "C" int h_call(void){
 switch(hArgs[0]){
 case 99: environ=envp; __libc_init_array(); return 0;
 case 0: {
  const uint8_t* sdat=reinterpret_cast<const uint8_t*>(hSdat);
  if(!bytes)bytes=new std::vector<uint8_t>(sdat,sdat+hSdatSize);
  PseudoFile file;file.data=bytes;
  delete player;player=nullptr;delete archive;
  std::memset(rings,0,sizeof rings);
  archive=new SDAT(file,hArgs[1]);
  player=new Player();player->sampleRate=hArgs[2];player->interpolation=Interpolation(hArgs[3]);
  if(!player->Setup(archive->sseq.get()))return 4;
  if(!pcm)pcm=new std::vector<uint8_t>(4*4096+64);
  return 0;}
 case 1: {
  const unsigned n=hArgs[1],offset=hArgs[2];
  player->GenerateSamples(*pcm,offset,n);
  std::memcpy(hOut,pcm->data()+offset,n*4);
  return 0;}
 case 2: perturb(hArgs[1],hArgs[2]);return 0;
 case 3: snapshot();return int(hSnapLen);
#ifdef HAS_KERNELS
 case 10: case 11: {
  // Kernel block: header words, cycles[128], acc[128], dl[128], dr[128], data[4104].
  uint32_t* h=reinterpret_cast<uint32_t*>(hKernel);
  static sm64ds_mixer::Pair acc[128];static int32_t dl[128],dr[128];static unsigned cycles[128];
  std::memcpy(cycles,h+16,sizeof cycles);std::memcpy(acc,h+16+128,sizeof acc);
  std::memcpy(dl,h+16+384,sizeof dl);std::memcpy(dr,h+16+512,sizeof dr);
  const int16_t* data=reinterpret_cast<const int16_t*>(h+16+640)+h[5];
  sm64ds_mixer::Voice v{data,h[7],h[8],h[9],h[10]!=0,int32_t(h[11]),int32_t(h[12]),h[13],int32_t(h[6]),false};
  sm64ds_mixer::set_portable(hArgs[0]==10);
  const unsigned ret=sm64ds_mixer::mix_voice(v,cycles,h[1]!=0,h[0],h[2]!=0,h[3]?acc:nullptr,h[4]?dl:nullptr,h[4]?dr:nullptr);
  uint32_t* o=reinterpret_cast<uint32_t*>(hSnap);
  o[0]=ret;o[1]=v.timer;o[2]=uint32_t(v.pos);o[3]=v.ended;
  std::memcpy(o+4,acc,sizeof acc);std::memcpy(o+4+256,dl,sizeof dl);std::memcpy(o+4+384,dr,sizeof dr);
  return 0;}
 case 12: case 13: {
  uint32_t* h=reinterpret_cast<uint32_t*>(hKernel);
  static sm64ds_mixer::Pair acc[128];std::memcpy(acc,h+16,sizeof acc);
  sm64ds_mixer::set_portable(hArgs[0]==12);
  std::memset(hSnap,0xA5,4*128+8);
  sm64ds_mixer::store_block(acc,h[0],hSnap+h[1]);
  int32_t* c=reinterpret_cast<int32_t*>(hSnap+1024);
  for(unsigned i=0;i<256;++i)c[i]=sm64ds_mixer::capture(int32_t(h[16+i]));
  return 0;}
#endif
 }
 return -1;
}
'''
STUBS = r'''
#include <sys/stat.h>
#include <stddef.h>
extern char __heap_start[], __heap_end[];
static char* brk_ptr;
void* _sbrk_r(void* r,ptrdiff_t n){(void)r;if(!brk_ptr)brk_ptr=__heap_start;char* p=brk_ptr;if(brk_ptr+n>__heap_end)return (void*)-1;brk_ptr+=n;return p;}
void* _sbrk(ptrdiff_t n){return _sbrk_r(0,n);}
int _write(int f,const void* b,size_t n){(void)f;(void)b;return (int)n;}
int _read(int f,void* b,size_t n){(void)f;(void)b;(void)n;return 0;}
int _close(int f){(void)f;return -1;}
int _fstat(int f,struct stat* s){(void)f;s->st_mode=S_IFCHR;return 0;}
int _isatty(int f){(void)f;return 1;}
int _lseek(int f,int o,int w){(void)f;(void)o;(void)w;return 0;}
void _exit(int c){(void)c;for(;;)__asm__ volatile("bkpt #0");}
int _kill(int p,int s){(void)p;(void)s;return -1;}
int _getpid(void){return 1;}
static unsigned tls_block[64];
void* __aeabi_read_tp(void){return tls_block;}
void _init(void){}
void _fini(void){}
unsigned sm64ds_unicorn_null_sources;
void sm64ds_sseq_null_source_seen(void){++sm64ds_unicorn_null_sources;}
'''
LINK = '''ENTRY(h_call)
SECTIONS {
  . = 0x00100000;
  .text : { *(.text .text.*) }
  .init : { KEEP(*(.init)) }
  .fini : { KEEP(*(.fini)) }
  .rodata : { *(.rodata .rodata.*) }
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) }
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; }
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array)) PROVIDE_HIDDEN(__preinit_array_end = .); }
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) PROVIDE_HIDDEN(__init_array_end = .); }
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(.fini_array)) PROVIDE_HIDDEN(__fini_array_end = .); }
  . = ALIGN(4096);
  .data : { *(.data .data.*) }
  .tdata : { *(.tdata .tdata.*) }
  .tbss : { *(.tbss .tbss.*) }
  .bss : { *(.bss .bss.* COMMON) }
  . = ALIGN(4096);
  __heap_start = .;
  __heap_end = 0x04000000;
}
'''
# The game's flags for the sseq library (build3ds CMake: sm64ds_sseq target).
# -fno-threadsafe-statics: no gthreads in this bare-metal runtime (the
# mixer's static initialisers run once either way).
FLAGS = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft', '-mword-relocations', '-ffunction-sections',
         '-D__3DS__', '-g', '-O2', '-DNDEBUG', '-fsigned-char', '-std=gnu++17', '-fno-threadsafe-statics',
         '-DChannel=Sm64dsNitroChannel', '-DPlayer=Sm64dsNitroPlayer', '-DTrack=Sm64dsNitroTrack']

out = a.output.resolve()
shutil.rmtree(out, ignore_errors=True)
pinned = ROOT / 'third_party/sm64ds-3ds/port'


def tree(name, patched):
    port = out / name / 'port'
    shutil.copytree(pinned / 'third_party/sseqplayer', port / 'third_party/sseqplayer')
    (port / 'audio').mkdir(parents=True)
    for header in (pinned / 'audio').glob('*.h'):
        shutil.copy2(header, port / 'audio' / header.name)
    if patched:
        player = port / 'third_party/sseqplayer/Player.cpp'
        player.write_text(patch_player(player.read_text()))
        (port / 'platform/3ds').mkdir(parents=True)
        shutil.copy2(ROOT / 'source/integrated_native/options' / HEADER, port / 'platform/3ds' / HEADER)
    (port / 'unicorn').mkdir()
    (port / 'unicorn/harness.cpp').write_text(HARNESS)
    (port / 'unicorn/stubs.c').write_text(STUBS)
    (port / 'unicorn/harness.ld').write_text(LINK)
    return port


def build(name, port, defines):
    objs = []
    for source in sorted((port / 'third_party/sseqplayer').glob('*.cpp')) + [port / 'unicorn/harness.cpp']:
        obj = port / 'unicorn' / (source.stem + '.o')
        r = subprocess.run([str(gxx), *FLAGS, *defines, '-w', '-c', str(source), '-o', str(obj)], capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stderr)
        objs.append(str(obj))
    subprocess.run([str(gcc), *FLAGS[:9], '-c', str(port / 'unicorn/stubs.c'), '-o', str(port / 'unicorn/stubs.o')], check=True)
    elf = out / (name + '.elf')
    subprocess.run([str(gxx), '-march=armv6k', '-mfloat-abi=hard', '-nostartfiles', '-T', str(port / 'unicorn/harness.ld'),
                    '-Wl,--gc-sections', '-o', str(elf), *objs, str(port / 'unicorn/stubs.o'),
                    '-Wl,--start-group', '-lstdc++', '-lc', '-lgcc', '-Wl,--end-group'], check=True)
    return elf


STACK, MAGIC, SDAT_BASE = 0x04000000, 0x04100000, 0x05000000


class Machine:
    def __init__(self, path, sdat):
        uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_11MPCORE)
        uc.mem_map(0x00100000, STACK - 0x00100000)
        uc.mem_map(STACK, 0x40000)
        uc.mem_map(MAGIC, 0x1000)
        self.sym = {}
        with open(path, 'rb') as f:
            elf = ELFFile(f)
            for seg in elf.iter_segments():
                if seg['p_type'] == 'PT_LOAD' and seg['p_filesz']:
                    uc.mem_write(seg['p_vaddr'], seg.data())
            for s in elf.get_section_by_name('.symtab').iter_symbols():
                self.sym[s.name] = s['st_value']
                assert not (s['st_info']['type'] == 'STT_FUNC' and s['st_value'] & 1), 'Thumb code in ' + str(path)
        if sdat:
            uc.mem_map(SDAT_BASE, (len(sdat) + 0xFFFF) & ~0xFFFF)
            uc.mem_write(SDAT_BASE, sdat)
            uc.mem_write(self.sym['hSdat'], struct.pack('<II', SDAT_BASE, len(sdat)))
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        self.uc, self.insns, self.hook = uc, 0, None
        assert self.call(99) == 0

    def counting(self, enabled):
        # Blocks translated without the hook never call it: flush them.
        if enabled and self.hook is None:
            self.hook = self.uc.hook_add(UC_HOOK_BLOCK, self._block)
            self.uc.ctl_flush_tb()
        elif not enabled and self.hook is not None:
            self.uc.hook_del(self.hook)
            self.hook = None
            self.uc.ctl_flush_tb()

    def _block(self, uc, address, size, user):
        self.insns += size // 4

    def call(self, op, *args):
        words = [op] + [x & 0xFFFFFFFF for x in args]
        self.uc.mem_write(self.sym['hArgs'], struct.pack('<%dI' % len(words), *words))
        self.uc.reg_write(UC_ARM_REG_SP, STACK + 0x40000 - 64)
        self.uc.reg_write(UC_ARM_REG_LR, MAGIC)
        self.uc.emu_start(self.sym['h_call'], MAGIC)
        r = self.uc.reg_read(UC_ARM_REG_R0)
        return r - (1 << 32) if r & 0x80000000 else r

    def read(self, name, size, offset=0):
        return bytes(self.uc.mem_read(self.sym[name] + offset, size))

    def write(self, name, data):
        self.uc.mem_write(self.sym[name], bytes(data))


failures = []


def check(label, values):
    first = values[0]
    for name, value in values[1:]:
        if value != first[1]:
            failures.append('%s: %s differs from %s' % (label, name, first[0]))
            return False
    return True


print('building base / armv6 / portable with devkitARM ...', flush=True)
base_port, cand_port = tree('base', False), tree('candidate', True)
elves = {'base': build('base', base_port, []),
         'armv6': build('armv6', cand_port, ['-DHAS_KERNELS']),
         'port': build('port', cand_port, ['-DHAS_KERNELS', '-DSM64DS_MIXER_PORTABLE'])}
rng = random.Random(a.seed)
sdat = a.sdat.read_bytes() if a.sdat else None
machines = {name: Machine(path, sdat) for name, path in elves.items()}
print('built', ', '.join('%s=%s' % (k, v.name) for k, v in elves.items()), flush=True)

if sdat:
    # 1a. Production timing: DS-native rate, 64-frame chunks, no perturbation.
    bgm = [58, 57, 59, 61, 62, 45, 66, 60, 44, 55]
    if a.quick:
        bgm = bgm[:3]
    print('\nwhole mixer, production calls (32728 Hz, 64-sample chunks):')
    totals = {'base': 0, 'armv6': 0}
    samples_total = 0
    for seq in bgm:
        counts = {}
        for name, m in machines.items():
            assert m.call(0, seq, 32728, 0) == 0
        # Half a second of the sequence start first (compared, not counted).
        for i in range(int(32728 * 0.5) // 64):
            check('seq %d warm-up %d pcm' % (seq, i), [(name, (m.call(1, 64, 0), m.read('hOut', 256))[1]) for name, m in machines.items()])
        chunks = int(32728 * a.seconds) // 64
        for name in ('base', 'armv6'):
            machines[name].counting(True)
        for i in range(chunks):
            outs = []
            for name, m in machines.items():
                before = m.insns
                m.call(1, 64, 0)
                counts[name] = counts.get(name, 0) + m.insns - before
                outs.append((name, m.read('hOut', 256)))
            if not check('seq %d chunk %d pcm' % (seq, i), outs):
                break
        snaps = [(name, m.read('hSnap', m.call(3))) for name, m in machines.items()]
        check('seq %d state' % seq, snaps)
        for name in ('base', 'armv6'):
            machines[name].counting(False)
            totals[name] += counts[name]
        samples_total += chunks * 64
        print('  seq %3d: base %4.0f  armv6 %4.0f instructions/sample  (%.2fx)' % (
            seq, counts['base'] / (chunks * 64), counts['armv6'] / (chunks * 64), counts['base'] / max(1, counts['armv6'])))
    print('  all     : base %4.0f  armv6 %4.0f instructions/sample  (%.2fx)' % (
        totals['base'] / samples_total, totals['armv6'] / samples_total, totals['base'] / max(1, totals['armv6'])))

    # 1b. Random and extreme state, rates, interpolation, chunks and offsets.
    print('\nwhole mixer, perturbed state:', flush=True)
    configs = [(32728, 0), (32768, 0), (48000, 0), (22050, 0), (32728, 1), (32728, 2), (32728, 4)]
    seqs = bgm + [0, 9, 20, 36, 51]
    calls = 0
    for config_index, (rate, interp) in enumerate(configs):
        for seq in (seqs if not a.quick else seqs[:2]):
            if failures:
                break
            for m in machines.values():
                assert m.call(0, seq, rate, interp) == 0
            for step in range(60 if not a.quick else 20):
                if step % 3 == 0:
                    seed, level = rng.getrandbits(32), rng.choice((0, 1, 2, 2))
                    for m in machines.values():
                        m.call(2, seed, level)
                n = rng.choice((1, 2, 7, 63, 64, 64, 64, 65, 127, 128, 129, 300, 512))
                offset = rng.choice((0, 0, 0, 4, 1, 2, 3, 8 * 1024 + 3))
                outs = []
                for name, m in machines.items():
                    m.call(1, n, offset)
                    outs.append((name, m.read('hOut', n * 4)))
                calls += 1
                if not check('rate %d interp %d seq %d step %d pcm' % (rate, interp, seq, step), outs):
                    break
                snaps = [(name, m.read('hSnap', m.call(3))) for name, m in machines.items()]
                if not check('rate %d interp %d seq %d step %d state' % (rate, interp, seq, step), snaps):
                    break
    print('  %d render calls x 3 builds compared (PCM and full state after each): %s' % (
        calls, 'identical' if not failures else 'DIFFERENT'))

# 2. Voice kernel: portable vs ARMv6 inside the candidate binary.
m = machines['armv6']
print('\nvoice kernel, %d random/extreme cases:' % a.kernel_cases, flush=True)
kernel_insns = {10: 0, 11: 0}
by_category = {}
m.counting(True)
for case in range(a.kernel_cases):
    span = rng.choice((1, 2, 3, 63, 64, 64, 64, 128, rng.randint(1, 128)))
    uniform = rng.random() < 0.7
    total = rng.choice((1, 2, 5, 64, 300, 4000, rng.randint(1, 4000)))
    data_offset = rng.choice((0, 0, 1, 2, 3))
    loop = rng.choice((0, total - 1, rng.randrange(total), (1 << 31) + 5 if rng.random() < 0.05 else 0))
    pos = rng.choice((-3, -2, -1, 0, total - 1, max(-3, total - 2), rng.randrange(total)))
    step = rng.choice((512, 512, 512, 1, 300, 1023, 4096, 65535))
    cycles = [step] * 128 if uniform else [rng.choice((511, 512, 513, 0, 1, 2048)) for _ in range(128)]
    reload = rng.choice((0xFE00, 0xFF00, 0xFE01, 0xFC00, 0x8000, 0, 0xFFF0, rng.getrandbits(16)))
    timer = rng.choice((0, 0xFFFF, rng.getrandbits(16)))
    volume = rng.choice((0, 1, 64, 126, 127, 128))
    gain = volume << rng.choice((0, 2, 3, 4))
    pan = rng.choice((0, 1, 64, 127, 128))
    gl, gr = gain * (128 - pan), gain * pan
    extreme = rng.random() < 0.3
    data = [rng.choice((-32768, 32767, -1, 0)) if extreme else rng.randint(-32768, 32767) for _ in range(4104)]
    acc = [rng.randint(-(1 << 27), 1 << 27) for _ in range(256)]
    dl = [rng.getrandbits(32) for _ in range(128)]
    dr = [rng.getrandbits(32) for _ in range(128)]
    mode = rng.randrange(4)
    header = [span, int(uniform), int(rng.random() < 0.9), mode & 1, mode >> 1, data_offset, pos & 0xFFFFFFFF, reload, total,
              loop & 0xFFFFFFFF, int(rng.random() < 0.7), gl, gr, timer, 0, 0]
    block = struct.pack('<16I', *header) + struct.pack('<128I', *cycles) + struct.pack('<256i', *acc) + \
        struct.pack('<128I', *dl) + struct.pack('<128I', *dr) + struct.pack('<4104h', *data)
    results = []
    audible = header[2]
    category = 'silent' if not audible or mode == 0 else 'direct' if mode >> 1 else 'mix' if uniform else 'mix-nonuniform'
    category += '-tiny' if total <= 5 else ''
    for op in (10, 11):
        m.write('hKernel', block)
        before = m.insns
        m.call(op)
        kernel_insns[op] += m.insns - before
        by_category.setdefault(category, {10: 0, 11: 0})[op] += m.insns - before
        results.append(('portable' if op == 10 else 'armv6', m.read('hSnap', 16 + 4 * 512)))
    if not check('kernel case %d %r' % (case, header), results):
        break
m.counting(False)
print('  results identical' if not any(f.startswith('kernel') for f in failures) else '  DIFFERENT')
print('  instructions: portable %d, armv6 %d (%.2fx)' % (kernel_insns[10], kernel_insns[11], kernel_insns[10] / max(1, kernel_insns[11])))
for category, c in sorted(by_category.items()):
    print('    %-20s portable %10d  armv6 %10d  (%.2fx)' % (category, c[10], c[11], c[10] / max(1, c[11])))

# 2b. Instructions per voice sample at the DS-native rate: marginal cost of
# 64 more samples (128 vs 64), so the harness copies cancel out. Audible
# mixed voice, long loop; reload 0xFE00 is one source step per output
# sample, 0xFF00 two, 0xFC00 one every two.
print('\nvoice kernel, marginal instructions per voice sample (DS-native rate):')
m.counting(True)
for reload in (0xFE00, 0xFF00, 0xFC00):
    per = {}
    for op in (10, 11):
        costs = []
        for span in (64, 128):
            header = [span, 1, 1, 1, 0, 0, 100, reload, 4000, 50, 1, 2048 * 64, 2048 * 64, 0x8000, 0, 0]
            block = struct.pack('<16I', *header) + struct.pack('<128I', *([512] * 128)) + bytes(4 * 512) + \
                struct.pack('<4104h', *[(i * 37) % 65536 - 32768 for i in range(4104)])
            m.write('hKernel', block)
            before = m.insns
            m.call(op)
            costs.append(m.insns - before)
        per[op] = (costs[1] - costs[0]) / 64
    print('  reload %#06x: portable %.1f, armv6 %.1f (%.2fx)' % (reload, per[10], per[11], per[10] / per[11]))
m.counting(False)

# 3. Clamp/pack store on extreme sums, unaligned destinations.
print('\nclamp/pack store:', flush=True)
specials = [-(1 << 31), (1 << 31) - 1, 0x7FFF << 8, (0x7FFF << 8) + 255, 0x8000 << 8, -0x8000 << 8, (-0x8000 << 8) - 1, -1, 0, 255, 256]
for case in range(600):
    span, dst = rng.randint(1, 128), rng.randrange(4)
    acc = [rng.choice(specials) if rng.random() < 0.3 else rng.randint(-(1 << 31), (1 << 31) - 1) for _ in range(256)]
    sums = [rng.choice(specials) if rng.random() < 0.3 else rng.randint(-(1 << 31), (1 << 31) - 1) for _ in range(256)]
    results = []
    for op in (12, 13):
        # Words h[16..] are the Pair accumulators for store_block, then the
        # sums for capture(): one call per input set.
        m.write('hKernel', struct.pack('<16I', span, dst, *([0] * 14)) + struct.pack('<256i', *acc))
        m.call(op)
        store = m.read('hSnap', 4 * 128 + 8)
        m.write('hKernel', struct.pack('<16I', span, dst, *([0] * 14)) + struct.pack('<256i', *sums))
        m.call(op)
        results.append(('portable' if op == 12 else 'armv6', store + m.read('hSnap', 1024, 1024)))
    if not check('store case %d' % case, results):
        break
print('  results identical' if not any(f.startswith('store') for f in failures) else '  DIFFERENT')

if failures:
    print('\nFAIL')
    for f in failures[:20]:
        print('  ' + f)
    sys.exit(1)
print('\nPASS: ARMv6 mixer is bit-identical to the pinned mixer')
