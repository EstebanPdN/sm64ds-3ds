"""Native-resolution stereo in the disposable integrated source tree.

Raising the 3D slider at native resolution needs a second 400x240 geometry
eye and composed panel (about 1.25 MiB more VRAM). It did not fit, so the
display fell back to DS resolution, and the refusal stayed until a setting
changed: lowering the slider kept the DS resolution too.

  * the right eye reuses the left eye's depth/stencil buffer; the command
    list is split before its clear, so the left eye has finished with it;
  * the scratch pool (captures and semi-transparent OBJ, which always select
    the DS-resolution layout) is released while native stereo is shown and
    reserved again when the layout leaves it;
  * a refusal applies only to the eye count that failed, so lowering the
    slider returns to native mono and raising it retries native stereo.
"""
from prepare_integrated_native_pause import GeneratedTree
from prepare_integrated_profiling import once


def prepare(output):
    tree = GeneratedTree(output)
    changed = []

    name = 'port/platform/3ds/gpu_composite.h'
    text = tree.read_text(name)
    text = once(text, 'bool scratch_pool_reserve();\n',
                'bool scratch_pool_reserve();\n'
                '// Native stereo: release the pool and refuse to reserve it until resumed.\n'
                'void scratch_pool_suspend(bool suspend);\n'
                'bool scratch_pool_suspended();\n')
    tree.write_text(name, text)
    changed.append(name)

    name = 'port/platform/3ds/gpu_composite.cpp'
    text = tree.read_text(name)
    text = once(text, '''static unsigned g_capture_idle=~0u;
bool scratch_pool_reserve(){
    if(!g_pool_state){
        g_pool_state=1;
        for(auto& s:g_pool)if(!s.init()){g_pool_state=-1;break;}
        std::printf("GPU scratch pool surfaces=%d remainingVRAM=%u\\n",g_pool_state,vramSpaceFree());
    }
    return g_pool_state>0;
}
''', '''static unsigned g_capture_idle=~0u;
// Native stereo needs this VRAM; none of the pool's users run there (they
// select the DS-resolution layout), so it is released while suspended.
static bool g_pool_suspended=false;
bool scratch_pool_reserve(){
    if(g_pool_suspended)return false;
    if(!g_pool_state){
        g_pool_state=1;
        for(auto& s:g_pool)if(!s.init()){g_pool_state=-1;break;}
        if(g_pool_state<0)for(auto& s:g_pool)s.destroy();
        std::printf("GPU scratch pool surfaces=%d remainingVRAM=%u\\n",g_pool_state,vramSpaceFree());
    }
    return g_pool_state>0;
}
// Outside a frame only (render target deletion waits for the GPU queue).
void scratch_pool_suspend(bool suspend){
    if(suspend==g_pool_suspended)return;
    g_pool_suspended=suspend;
    if(suspend){
        if(g_pool_state>0)for(auto& s:g_pool)s.destroy();
        for(auto& slot:g_bank_slot)slot=-1;
        g_capture_idle=~0u;
        std::printf("GPU scratch pool released for native stereo remainingVRAM=%u\\n",vramSpaceFree());
    }
    g_pool_state=0; // reserved again by the next compositor init
}
bool scratch_pool_suspended(){return g_pool_suspended;}
''')
    tree.write_text(name, text)
    changed.append(name)

    name = 'port/platform/3ds/viewer_3ds.cpp'
    text = tree.read_text(name)
    # Refusals remember the eye count that failed.
    text = once(text, '''    static uint32_t failed_generation=~0u;
    const uint32_t generation=enhance::live().generation.load(std::memory_order_relaxed);
    if(wanted.native && generation==failed_generation)
''', '''    static uint32_t failed_generation=~0u;static unsigned failed_eyes=0;
    const uint32_t generation=enhance::live().generation.load(std::memory_order_relaxed);
    // A refusal holds for the eye count that failed: lowering the 3D slider
    // returns to native mono, raising it again retries native stereo.
    const bool native_refused=wanted.native && generation==failed_generation &&
      display_mode::eyes(wanted,stereo,a_upper,plan.delay_3d,stereo_slider())==failed_eyes;
    bool refused_now=false;
    if(native_refused)
''')
    text = once(text, '''    ds_gpu::sm64ds_native_geometry_width=wanted.geometry_width;
    if(!resident_init()){''', '''    ds_gpu::sm64ds_native_geometry_width=wanted.geometry_width;
    if(!g_resident_ready){
        // Native stereo takes the scratch pool's VRAM (gpu_composite.cpp).
        const bool suspend=g_display.native && g_display_eyes==2;
        if(suspend!=ds_gpu::scratch_pool_suspended()){
            if(g_direct_open)end_gpu_frame();
            ds_gpu::scratch_pool_suspend(suspend);
        }
    }
    if(!resident_init()){''')
    # The DS-resolution fallback uses the pool again.
    text = once(text, '''      ds_gpu::sm64ds_native_geometry_width=256;
      if(!resident_init()){g_present_refusal="display-vram";return false;}''', '''      ds_gpu::sm64ds_native_geometry_width=256;
      ds_gpu::scratch_pool_suspend(false);
      if(!resident_init()){g_present_refusal="display-vram";return false;}''')
    text = once(text, '      failed_generation=generation;\n',
                '      failed_generation=generation;failed_eyes=wanted_eyes;refused_now=true;\n')
    text = once(text, '      (generation==failed_generation?64u:0u);',
                '      (native_refused || refused_now?64u:0u);')
    # The right eye of native stereo shares the left eye's depth/stencil.
    text = once(text, '''        g_resident_geometry_target[eye]=C3D_RenderTargetCreateFromTex(&texture,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8);
        if(!g_resident_geometry_target[eye]){resident_destroy();return false;}
''', '''        // Native stereo: the right eye is drawn after the left one, so both
        // use one depth/stencil buffer (cleared after a split in gpu_frame).
        const bool shared_depth=eye==1 && g_display.native;
        g_resident_geometry_target[eye]=C3D_RenderTargetCreateFromTex(&texture,GPU_TEXFACE_2D,0,
            shared_depth?C3D_DEPTHTYPE(-1):C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8));
        if(!g_resident_geometry_target[eye]){resident_destroy();return false;}
        if(shared_depth){
            C3D_FrameBufDepth(&g_resident_geometry_target[1]->frameBuf,
                g_resident_geometry_target[0]->frameBuf.depthBuf,GPU_RB_DEPTH24_STENCIL8);
            g_resident_geometry_target[1]->ownsDepth=false;
        }
''')
    text = once(text, '''    if (slider > 0.0f && !stacked) {
        C3D_RenderTargetClear(right, C3D_CLEAR_ALL, clear_color, clear_depth);
''', '''    if (slider > 0.0f && !stacked) {
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
        // Shared depth (native stereo): the clear is a GX fill queued now,
        // so the left eye's commands are submitted ahead of it.
        if(resident && !right->ownsDepth)C3D_FrameSplit(GX_CMDLIST_FLUSH);
#endif
        C3D_RenderTargetClear(right, C3D_CLEAR_ALL, clear_color, clear_depth);
''')
    tree.write_text(name, text)
    changed.append(name)
    tree.commit()
    return changed
