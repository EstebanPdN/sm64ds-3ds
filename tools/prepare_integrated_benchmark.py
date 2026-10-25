#!/usr/bin/env python3
"""Copy the strictly opt-in diagnostic campaign helper into the derived overlay."""
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]
def prepare(output):
 output=Path(output).resolve()
 if output.is_relative_to((ROOT/'third_party').resolve()):raise ValueError('Pinned vendor tree is immutable')
 base=output/'port/platform/3ds'
 for name in ('native_benchmark.h','native_benchmark.cpp'):
  shutil.copyfile(ROOT/'source/integrated_native/options'/name,base/name)
 path=output/'port/CMakeLists.txt';text=path.read_text()
 old='target_sources(smoke_player PRIVATE platform/3ds/cstick_camera.cpp platform/3ds/native_pause_menu.cpp)'
 assert text.count(old)==1,'Reviewed benchmark CMake hook changed'
 text=text.replace(old,old+'\n    target_sources(smoke_player PRIVATE platform/3ds/native_benchmark.cpp)',1)
 path.write_text(text)
 return ['port/platform/3ds/native_benchmark.h','port/platform/3ds/native_benchmark.cpp','port/CMakeLists.txt']
