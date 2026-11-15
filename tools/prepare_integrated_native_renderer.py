"""Stage the fresh native backend/hooks in a GENERATED game checkout only.

By default this installs build inputs and linker seams. --runtime additionally
installs the opt-in native frame/resource owner. The comparison route remains
the default. Native mode refuses failed/unmapped model occurrences and unknown
writers; it never falls through into the original graphics submitter.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ANCHOR='    # Only the game executable supplies the original SDK transfer entry point.\n'
MARKER='    # SM64DS fresh native model producer: explicit runtime installation required.\n'


def dialogue_ppu_patch(text):
    overlay=' return a==0x04001000u?native_lower::lower_dispcnt(value,native_lower_engine_b()):value;'
    plain='inline uint32_t rd32(uint32_t a) { return *static_cast<volatile uint32_t *>(host_ptr(a)); }'
    read32=overlay if overlay in text else plain
    replacement32=''' if(a==0x04000000u)return sm64ds::render::dialogue_window_view::dispcnt(value,
    sm64ds::render::dialogue_window_view::owned.load(std::memory_order_acquire));
 return a==0x04001000u?native_lower::lower_dispcnt(value,native_lower_engine_b()):value;''' if read32==overlay else '''inline uint32_t rd32(uint32_t a) {
    const auto value=*static_cast<volatile uint32_t *>(host_ptr(a));
    return a==0x04000000u?sm64ds::render::dialogue_window_view::dispcnt(value,
        sm64ds::render::dialogue_window_view::owned.load(std::memory_order_acquire)):value;
}'''
    shadow='    if((!g_window_cut_scope || !window_cut_enabled()) && !(eng==ENGINE_B && native_lower_engine_b()))return base;'
    plain_shadow='    if(!g_window_cut_scope || !window_cut_enabled())return base;'
    key=shadow if shadow in text else plain_shadow
    key_condition=key[len('    if('):-len(')return base;')]
    replacements={
        '#include "ntr/ppu.h"\n': '#include "ntr/ppu.h"\n#include "platform/3ds/native_renderer/dialogue_window_view.hpp"\n',
        'inline uint16_t rd16(uint32_t a) { return *static_cast<volatile uint16_t *>(host_ptr(a)); }': '''inline uint16_t rd16(uint32_t a) {
    const auto value=*static_cast<volatile uint16_t *>(host_ptr(a));
    if(a>=0x04000040u&&a<=0x04000054u)return sm64ds::render::dialogue_window_view::control(a-0x04000000u,value,
        sm64ds::render::dialogue_window_view::owned.load(std::memory_order_acquire));
    return value;
}''',
        read32: replacement32,
        key: '    if(('+key_condition+') && !(eng==ENGINE_A&&sm64ds::render::dialogue_window_view::owned.load(std::memory_order_acquire)))return base;',
        '    const bool cut=g_window_cut_scope && windows_binary(eng);': '''    const bool cut=(eng==ENGINE_A&&sm64ds::render::dialogue_window_view::owned.load(std::memory_order_acquire)) ||
        (g_window_cut_scope && windows_binary(eng));''',
    }
    for before,after in replacements.items():
        if text.count(before)!=1:raise ValueError('Native dialogue PPU view contract changed: '+before[:70])
        text=text.replace(before,after,1)
    return text


def native_affine_ppu_patch(text,header):
    anchor='static std::optional<PpuPreparedCandidates> gpu_affine_candidates(Engine eng,BgCandidateCache& cache,const PpuCompositionState& scene){'
    guard='if(&sm64ds_gpu_affine_ready && !sm64ds_gpu_affine_ready){'
    end='void ppu_apply_window_cut(Engine eng,PpuPreparedCandidates& candidates){candidates.state.window_cut=window_cut_for(eng);}'
    declaration='void ppu_apply_window_cut(Engine eng,PpuPreparedCandidates& candidates);'
    # BUILD134 cost cuts: the console A/B switch and word helpers the
    # publication (and ppu_cost_cuts_patch/affine_memo_patch) use.
    include='#include <cstdlib>\n';helpers='// DS palettes are BGR555.\n'
    for before in (anchor,guard,end,include,helpers):
        if text.count(before)!=1:raise ValueError('Native affine PPU boundary changed: '+before[:60])
    if header.count(declaration)!=1:raise ValueError('Native affine PPU header boundary changed')
    text=text.replace(include,include+'#if defined(__3DS__)\n#include <sys/stat.h>\n#endif\n',1)
    text=text.replace(helpers,(ROOT/'tools/native_renderer/ppu_cost_cuts.inc').read_text()+helpers,1)
    text=text.replace(anchor,'static bool native_affine_consumer=false;\n'+anchor,1)
    text=text.replace(guard,'if(!native_affine_consumer && &sm64ds_gpu_affine_ready && !sm64ds_gpu_affine_ready){',1)
    text=text.replace(end,end+'\n'+(ROOT/'tools/native_renderer/ppu_affine_front.inc').read_text(),1)
    header=header.replace(declaration,declaration+'\n// Native map + owned opaque foreground; refuses unsupported pixel effects.\nbool ppu_prepare_native_affine(Engine,Framebuffer& foreground,PpuGpuAffineLayer&);'
        '\n// BUILD129: bands its OBJs may cover and its overlay base (map upload hints).\nuint32_t ppu_native_affine_bands(const Framebuffer& foreground,const uint32_t** base,uint64_t* base_generation);',1)
    return text,header



def ds_math_unit_patch(io_text,div_text):
    """Exact VFP divide/square root (source/native_renderer/ds_math_unit.h).

    ARMv6K has no integer divide: the DS divider's 64-bit cases went through
    libgcc's shift-subtract loop and the square-root unit through a 32-step
    restoring loop (NormalizeVec3 issues both). cstd::fdiv, a host C divide of
    (a << 12) / b, also called libgcc. Results are identical for every input."""
    def once(text,anchor,replacement):
        if text.count(anchor)!=1:raise ValueError('Native DS math unit contract changed: '+anchor[:70])
        return text.replace(anchor,replacement,1)
    include='#include "../platform/3ds/native_renderer/ds_math_unit.h"\n'
    io_text=once(io_text,'#include "unmapped_capture_clock.h"\n','#include "unmapped_capture_clock.h"\n'+include)
    io_text=once(io_text,'''    } else {
        quot = n / d;
        rem = n % d;
    }''','''    } else if (!sm64ds_ds_math_divide(n, d, &quot, &rem)) {
        // The VFP's exact divide above; the library keeps the remaining edges.
        quot = n / d;
        rem = n % d;
    }''')
    io_text=once(io_text,'''uint32_t isqrt64(uint64_t v) {
    // Integer floor(sqrt(v)) by restoring bit-by-bit; matches the DS unit's result.
    uint64_t rem = 0, root = 0;
    for (int i = 0; i < 32; ++i) {
        root <<= 1;
        rem = (rem << 2) | (v >> 62);
        v <<= 2;
        if (rem > root) {
            rem -= root | 1;
            root += 2;
        }
    }
    return static_cast<uint32_t>(root >> 1);
}''','''uint32_t isqrt64(uint64_t v) {
    // floor(sqrt(v)), the DS unit's result: the VFP's square root with an
    // exact integer correction instead of a 32-step restoring loop.
    return sm64ds_ds_math_isqrt64(v);
}''')
    div_text=once(div_text,'typedef int s32;\n',include+'typedef int s32;\n')
    div_text=once(div_text,'    return (s32)(((long long)a << 12) / b);\n','    return (s32)sm64ds_ds_math_fdiv(a, b); /* exact, without libgcc */\n')
    return io_text,div_text


def lz77_patch(text):
    """port/hal/fs.cpp: LZ77 (type 0x10) decode without a bound check and an
    index computation per output byte. Same bytes for every well-formed
    stream (all 540 LZ77 files of the ROM compared), after sm64ds-enhanced
    patch 0038. Level and model loads decompress through this function."""
    old='''    u32 out = 0;
    s += 4;
    while (out < size) {
        u8 flags = *s++;
        for (int bit = 0; bit < 8 && out < size; ++bit) {
            if (flags & (0x80 >> bit)) {
                u32 len = (s[0] >> 4) + 3;
                u32 disp = ((s[0] & 0xF) << 8 | s[1]) + 1;
                s += 2;
                while (len-- && out < size) {
                    d[out] = d[out - disp];
                    ++out;
                }
            } else {
                d[out++] = *s++;
            }
        }
    }
}'''
    new='''    u8 *const end = d + size;
    s += 4;
    while (d < end) {
        unsigned flags = *s++;
        for (int bit = 0; bit < 8 && d < end; ++bit, flags <<= 1) {
            if (flags & 0x80) {
                unsigned len = (s[0] >> 4) + 3;
                const u8 *from = d - (((s[0] & 0xF) << 8 | s[1]) + 1);
                s += 2;
                const unsigned left = (unsigned)(end - d);
                if (len > left) len = left;
                /* forward byte copy: overlapping references repeat bytes */
                do *d++ = *from++; while (--len);
            } else {
                *d++ = *s++;
            }
        }
    }
}'''
    if text.count(old)!=1:raise ValueError('Native LZ77 decode contract changed')
    return text.replace(old,new,1)


def diagnostic_capture_patch(text):
    old = ''' if(sm64ds_fresh_native_enabled() && (native_diagnostics::requested || native_diagnostics::partial_requested)){
  native_diagnostics::requested=native_diagnostics::partial_requested=false;
  std::puts("Fresh native: legacy capture unavailable; native target/resource diagnostics pending");
 }
'''
    if old in text:
        if text.count(old)!=1:raise ValueError('Duplicate native diagnostic disable boundary')
        text=text.replace(old,'',1)
    if 'legacy capture unavailable' in text:raise ValueError('Changed native diagnostic disable boundary')
    replacements={
        '  native_diagnostics::requested=native_diagnostics::partial_requested=false;player_present_pipeline_drain();':
            '  native_diagnostics::requested=native_diagnostics::partial_requested=false;\n'
            '  const bool native_capture=sm64ds_fresh_native_enabled();\n'
            '  const bool native_capture_ready=!native_capture||sm64ds_fresh_native_diagnostic_begin();\n'
            '  if(!native_capture)player_present_pipeline_drain();',
        '  const bool render_ok=capture_ok && native_diagnostics::capture_render(native_diagnostics::last_capture_stem);':
            '  const bool render_ok=capture_ok && native_capture_ready && (native_capture?\n'
            '   sm64ds_fresh_native_diagnostic_capture(native_diagnostics::last_capture_stem):\n'
            '   native_diagnostics::capture_render(native_diagnostics::last_capture_stem));\n'
            '  if(native_capture)sm64ds_fresh_native_diagnostic_end();',
    }
    for before,after in replacements.items():
        if text.count(before)!=1:raise ValueError('Native diagnostic source contract changed: '+before[:70])
        text=text.replace(before,after,1)
    return text


def native_cadence_metrics_patch(clock,cadence):
    before=' bool valid()const{return valid_;}'
    after=''' bool postpone(uint64_t ticks){
  if(!valid_||next_>std::numeric_limits<uint64_t>::max()-ticks)return valid_=false;
  next_+=ticks;return true;
 }
 bool valid()const{return valid_;}'''
    if clock.count(before)!=1:raise ValueError('Native clock pause boundary changed')
    clock=clock.replace(before,after,1)
    changes=[
        ('bool catch_up=true,fixed_timeline=false;','uint64_t clock_started_tick=0,clock_started_frame=0;\nbool catch_up=true,fixed_timeline=false;'),
        ('    clock.reset();last_frame=observed_cycles=cycle_frames=0;','    clock_started_tick=clock_started_frame=0;\n    clock.reset();last_frame=observed_cycles=cycle_frames=0;'),
        ('    if(!clock)clock.emplace(SYSCLOCK_ARM11,33513982,560190,svcGetSystemTick(),last_frame,!fixed_timeline);',
         '    if(!clock){clock_started_tick=svcGetSystemTick();clock_started_frame=last_frame;\n'
         '        clock.emplace(SYSCLOCK_ARM11,33513982,560190,clock_started_tick,last_frame,!fixed_timeline);\n    }'),
        (':1500ull)*SYSCLOCK_ARM11',':(fixed_timeline?0ull:1500ull))*SYSCLOCK_ARM11'),
        ('void report(){\n    if(!configured)return;',
         'void exclude_pause(uint64_t began){\n'
         '    if(!fixed_timeline||!clock||!began)return;\n'
         '    const auto duration=svcGetSystemTick()-began;\n'
         '    if(!clock->postpone(duration))fail("pause timeline overflow");\n'
         '    clock_started_tick+=duration;\n}\n'
         'void report(){\n    if(!configured)return;\n'
         '    if(clock)std::printf("Native cadence rate: events=%llu elapsedMs=%.3f targetMs=%.3f fixed=%u\\n",\n'
         '        (unsigned long long)(last_frame-clock_started_frame),double(svcGetSystemTick()-clock_started_tick)*1000/SYSCLOCK_ARM11,\n'
         '        double(last_frame-clock_started_frame)*560190000.0/33513982,unsigned(fixed_timeline));'),
        ('extern "C" void sm64ds_observed_cycle_begin(){observed_cadence::cycle_begin();}',
         'extern "C" void sm64ds_observed_cycle_begin(){observed_cadence::cycle_begin();}\n'
         'extern "C" void sm64ds_native_cadence_pause_ended(uint64_t began){observed_cadence::exclude_pause(began);}')
    ]
    for before,after in changes:
        if cadence.count(before)!=1:raise ValueError('Native cadence observation boundary changed: '+before[:60])
        cadence=cadence.replace(before,after,1)
    return clock,cadence


def fixed_cadence_patch(clock, cadence):
    replacements=[
        (' bool valid_=false;', ' bool rebase_late_cycles_=true;\n bool valid_=false;'),
        ('uint64_t completed_frames=0):next_(now),frame_(completed_frames){',
         'uint64_t completed_frames=0,bool rebase_late_cycles=true):next_(now),frame_(completed_frames),rebase_late_cycles_(rebase_late_cycles){'),
        ('  if(cycle_events_ && last_late_){', '  if(rebase_late_cycles_ && cycle_events_ && last_late_){'),
    ]
    for before,after in replacements:
        if clock.count(before)!=1:raise ValueError('Native fixed clock boundary changed: '+before)
        clock=clock.replace(before,after,1)
    replacements=[
        ('bool catch_up=true;', 'bool catch_up=true,fixed_timeline=false;'),
        ('    clock.reset();last_frame=observed_cycles=cycle_frames=0;',
         '    {const char* native=std::getenv("SM64DS_FRESH_NATIVE");fixed_timeline=native && native[0]==\'1\' && native[1]==0;}\n'
         '    clock.reset();last_frame=observed_cycles=cycle_frames=0;'),
        ('svcGetSystemTick(),last_frame);', 'svcGetSystemTick(),last_frame,!fixed_timeline);'),
        ('    if(catch_up && enabled){', '    if(catch_up && enabled && !fixed_timeline){'),
        ('    return configured && enabled && catch_up && clock && debt_ticks>frame_ticks/2;',
         '    if(!configured || !enabled || !catch_up || !clock)return false;\n'
         '    if(fixed_timeline){const auto now=svcGetSystemTick();return now>clock->target() && now-clock->target()>frame_ticks/2;}\n'
         '    return debt_ticks>frame_ticks/2;'),
        ('    std::printf("Frame skip catchUp=%u skippedFrames=%llu repaidMs=%.1f\\n",unsigned(catch_up),',
         '    std::printf("Frame skip fixedTimeline=%u catchUp=%u skippedFrames=%llu repaidMs=%.1f\\n",unsigned(fixed_timeline),unsigned(catch_up),'),
    ]
    for before,after in replacements:
        if cadence.count(before)!=1:raise ValueError('Native fixed cadence boundary changed: '+before)
        cadence=cadence.replace(before,after,1)
    return native_cadence_metrics_patch(clock,cadence)


def exit_safety_patch(present,entry):
    """BUILD115: no thread may outlive the heaps on any exit path.

    BUILD114 crashed on Old 3DS hardware in the castle pause: an abort ran
    libctru's exit, which unmaps the linear heap while the core-1 audio
    thread still rendered into its NDSP buffer (Luma data abort, no log).
    The runtime's userAppExit calls the bounded stop added here; the audio
    thread installs the runtime's SIGABRT handler (newlib keeps one per
    thread). game_entry keeps the previous boot's log for the test tour."""
    import re
    def once(text,anchor,replacement):
        if text.count(anchor)!=1:raise ValueError('Native exit contract changed: '+anchor[:70])
        return text.replace(anchor,replacement,1)
    present=once(present,'void audio_loop(void*){\n    on_worker=true;\n',
        'extern "C" void sm64ds_fatal_handlers_install(void) __attribute__((weak));\n'
        'void audio_loop(void*){\n    on_worker=true;\n    if(sm64ds_fatal_handlers_install)sm64ds_fatal_handlers_install();\n')
    stop=('void player_present_pipeline_stop_audio(){\n'
          '    using namespace present_pipeline;\n'
          '    if(!enabled)return;\n'
          '    wait_idle();audio_stopped=true;\n'
          '    if(audio_thread){threadJoin(audio_thread,U64_MAX);threadFree(audio_thread);audio_thread=nullptr;}\n'
          '}\n')
    present=once(present,stop,stop+(
        '// Exit path (userAppExit): bounded, because an abort can leave the mixer\n'
        '// waiting for a lock its owner will never release. Idempotent.\n'
        'extern "C" void sm64ds_present_pipeline_exit_stop(){\n'
        '    using namespace present_pipeline;\n'
        '    audio_stopped=true;__sync_synchronize();\n'
        '    if(audio_thread&&threadGetCurrent()!=audio_thread&&R_SUCCEEDED(threadJoin(audio_thread,250000000ULL))){\n'
        '        threadFree(audio_thread);audio_thread=nullptr;\n'
        '    }\n'
        '}\n'))
    pattern=re.compile(r' std::freopen\("(sdmc:/[^"]+)","w",stdout\);')
    if not pattern.search(entry):raise ValueError('Native exit contract changed: log freopen')
    entry=pattern.sub(lambda m:' keep_previous_log("'+m.group(1)+'");std::freopen("'+m.group(1)+'","w",stdout);',entry)
    entry=once(entry,'\nint main(){\n',(
        '\n// BUILD115: the previous boot\'s log survives as NAME-anterior.EXT; the test\n'
        '// tour copies it beside the reports of a level that did not finish.\n'
        'static void keep_previous_log(const char* path){\n'
        ' char previous[96];const char* dot=std::strrchr(path,\'.\');\n'
        ' if(!dot||std::snprintf(previous,sizeof previous,"%.*s-anterior%s",int(dot-path),path,dot)>=int(sizeof previous))return;\n'
        ' std::remove(previous);std::rename(path,previous);\n'
        '}\n'
        'int main(){\n'))
    return present,entry


