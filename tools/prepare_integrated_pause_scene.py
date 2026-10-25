"""Hold only Stage 3D geometry during pause; retain both original 2D screens."""
from prepare_integrated_native_pause import GeneratedTree

def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Pause presentation contract changed: '+old[:90])
    return text.replace(old,new,1)

def prepare(output):
    tree=GeneratedTree(output)
    output=tree.root
    path=output/'port/platform/3ds/viewer_3ds.cpp';s=tree.read_text(path.relative_to(output))
    if 'g_pause_held_valid' in s:raise ValueError('Pause presentation already prepared')
    s='#include "native_diagnostics.hpp"\n'+s
    s=once(s,'static bool g_resident_ready;', '''static bool g_resident_ready;
// Render-worker-owned geometry validity. Game state arrives atomically.
// Geometry targets are held in place; PPU/HUD/OBJ and both LCD panels stay live.
static bool g_pause_held_valid=false,g_pause_held_stereo=false;
static bool g_pause_hold_frame=false;''')
    s=once(s,'static bool resident_destroy(){', '''static bool resident_destroy(){
    g_pause_held_valid=false;g_pause_held_stereo=false;g_pause_hold_frame=false;''')
    s=once(s,'    const bool menu=g_menu_presentation.load(std::memory_order_relaxed);', '''    const bool menu=g_menu_presentation.load(std::memory_order_relaxed);
    const bool pause_requested=enhance::live().pause_scene.load(std::memory_order_acquire);
    if(menu)g_pause_held_valid=false;
    const bool pause_hold=pause_requested && !menu && g_resident_ready && g_pause_held_valid;''')
    eyes='    const unsigned wanted_eyes=display_mode::eyes(wanted,stereo,a_upper,plan.delay_3d,stereo_slider());'
    if eyes in s:
        s=once(s,eyes,'''    // A layout rebuild would delete the held geometry; apply changes on resume.
    if(pause_hold)wanted=g_display;
    const unsigned wanted_eyes=pause_hold?g_display_eyes:display_mode::eyes(wanted,stereo,a_upper,plan.delay_3d,stereo_slider());''')
    else:
        s=once(s,'    static uint32_t failed_generation=~0u;','    if(pause_hold)wanted=g_display;\n    static uint32_t failed_generation=~0u;')
        s=once(s,'    if(wanted.native && generation==failed_generation)','    if(!pause_hold && wanted.native && generation==failed_generation)')
    # Keep gpu_frame's real FrameBegin, pacing, direct-token handling and queue
    # ownership. Return only after those obligations and before texture upload,
    # target clearing or any draw that could overwrite held geometry.
    anchor='    // Reuse only resident geometry. Current OBJ/PPU/LCD work still follows.'
    s=once(s,anchor,'''#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    if(resident && g_pause_hold_frame){
        g_native_stereo=g_pause_held_stereo;
        gfxSet3D(g_native_stereo);
        return true;
    }
    // Any new resident draw replaces these targets, even if later texture or
    // composition work fails. A previous successful frame no longer owns them.
    if(resident)g_pause_held_valid=false;
#endif
'''+anchor)
    s=once(s,'    if(!gpu_frame(v,nv,b,nb,"Native scene","Resident candidate",1.f,convergence,perspective,',
       '    g_pause_hold_frame=pause_hold;\n'
       '    if(!gpu_frame(v,nv,b,nb,"Native scene","Resident candidate",1.f,convergence,perspective,')
    s=once(s,'    const auto upper=a_upper?a:other,lower=a_upper?other:a;', '''    auto upper=a_upper?a:other;
    const auto lower=a_upper?other:a;
    if(pause_hold && a_upper && !upper.gpu_affine && !plan.view && !plan.capture && !plan.delay_3d){
        // Preserve priority1 BG2 (the original cream statistics frame), in
        // addition to priority0 BG3 text. Put the held geometry behind both.
        // This is a local composition view, never a register/cache mutation.
        upper.state.has_geometry=true;upper.state.geometry_priority=2;
    }''')
    s=once(s,'    screen_scale::prepare_extension(upper,!plan.view && !upper.state.window_cut);',
       '    if(!pause_hold)screen_scale::prepare_extension(upper,!plan.view && !upper.state.window_cut);')
    s=once(s,'        else compositor.opaque(g_resident_panels[panel],scene,(engine_a && panel!=1)?geometry_a:eye_geometry);', '''        else {
            const bool composed=compositor.opaque(g_resident_panels[panel],scene,(engine_a && panel!=1)?geometry_a:eye_geometry);
            if(pause_hold && panel==0 && native_diagnostics::verbose()){
                const uint64_t key=uint64_t(scene.state.blend) | (uint64_t(scene.state.alpha)<<16) |
                  (uint64_t(scene.state.has_geometry)<<32) | (uint64_t(scene.state.geometry_priority)<<33) |
                  (uint64_t(scene.gpu_affine!=nullptr)<<36) | (uint64_t(scene.state.bg_first_blend)<<37) |
                  (uint64_t(scene.state.obj_first_blend)<<38) | (uint64_t(composed)<<39);
                static uint64_t previous=~uint64_t(0);static unsigned reports=0;
                if(key!=previous && reports<64){previous=key;++reports;
                    std::printf("Pause composition ok=%u blend=%04x alpha=%04x geometry=%u priority=%u affine=%u bgFirst=%u objFirst=%u reports=%u\\n",
                      unsigned(composed),unsigned(scene.state.blend),unsigned(scene.state.alpha),unsigned(scene.state.has_geometry),
                      unsigned(scene.state.geometry_priority),unsigned(scene.gpu_affine!=nullptr),unsigned(scene.state.bg_first_blend),unsigned(scene.state.obj_first_blend),reports);
                }
            }
        }''')
    s=once(s,'        screen_scale::prepare_layers(upper,(!plan.view && !upper.state.window_cut)?stereo_slider():0.f,upper_objects);', '''        if(!pause_hold)screen_scale::prepare_layers(upper,(!plan.view && !upper.state.window_cut)?stereo_slider():0.f,upper_objects);
        else screen_scale::g_layer_frame=false;''')
    s=once(s,'    end_gpu_frame();tick_fps("Native scene","Resident candidate");', '''    end_gpu_frame();tick_fps("Native scene","Resident candidate");
    if(!pause_requested && !menu && a_upper && upper.state.has_geometry &&
       !plan.view && !plan.capture && !plan.delay_3d){
        g_pause_held_valid=true;g_pause_held_stereo=g_native_stereo;
    }''')
    # No bottom-screen changes and no PPU affine opt-out: retain original map.
    tree.write_text(path.relative_to(output),s)
    tree.commit()
    return ['port/platform/3ds/viewer_3ds.cpp']
