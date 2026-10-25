"""Integrate the validated DS-world motion owner, without laboratory fixtures."""
from pathlib import Path
import shutil,sys
ROOT=Path(__file__).resolve().parents[1]
def once(text,old,new):
 assert text.count(old)==1,'Movement integration seam changed: '+old[:80]
 return text.replace(old,new,1)
def prepare(output):
 output=Path(output).resolve()
 assert not output.is_relative_to((ROOT/'third_party').resolve())
 sys.path.insert(0,str(ROOT/'tools/movement_lab'))
 from build_core import build
 archive=build(True)
 dest=output/'port/platform/3ds/movement';dest.mkdir(parents=True,exist_ok=True)
 changed=[]
 for source in sorted((ROOT/'source/integrated_native/movement').iterdir()):
  if source.suffix not in {'.h','.hpp','.cpp'}:continue
  target=dest/source.name;shutil.copyfile(source,target);changed.append(str(target.relative_to(output)))
 # The archive and public includes are frozen alongside the adapted source.
 for folder in ['src','include']:
  origin=ROOT/'build/movement-lab/core/source'/folder
  if origin.exists():
   for source in origin.rglob('*'):
    if source.is_file() and source.suffix in {'.h','.inc'}:
     target=dest/'core'/folder/source.relative_to(origin);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target);changed.append(str(target.relative_to(output)))
 target=dest/'libmotion64.a';shutil.copyfile(archive,target);changed.append(str(target.relative_to(output)))
 path=output/'sm64ds-decomp/src/_ZN6Player8BehaviorEv.cpp';s=path.read_text()
 s='#include "native_motion.hpp"\n'+s
 s=once(s,'        if (*(void **)&st->mMain != 0)\n            (this->*(st->mMain))();','        if (!native_motion::step(this)) { st=mState; if (*(void **)&st->mMain != 0) (this->*(st->mMain))(); }')
 s=once(s,'    func_ov002_020bf36c(((char *)this), ((char *)this) + 0x2d4);','    if (!native_motion_owns_position()) func_ov002_020bf36c(((char *)this), ((char *)this) + 0x2d4);')
 s='extern "C" void func_ov002_020c14b8(void*);\n'+s
 s=once(s,'    func_ov002_020c2db8(((char *)this));','    if (native_motion_owns_position()) func_ov002_020c14b8(this);\n    else func_ov002_020c2db8(((char *)this));')
 s=once(s,'    return 1;\n}','    native_motion::commit(this);\n    return 1;\n}')
 path.write_text(s);changed.append(str(path.relative_to(output)))
 path=output/'port/platform/3ds/game_main.cpp';s=path.read_text();s='#include "movement/native_motion.hpp"\n'+s
 s=once(s,' const uint32_t pause_requests=sm64ds_native_pause_take_requests();',' native_motion::frame(current_scene,updates);\n const uint32_t pause_requests=sm64ds_native_pause_take_requests();')
 path.write_text(s);changed.append(str(path.relative_to(output)))
 path=output/'port/platform/3ds/enhance_runtime.cpp';s=path.read_text();s='#include "movement/native_motion.hpp"\n'+s
 s='extern "C" unsigned sm64ds_render60_scene() __attribute__((weak));\n'+s
 s=once(s,'uint32_t filter_input(uint32_t held, uint32_t down, bool& touch_allowed) {','uint32_t filter_input(uint32_t held, uint32_t down, bool& touch_allowed) {\n    held=native_motion::input(held,down);\n    held=native_motion::filter_circle_digital(held,enhance::live().circle_pad_analog.load(std::memory_order_relaxed),sm64ds_render60_scene&&sm64ds_render60_scene()==3,enhance::live().pause_scene.load(std::memory_order_acquire));')
 path.write_text(s);changed.append(str(path.relative_to(output)))
 path=output/'port/CMakeLists.txt';s=path.read_text();s+='''
# One authoritative locomotion owner; DS keeps interactions and special abilities.
target_sources(smoke_player PRIVATE platform/3ds/movement/native_motion.cpp platform/3ds/movement/ds_world_query.cpp)
target_include_directories(smoke_player PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds" "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/movement" "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/movement/core/src")
target_link_libraries(smoke_player PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/movement/libmotion64.a")
''';path.write_text(s);changed.append(str(path.relative_to(output)))
 return changed
