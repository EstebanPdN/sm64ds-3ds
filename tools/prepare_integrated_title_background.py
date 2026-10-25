"""Derive the approved lateral reflection without changing DS assets or ROM."""
from pathlib import Path
from prepare_integrated_native_pause import GeneratedTree
ROOT=Path(__file__).resolve().parents[1]
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Title background contract changed: '+old[:80])
    return text.replace(old,new,1)
def prepare(output):
    tree=GeneratedTree(output)
    output=tree.root
    path=output/'port/platform/3ds/viewer_3ds.cpp'
    text=tree.read_text(path.relative_to(output))
    text='#include "title_background.hpp"\n'+text
    text=once(text,'    if(!indexed){\n        for(unsigned i=0;i<64;++i)source_x[i]=uint8_t(i<32?224+i:i-32);',
        '    static bool previous_reflected=false;\n'
        '    const bool reflected=g_menu_presentation.load(std::memory_order_relaxed);\n'
        '    if(reflected!=previous_reflected){g_ext_have_key=false;previous_reflected=reflected;}\n'
        '    for(unsigned i=0;i<64;++i)source_x[i]=uint8_t(title_background::extension_source(i,reflected));\n'
        '    if(!indexed){')
    text=once(text,'C3D_TexSetWrap(&g_layer_sky,GPU_REPEAT,GPU_CLAMP_TO_EDGE);',
        'C3D_TexSetWrap(&g_layer_sky,GPU_MIRRORED_REPEAT,GPU_CLAMP_TO_EDGE);')
    # Only title/menu art gets extended into the LCD margins. Gameplay 4:3
    # leaves the already-cleared black margins untouched.
    text=once(text,'screen_scale::prepare_extension(upper,!plan.view && !upper.state.window_cut);',
        'screen_scale::prepare_extension(upper,g_menu_presentation.load(std::memory_order_relaxed) && !plan.view && !upper.state.window_cut);')
    # The retail face-background constructor explicitly creates an untextured
    # white veil with alpha=1 (f_108[2], polygon ID22). Over its black plate the
    # DS blend weight (alpha+1)/32 produces RGB16 on the initial star screen.
    # Remove that constant veil at its producer, not in the compositor: keep
    # all dynamic fades, the star, other low-alpha polygons and the clear plane.
    menu_path=output/'sm64ds-decomp/src/func_ov007_020b2998.c'
    menu=once(tree.read_text(menu_path.relative_to(output)),'      w->f_50 = 0x1000;',
        '      w->f_50 = 0; // Native title: no permanent white alpha-1 veil.')
    # Verify every contract before writing the generated files.
    header=ROOT/'source/integrated_native/options/title_background.hpp'
    tree.write_bytes('port/platform/3ds/title_background.hpp',header.read_bytes())
    tree.write_text(path.relative_to(output),text)
    tree.write_text(menu_path.relative_to(output),menu)
    tree.commit()
    return ['port/platform/3ds/title_background.hpp','port/platform/3ds/viewer_3ds.cpp',
            'sm64ds-decomp/src/func_ov007_020b2998.c']
