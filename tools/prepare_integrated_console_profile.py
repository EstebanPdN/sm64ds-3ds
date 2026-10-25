"""Old / New 3DS profiles in the disposable integrated source tree.

console_profile.hpp turns the console model and the memory the system granted
into budgets; this wires them in:
  * GPU texture cache (viewer) and decoded DS texture cache (ntr/gx.cpp) take
    their size from the profile (the star select preload too, see
    prepare_integrated_loading.py);
  * the presentation worker runs on core 2 of a New 3DS with the PPU helper on
    core 1 (Old: both on core 1), falling back to core 1 when core 2 is
    unavailable (a 3DSX launched without core 2 access).
The exheader (packaging/sm64ds-integrated.rsf) asks for the New 3DS 124 MB
mode, 804 MHz, L2 cache and core 2; an Old 3DS keeps its 80 MB mode.
"""
from prepare_integrated_native_pause import GeneratedTree
from prepare_integrated_profiling import once


def prepare(output):
    tree = GeneratedTree(output)
    changed = []

    name = 'port/CMakeLists.txt'
    text = tree.read_text(name)
    text = once(text, 'target_sources(ntr PRIVATE platform/3ds/fs_prefetch_wait.cpp)',
                'target_sources(ntr PRIVATE platform/3ds/fs_prefetch_wait.cpp platform/3ds/console_profile.cpp)')
    tree.write_text(name, text)
    changed.append(name)

    name = 'port/platform/3ds/viewer_3ds.cpp'
    text = tree.read_text(name)
    text = once(text, 'static constexpr size_t texture_budget=8*1024*1024;\n',
                '// Old/New 3DS budget (console_profile.hpp).\n'
                'static size_t texture_budget(){static const size_t bytes=console_profile::current().gpu_textures;return bytes;}\n')
    text = once(text, '    while(all_unused || g_texture_bytes+needed>texture_budget){',
                '    while(all_unused || g_texture_bytes+needed>texture_budget()){')
    text = once(text, '#include "native_lower_state.hpp"\n', '#include "native_lower_state.hpp"\n#include "console_profile.hpp"\n')
    tree.write_text(name, text)
    changed.append(name)

    name = 'port/ntr/gx.cpp'
    text = tree.read_text(name)
    text = once(text, 'constexpr size_t texture_cache_budget=8*1024*1024;\n',
                '// Old/New 3DS budget: platform/3ds/console_profile.cpp on the console,\n'
                '// 8 MiB in host builds.\n'
                'extern "C" __attribute__((weak)) size_t sm64ds_profile_decoded_texture_budget(){return 8*1024*1024;}\n'
                'static size_t texture_cache_budget(){static const size_t bytes=sm64ds_profile_decoded_texture_budget();return bytes;}\n')
    text = once(text, 'g_texture_bytes>texture_cache_budget &&', 'g_texture_bytes>texture_cache_budget() &&')
    tree.write_text(name, text)
    changed.append(name)

    name = 'port/platform/3ds/player_present.cpp'
    text = tree.read_text(name)
    text = once(text, '''    // Worker core (hardware bring-up): SM64DS_PIPELINE_CORE, default 1.
    int core=1;
    if(const char* c=std::getenv("SM64DS_PIPELINE_CORE"))core=std::atoi(c);
    if(core==1){''', '''    // Worker core: New 3DS core 2 with the PPU helper on core 1, Old 3DS
    // both on core 1 (console_profile.hpp); SM64DS_PIPELINE_CORE overrides.
    int core=console_profile::current().worker_core;
    if(const char* c=std::getenv("SM64DS_PIPELINE_CORE"))core=std::atoi(c);
    int helper_core=core==2?1:core;
    if(core==1 || helper_core==1){''')
    text = once(text, '''    worker=threadCreate(main_loop,nullptr,64*1024,priority-1,core,false);
''', '''    worker=threadCreate(main_loop,nullptr,64*1024,priority-1,core,false);
    if(!worker && core!=1){
        // No core 2 access (a 3DSX started without it): the Old layout.
        std::printf("Present pipeline: core %d unavailable, worker on core 1\\n",core);
        core=helper_core=1;
        worker=threadCreate(main_loop,nullptr,64*1024,priority-1,core,false);
    }
''')
    text = once(text, '        helper=threadCreate(helper_loop,nullptr,64*1024,priority-1,core,false);',
                '        helper=threadCreate(helper_loop,nullptr,64*1024,priority-1,helper_core,false);')
    text = once(text, '    std::printf("Present pipeline core%d worker=%u asyncAudio=%u\\n",core,unsigned(enabled),unsigned(asynchronous_audio));',
                '    std::printf("Present pipeline core%d helperCore%d worker=%u helper=%u asyncAudio=%u\\n",core,helper_core,unsigned(enabled),unsigned(helper!=nullptr),unsigned(asynchronous_audio));')
    text = once(text, '#include "profile_probe.hpp"\n', '#include "profile_probe.hpp"\n#include "console_profile.hpp"\n')
    tree.write_text(name, text)
    changed.append(name)
    tree.commit()
    return changed
