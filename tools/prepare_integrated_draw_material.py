"""Elide identical adjacent material register setup, never draws or ordering."""
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]


def material_block(text):
 start=text.index('        // Native render targets reflect Y;')
 end=text.index('        const unsigned pa = (b[i].polygon_attr >> 16) & 31;\n        if (!pa)',start)
 return start,end,text[start:end]


def prepare(output):
 output=Path(output).resolve()
 if output.is_relative_to((ROOT/'third_party').resolve()):raise ValueError('Pinned vendor tree is immutable')
 path=output/'port/platform/3ds/viewer_3ds.cpp';s=path.read_text()
 assert 'draw_material::Cache' not in s
 anchor='    for (int pass_phase = 0; pass_phase < (ds_order&&native?2:1); ++pass_phase)'
 assert s.count(anchor)==1
 s=s.replace(anchor,'    draw_material::Cache material_cache;\n'+anchor,1)
 start,end,block=material_block(s)
 policy='                const unsigned policy=enhance::live().tex_filter.load(std::memory_order_relaxed);\n'
 assert block.count(policy)==1
 block=block.replace(policy,'',1)
 winding='        const bool back_ccw=(faces==0x80)!=native;\n'
 assert block.count(winding)==1;block=block.replace(winding,'',1)
 # Before considering reuse, exclude a missing resource. A failed setup
 # cannot establish a valid cached state for the next batch.
 wrapper='''        const auto* material_texture=b[i].tex?g_batch_textures[i]:nullptr;
        if(b[i].tex && !material_texture){material_cache.invalidate();continue;}
        const unsigned policy=enhance::live().tex_filter.load(std::memory_order_relaxed);
        const draw_material::Key material_key{reinterpret_cast<std::uintptr_t>(material_texture),b[i].polygon_attr,b[i].wrap,policy,b[i].tex!=nullptr};
        const bool material_safe=native && !shadow_plan && ((b[i].polygon_attr>>4)&3)!=3;
        const bool back_ccw=(faces==0x80)!=native;
        if(material_cache.setup(material_key,material_safe)){
'''+block+'''        }
'''
 s=s[:start]+wrapper+s[end:]
 s='#include "draw_material_cache.hpp"\n'+s
 path.write_text(s)
 shutil.copyfile(ROOT/'source/integrated_native/draw_material_cache.hpp',output/'port/platform/3ds/draw_material_cache.hpp')
 return ['port/platform/3ds/viewer_3ds.cpp','port/platform/3ds/draw_material_cache.hpp']
