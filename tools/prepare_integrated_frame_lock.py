"""Display > Lock FPS in the disposable integrated source tree.

Presentation cadence only. The original game keeps its 30 Hz update clock in
every mode; observed_cadence pacing and frame-skip debt are unchanged.
  60  Render60 midpoint plus exact image, Citro3D LCD synchronization.
  30  exact image once per update; the held VBlank skips GPU work.
  Off midpoints without waiting for the LCD (latest image wins).
"""
from prepare_integrated_native_pause import GeneratedTree
from prepare_integrated_profiling import once


def prepare(output):
    tree=GeneratedTree(output)
    name='port/platform/3ds/player_present.cpp';text=tree.read_text(name)
    text=once(text,'    job.mesh=compiled?render60::interpolation.select(',
        '    render60::interpolation.set_enabled(enhance::live().fps_lock.load(std::memory_order_relaxed)!=enhance::kFpsLock30);\n'
        '    job.mesh=compiled?render60::interpolation.select(')
    tree.write_text(name,text)
    name='port/platform/3ds/viewer_3ds.cpp';text=tree.read_text(name)
    # Read once per presentation from the live settings instead of scanning
    # the environment three times per frame.
    text=once(text,'static display_mode::Plan g_display=display_mode::plan(0,0);',
        'static display_mode::Plan g_display=display_mode::plan(0,0);\n'
        'static bool present_unlocked(){return enhance::live().fps_lock.load(std::memory_order_relaxed)==enhance::kFpsLockOff;}')
    old='std::getenv("PLAYER_RESIDENT_NO_VBLANK_WAIT")'
    if text.count(old)!=3:raise ValueError('Frame lock contract changed: LCD wait sites')
    text=text.replace(old,'present_unlocked()')
    tree.write_text(name,text)
    tree.commit()
    return ['port/platform/3ds/player_present.cpp','port/platform/3ds/viewer_3ds.cpp']