def memory_headroom_patch(ppu,bridge):
    """BUILD115: keep a libc heap reserve on Old 3DS.

    A second PPU cache way costs ~2.5 MB of libc heap per engine. BUILD114
    allocated it for both engines in the castle pause on hardware and the
    process aborted right after (silent std::terminate). A refused way only
    re-prepares the 2D planes on alternating display configurations.
    The worker's operator new fallback into the unlocked game heap is now
    reported (it means the libc heap is exhausted)."""
    def once(text,anchor,replacement):
        if text.count(anchor)!=1:raise ValueError('Native memory contract changed: '+anchor[:70])
        return text.replace(anchor,replacement,1)
    ppu=once(ppu,'#include <atomic>\n',(
        '#include <atomic>\n'
        '#if defined(__3DS__)\n'
        '#include <malloc.h>\n'
        '#include <unistd.h>\n'
        'extern "C" char* fake_heap_end;\n'
        '#endif\n'))
    allocate=('static bool allocate_cache_way(Engine eng,unsigned way){\n'
              '    auto& w=cache_ways[eng];\n'
              '    if(w.prepared[way])return true;\n'
              '    if(w.failed)return false;\n')
    ppu=once(ppu,allocate,(
        '// BUILD115: libc heap free (arena chunks + never-claimed sbrk range) must\n'
        '// still cover a reserve after the new way. SM64DS_PPU_WAY_RESERVE_KB tunes it.\n'
        'static bool cache_way_headroom(size_t bytes){\n'
        '#if defined(__3DS__)\n'
        '    static const size_t reserve=[]{const char* v=std::getenv("SM64DS_PPU_WAY_RESERVE_KB");return size_t(v?std::atoi(v):6144)*1024;}();\n'
        '    const struct mallinfo info=mallinfo();char* brk=static_cast<char*>(sbrk(0));\n'
        '    const size_t unclaimed=(fake_heap_end&&brk!=reinterpret_cast<char*>(-1)&&fake_heap_end>brk)?size_t(fake_heap_end-brk):0;\n'
        '    const size_t available=size_t(info.fordblks)+unclaimed;\n'
        '    if(available>=bytes+reserve)return true;\n'
        '    static unsigned refused=0;\n'
        '    if(refused++<4)std::printf("PPU cache way 2 refused: heapFree=%u need=%u reserve=%u\\n",unsigned(available),unsigned(bytes),unsigned(reserve));\n'
        '    return false;\n'
        '#else\n'
        '    (void)bytes;return true;\n'
        '#endif\n'
        '}\n')+allocate+'    if(!cache_way_headroom(sizeof(PreparedCache)+sizeof(BgCandidateCache)))return false;\n')
    fallback=('    if (sm64ds_on_present_worker()) {\n'
              '        void *p = malloc(size ? size : 1);\n'
              '        if (p) return p;\n'
              '    }\n')
    bridge=once(bridge,fallback,(
        '    if (sm64ds_on_present_worker()) {\n'
        '        void *p = malloc(size ? size : 1);\n'
        '        if (p) return p;\n'
        '        /* BUILD115: the game heap has no locking, so this fallback can race\n'
        '           the game thread. Report it: the libc heap is exhausted. */\n'
        '        static int reported;\n'
        '        if (reported++ < 4) { printf("WARNING: worker operator new(%u) fell back to the game heap: libc heap exhausted\\n", size); fflush(stdout); }\n'
        '    }\n'))
    bridge=once(bridge,'#include <cstdlib>\n','#include <cstdlib>\n#include <cstdio>\n')
    return ppu,bridge


