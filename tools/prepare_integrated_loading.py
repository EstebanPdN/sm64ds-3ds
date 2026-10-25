"""Level loading in the disposable integrated source tree.

Star select preload: while the star select plays, the prefetch thread reads
the chosen level's files into RAM (the files that level read the last time it
loaded, or the built-in list); the level load takes them from there and the
memory is released once the level has loaded (hal/fs.cpp, level_files.hpp).

Load report: every game frame longer than 250 ms (a level or area load) logs
its duration with the file costs inside it, so console logs show where a
loading stall goes (SD opens and reads, decompression, everything else).
"""
from prepare_integrated_native_pause import GeneratedTree
from prepare_integrated_profiling import once

# hal/fs.cpp, after prefetch_find(): the level cache, its recording and the
# prefetch-thread job. 3DS only (the lock lives in fs_prefetch_wait.cpp and
# the budget in console_profile.cpp, which may include libctru).
LEVEL_CACHE = r'''
// ---- star select preload ---------------------------------------------------
// A level load read ~75 files from the SD (Bob-omb Battlefield: 1.2 MB, about
// 10 s on the console before the package handle was shared). While the star
// select plays, the prefetch thread reads the chosen level's files into RAM;
// the load copies them from here and the cache is freed once the level has
// loaded. Each load records the files it read (level_files.hpp), kept in
// sdmc:/3ds/sm64ds/level-files.bin for the next visit.
#ifdef __3DS__
#include "../platform/3ds/level_files_seed.hpp"
extern "C" void sm64ds_level_lock();
extern "C" void sm64ds_level_unlock();
extern "C" size_t sm64ds_profile_level_preload_budget(); // console_profile.cpp: Old/New 3DS
enum { LEVEL_FILE_MAX = 256 * 1024 };
struct LevelFile { u16 id; u32 size; u8 *data; };
static LevelFile g_level_cache[level_files::per_level]; // level lock
static unsigned g_level_cache_count, g_level_cache_bytes;  // level lock
static int g_level_cache_level = -1;                        // level lock
static volatile int g_level_request = -1; // star select: level to read (game thread)
static volatile int g_level_loading = -1; // a level load runs: stop reading
static volatile int g_level_filled = -1;  // last request the thread finished
static u16 g_record[level_files::per_level]; static unsigned g_record_count; static int g_record_level = -1; // game thread
static unsigned g_level_hits;                                                                          // game thread
static u16 g_pending[level_files::per_level]; static unsigned g_pending_count; static volatile int g_pending_level = -1; // level lock
static level_files::Store *g_level_store; // prefetch thread
static const char *const LEVEL_STORE = "sdmc:/3ds/sm64ds/level-files.bin";

static void level_record(u32 id)
{
    if (g_record_level < 0 || id >= MAX_FILES) return;
    for (unsigned i = 0; i < g_record_count; ++i) if (g_record[i] == id) return;
    if (g_record_count < level_files::per_level) g_record[g_record_count++] = (u16)id;
}
static void level_cache_free_locked(void)
{
    for (unsigned i = 0; i < g_level_cache_count; ++i) free(g_level_cache[i].data);
    g_level_cache_count = g_level_cache_bytes = 0; g_level_cache_level = -1;
}
// Game thread: a fresh copy of a preloaded file.
static bool level_cache_take(u32 id, u8 *&raw, long &size)
{
    bool hit = false;
    sm64ds_level_lock();
    for (unsigned i = 0; i < g_level_cache_count; ++i) if (g_level_cache[i].id == id) {
        const LevelFile &f = g_level_cache[i];
        raw = (u8 *)malloc(f.size ? f.size : 1);
        if (raw) { memcpy(raw, f.data, f.size); size = (long)f.size; hit = true; ++g_level_hits; }
        break;
    }
    sm64ds_level_unlock();
    return hit;
}
static bool level_job_pending(void)
{
    if (g_pending_level >= 0) return true;
    const int request = g_level_request;
    return request >= 0 && request != g_level_filled && g_level_loading < 0;
}
static void level_store_ready(void)
{
    if (g_level_store) return;
    g_level_store = (level_files::Store *)malloc(sizeof *g_level_store);
    if (!g_level_store) return;
    FILE *f = fopen(LEVEL_STORE, "rb");
    const bool read = f && fread(g_level_store, 1, sizeof *g_level_store, f) == sizeof *g_level_store;
    if (f) fclose(f);
    if (!read || !g_level_store->valid(MAX_FILES)) g_level_store->reset(MAX_FILES);
    g_level_store->seed(level_files::seeds, level_files::seed_count);
}
static void level_store_save(void)
{
    static const char *const temporary = "sdmc:/3ds/sm64ds/level-files.tmp";
    FILE *f = fopen(temporary, "wb");
    if (!f) return;
    const bool written = fwrite(g_level_store, 1, sizeof *g_level_store, f) == sizeof *g_level_store;
    if (fclose(f) != 0 || !written) { remove(temporary); return; }
    remove(LEVEL_STORE);
    rename(temporary, LEVEL_STORE);
}
// Prefetch thread: keep a recorded list, then read the requested level.
static void level_jobs(void)
{
    if (g_pending_level >= 0) {
        u16 ids[level_files::per_level]; unsigned n = 0; int level = -1;
        sm64ds_level_lock();
        if (g_pending_level >= 0) { level = g_pending_level; n = g_pending_count; memcpy(ids, g_pending, n * sizeof *ids); g_pending_level = -1; }
        sm64ds_level_unlock();
        level_store_ready();
        if (level >= 0 && g_level_store && g_level_store->merge((unsigned)level, ids, n)) level_store_save();
    }
    const int level = g_level_request;
    if (level < 0 || level == g_level_filled || g_level_loading >= 0) return;
    level_store_ready();
    unsigned n = 0;
    const u16 *ids = g_level_store ? g_level_store->list((unsigned)level, n) : nullptr;
    sm64ds_level_lock();
    if (g_level_cache_level != level) { level_cache_free_locked(); g_level_cache_level = level; }
    sm64ds_level_unlock();
    static const u32 budget = (u32)sm64ds_profile_level_preload_budget();
    const unsigned long long t0 = fs_ticks();
    unsigned files = 0, bytes = 0; bool stopped = false;
    char path[PATH_MAX_ * 2];
    for (unsigned i = 0; i < n; ++i) {
        if (__atomic_load_n(&g_prefetch_stop, __ATOMIC_ACQUIRE) || g_level_request != level || g_level_loading >= 0) { stopped = true; break; }
        const u32 id = ids[i];
        if (id >= MAX_FILES || !g_paths[id][0] || prefetch_find(id)) continue;
        bool cached = false;
        sm64ds_level_lock();
        for (unsigned k = 0; k < g_level_cache_count && !cached; ++k) cached = g_level_cache[k].id == id;
        sm64ds_level_unlock();
        if (cached) continue;
        snprintf(path, sizeof path, "%s/extracted/dsd/files/%s", asset_root(), g_paths[id]);
        const unsigned long long t1 = fs_ticks();
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        long size = -1;
        if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
        u8 *data = nullptr;
        if (size > 0 && size <= LEVEL_FILE_MAX && g_level_cache_bytes + (u32)size <= budget && fseek(f, 0, SEEK_SET) == 0) {
            data = (u8 *)malloc((size_t)size);
            if (data && fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); data = nullptr; }
        }
        fclose(f);
        sm64ds_fs_stats[4] += 1; sm64ds_fs_stats[6] += fs_ticks() - t1;
        if (!data) continue;
        sm64ds_fs_stats[5] += (unsigned long long)size;
        sm64ds_level_lock();
        if (g_level_cache_level == level && g_level_cache_count < level_files::per_level) {
            g_level_cache[g_level_cache_count++] = {(u16)id, (u32)size, data};
            g_level_cache_bytes += (u32)size; ++files; bytes += (u32)size; data = nullptr;
        }
        sm64ds_level_unlock();
        free(data);
    }
    g_level_filled = level;
    printf("Level preload level=%d listed=%u files=%u KiB=%u ms=%u stopped=%u\n", level, n, files, bytes / 1024,
           unsigned((fs_ticks() - t0) / 268111ull), unsigned(stopped));
}
// Game thread API (game_main.cpp scene transitions).
extern "C" void sm64ds_fs_level_select(int level)
{
    g_level_request = level;
    sm64ds_prefetch_notify();
}
extern "C" void sm64ds_fs_level_begin(int level)
{
    g_level_loading = level;
    g_record_level = level; g_record_count = 0; g_level_hits = 0;
}
extern "C" void sm64ds_fs_level_end(void)
{
    if (g_record_level < 0) return;
    sm64ds_level_lock();
    level_cache_free_locked();
    memcpy(g_pending, g_record, g_record_count * sizeof *g_record);
    g_pending_count = g_record_count; g_pending_level = g_record_level;
    sm64ds_level_unlock();
    char ids[level_files::per_level * 6 + 1]; unsigned at = 0; ids[0] = 0;
    for (unsigned i = 0; i < g_record_count && at + 7 < sizeof ids; ++i)
        at += (unsigned)snprintf(ids + at, sizeof ids - at, i ? ",%u" : "%u", unsigned(g_record[i]));
    printf("Level files level=%d loaded=%u preloaded=%u ids=%s\n", g_record_level, g_record_count, g_level_hits, ids);
    g_record_level = -1; g_level_loading = -1; g_level_request = -1; g_level_filled = -1;
    sm64ds_prefetch_notify();
}
#else
static inline void level_record(u32) {}
static inline bool level_cache_take(u32, u8 *&, long &) { return false; }
static inline bool level_job_pending(void) { return false; }
static inline void level_jobs(void) {}
#endif
'''

