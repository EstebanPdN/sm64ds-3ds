"""Remove evidenced idle work without changing file/load or game contracts."""
from prepare_integrated_native_pause import GeneratedTree
from prepare_integrated_profiling import once


def derive_fs(text):
    text='''#ifdef __3DS__
extern "C" void sm64ds_prefetch_wait_init();
extern "C" void sm64ds_prefetch_notify();
extern "C" void sm64ds_prefetch_wait_begin();
extern "C" void sm64ds_prefetch_wait();
#else
static inline void sm64ds_prefetch_notify(){}
static inline void sm64ds_prefetch_wait_begin(){}
static inline void sm64ds_prefetch_wait(){}
#endif
'''+text
    text=once(text,'extern "C" void sm64ds_fs_prefetch_scene(unsigned scene) { __atomic_store_n(&g_prefetch_scene, scene, __ATOMIC_RELEASE); }',
        'extern "C" void sm64ds_fs_prefetch_scene(unsigned scene) { __atomic_store_n(&g_prefetch_scene, scene, __ATOMIC_RELEASE); sm64ds_prefetch_notify(); }')
    # The original loop clears all yield predicates once the player pass
    # finishes. It then reads every MG file even inside an adventure level.
    start=text.index('    player.yield = prefetch_minigame_side;\n    prefetch_run(menu);')
    end=text.index('\n}\nextern "C" void *threadCreate',start)
    text=text[:start]+'''    player.yield = prefetch_minigame_side;
    menu.yield = ui.yield = rest.yield = [](){return !prefetch_minigame_side();};
    while (!__atomic_load_n(&g_prefetch_stop, __ATOMIC_ACQUIRE)) {
        // Clear before observing the scene: a concurrent scene notification
        // remains sticky through the following work/check and cannot be lost.
        sm64ds_prefetch_wait_begin();
        if (g_prefetch_count >= PREFETCH_MAX) break;
        if (prefetch_minigame_side()) {
            prefetch_run(menu);prefetch_run(ui);prefetch_run(rest);
        } else {
            prefetch_run(player);
        }
        if (player.done && menu.done && ui.done && rest.done) break;
        if (__atomic_load_n(&g_prefetch_stop, __ATOMIC_ACQUIRE)) break;
        // No scene-specific work remains. Park until scene change or exit;
        // no polling, speculative file opens or repeated catalogue scans.
        sm64ds_prefetch_wait();
    }'''+text[end:]
    text=once(text,'__atomic_store_n(&g_prefetch_stop, true, __ATOMIC_RELEASE);',
        '__atomic_store_n(&g_prefetch_stop, true, __ATOMIC_RELEASE);\n    sm64ds_prefetch_notify();')
    text=once(text,'g_prefetch_thread = threadCreate(prefetch_thread, nullptr, 64 * 1024, 0x3f, 1, false);',
        'sm64ds_prefetch_wait_init();\n    g_prefetch_thread = threadCreate(prefetch_thread, nullptr, 64 * 1024, 0x3f, 1, false);')
    return text


def prepare(output):
    tree=GeneratedTree(output)
    name='port/hal/fs.cpp';tree.write_text(name,derive_fs(tree.read_text(name)))
    name='port/CMakeLists.txt';text=tree.read_text(name)
    text+='\nif(NINTENDO_3DS)\n  target_sources(ntr PRIVATE platform/3ds/fs_prefetch_wait.cpp)\nendif()\n'
    tree.write_text(name,text)
    name='port/platform/3ds/game_entry.cpp';text=tree.read_text(name)
    text=once(text,' setenv("SM64DS_GAME_TRACE_INPUT","1",1);',
        ' // Input telemetry is enabled after saved Developer settings load.')
    text=once(text,' enhance::boot_apply();',
        ' enhance::boot_apply();\n if(enhance::live().developer_dumps.load(std::memory_order_relaxed))setenv("SM64DS_GAME_TRACE_INPUT","1",1);')
    tree.write_text(name,text)
    name='port/platform/3ds/game_main.cpp';text=tree.read_text(name)
    text=once(text,' if(native_diagnostics::enabled() && current_scene==1 && frame>=900 && frame<1800){',
        ' static bool title_capture_checked=false;\n'
        ' if(!title_capture_checked && native_diagnostics::enabled() && current_scene==1 && frame>=900 && frame<1800){\n'
        '  title_capture_checked=true;')
    tree.write_text(name,text)
    tree.commit()
    return ['port/hal/fs.cpp','port/CMakeLists.txt','port/platform/3ds/game_entry.cpp',
            'port/platform/3ds/game_main.cpp']