def affine_memo_patch(text):
    """BUILD129: rotating/scrolling bottom map without the full memo miss."""
    def once(t,a,b):
        if t.count(a)!=1:raise ValueError('Affine memo boundary changed: '+a[:70])
        return t.replace(a,b,1)
    text=once(text,'    static Memo memos[2];auto& memo=memos[eng];\n',
        '    static Memo memos[2];auto& memo=memos[eng];\n'
        '    // BUILD129 fast-path check (PPU_AFFINE_FAST_CHECK=1): expected descriptor.\n'
        '    static PpuGpuAffineLayer affine_expected[2]{};static bool affine_expect[2]{};static unsigned affine_checks=0,affine_mismatches=0;affine_expect[eng]=false;\n'
        '    // BUILD134 cost cut: what a memo hit compares (ppu_affine_memo_plan.inc).\n'
        '    struct MemoPlan {struct Range {uint32_t address,size,offset;};HostVector<Range> ranges;bool valid=false;};\n'
        '    static MemoPlan memo_plans[2];auto& memo_plan=memo_plans[eng];\n')
    stored='        if(size){size_t offset=0;for(const auto& span:memo_spans){std::memcpy(memo.bytes.data()+offset,host_ptr(span.address),span.size);offset+=span.size;}memo.valid=true;}\n'
    text=once(text,stored,stored+(ROOT/'tools/native_renderer/ppu_affine_memo_plan.inc').read_text())
    old=('        bool same=memo.valid && memo.spans.size()==merged;\n'
         '        for(unsigned i=0;same && i<merged;++i)same=memo.spans[i].address==memo_spans[i].address && memo.spans[i].size==memo_spans[i].size;\n'
         '        size_t offset=0;\n'
         '        for(unsigned i=0;same && i<merged;++i){same=!std::memcmp(memo.bytes.data()+offset,host_ptr(memo_spans[i].address),memo_spans[i].size);offset+=memo_spans[i].size;}\n'
         '        if(same && s.generation){')
    # The fast path returns exactly like the memo hit that follows the
    # compare (its mode-1 resolution differs between vendor and render60).
    hit_head='        if(same && s.generation){\n'
    if text.count(old)!=1 or text.count(old[:-len('        if(same && s.generation){')]+hit_head)!=1:
        raise ValueError('Affine memo boundary changed: memo compare')
    start=text.index(old)+len(old)+1;stop=text.index('        }\n',start)
    hit=text[start:stop]
    if not hit.endswith('return out;\n') or 'PpuPreparedCandidates out{' not in hit:raise ValueError('Affine memo boundary changed: memo hit')
    new=(ROOT/'tools/native_renderer/ppu_affine_memo.inc').read_text().rstrip('\n')
    new=new.replace('@MEMO_HIT_RETURN@',''.join('        '+line for line in hit.splitlines(True)).rstrip('\n'))
    text=once(text,old,new)
    # The fast-path check compares the descriptor the full path published,
    # right before the function's final return.
    tail='\n}\nstatic BgCandidateCache& bg_only_cache(Engine eng){'
    if text.count(tail)!=1:raise ValueError('Affine memo boundary changed: function end')
    at=text.rfind('\n    PpuCompositionState resolved=scene;',0,text.index(tail))+1
    if at<=0:raise ValueError('Affine memo boundary changed: final return')
    check=(ROOT/'tools/native_renderer/ppu_affine_memo_check.inc').read_text()
    text=text[:at]+check+text[at:]
    return text

