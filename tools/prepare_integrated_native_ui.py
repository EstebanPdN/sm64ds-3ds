"""Replace only pause UI layers, keeping original map, game state and input."""
from prepare_integrated_native_pause import GeneratedTree
def once(text,old,new):
 if text.count(old)!=1:raise ValueError('Native lower UI contract changed: '+old[:90])
 return text.replace(old,new,1)
def ppu_source(s):
 s='#include "platform/3ds/native_lower_state.hpp"\n'+s
 old='inline uint32_t rd32(uint32_t a) { return *static_cast<volatile uint32_t *>(host_ptr(a)); }'
 s=once(s,old,'''// Engine B is the physical upper LCD in the title. Only hide its retail
// pause layers when POWCNT1 actually routes A above B. Front Options already
// provide an opaque image over engine A; the title's upper BG/OBJ stay live.
inline bool native_lower_engine_b() {
 return native_lower::replacing() && (rd16(0x04000304u)&0x8000u);
}
inline uint32_t rd32(uint32_t a) {
 const uint32_t value=*static_cast<volatile uint32_t *>(host_ptr(a));
 return a==0x04001000u?native_lower::lower_dispcnt(value,native_lower_engine_b()):value;
}''')
 s=once(s,'if(!g_window_cut_scope || !window_cut_enabled())return base;',
  'if((!g_window_cut_scope || !window_cut_enabled()) && !(eng==ENGINE_B && native_lower_engine_b()))return base;')
 return s

def prepare(output):
 tree=GeneratedTree(output)
 output=tree.root
 ppu=output/'port/ntr/ppu.cpp';tree.write_text(ppu.relative_to(output),ppu_source(tree.read_text(ppu.relative_to(output))))
 viewer=output/'port/platform/3ds/viewer_3ds.cpp';s=tree.read_text(viewer.relative_to(output))
 s='#include "native_lower_state.hpp"\n'+s
 s=once(s,'g_tex_ok[kind]=true;g_uploaded[kind]=0;',
  'g_tex_ok[kind]=true;g_uploaded[kind]=0;\n            if(kind==0)native_lower::gpu_ready.store(true,std::memory_order_release);')
 s=once(s,'else C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);',
  'else C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);')
 s=once(s,'static void shutdown(){for(unsigned i=0;i<2;++i)',
  'static void shutdown(){native_lower::gpu_ready.store(false,std::memory_order_release);for(unsigned i=0;i<2;++i)')
 # Preserve the ROM's HUD portraits/numerals and palette at their original
 # DS resolution. The original object renderer handles scale and composition;
 # the rejected replacement atlas is intentionally not linked into this path.
 s=once(s,'// ---- DS fog (levels-render)',
  'static C3D_RenderTarget* sm64ds_diagnostic_bottom=nullptr;\n\n// ---- DS fog (levels-render)')
 s=once(s,'    enhance_draw::draw(g_resident_bottom,0);',
  '    sm64ds_diagnostic_bottom=g_resident_bottom;\n    enhance_draw::draw(g_resident_bottom,0);')
 s=once(s,'    enhance_draw::draw(g_blit_bottom,0);',
  '    sm64ds_diagnostic_bottom=g_blit_bottom;\n    enhance_draw::draw(g_blit_bottom,0);')
 s+='''
// Read the completed game target, not the LCD buffer painted by dump_progress.
bool v3ds_diagnostic_final_bottom(uint32_t* output){
 auto* target=sm64ds_diagnostic_bottom;
 if(!output || !target || (target!=g_resident_bottom && target!=g_blit_bottom) ||
    target->frameBuf.width!=240 || target->frameBuf.height!=320)return false;
 auto* pixels=static_cast<u32*>(linearAlloc(320*240*4));if(!pixels)return false;
 const u32 flags=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
  GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
 C3D_SyncDisplayTransfer(static_cast<u32*>(target->frameBuf.colorBuf),GX_BUFFER_DIM(240,320),pixels,GX_BUFFER_DIM(240,320),flags);
 GSPGPU_InvalidateDataCache(pixels,320*240*4);
 for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)output[y*320+x]=0xff000000u|(pixels[x*240+239-y]>>8);
 linearFree(pixels);return true;
}
'''
 tree.write_text(viewer.relative_to(output),s)
 header=output/'port/platform/3ds/native_obj_gpu.h';s=tree.read_text(header.relative_to(output))
 s=once(s,'bool draw(Surface& output,const ObjPlan& plan);',
  'bool draw(Surface& output,const ObjPlan& plan,const bool* ignored=nullptr);')
 tree.write_text(header.relative_to(output),s)
 objects=output/'port/platform/3ds/native_obj_gpu.cpp';s=tree.read_text(objects.relative_to(output))
 s=once(s,'bool NativeObjects::draw(Surface& output,const ObjPlan& plan){',
  'bool NativeObjects::draw(Surface& output,const ObjPlan& plan,const bool* ignored){')
 s=once(s,'        const auto& s=plan.sprites[plan.order[i]];',
  '        if(ignored&&ignored[plan.order[i]])continue;\n        const auto& s=plan.sprites[plan.order[i]];')
 tree.write_text(objects.relative_to(output),s)
 tree.commit()
 return ['port/ntr/ppu.cpp','port/platform/3ds/viewer_3ds.cpp',
         'port/platform/3ds/native_obj_gpu.h','port/platform/3ds/native_obj_gpu.cpp']