# hal/fs.cpp, before FS_InitFile: FSFile opens copy small preloaded files
# into the ROM cache, which owns them while they are open.
LEVEL_ROM = r'''#ifdef __3DS__
static int level_cache_to_rom(u32 id)
{
    int slot = -1;
    sm64ds_level_lock();
    for (unsigned i = 0; i < g_level_cache_count; ++i) if (g_level_cache[i].id == id) {
        if (g_level_cache[i].size <= ROM_CACHE_MAX_FILE) {
            slot = rom_cache_insert_bytes(id, g_level_cache[i].data, g_level_cache[i].size);
            if (slot >= 0) ++g_level_hits;
        }
        break;
    }
    sm64ds_level_unlock();
    return slot;
}
#else
static inline int level_cache_to_rom(u32) { return -1; }
#endif
'''


def prepare(output):
    tree = GeneratedTree(output)
    changed = []
    name = 'port/hal/fs.cpp'
    text = tree.read_text(name)
    # Forward declarations: the prefetch thread precedes the level cache.
    text = once(text, 'static volatile u8 g_prefetch_tried[MAX_FILES];',
                'static bool level_job_pending(void);\nstatic void level_jobs(void);\n'
                'static volatile u8 g_prefetch_tried[MAX_FILES];')
    # Passes yield to a level request; the thread stays parked, never exits.
    text = once(text, '''        if (__atomic_load_n(&g_prefetch_stop, __ATOMIC_ACQUIRE)) return false;
        if (!g_paths[id][0] || g_prefetch_tried[id] || !pass.wanted(g_paths[id])) continue;''',
                '''        if (__atomic_load_n(&g_prefetch_stop, __ATOMIC_ACQUIRE) || level_job_pending()) return false;
        if (!g_paths[id][0] || g_prefetch_tried[id] || !pass.wanted(g_paths[id])) continue;''')
    text = once(text, '''        sm64ds_prefetch_wait_begin();
        if (g_prefetch_count >= PREFETCH_MAX) break;
        if (prefetch_minigame_side()) {
            prefetch_run(menu);prefetch_run(ui);prefetch_run(rest);
        } else {
            prefetch_run(player);
        }
        if (player.done && menu.done && ui.done && rest.done) break;
        if (__atomic_load_n(&g_prefetch_stop, __ATOMIC_ACQUIRE)) break;''', '''        sm64ds_prefetch_wait_begin();
        level_jobs(); // star select preload first
        if (g_prefetch_count < PREFETCH_MAX) {
            if (prefetch_minigame_side()) {
                prefetch_run(menu);prefetch_run(ui);prefetch_run(rest);
            } else {
                prefetch_run(player);
            }
        }
        if (__atomic_load_n(&g_prefetch_stop, __ATOMIC_ACQUIRE)) break;
        if (level_job_pending()) continue;''')
    text = once(text, '''    return n ? &g_prefetch[n - 1] : 0;
}
''', '''    return n ? &g_prefetch[n - 1] : 0;
}
''' + LEVEL_CACHE)
    # SharedFilePtr::Load: preloaded files, and every load is recorded.
    text = once(text, '''    if(!raw && pristine_enabled()){
        std::size_t size=0;raw=pristine_cache.copy_hit(self->fileID,path,size);''', '''    if(!raw && self->fileID<0x8000)level_cache_take(self->fileID,raw,fsize);
    if(!raw && pristine_enabled()){
        std::size_t size=0;raw=pristine_cache.copy_hit(self->fileID,path,size);''')
    text = once(text, '''    if (fsize > 8 && memcmp(raw, "LZ77", 4) == 0) {''',
                '''    if (self->fileID < 0x8000 && !prefetch_find(self->fileID)) level_record(self->fileID);
    if (fsize > 8 && memcmp(raw, "LZ77", 4) == 0) {''')
    # FSFile opens of ROM files: recorded, and served from the level cache.
    text = once(text, '''    if (rom_cache_enabled()) { const int slot=rom_cache_find(id.file); if (slot>=0) { sm64ds_fs_stats[3]+=1; return open_cached(slot); } }''',
                '''    if (rom_cache_enabled()) { const int slot=rom_cache_find(id.file); if (slot>=0) { level_record(id.file); sm64ds_fs_stats[3]+=1; return open_cached(slot); } }''')
    text = once(text, '''    const unsigned long long sd_t0=fs_ticks();
    FILE *f = fopen(path, "rb");''', '''    level_record(id.file);
    if (rom_cache_enabled()) { const int slot=level_cache_to_rom(id.file); if (slot>=0) { sm64ds_fs_stats[3]+=1; return open_cached(slot); } }
    const unsigned long long sd_t0=fs_ticks();
    FILE *f = fopen(path, "rb");''')
    text = once(text, 'extern "C" void FS_InitFile(void *self)\n', LEVEL_ROM + 'extern "C" void FS_InitFile(void *self)\n')
    tree.write_text(name, text)
    changed.append(name)

    name = 'port/platform/3ds/game_main.cpp'
    text = tree.read_text(name)
    text = once(text, ''' if(native_diagnostics::verbose() && frame%120==0)std::printf("Game vblank=%llu updates=%u scene=%u\\n",(unsigned long long)frame,updates,current_scene);
 return true;
}''', ''' {   // Load report: frames over 250 ms with the file work inside them.
  static uint64_t last_tick=0,last_load=0;static unsigned long long last_fs[8]{};
  const uint64_t now=svcGetSystemTick(),load=sm64ds_frame_prof[21];
  unsigned long long fs[8];for(unsigned i=0;i<8;++i)fs[i]=sm64ds_fs_stats[i];
  if(last_tick && now-last_tick>268111856ull/4){
   const auto ms=[](uint64_t t){return (unsigned long long)(t/268111ull);};
   std::printf("Long frame ms=%llu scene=%u sdOpens=%llu sdKiB=%llu sdMs=%llu ramHits=%llu loadMs=%llu prefetchOpens=%llu\\n",
    ms(now-last_tick),current_scene,fs[0]-last_fs[0],(fs[1]-last_fs[1])/1024,ms(fs[2]-last_fs[2]),fs[3]-last_fs[3],
    ms(load>=last_load?load-last_load:load),fs[4]-last_fs[4]);
  }
  last_tick=now;last_load=load;for(unsigned i=0;i<8;++i)last_fs[i]=fs[i];
 }
 if(native_diagnostics::verbose() && frame%120==0)std::printf("Game vblank=%llu updates=%u scene=%u\\n",(unsigned long long)frame,updates,current_scene);
 // Renderer path report (diagnostics): compiled BMD admissions/refusals and
 // the native mesh totals, every 20 s, so a scene stuck at 30 Hz shows why.
 if(native_diagnostics::verbose() && frame%1200==600){bmd_compiled_stats();native_mesh_print_stats();player_present_pipeline_stats();}
 return true;
}''')
    hint = 'extern "C" void sm64ds_fs_prefetch_scene(unsigned scene); // hal/fs.cpp: background prefetch order hint\n'
    text = once(text, hint, hint + 'extern "C" volatile unsigned long long sm64ds_fs_stats[8]; // hal/fs.cpp file access counters\n'
                'extern "C" volatile uint64_t sm64ds_frame_prof[48];\n'
                '// hal/fs.cpp star select preload: chosen level, its load, load settled.\n'
                'extern "C" void sm64ds_fs_level_select(int level);\n'
                'extern "C" void sm64ds_fs_level_begin(int level);\n'
                'extern "C" void sm64ds_fs_level_end(void);\n'
                'extern "C" void bmd_compiled_stats();   // ntr/bmd_sdk_bridge.inc\n'
                'extern "C" void native_mesh_print_stats(); // viewer_3ds.cpp\n'
                'void player_present_pipeline_stats();      // player_present.cpp\n'
                'static unsigned level_load_updates; // updates since a level load began (0: none)\n')
    # Star select (4): read the chosen level; stage (3): record its load for
    # three seconds of updates (objects spawn and load after the first frame).
    text = once(text, '''  current_scene=next;
  sm64ds_fs_prefetch_scene(next);
''', '''  current_scene=next;
  sm64ds_fs_prefetch_scene(next);
  if(level_load_updates){level_load_updates=0;sm64ds_fs_level_end();}
  if(next==4)sm64ds_fs_level_select(data_02092110);
  if(next==3){sm64ds_fs_level_begin(data_02092110);level_load_updates=1;}
''')
    text = once(text, ' ++updates;__real_func_02019390();',
                ' if(level_load_updates && ++level_load_updates>90){level_load_updates=0;sm64ds_fs_level_end();}\n'
                ' ++updates;__real_func_02019390();')
    tree.write_text(name, text)
    changed.append(name)
    tree.commit()
    return changed
