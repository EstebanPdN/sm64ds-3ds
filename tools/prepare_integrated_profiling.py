"""Read-only timing/context probes in the disposable integrated source tree."""
from prepare_integrated_native_pause import GeneratedTree


def once(text,before,after):
    if text.count(before)!=1:raise ValueError('Profiling contract changed: '+before[:80])
    return text.replace(before,after,1)


def prepare(output):
    tree=GeneratedTree(output)
    name='port/platform/3ds/game_main.cpp';text=tree.read_text(name)
    text='#include "profile_probe.hpp"\n'+text
    text=once(text,'extern "C" void __wrap_func_02019390(){',
        'extern "C" void __wrap_func_02019390(){\n profile_probe::Scope update_scope(37,native_diagnostics::enabled());')
    text=once(text,' if(pipeline){bool done=false;',
        ' if(pipeline){profile_probe::Scope audio_scope(38,native_diagnostics::enabled());bool done=false;')
    text=once(text,' if(sound_pending){',
        ' if(sound_pending){\n  profile_probe::Scope audio_scope(38,native_diagnostics::enabled());')
    # BUILD134 (needs prepare_integrated_audio's deferral): submit the batch a
    # busy mixer deferred, and retry a busy maintenance on a later display
    # event instead of parking the game thread behind the mixer.
    text='#include <sys/stat.h>\n'+text
    text=once(text,' if(sound_pending){',' sm64ds_native_sound_flush_deferred();\n if(sound_pending){')
    text=once(text,'  const bool sound_ok=sm64ds_native_sound_maintenance();sm64ds_native_sound_retract();\n',
        '''  // BUILD134: the BUILD 133 console tour measured the game thread parked
  // 1.75 ms per update behind the mixer (which holds its lock 77% of the
  // time on the Old 3DS core-1 quota). Maintenance only frees finished
  // players (a start or stop collects for itself), so a busy lock is tried
  // again on a later display event; it blocks only after a long run of
  // misses. sdmc:/3ds/sm64ds/audio-espera.txt restores the blocking call.
  static const bool blocking=[](){struct stat st;return stat("sdmc:/3ds/sm64ds/audio-espera.txt",&st)==0;}();
  static unsigned misses=0;bool done=false;bool sound_ok=true;
  if(!blocking&&misses<16){sound_ok=sm64ds_native_sound_try_maintenance(done);if(done)misses=0;else ++misses;}
  else{sound_ok=sm64ds_native_sound_maintenance();sm64ds_native_sound_retract();misses=0;}
''')
    text=once(text,' observed_cadence::after_frame(frame);',
        ' {profile_probe::Scope cadence_scope(39,native_diagnostics::enabled());observed_cadence::after_frame(frame);}')
    text=once(text,' native_motion::frame(current_scene,updates);',
        ' {profile_probe::Scope motion_scope(40,native_diagnostics::enabled());native_motion::frame(current_scene,updates);}')
    text=once(text,'native_benchmark::frame(current_scene,updates,native_diagnostics::enabled(),benchmark_new,svcGetSystemTick())',
        '(native_diagnostics::enabled() && native_benchmark::frame(current_scene,updates,true,benchmark_new,svcGetSystemTick()))')
    before='''extern "C" unsigned sm64ds_profile_context(){return 0x534d0000u |
 unsigned(current_scene==3 && data_0209f2c4!=0) |
 (unsigned(current_scene==3 && data_0209d660!=0)<<1) |
 (unsigned(current_scene==3 && data_0209fc48!=0)<<2);}'''
    after='''extern "C" unsigned sm64ds_profile_context(){
 const unsigned flags=unsigned(current_scene==3 && data_0209f2c4!=0) |
  (unsigned(current_scene==3 && data_0209d660!=0)<<1) |
  (unsigned(current_scene==3 && data_0209fc48!=0)<<2);
 const bool valid=current_scene==3 && data_0209f318;
 unsigned short yaw=0;int look_y=0,pos_y=0;
 if(valid){const auto* c=reinterpret_cast<const unsigned char*>(data_0209f318);
  std::memcpy(&yaw,c+0x17c,2);std::memcpy(&look_y,c+0x84,4);std::memcpy(&pos_y,c+0x90,4);}
 return profile_probe::context(flags,valid,yaw,(pos_y>look_y)-(pos_y<look_y));
}'''
    text=once(text,before,after)
    text=once(text,' if(list==data_020a4b98){',
        ' if(list==data_020a4b98){\n  profile_probe::Scope render_scope(46,native_diagnostics::enabled());')
    text=once(text,'extern "C" void* __wrap_func_02043fdc(void* list){',
        'extern "C" void* __wrap_func_02043fdc(void* list){\n'
        ' profile_probe::Scope actor_scope(47,native_diagnostics::enabled() && list!=data_020a4b98);')
    tree.write_text(name,text)
    name='port/platform/3ds/player_present.cpp';text=tree.read_text(name)
    text='#include "profile_probe.hpp"\n'+text
    text=once(text,'const uint64_t t0=svcGetSystemTick();audio_ok=audio_pump();audio_ticks+=svcGetSystemTick()-t0;',
        'const uint64_t t0=svcGetSystemTick();audio_ok=audio_pump();const uint64_t elapsed=svcGetSystemTick()-t0;audio_ticks+=elapsed;\n'
        '            if(native_diagnostics::enabled())profile_probe::audio_ticks.fetch_add(uint32_t(elapsed),std::memory_order_relaxed);')
    tree.write_text(name,text)
    tree.commit()
    return ['port/platform/3ds/game_main.cpp','port/platform/3ds/player_present.cpp']
