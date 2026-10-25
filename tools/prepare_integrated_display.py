#!/usr/bin/env python3
"""Auditable display experiment, applied only to the derived build overlay.

Higher resolution is limited to GPU-resident upper geometry/composition. DS
capture/feedback and alpha scratch retain their exact 256x192 implementation.
No changes to the pinned vendor/decomp checkout or bottom touch coordinates.
"""
from pathlib import Path
from prepare_integrated_native_pause import GeneratedTree
ROOT=Path(__file__).resolve().parents[1]
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Display hook changed: '+old[:100])
    return text.replace(old,new,1)
def prepare(output):
    tree=GeneratedTree(output)
    output=tree.root
    base=output/'port/platform/3ds'
    for name in ['display_culling.hpp','display_culling.cpp']:
        tree.write_bytes((base/name).relative_to(output), (ROOT/'source/integrated_native/options'/name).read_bytes())
    tree.write_bytes((base/'display_mode.hpp').relative_to(output), (ROOT/'source/integrated_native/options/display_mode.hpp').read_bytes())
    path=base/'gpu_composite.h';s=tree.read_text(path.relative_to(output))
    s=once(s,'    bool init();\n    void destroy();','    unsigned width=256,height=192;\n    bool init(unsigned w=256,unsigned h=192);\n    void destroy();')
    tree.write_text(path.relative_to(output),s)
    path=base/'gpu_composite.cpp';s=tree.read_text(path.relative_to(output))
    s=once(s,'bool Surface::init(){\n    if(!C3D_TexInitVRAM(&texture,256,256,GPU_RGBA8))return false;',
      'bool Surface::init(unsigned w,unsigned h){\n    width=w;height=h;\n    if(!C3D_TexInitVRAM(&texture,w>256?512:256,256,GPU_RGBA8))return false;')
    s=once(s,'linearAlloc(sizeof(Vertex)*6*4)','linearAlloc(sizeof(Vertex)*6*12)')
    s=once(s,'for(unsigned variant=0;variant<4;++variant){\n        const float lower0=(variant&1)?0.f:.75f,upper0=(variant&1)?.75f:0.f;',
      'for(unsigned variant=0;variant<12;++variant){\n        const unsigned group=variant/4;\n        const float extent=group?.9375f:.75f;\n        const float u=group==1?.625f:group==2?.78125f:1.f;\n        const float lower0=(variant&1)?0.f:extent,upper0=(variant&1)?extent:0.f;')
    # Coordinate0 is sampled geometry; coordinate1 stays the DS priority/front mask.
    start=s.index('for(unsigned variant=0;variant<12;');end=s.index('    GSPGPU_FlushDataCache(vertices_',start)
    q=s[start:end].replace('{1,lower0}','{u,lower0}').replace('{1,upper0}','{u,upper0}')
    s=s[:start]+q+s[end:]
    s=once(s,'GSPGPU_FlushDataCache(vertices_,sizeof(Vertex)*6*4);','GSPGPU_FlushDataCache(vertices_,sizeof(Vertex)*6*12);')
    s=once(s,'C3D_FrameDrawOn(output.target);\n    C3D_SetViewport(0,0,256,192);','C3D_FrameDrawOn(output.target);\n    C3D_SetViewport(0,0,output.width,output.height);')
    s=once(s,'vertices_+6*(unsigned(resident)+2*unsigned(secondary))',
      'vertices_+6*(unsigned(resident)+2*unsigned(secondary)+\n        (input.width==512?(output.width==320?4:8):0))')
    # A native geometry may be sampled into the lower DS panel. Its width must
    # follow the geometry, not the destination: exported uniform frame choice.
    s=once(s,'namespace ds_gpu {','namespace ds_gpu {\nextern unsigned sm64ds_native_geometry_width;')
    s=s.replace('(output.width==320?4:8)','(sm64ds_native_geometry_width==320?4:8)')
    s=once(s,'input.target->frameBuf.colorBuf),GX_BUFFER_DIM(256,256),scratch,GX_BUFFER_DIM(256,256),flags);',
      'input.target->frameBuf.colorBuf),GX_BUFFER_DIM(input.texture.width,256),scratch,GX_BUFFER_DIM(input.texture.width,256),flags);')
    s=once(s,'GSPGPU_InvalidateDataCache(scratch,256*256*4);\n    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){\n        const uint32_t rgba=scratch[(64+y)*256+x];',
      'GSPGPU_InvalidateDataCache(scratch,input.texture.width*256*4);\n    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){\n        const uint32_t rgba=scratch[(256-input.height+y*input.height/192)*input.texture.width+x*input.width/256];')
    tree.write_text(path.relative_to(output),s)
    path=base/'viewer_3ds.cpp';s=tree.read_text(path.relative_to(output))
    s='#include "display_mode.hpp"\n'+s
    s=once(s,'static bool g_resident_ready;',
      'static bool g_resident_ready;\nstatic display_mode::Plan g_display=display_mode::plan(0,0);\nstatic unsigned g_display_eyes=2;\nstatic const char* g_display_failure="none";\n')
    s=once(s,'static bool ds_layout(){\n    const int forced=layout_override();\n    return forced>0 || (forced==0 && g_menu_presentation.load(std::memory_order_relaxed));\n}',
      'static bool ds_layout(){\n    return g_menu_presentation.load(std::memory_order_relaxed) ||\n      enhance::live().screen_mode.load(std::memory_order_relaxed)==0;\n}')
    # Rebuild uses resident_destroy's own FrameBegin drain, outside active lists.
    old='    if(!resident_init()){g_present_refusal="core4";return false;}'
    new='''    const bool menu=g_menu_presentation.load(std::memory_order_relaxed);
    const bool feedback=plan.capture || plan.view || plan.delay_3d || !a_upper || !a.state.has_geometry ||
      a.state.window_cut || other.state.window_cut ||
      a.state.obj_first_blend || a.state.semi_back || a.state.bg_first_blend ||
      other.state.obj_first_blend || other.state.semi_back || other.state.bg_first_blend ||
      a.state.has_obj_alpha || other.state.has_obj_alpha;
    auto wanted=display_mode::plan(enhance::live().screen_mode.load(std::memory_order_relaxed),
      enhance::live().res3d.load(std::memory_order_relaxed),menu,feedback);
    static uint32_t failed_generation=~0u;
    const uint32_t generation=enhance::live().generation.load(std::memory_order_relaxed);
    if(wanted.native && generation==failed_generation)
      wanted=display_mode::plan(enhance::live().screen_mode.load(std::memory_order_relaxed),0,menu,true);
    const unsigned wanted_eyes=display_mode::eyes(wanted,stereo,a_upper,plan.delay_3d,stereo_slider());
    if(g_resident_ready && (wanted.geometry_width!=g_display.geometry_width ||
       wanted.geometry_height!=g_display.geometry_height || wanted.width!=g_display.width || wanted_eyes!=g_display_eyes)){
      if(g_direct_open)end_gpu_frame();
      if(!resident_destroy()){g_present_refusal="display-drain";return false;}
    }
    if(wanted.native && !g_display.native){
      if(g_direct_open)end_gpu_frame();
      if(!C3D_FrameBegin(0)){g_present_refusal="display-layout-drain";return false;}
      end_gpu_frame();
      screen_scale::release();deferred_release_flush();
    }
    g_display=wanted;g_display_eyes=wanted_eyes;
    ds_gpu::sm64ds_native_geometry_width=wanted.geometry_width;
    if(!resident_init()){
      if(!g_display.native){g_present_refusal="core4";return false;}
      std::printf("Native resolution refused at=%s requested=%ux%u eyes=%u remainingVRAM=%u remainingLinear=%u\\n",g_display_failure,wanted.geometry_width,wanted.geometry_height,wanted_eyes,unsigned(vramSpaceFree()),unsigned(linearSpaceFree()));
      g_display=display_mode::plan(enhance::live().screen_mode.load(std::memory_order_relaxed),0,menu,true);g_display_eyes=2;
      ds_gpu::sm64ds_native_geometry_width=256;
      if(!resident_init()){g_present_refusal="display-vram";return false;}
      failed_generation=generation;
      std::printf("Native resolution unavailable: DS fallback\\n");
    }
    const unsigned fallback=(menu?1u:0u)|(!a.state.has_geometry?2u:0u)|
      (plan.capture || plan.view || plan.delay_3d?4u:0u)|(!a_upper?8u:0u)|
      (a.state.window_cut || other.state.window_cut?16u:0u)|
      ((a.state.obj_first_blend || a.state.semi_back || a.state.bg_first_blend ||
      other.state.obj_first_blend || other.state.semi_back || other.state.bg_first_blend ||
      a.state.has_obj_alpha || other.state.has_obj_alpha)?32u:0u)|
      (generation==failed_generation?64u:0u);
    const unsigned requested=enhance::live().res3d.load(std::memory_order_relaxed);
    enhance::live().actual_render_width.store(uint16_t(g_display.geometry_width),std::memory_order_relaxed);
    enhance::live().actual_render_height.store(uint16_t(g_display.geometry_height),std::memory_order_relaxed);
    enhance::live().native_fallback.store(requested==1 && !g_display.native,std::memory_order_relaxed);
    const unsigned framing=enhance::live().screen_mode.load(std::memory_order_relaxed);
    const uint64_t display_key=(uint64_t(requested)<<40)|(uint64_t(framing)<<32)|
      (uint64_t(fallback)<<24)|(uint64_t(g_display.geometry_width)<<12)|g_display.geometry_height;
    static uint64_t last_display_key=~uint64_t(0);
    if(display_key!=last_display_key){last_display_key=display_key;
      std::printf("Display actual requestedResolution=%u framing=%u geometry=%ux%u upperPanel=%ux%u lcd=%ux240 eyes=%u projectionX=%.3f fallbackMask=%u menu=1 noGeometry=2 capture=4 lower=8 cut=16 alpha=32 vram=64\\n",
      requested,framing,g_display.geometry_width,g_display.geometry_height,
      g_resident_panels[0].width,g_resident_panels[0].height,g_display.width,g_display_eyes,
      double(g_display.projection_x),fallback);
    }
'''
    s=once(s,old,new)
    # Disable stacked stereo in native mode; each eye owns a normal512x256target.
    s=once(s,'if(stack_option && C3D_TexInitVRAM(&g_resident_stack,256,512,GPU_RGBA8))',
      'if(!g_display.native && stack_option && C3D_TexInitVRAM(&g_resident_stack,256,512,GPU_RGBA8))')
    s=once(s,'for(unsigned eye=0;eye<2 && !g_resident_stacked;++eye){',
      'for(unsigned eye=0;eye<g_display_eyes && !g_resident_stacked;++eye){')
    s=once(s,'static bool resident_init(){\n    if(g_resident_ready)return true;',
      'static bool resident_init(){\n    if(g_resident_ready)return true;\n    g_display_failure="geometry-color";')
    s=once(s,'g_resident_geometry_target[eye]=C3D_RenderTargetCreateFromTex',
      'g_display_failure="geometry-depth";\n        g_resident_geometry_target[eye]=C3D_RenderTargetCreateFromTex')
    s=once(s,'    for(auto& compositor:g_resident_compositor)\n#ifdef',
      '    g_display_failure="compositor";\n    for(auto& compositor:g_resident_compositor)\n#ifdef')
    s=once(s,'    for(auto& panel:g_resident_panels)if(!panel.init())',
      '    g_display_failure="panel-color";\n    for(auto& panel:g_resident_panels)if(!panel.init())')
    s=once(s,'    g_resident_bottom=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,-1);',
      '    g_display_failure="bottom-target";\n    g_resident_bottom=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,-1);')
    s=once(s,'C3D_TexInitVRAM(&texture,256,256,GPU_RGBA8)',
      'C3D_TexInitVRAM(&texture,display_mode::texture_width(g_display),256,GPU_RGBA8)')
    s=once(s,'for(auto& panel:g_resident_panels)if(!panel.init()){resident_destroy();return false;}',
      'for(unsigned i=0;i<3;++i)if(!g_resident_panels[i].init(i<g_display_eyes?g_display.geometry_width:256,\n      i<g_display_eyes?g_display.geometry_height:192)){resident_destroy();return false;}')
    s=once(s,'if(!C3D_TexInitVRAM(&texture,display_mode::texture_width(g_display),256,GPU_RGBA8))',
      'if((g_display_failure="geometry-color",!C3D_TexInitVRAM(&texture,display_mode::texture_width(g_display),256,GPU_RGBA8)))')
    # The final native panels bypass DS layout/filtering: preserve every native
    # geometry pixel. The lower panel retains its original transform/texture.
    s=once(s,'const float width=panel==2?320.f:400.f;\n        const float u0=-.5f/width+1.f/65536.f;\n        const float v0=.75f+.5f*.75f/240.f-1.f/65536.f;',
      'const float extent=panel<2 && g_display.native?.9375f:.75f;\n        const float uextent=panel<2 && g_display.native?g_display.geometry_width/512.f:1.f;\n        const float half=panel==2?1.f:g_display.width/400.f;\n        const float u0=0.f;\n        const float v0=extent;')
    start=s.index('const float extent=panel<2 && g_display.native?');end=s.index('    GSPGPU_FlushDataCache(g_resident_vertices',start)
    q=s[start:end].replace('{-1,1,','{-half,1,').replace('{1,1,','{half,1,').replace('{1,-1,','{half,-1,').replace('{-1,-1,','{-half,-1,').replace('u0+1','u0+uextent').replace('v0-.75f','v0-extent')
    s=s[:start]+q+s[end:]
    s=once(s,'if(!screen_scale::draw(targets,panel_textures,g_native_stereo||screen_scale::g_layer_frame,true)){',
      'if(g_display.native || !screen_scale::draw(targets,panel_textures,g_native_stereo||screen_scale::g_layer_frame,true)){')
    s=once(s,'const bool top_done=screen_scale::draw_game_top(panel_textures,targets,g_native_stereo,true);',
      'const bool top_done=!g_display.native && screen_scale::draw_game_top(panel_textures,targets,g_native_stereo,true);')
    # Non-stacked geometry viewport. DS snap must not quantize native vertices.
    s=s.replace('else C3D_SetViewport(0,0,256,192);','else C3D_SetViewport(0,0,g_display.geometry_width,g_display.geometry_height);')
    s=s.replace('if(resident)C3D_SetViewport(0,0,256,192);','if(resident)C3D_SetViewport(0,0,g_display.geometry_width,g_display.geometry_height);')
    s=s.replace('C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix,1.f,0.f,0.f,0.f);',
      'C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix,\n#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE\n        resident && perspective?g_display.projection_x:1.f,\n#else\n        1.f,\n#endif\n        0.f,0.f,0.f);')
    # Second eye uses explicit +0 syntax; same aspect correction.
    s=s.replace('C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+0,1.f,0.f,0.f,0.f);\n            C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+1,0.f,-1.f,0.f,0.f);',
      'C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+0,resident && perspective?g_display.projection_x:1.f,0.f,0.f,0.f);\n            C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+1,0.f,-1.f,0.f,0.f);')
    s=once(s,'slider,convergence,aspect_x,perspective,std::getenv("PLAYER_GPU_SHADOWS")',
      'slider,convergence,aspect_x*g_display.projection_x,perspective,std::getenv("PLAYER_GPU_SHADOWS")')
    s=once(s,'g_u_fix+0,1.f,0.f,0.f,sign*g_eye.shift',
      'g_u_fix+0,g_display.projection_x,0.f,0.f,sign*g_eye.shift')
    # Locate exact snap policy rather than disabling unrelated DS raster paths.
    s=s.replace('g_resident_diagnostic=static_cast<uint32_t*>(linearAlloc(256*256*4))',
      'g_resident_diagnostic=static_cast<uint32_t*>(linearAlloc(512*256*4))')
    s=once(s,'static_cast<u32*>(g_resident_geometry[eye].data),GX_BUFFER_DIM(256,256),\n            g_resident_diagnostic,GX_BUFFER_DIM(256,256)',
      'static_cast<u32*>(g_resident_geometry[eye].data),GX_BUFFER_DIM(g_resident_geometry[eye].width,256),\n            g_resident_diagnostic,GX_BUFFER_DIM(g_resident_geometry[eye].width,256)')
    s=once(s,'output[y*256+x]=decode_native_pixel(g_resident_diagnostic[(255-y)*256+x]);',
      'output[y*256+x]=decode_native_pixel(g_resident_diagnostic[\n              (255-y*g_display.geometry_height/192)*g_resident_geometry[eye].width+x*g_display.geometry_width/256]);')
    s=s.replace('GSPGPU_InvalidateDataCache(g_resident_diagnostic,256*256*4);',
      'GSPGPU_InvalidateDataCache(g_resident_diagnostic,g_resident_geometry[eye].width*256*4);')
    tree.write_text(path.relative_to(output),s)
    path=base/'gpu_composite.h';s=tree.read_text(path.relative_to(output));s=once(s,'namespace ds_gpu {','namespace ds_gpu {\nextern unsigned sm64ds_native_geometry_width;');tree.write_text(path.relative_to(output),s)
    path=base/'gpu_composite.cpp';s=tree.read_text(path.relative_to(output));s=once(s,'extern unsigned sm64ds_native_geometry_width;','unsigned sm64ds_native_geometry_width=256;');tree.write_text(path.relative_to(output),s)
    path=output/'port/CMakeLists.txt';s=tree.read_text(path.relative_to(output))
    s+='\n# Horizontal culling agrees with the selected Wide projection.\nif(TARGET smoke_player)\n  target_sources(smoke_player PRIVATE platform/3ds/display_culling.cpp)\n  target_link_options(smoke_player PRIVATE "-Wl,--wrap=_ZN7Clipper13Func_0201559CEv")\nendif()\n'
    tree.write_text(path.relative_to(output),s)
    tree.commit()
    return ['port/platform/3ds/'+n for n in ['display_mode.hpp','display_culling.hpp','display_culling.cpp','viewer_3ds.cpp','gpu_composite.h','gpu_composite.cpp']]+['port/CMakeLists.txt']