def ppu_cost_cuts_patch(text):
    """BUILD134: exact cost cuts in pinned PPU code (same outputs).

    Engine A's OBJ-only update looked for a kept semi-transparent OBJ pixel
    one byte at a time over the 49,152 front-layer bytes (~0.6 ms per HUD
    change on the Old 3DS): by words behind the console switch
    (ppu_cost_cuts.inc). The GPU-affine attempt read each BG's registers
    twice: its first pass keeps the configurations (pure reads of the same
    registers within one call)."""
    def once(t,a,b):
        if t.count(a)!=1:raise ValueError('PPU cost cut boundary changed: '+a[:70])
        return t.replace(a,b,1)
    scan='        for(unsigned k=0;k<SCREEN_W*SCREEN_H;++k)if(scene.front_layer[k]&0x80){scene.has_obj_alpha=true;break;}\n'
    text=once(text,scan,
        '        if(ppu_cost_cuts())scene.has_obj_alpha=ppu_any_bit7(scene.front_layer,SCREEN_W*SCREEN_H); // BUILD134: by words\n'
        '        else\n'+scan)
    text=once(text,'    int only=-1;int overlay_bg[3]{};unsigned overlays=0;\n',
        '    int only=-1;int overlay_bg[3]{};unsigned overlays=0;\n'
        '    BgConfig configs[4]; // BUILD134: each BG read once per attempt\n')
    text=once(text,'        const auto cfg=read_bg(em,bg,disp);if(!cfg.enabled)continue;\n',
        '        configs[bg]=read_bg(em,bg,disp);const auto& cfg=configs[bg];if(!cfg.enabled)continue;\n')
    text=once(text,'    BgConfig c=read_bg(em,only,disp);\n','    BgConfig c=configs[only];\n')
    text=once(text,'        ocfg[k]=read_bg(em,overlay_bg[k],disp);\n','        ocfg[k]=configs[overlay_bg[k]];\n')
    return text

def ppu_bug_fixes_patch(text):
    """BUILD135: three faults in pinned PPU code (found by the BUILD134 PPU harness).

    1. The CPU path kept its per-sprite OBJ layer across preparations with a
       key (DISPCNT, OBJ palette, OAM, sprite tiles) that left out the
       256-colour extended OBJ palette (DISPCNT bit 31): after a change there
       the sprites kept their old colours (PPU_NO_SPRITE_INCREMENTAL=1 hid it).
       The palette as obj_ext_color reads it is now part of the key.
    2. BLDCNT mode 1 with the backdrop as both first and second target: the
       CPU composition (the reference every fallback runs) blends the backdrop
       with itself where nothing covers it; the GPU affine publication drew the
       plain backdrop. That state now takes the CPU path (not seen in the game;
       the harness reached it with synthetic registers).
    3. shift_rows moved a pointer by a negative int times size_t (undefined
       behaviour; the address was the intended one)."""
    def once(t,a,b):
        if t.count(a)!=1:raise ValueError('PPU bug fix boundary changed: '+a[:70])
        return t.replace(a,b,1)
    text=once(text,'template<class Put,class Box=NoObjBox> static void scan_obj(',
        '// BUILD135: the whole extended OBJ palette as obj_ext_color reads it.\n'
        'static void obj_ext_snapshot(Engine eng,uint16_t* out){\n'
        '    auto rd8=[](uint32_t a){return *static_cast<volatile uint8_t*>(host_ptr(a));};\n'
        '    if(eng==ENGINE_B){\n'
        '        if((rd8(0x04000249)&0x87)==0x83)std::memcpy(out,host_ptr(0x068a0000),8192);else std::memset(out,0,8192);\n'
        '        return;\n'
        '    }\n'
        '    std::memset(out,0,8192);\n'
        '    for(unsigned bank=0;bank<2;++bank)if((rd8(0x04000245+bank)&0x87)==0x85){\n'
        '        uint16_t words[4096];std::memcpy(words,host_ptr(0x06890000+bank*0x4000),8192);\n'
        '        for(unsigned i=0;i<4096;++i)out[i]|=words[i];\n'
        '    }\n'
        '}\n'
        'template<class Put,class Box=NoObjBox> static void scan_obj(')
    text=once(text,'    uint32_t disp=0;\n    bool layer_valid=false;\n};\nstatic uint64_t obj_sprite_updates;',
        '    uint32_t disp=0;\n    bool layer_valid=false;\n'
        '    uint16_t ext[4096]{};bool ext_valid=false; // BUILD135: extended OBJ palette in use\n'
        '};\nstatic uint64_t obj_sprite_updates;')
    text=once(text,'        !std::memcmp(incremental->pltt,pltt_now,512);\n',
        '        !std::memcmp(incremental->pltt,pltt_now,512);\n'
        '    // BUILD135: with DISPCNT bit 31 a 256-colour sprite takes its colours from\n'
        '    // the extended OBJ palette, compared here like the standard one.\n'
        '    static uint16_t ext_now[4096];const bool ext_used=(disp&0x80000000u)!=0;\n'
        '    if(incremental&&ext_used)obj_ext_snapshot(eng,ext_now);\n'
        '    if(ext_used)layer_reusable=layer_reusable&&incremental->ext_valid&&!std::memcmp(incremental->ext,ext_now,sizeof ext_now);\n')
    text=once(text,'        std::memcpy(incremental->oam,oam_now,1024);std::memcpy(incremental->pltt,pltt_now,512);\n',
        '        std::memcpy(incremental->oam,oam_now,1024);std::memcpy(incremental->pltt,pltt_now,512);\n'
        '        incremental->ext_valid=ext_used;if(ext_used)std::memcpy(incremental->ext,ext_now,sizeof ext_now);\n')
    text=once(text,'        if((first&~(overlay_mask|0x20u)) || (second&overlay_mask) || (scene.blend&0x1010))return std::nullopt;\n',
        '        if((first&~(overlay_mask|0x20u)) || (second&overlay_mask) || (scene.blend&0x1010))return std::nullopt;\n'
        '        // BUILD135: the CPU composition blends an uncovered backdrop with itself.\n'
        '        if((first&0x20u) && (second&0x20u))return std::nullopt;\n')
    text=once(text,'        else std::memcpy(d-rows*row_bytes,s,keep*row_bytes);',
        '        else std::memcpy(d+size_t(-rows)*row_bytes,s,keep*row_bytes); // BUILD135: no negative size_t product')
    return text

