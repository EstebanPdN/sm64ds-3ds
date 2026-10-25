"""Pixel-perfect DS 2D in the disposable integrated source tree.

Backgrounds keep their full-screen stretch; in gameplay the 2D interface is
drawn at 1x (native_sprite_overlay.hpp):
  * lower screen: sprites in front of every background pixel (counters, map
    icons) leave the DS OBJ layer and are drawn over the LCD at 1x, grouped
    and placed by sprite_layout.hpp;
  * upper screen: the compositor draws its passes over ui_layout.hpp tiles,
    so each 2D element (box, text, counter) is composed 1:1 over the 3D.
"""
from prepare_integrated_native_pause import GeneratedTree
from prepare_integrated_profiling import once

# Tile vertices for the 12 setup() variants: bit0 resident input (v of
# coordinate0 reversed), bit1 resident secondary (v of coordinate1
# reversed), group = variant/4 (0 DS-size input, 1/2 native 320/400 input
# of a 512-wide texture). Content tiles map DS pixels 1:1; background tiles
# sample the centre of a DS pixel without 2D. Native inputs stay identity.
SET_LAYOUT = r'''bool Composite::set_layout(const ui_layout::Layout& layout,unsigned width,unsigned height){
    layout_tiles_=0;
    if(!layout.count || layout.count>ui_layout::max_tiles || width>512 || height>256)return false;
    if(!layout_vertices_)layout_vertices_=static_cast<Vertex*>(linearAlloc(sizeof(Vertex)*12*ui_layout::max_tiles*6));
    if(!layout_vertices_)return false;
    const float w=float(width),h=float(height);
    for(unsigned variant=0;variant<12;++variant){
        const bool ds_input=variant<4,resident=variant&1,secondary=variant&2;
        Vertex* out=layout_vertices_+variant*ui_layout::max_tiles*6;
        for(unsigned i=0;i<layout.count;++i){
            const auto& t=layout.tiles[i];
            auto corner=[&](int px,int py){
                const float dx=t.content?float(t.sx+(px-t.x0)):t.sx+.5f,dy=t.content?float(t.sy+(py-t.y0)):t.sy+.5f;
                Vertex v{};
                v.position[0]=px/w*2.f-1.f;v.position[1]=1.f-py/h*2.f;v.position[2]=-.5f;v.position[3]=1.f;
                v.uv0[0]=ds_input?dx/256.f:px/512.f;
                v.uv0[1]=ds_input?(resident?(192.f-dy)/256.f:dy/256.f):(resident?(h-py)/256.f:py/256.f);
                v.uv1[0]=dx/256.f;v.uv1[1]=secondary?(192.f-dy)/256.f:dy/256.f;
                return v;
            };
            const Vertex bl=corner(t.x0,t.y1),br=corner(t.x1,t.y1),tr=corner(t.x1,t.y0),tl=corner(t.x0,t.y0);
            const Vertex quad[]={bl,br,tr,bl,tr,tl};
            std::memcpy(out+6*i,quad,sizeof quad);
        }
        GSPGPU_FlushDataCache(out,sizeof(Vertex)*6*layout.count);
    }
    layout_tiles_=layout.count;layout_width_=width;layout_height_=height;
    return true;
}
'''