def cadence_stall_patch(cadence):
    """BUILD129: the fixed timeline forgives stalls instead of fast-forwarding.

    BUILD128 hardware: after every 3.9 s level load the fixed timeline (which
    never rebases) repaid the whole load at ~1.85x speed, 2-3 updates per
    image for seconds (53% of 01-quieto intervals). The DS itself just
    continues after a load. A frame later than 6 display frames (100 ms: a
    load, an SD write, an IPC stall; never one slow update) keeps only 2
    frames of debt; shorter lateness is still caught up by frame skip.
    PLAYER_CADENCE_KEEP_STALLS=1 restores the old behaviour."""
    def once(t,a,b):
        if t.count(a)!=1:raise ValueError('Cadence stall boundary changed: '+a[:70])
        return t.replace(a,b,1)
    cadence=once(cadence,'uint64_t clock_started_tick=0,clock_started_frame=0;\n',
        'uint64_t clock_started_tick=0,clock_started_frame=0;\n'
        'uint64_t forgiven_ticks=0,forgiven_stalls=0;\n')
    cadence=once(cadence,'    debt_ticks=repaid_ticks=skipped_frames=0;\n',
        '    debt_ticks=repaid_ticks=skipped_frames=0;forgiven_ticks=forgiven_stalls=0;\n')
    cadence=once(cadence,'    late_ticks+=decision.lateness;\n',
        '    late_ticks+=decision.lateness;\n'
        '    {static const bool keep_stalls=std::getenv("PLAYER_CADENCE_KEEP_STALLS")!=nullptr;\n'
        '     const uint64_t display=SYSCLOCK_ARM11*560190ull/33513982ull;\n'
        '     if(fixed_timeline && catch_up && enabled && !keep_stalls && decision.lateness>6*display){\n'
        '        const uint64_t forgive=decision.lateness-2*display;\n'
        '        if(!clock->postpone(forgive))fail("stall timeline overflow");\n'
        '        clock_started_tick+=forgive;forgiven_ticks+=forgive;++forgiven_stalls;\n'
        '     }}\n')
    cadence=once(cadence,'    std::printf("Frame skip fixedTimeline=',
        '    std::printf("Native cadence stalls: forgiven=%llu forgivenMs=%.1f\\n",(unsigned long long)forgiven_stalls,double(forgiven_ticks)*1000.0/SYSCLOCK_ARM11);\n'
        '    std::printf("Frame skip fixedTimeline=')
    return cadence

def runtime_patches(output):
    paths=('port/platform/3ds/game_main.cpp','port/platform/3ds/command_drain.cpp','port/platform/3ds/native_mesh_camera.cpp','port/platform/3ds/player_present.cpp','port/ntr/ppu.cpp','port/platform/3ds/game_entry.cpp','port/platform/3ds/observed_cycle_clock.h','port/platform/3ds/observed_cadence.cpp','port/ntr/include/ntr/ppu.h','port/ntr/io.cpp','port/hal/cstd_div.c','port/hal/fs.cpp','port/hal/cxxname_bridge.cpp')
    originals={name:(output/name).read_text() for name in paths}
    def once(text,anchor,replacement):
        if text.count(anchor)!=1:raise ValueError('Native runtime source contract changed: '+anchor[:70])
        return text.replace(anchor,replacement,1)
    name=paths[0];text=originals[name]
    text=once(text,'#include "ntr/gx_gpu_geometry.h"\n','#include "ntr/gx_gpu_geometry.h"\n#include "native_renderer/game_runtime.h"\n')
    # Prepared render60 actor dispatch supplies generation + address. Publish
    # that semantic owner to the fresh producer without linking the smoke
    # executable against globals supplied only by the full game entry point.
    actor_set='   sm64ds_render60_current_actor=(uint64_t(actor[1])<<32)|uint32_t(uintptr_t(actor));\n'
    actor_reset='   sm64ds_render60_current_actor=0;current=next;\n'
    if actor_set in text:
        text=once(text,actor_set,actor_set+'   sm64ds_fresh_native_actor(sm64ds_render60_current_actor);\n')
        text=once(text,actor_reset,'   sm64ds_fresh_native_actor(0);\n'+actor_reset)
    text=once(text,' observed_cadence::configure();\n player_geometry::startup();\n',''' observed_cadence::configure();
 if(!sm64ds_fresh_native_startup())return 1;
 struct FreshNativeCleanup { ~FreshNativeCleanup(){sm64ds_fresh_native_shutdown();} } fresh_native_cleanup;
 if(!sm64ds_fresh_native_enabled())player_geometry::startup();
''')
    start=' const char* mesh_option=std::getenv("PLAYER_NATIVE_MESH");\n'
    stop=' ntr::gx_begin_frame();hidScanInput();\n'
    if text.count(start)!=1 or text.count(stop)!=1:raise ValueError('Native update boundary changed')
    first=text.index(start);last=text.index(stop,first)+len(stop)
    old=text[first:last].replace('ntr::gx_begin_frame();hidScanInput();','ntr::gx_begin_frame();')
    text=text[:first]+''' if(sm64ds_fresh_native_enabled())sm64ds_fresh_native_begin();
 else{
'''+old+''' }
 hidScanInput();
'''+text[last:]
    start='  native_mesh_driver_reset();ntr::gx_native_mesh_camera_cut();\n'
    overlay_start='  sm64ds_render60_cut();native_mesh_driver_reset();ntr::gx_native_mesh_camera_cut();\n'
    if text.count(start)==0 and text.count(overlay_start)==1:start=overlay_start
    stop='  ntr::gx_texture_cache_scene_trim(); // and its decoded textures\n'
    if text.count(start)!=1 or text.count(stop)!=1:raise ValueError('Native scene boundary changed')
    first=text.index(start);last=text.index(stop,first)+len(stop)
    text=text[:first]+'''  sm64ds_fresh_native_scene_changed();
  if(!sm64ds_fresh_native_enabled()){
'''+text[first:last]+'''  }
'''+text[last:]
    text=once(text,' v3ds_renderer_init();\n',' if(sm64ds_fresh_native_enabled()){REQUIRE(sm64ds_fresh_native_graphics());}\n else v3ds_renderer_init();\n')
    text=once(text,' sm64ds_native_sound_output_close();v3ds_exit();',' sm64ds_native_sound_output_close();if(!sm64ds_fresh_native_enabled())v3ds_exit();')
    text=once(text,' if(capturing){\n',' if(capturing && !sm64ds_fresh_native_enabled()){\n')
    # Optional maintained diagnostics overlay is absent from the pinned base.
    # When installed, every target-dependent capture boundary must match.
    if '#include "native_diagnostics.hpp"' in text:
        text=once(text,' if(!title_capture_checked && native_diagnostics::enabled()',
            ' if(!title_capture_checked && !sm64ds_fresh_native_enabled() && native_diagnostics::enabled()')
        text=once(text,' const bool benchmark_capture=(native_diagnostics::enabled() &&',
            ' if(sm64ds_fresh_native_enabled())native_benchmark::native_course(current_scene,updates);\n const bool benchmark_capture=(!sm64ds_fresh_native_enabled() && native_diagnostics::enabled() &&')
        text=diagnostic_capture_patch(text)
    text=once(text,' else if(consecutive_skips=0,player_present(capturing,false))present_failures=0;', ''' else if(consecutive_skips=0,(sm64ds_fresh_native_enabled()?
  sm64ds_fresh_native_present(((ntr::io_read(0x04000000,4)&((ntr::io_read(0x04000000,4)&8)?0x1e00:0x1f00))|
                              (ntr::io_read(0x04001000,4)&0x1f00))!=0):
  player_present(capturing,false)))present_failures=0;
 else if(sm64ds_fresh_native_diagnostic_hold())present_failures=0;''')
    text=once(text,' if(skip_present){++consecutive_skips;observed_cadence::note_skipped();present_failures=0;}',
        ' if(skip_present){if(sm64ds_fresh_native_enabled())sm64ds_fresh_native_skip();++consecutive_skips;observed_cadence::note_skipped();present_failures=0;}')
    # Native mode decides skips per update, not per VBlank: dry updates are
    # chosen before the game draws, and the 30 Hz intermediate VBlank (which
    # finishes New 3DS interpolation) is never skipped. At most two updates in
    # a row go unshown; the VBlank-counted limit of three let the forced
    # present land on an intermediate VBlank and show nothing.
    exact=' !sm64ds_render60_need_exact() &&' if ' const bool skip_present=!capturing && !sm64ds_render60_need_exact() &&' in text else ''
    text=once(text,' const bool skip_present=!capturing &&'+exact+' consecutive_skips<3 && observed_cadence::behind() &&\n  !(ntr::io_read(0x04000064,4)&0x80000000u) && !ntr::io_display_capture();',
        ' const bool skip_present=sm64ds_fresh_native_enabled()?sm64ds_fresh_native_skip_wanted(capturing)!=0:\n  (!capturing &&'+exact+' consecutive_skips<3 && observed_cadence::behind() &&\n  !(ntr::io_read(0x04000064,4)&0x80000000u) && !ntr::io_display_capture());')
    patches={name:text}
    name=paths[1];text=originals[name]
    text=once(text,'#include "command_budget.h"\n','#include "command_budget.h"\n#include "native_renderer/game_runtime.h"\n')
    text=once(text,' if(sm64ds_shadow_probe_pending)sm64ds_shadow_probe_readback();',
        ' if(!sm64ds_fresh_native_enabled() && sm64ds_shadow_probe_pending)sm64ds_shadow_probe_readback();')
    text=once(text,' if(ok)gpu_passes::frame_begin();',' if(ok && !sm64ds_fresh_native_enabled())gpu_passes::frame_begin();')
    patches[name]=text
    name=paths[2];text=originals[name]
    text=once(text,'#include "native_mesh_api.h"\n','#include "native_mesh_api.h"\n#include "native_renderer/game_runtime.h"\n')
    text=once(text,'extern "C" int __wrap__ZN6Camera6RenderEv(void* camera){\n',
        'extern "C" int __wrap__ZN6Camera6RenderEv(void* camera){\n if(sm64ds_fresh_native_enabled())return __real__ZN6Camera6RenderEv(camera);\n')
    text=once(text,' if(draw&&camera&&target&&camera_depth){\n',
        ' if(sm64ds_fresh_native_enabled()){sm64ds_fresh_native_look_at(camera,up,target,int(draw),matrix);return;}\n if(draw&&camera&&target&&camera_depth){\n')
    text=once(text,'extern "C" int __wrap__ZN6Camera11ChangeStateEPNS_5StateE(void* camera,void* state){\n',
        'extern "C" int __wrap__ZN6Camera11ChangeStateEPNS_5StateE(void* camera,void* state){\n if(sm64ds_fresh_native_enabled())return __real__ZN6Camera11ChangeStateEPNS_5StateE(camera,state);\n')
    patches[name]=text
    name=paths[3];text=originals[name]
    text=once(text,'#include "ntr/ppu.h"\n','#include "ntr/ppu.h"\n#include "native_renderer/game_runtime.h"\n')
    text=once(text,'extern "C" bool sm64ds_on_present_worker(){return present_pipeline::on_worker;}',
        'extern "C" bool sm64ds_on_present_worker(){return present_pipeline::on_worker || sm64ds_fresh_native_on_worker();}')
    anchor='    audio_pump=audio;audio_async=asynchronous_audio;start_ms=v3ds_time_ms();\n'
    text=once(text,anchor,anchor+'''    if(sm64ds_fresh_native_enabled()){
        // Keep the mixer off the core shared by game and native rendering.
        // Fall back to a higher-priority core0 pump if core1 is unavailable.
        // BUILD124: core 1 belongs to the render worker when APT granted it
        // 80%; the mixer then pumps on core 0 above the game thread.
        int audio_core=fresh_render_core1?0:1;
        audio_thread=threadCreate(audio_loop,nullptr,64*1024,priority-2,audio_core,false);
        if(!audio_thread&&audio_core!=0){audio_core=0;audio_thread=threadCreate(audio_loop,nullptr,64*1024,priority-2,audio_core,false);}
        enabled=audio_thread!=nullptr;
        sm64ds_fresh_native_core_layout(fresh_render_core1,audio_core);
        std::printf("Fresh native audio: core=%d created=%u legacyRenderWorkers=0 renderCore1=%u\\n",audio_core,unsigned(enabled),unsigned(fresh_render_core1));
        return enabled;
    }
''')
    head='bool player_present_pipeline_enable(bool (*audio)(),bool asynchronous_audio){\n    using namespace present_pipeline;\n    if(enabled)return true;\n'
    text=once(text,head,head+'    bool fresh_render_core1=false; // BUILD124 Old 3DS core layout\n')
    text=once(text,'const Result limit=APT_SetAppCpuTimeLimit(percent);',
        '''// BUILD124: Old 3DS native rendering asks for 80% of core 1 for its
        // render worker (exheader MaxCpu 0xD0: PM allows MaxCpu&0x7F percent;
        // 0x9E refused 80 with d8e05bf4). A refusal (Homebrew Launcher, an
        // older CIA) keeps the BUILD123 layout: 30% for the mixer.
        fresh_render_core1=sm64ds_fresh_native_enabled()&&core==1&&sm64ds_fresh_native_render_core1_wanted();
        Result limit=APT_SetAppCpuTimeLimit(fresh_render_core1?80u:sm64ds_fresh_native_enabled()?std::min(percent,30u):percent);
        if(fresh_render_core1&&R_FAILED(limit)){
            std::printf("Present pipeline: 80%% of core 1 refused %08lx; render stays on core 0\\n",(unsigned long)limit);
            fresh_render_core1=false;limit=APT_SetAppCpuTimeLimit(30);
        }''')
    patches[name]=text
    patches[paths[4]]=dialogue_ppu_patch(originals[paths[4]])
    patches[paths[4]],patches[paths[8]]=native_affine_ppu_patch(patches[paths[4]],originals[paths[8]])
    patches[paths[5]]=once(originals[paths[5]],' setvbuf(stdout,nullptr,_IONBF,0);',
        ''' // Unbuffered (BUILD120): a boot that stops during startup must leave its
 // last line on the SD. smoke_main keeps the log unbuffered during play too.
 setvbuf(stdout,nullptr,_IONBF,0);''')
    patches[paths[6]],patches[paths[7]]=fixed_cadence_patch(originals[paths[6]],originals[paths[7]])
    patches[paths[7]]=cadence_stall_patch(patches[paths[7]])
    patches[paths[9]],patches[paths[10]]=ds_math_unit_patch(originals[paths[9]],originals[paths[10]])
    patches[paths[11]]=lz77_patch(originals[paths[11]])
    patches[paths[3]],patches[paths[5]]=exit_safety_patch(patches[paths[3]],patches[paths[5]])
    patches[paths[4]],patches[paths[12]]=memory_headroom_patch(patches[paths[4]],originals[paths[12]])
    patches[paths[4]]=affine_memo_patch(patches[paths[4]])
    patches[paths[4]]=ppu_cost_cuts_patch(patches[paths[4]])
    patches[paths[4]]=ppu_bug_fixes_patch(patches[paths[4]])
    return patches