def prepare(output):
    tree = GeneratedTree(output)
    name = 'port/platform/3ds/viewer_3ds.cpp'
    text = tree.read_text(name)
    text = once(text, 'namespace enhance_draw {static void shutdown();}\n',
                'namespace enhance_draw {static void shutdown();}\n'
                'namespace sprite_overlay {static void shutdown();}\n')
    text = once(text, '    enhance_draw::shutdown();\n    C3D_Fini();',
                '    enhance_draw::shutdown();sprite_overlay::shutdown();\n    C3D_Fini();')
    text = once(text, 'static C3D_RenderTarget* sm64ds_diagnostic_bottom=nullptr;\n',
                'static C3D_RenderTarget* sm64ds_diagnostic_bottom=nullptr;\n'
                '#include "native_sprite_overlay.hpp"\n')
    # The lower engine's front-most sprites leave its DS OBJ layer...
    text = once(text, '''    uint64_t compose_tick=frame_profile::ticks();
    try{''', '''    uint64_t compose_tick=frame_profile::ticks();
    sprite_overlay::reset();
    try{''')
    text = once(text, '            if(!g_resident_objects[engine].draw(g_resident_object_layer[engine],*objects[engine])){',
                '''            // Pixel-perfect sprites: gameplay lower screen only (menus keep the DS layer).
            const bool* moved=engine==(a_upper?1u:0u)?sprite_overlay::prepare(objects[engine],scenes[engine],
                !menu && !plan.view && !plan.capture):nullptr;
            if(!g_resident_objects[engine].draw(g_resident_object_layer[engine],*objects[engine],moved)){''')
    # ...and are drawn at 1x over the stretched lower panel, under native UI.
    text = once(text, '    sm64ds_diagnostic_bottom=g_resident_bottom;\n',
                '    sprite_overlay::draw(g_resident_bottom);\n    sm64ds_diagnostic_bottom=g_resident_bottom;\n')
    # ...and the upper engine's 2D elements are composed 1:1 over the 3D.
    text = once(text, '    compose_panel(upper_compositor,0,upper,a_upper,g_resident_geometry[0]);\n',
                '''    {   // Pixel-perfect upper 2D: gameplay only, A on the upper LCD.
        const ds_gpu::ObjPlan* upper_objects=nullptr;
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
        upper_objects=objects[a_upper?0u:1u];
#endif
        ui_overlay::apply(upper_compositor,upper,upper_objects,g_resident_panels[0].width,g_resident_panels[0].height,
            !menu && a_upper && !plan.view && !plan.capture && !plan.delay_3d);
    }
    compose_panel(upper_compositor,0,upper,a_upper,g_resident_geometry[0]);
''')
    text = once(text, '''            g_resident_object_merge[engine].compose(g_resident_compositor[engine],
                g_resident_panels[panel],g_resident_object_layer[engine].texture,scenes[engine].state);
    }
#endif
''', '''            g_resident_object_merge[engine].compose(g_resident_compositor[engine],
                g_resident_panels[panel],g_resident_object_layer[engine].texture,scenes[engine].state);
    }
#endif
    upper_compositor.clear_layout();
''')
    tree.write_text(name, text)
    changed = [name]
    # Compositor: passes on the laid-out panel draw the tiles.
    name = 'port/platform/3ds/gpu_composite.h'
    text = tree.read_text(name)
    text = once(text, '#include "gpu_capture_plan.h"\n', '#include "gpu_capture_plan.h"\n#include "ui_layout.hpp"\n')
    text = once(text, '''    bool fill_cut(Surface& output,const ntr::PpuWindowCut& cut);
private:''', '''    bool fill_cut(Surface& output,const ntr::PpuWindowCut& cut);
    // Pixel-perfect upper 2D (ui_layout.hpp): until clear_layout(), passes
    // whose output is width x height draw these tiles instead of one quad.
    bool set_layout(const ui_layout::Layout& layout,unsigned width,unsigned height);
    void clear_layout(){layout_tiles_=0;}
private:''')
    text = once(text, '    Vertex* vertices_=nullptr;\n',
                '    Vertex* vertices_=nullptr;\n'
                '    Vertex* layout_vertices_=nullptr; // 12 variants x ui_layout::max_tiles quads\n'
                '    unsigned layout_tiles_=0,layout_width_=0,layout_height_=0;\n'
                '    int draw_count_=6; // vertices of the quad(s) bound by the last setup()\n')
    tree.write_text(name, text)
    changed.append(name)
    name = 'port/platform/3ds/gpu_composite.cpp'
    text = tree.read_text(name)
    text = once(text, '''    BufInfo_Add(buffers,vertices_+6*(unsigned(resident)+2*unsigned(secondary)+
        (input.width==512?(sm64ds_native_geometry_width==320?4:8):0)),sizeof(Vertex),3,0x210);''',
                '''    const unsigned variant=unsigned(resident)+2*unsigned(secondary)+
        (input.width==512?(sm64ds_native_geometry_width==320?4:8):0);
    const bool tiled=layout_tiles_ && output.width==layout_width_ && output.height==layout_height_;
    BufInfo_Add(buffers,tiled?layout_vertices_+variant*ui_layout::max_tiles*6:vertices_+6*variant,sizeof(Vertex),3,0x210);
    draw_count_=tiled?int(6*layout_tiles_):6;''')
    # Every setup()-based pass draws draw_count_ vertices; the affine layer
    # draw binds its own quad and keeps six.
    affine = '    C3D_TexEnvColor(tev,(32u*(unsigned(layer.priority)+2u))<<24);\n    C3D_DrawArrays(GPU_TRIANGLES,0,6);'
    assert text.count(affine) == 1
    text = text.replace(affine, affine.replace('0,6);', '0,6);//affine'))
    assert text.count('C3D_DrawArrays(GPU_TRIANGLES,0,6);\n') == 8, text.count('C3D_DrawArrays(GPU_TRIANGLES,0,6);\n')
    text = text.replace('C3D_DrawArrays(GPU_TRIANGLES,0,6);\n', 'C3D_DrawArrays(GPU_TRIANGLES,0,draw_count_);\n')
    text = text.replace('C3D_DrawArrays(GPU_TRIANGLES,0,6);//affine', 'C3D_DrawArrays(GPU_TRIANGLES,0,6);')
    text = once(text, '''void Composite::destroy(){
    if(vertices_)linearFree(vertices_);
    vertices_=nullptr;''', '''void Composite::destroy(){
    if(vertices_)linearFree(vertices_);
    vertices_=nullptr;
    if(layout_vertices_)linearFree(layout_vertices_);
    layout_vertices_=nullptr;layout_tiles_=0;''')
    assert text.endswith('    return true;\n}\n}\n')
    text = text[:-2] + SET_LAYOUT + '}\n'
    tree.write_text(name, text)
    changed.append(name)
    name = 'port/platform/3ds/native_obj_merge.cpp'
    text = tree.read_text(name)
    text = once(text, '    C3D_DrawArrays(GPU_TRIANGLES,0,6);\n', '    C3D_DrawArrays(GPU_TRIANGLES,0,compositor.draw_count_);\n')
    tree.write_text(name, text)
    changed.append(name)
    tree.commit()
    return changed