def prepare(output,catalog,install_runtime=False,image_catalog=None,menu_catalog=None,canvas_catalog=None,background_catalog=None,menu_obj_catalog=None,menu_backdrop_catalog=None):
    output=Path(output).resolve();catalog=Path(catalog).resolve(strict=True)
    if not output.is_relative_to((ROOT/'build').resolve()):raise ValueError('Native integration requires a generated build/ checkout')
    cmake=output/'port/CMakeLists.txt';text=cmake.read_text()
    if text.count(ANCHOR)!=1 or MARKER in text:raise ValueError('Native renderer game-target contract changed or already staged')
    sources={p.name:p.read_bytes() for p in (ROOT/'source/native_renderer').iterdir() if p.suffix in ('.cpp','.hpp','.h','.pica','.S')}
    sources.update({p.name:p.read_bytes() for p in (ROOT/'source/integrated_native/renderer').iterdir() if p.suffix in ('.cpp','.hpp','.h')})
    sources['fixed_math.cpp']=(ROOT/'source/core/fixed_math.cpp').read_bytes()
    sources['sm64ds/fixed_math.hpp']=(ROOT/'include/sm64ds/fixed_math.hpp').read_bytes()
    header=catalog.read_bytes()
    if not header.startswith(b'#pragma once\n#include "model_instance.hpp"\nnamespace sm64ds::render::prepared {\n'):
        raise ValueError('Expected the generated native source catalog')
    sources['native_model_catalog.hpp']=header
    if image_catalog is not None:
        image_header=Path(image_catalog).resolve(strict=True).read_bytes()
        if not image_header.startswith(b'#pragma once\n#include "image_catalog.hpp"\nnamespace sm64ds::render::prepared_images {\n'):
            raise ValueError('Expected the generated native image-only catalog')
        sources['native_image_catalog.hpp']=image_header
    if menu_catalog is not None:
        menu_header=Path(menu_catalog).resolve(strict=True).read_bytes()
        if not menu_header.startswith(b'#pragma once\n#include "menu_cell_capture.hpp"\nnamespace sm64ds::render::game_hooks::prepared_menu {\n'):
            raise ValueError('Expected the generated native menu cell catalog')
        sources['native_menu_cell_catalog.hpp']=menu_header
    if menu_obj_catalog is not None:
        obj_header=Path(menu_obj_catalog).resolve(strict=True).read_bytes()
        if not obj_header.startswith(b'#pragma once\n#include "menu_obj_catalog.hpp"\n') or image_catalog is None:
            raise ValueError('Expected complete native OBJ views and matching image catalog')
        sources['native_menu_obj_catalog.hpp']=obj_header
    if menu_backdrop_catalog is not None:
        backdrop_header=Path(menu_backdrop_catalog).resolve(strict=True).read_bytes()
        if not backdrop_header.startswith(b'#pragma once\n#include "menu_backdrops.hpp"\n') or canvas_catalog is None:
            raise ValueError('Expected complete backdrop views and matching canvas catalog')
        sources['native_menu_backdrop_catalog.hpp']=backdrop_header
    if canvas_catalog is not None:
        canvas_header=Path(canvas_catalog).resolve(strict=True).read_bytes()
        if not canvas_header.startswith(b'#pragma once\n#include "canvas_catalog.hpp"\nnamespace sm64ds::render::prepared_canvases {\n'):
            raise ValueError('Expected the generated native canvas catalog')
        sources['native_canvas_catalog.hpp']=canvas_header
    if background_catalog is not None:
        background_header=Path(background_catalog).resolve(strict=True).read_bytes()
        if not canvas_catalog or not background_header.startswith(b'#pragma once\n#include "menu_backgrounds.hpp"\nnamespace sm64ds::render::game_hooks::prepared_backgrounds {\n'):
            raise ValueError('Expected matching prepared canvas and original-background catalogs')
        sources['native_background_catalog.hpp']=background_header
    destination=output/'port/platform/3ds/native_renderer'
    if destination.exists():raise ValueError('Native renderer destination must be new')
    if not cmake.resolve().is_relative_to(output) or not destination.resolve().is_relative_to(output):
        raise ValueError('Integration path escaped the generated checkout')
    names=sorted(name for name in sources if name.endswith(('.cpp','.S')))
    block=MARKER+'    enable_language(ASM)\n    target_sources(smoke_player PRIVATE\n'+''.join(
        '        platform/3ds/native_renderer/'+name+'\n' for name in names)+''')
    target_include_directories(smoke_player PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/native_renderer")
    set_property(SOURCE platform/3ds/native_renderer/game_model_pose.cpp APPEND PROPERTY COMPILE_DEFINITIONS SM64DS_NATIVE_AFFINE_ASM=1)
    target_compile_definitions(smoke_player PRIVATE SM64DS_NATIVE_CATALOG_HEADER="native_model_catalog.hpp")
    target_link_options(smoke_player PRIVATE
        "-Wl,--wrap=_ZN13SharedFilePtr4LoadEv"
        "-Wl,--wrap=_ZN13SharedFilePtr7ReleaseEv"
        "-Wl,--wrap=func_0201818c"
        "-Wl,--wrap=_ZN3ntr8io_writeEmyj"
        "-Wl,--wrap=_ZN3ntr7io_readEmj"
        "-Wl,--wrap=hidKeysHeld"
        "-Wl,--wrap=hidKeysDown"
        "-Wl,--wrap=hidTouchRead"
        "-Wl,--wrap=hidScanInput"
        "-Wl,--wrap=hidCircleRead"
        "-Wl,--wrap=hidCstickRead"
        "-Wl,--wrap=irrstCstickRead"
        "-Wl,--wrap=aptMainLoop"
        "-Wl,--wrap=_ZN5Model8LoadFileER13SharedFilePtr"
        "-Wl,--wrap=func_ov007_020b3f20"
        "-Wl,--wrap=func_ov007_020b3edc"
        "-Wl,--wrap=func_ov007_020ada70"
        "-Wl,--wrap=func_ov007_020c232c"
        "-Wl,--wrap=func_020462d0"
        "-Wl,--wrap=func_02046bbc"
        "-Wl,--wrap=_ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3")
    ctr_add_shader_library(sm64ds_fresh_native_shader platform/3ds/native_renderer/rigid.v.pica
        platform/3ds/native_renderer/affine.v.pica platform/3ds/native_renderer/batch.v.pica)
    dkp_add_embedded_binary_library(sm64ds_fresh_native_shader_bin sm64ds_fresh_native_shader)
    target_link_libraries(smoke_player PRIVATE sm64ds_fresh_native_shader_bin)
'''
    changes=runtime_patches(output) if install_runtime else {}
    if image_catalog is not None:
        block+='    target_compile_definitions(smoke_player PRIVATE SM64DS_NATIVE_IMAGE_CATALOG_HEADER="native_image_catalog.hpp")\n'
    if menu_catalog is not None:
        block+='    target_compile_definitions(smoke_player PRIVATE SM64DS_NATIVE_MENU_CATALOG_HEADER="native_menu_cell_catalog.hpp")\n'
    if menu_obj_catalog is not None:
        block+='    target_compile_definitions(smoke_player PRIVATE SM64DS_NATIVE_MENU_OBJ_CATALOG_HEADER="native_menu_obj_catalog.hpp")\n'
    if menu_backdrop_catalog is not None:
        block+='    target_compile_definitions(smoke_player PRIVATE SM64DS_NATIVE_MENU_BACKDROP_CATALOG_HEADER="native_menu_backdrop_catalog.hpp")\n'
    if canvas_catalog is not None:
        block+='    target_compile_definitions(smoke_player PRIVATE SM64DS_NATIVE_CANVAS_CATALOG_HEADER="native_canvas_catalog.hpp")\n'
    if background_catalog is not None:
        block+='    target_compile_definitions(smoke_player PRIVATE SM64DS_NATIVE_BACKGROUND_CATALOG_HEADER="native_background_catalog.hpp")\n'
    if any(not (output/name).resolve().is_relative_to(output) for name in changes):
        raise ValueError('Runtime source path escaped the generated checkout')
    if install_runtime:
        block+='''    set_source_files_properties(
        platform/3ds/native_renderer/game_runtime.cpp
        platform/3ds/native_renderer/game_graphics_guard.cpp
        platform/3ds/native_renderer/menu_sprite_hooks.cpp
        platform/3ds/native_renderer/particle_hooks.cpp
        platform/3ds/native_renderer/menu_oam_hooks.cpp
        platform/3ds/native_renderer/menu_display_state.cpp
        platform/3ds/native_renderer/menu_backgrounds.cpp
        platform/3ds/native_renderer/dialogue_hooks.cpp
        platform/3ds/native_renderer/ds_layer_census.cpp
        platform/3ds/native_renderer/game_native_math.cpp
        platform/3ds/native_renderer/test_mode.cpp
        platform/3ds/native_renderer/test_mode_game.cpp
        PROPERTIES COMPILE_DEFINITIONS SM64DS_FRESH_NATIVE_GAME=1)
    target_link_options(smoke_player PRIVATE
        "-Wl,--wrap=func_0201f32c"
        "-Wl,--wrap=func_0201eaac"
        "-Wl,--wrap=_ZN7Message7AddCharEc"
        "-Wl,--wrap=func_0201b388"
        "-Wl,--wrap=func_0201adfc"
        "-Wl,--wrap=LenVec3"
        "-Wl,--wrap=func_02053274"
        "-Wl,--wrap=_ZN4cstd4sqrtEy"
        "-Wl,--wrap=func_02053644"
        "-Wl,--wrap=func_02049018"
        "-Wl,--wrap=_ZN3ntr13gx_write_fifoEm"
        "-Wl,--wrap=_ZN3ntr13gx_write_portEmm"
        "-Wl,--wrap=_ZN3ntr19gx_write_fifo_blockEPKmi"
        "-Wl,--wrap=func_02055624"
        "-Wl,--wrap=func_020557b4"
        "-Wl,--wrap=func_020555a4"
        "-Wl,--wrap=func_ov007_020b2308"
        "-Wl,--wrap=func_ov007_020b22e0"
        "-Wl,--wrap=func_ov007_020c1620"
        "-Wl,--wrap=func_ov007_020c421c"
        "-Wl,--wrap=func_ov007_020ca5f0"
        "-Wl,--wrap=func_ov007_020c4fcc"
        "-Wl,--wrap=func_ov007_020c19cc"
        "-Wl,--wrap=func_ov007_020bd4e8"
        "-Wl,--wrap=func_ov007_020c9688"
        "-Wl,--wrap=func_ov007_020c121c"
        "-Wl,--wrap=func_ov007_020c0eb0"
        "-Wl,--wrap=func_ov007_020b7138"
        "-Wl,--wrap=func_ov007_020bcba8"
        "-Wl,--wrap=func_ov007_020bd648"
        "-Wl,--wrap=func_ov007_020b7384"
        "-Wl,--wrap=_ZN3G2x18SetBlendBrightnessEPVtts"
        "-Wl,--wrap=_ZN3G2x13SetBlendAlphaEPVttttj"
        "-Wl,--wrap=func_020551f0"
        "-Wl,--wrap=func_ov007_020c897c"
        "-Wl,--wrap=func_ov007_020c076c"
        "-Wl,--wrap=func_ov007_020c03b0"
        "-Wl,--wrap=func_ov007_020adc08"
        "-Wl,--wrap=func_ov007_020c0a9c"
        "-Wl,--wrap=func_ov007_020c0638"
        "-Wl,--wrap=func_ov007_020c0288"
        "-Wl,--wrap=func_ov007_020b1390"
        "-Wl,--wrap=func_0204a4c8"
        "-Wl,--wrap=func_0204a17c"
        "-Wl,--wrap=func_0204a5c8"
        "-Wl,--wrap=func_0204af3c"
        "-Wl,--wrap=func_0204be40"
        "-Wl,--wrap=func_0204bbd8"
        "-Wl,--wrap=func_ov007_020b7764"
        "-Wl,--wrap=_ZN8Particle10SysTrackerD1Ev"
        "-Wl,--wrap=func_020190b8"
        "-Wl,--wrap=_ZN3G3X13SetClearColorEtiiib"
        "-Wl,--wrap=_ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3")
    option(SM64DS_FRESH_NATIVE_WRITER_PROBE "Diagnostic source-writer provenance; never a performance build" OFF)
    if(SM64DS_FRESH_NATIVE_WRITER_PROBE)
        set_source_files_properties(platform/3ds/native_renderer/game_writer_probe.cpp
            PROPERTIES COMPILE_DEFINITIONS SM64DS_FRESH_NATIVE_WRITER_PROBE=1)
        target_link_options(smoke_player PRIVATE "-Wl,--wrap=_ZN3ntr8io_writeEmyj")
    endif()
'''
    patched=text.replace(ANCHOR,block+ANCHOR,1)
    # Finish contract/path/source validation before writing any output file.
    destination.mkdir(parents=True)
    for name,data in sources.items():
        path=destination/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    cmake.write_text(patched)
    for name,data in changes.items():(output/name).write_text(data)
    records={str((destination/name).relative_to(output)):hashlib.sha256(data).hexdigest() for name,data in sources.items()}
    records[str(cmake.relative_to(output))]=hashlib.sha256(patched.encode()).hexdigest()
    records.update({name:hashlib.sha256(data.encode()).hexdigest() for name,data in changes.items()})
    (output/'native-renderer-overlay.json').write_text(json.dumps(dict(scope='Fresh native opt-in runtime' if install_runtime else 'Fresh native build/link seams only',
        runtime_installed=install_runtime,activation_environment='SM64DS_FRESH_NATIVE=1' if install_runtime else None,
        game_renderer_migrated=False,files=records),indent=2)+'\n')
    return sorted(records)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--catalog',type=Path,required=True)
    parser.add_argument('--runtime',action='store_true',help='Install opt-in native scene/frame owner; incomplete producer coverage remains refused')
    parser.add_argument('--image-catalog',type=Path,help='Prepared native image-only catalog; required to activate the experimental game renderer')
    parser.add_argument('--menu-catalog',type=Path,help='Prepared typed menu cell bindings; required to activate the experimental game renderer')
    parser.add_argument('--menu-obj-catalog',type=Path,help='Complete native OBJ views selected by layout, engine and language')
    parser.add_argument('--menu-backdrop-catalog',type=Path,help='Complete static menu backdrop views; requires matching canvas catalog')
    parser.add_argument('--canvas-catalog',type=Path,help='Prepared native2D geometry; source background/font producers still require complete coverage')
    parser.add_argument('--background-catalog',type=Path,help='Reviewed original menu background bindings; requires matching prepared canvas catalog')
    args=parser.parse_args()
    print(json.dumps(prepare(args.source,args.catalog,args.runtime,args.image_catalog,args.menu_catalog,args.canvas_catalog,args.background_catalog,args.menu_obj_catalog,args.menu_backdrop_catalog),indent=2))
