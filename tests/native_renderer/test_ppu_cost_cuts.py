"""BUILD134 PPU cost cuts: the generated port/ntr/ppu.cpp against the BUILD 133
source, output for output, over randomized multi-frame sequences (no Azahar).

Two comparisons, each one executable holding both PPU builds (each compiled
with its own `ntr` namespace) over the same DS memory:
  cuts-off:  the generated source compiled with the cuts off (PPU_COST_CUTS_OFF,
             what sdmc:/3ds/sm64ds/sin-recortes.txt selects on the console)
             against the same source with the cuts.
  build133:  build/host-ppu/old/ppu_build133.cpp (the build-tree file before
             the cuts) against the generated source, when that copy exists.
Builds in build/host-ppu only."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'build/native-renderer/game-runtime-20261026-r1/source/port'
WORK=ROOT/'build/host-ppu'
# The source under test; SM64DS_PPU_CANDIDATE points at another copy of it.
CANDIDATE=Path(os.environ.get('SM64DS_PPU_CANDIDATE',PORT/'ntr/ppu.cpp'))
SHIM='''// Host stand-in for newlib's mallinfo in the __3DS__ branch of ppu.cpp.
#pragma once
#include <stdlib.h>
struct mallinfo { size_t fordblks; };
static inline struct mallinfo mallinfo(void){struct mallinfo m;m.fordblks=(size_t)1<<30;return m;}
'''


def build(name,reference,reference_flags,sanitize=False):
    folder=WORK/name;folder.mkdir(parents=True,exist_ok=True)
    (WORK/'shim').mkdir(exist_ok=True);(WORK/'shim/malloc.h').write_text(SHIM)
    compiler=shutil.which('clang++') or shutil.which('c++')
    common=[compiler,'-std=c++17','-O2','-g','-D__3DS__','-Wno-deprecated-declarations','-I'+str(WORK/'shim'),'-I'+str(PORT),
        '-I'+str(PORT/'ntr/include'),'-I'+str(ROOT/'tests/native_renderer')]
    # pointer-overflow is checked too since BUILD135 fixed shift_rows (it
    # subtracted a negative int times a size_t from a pointer).
    if sanitize:common+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    objects=[]
    def compile_one(source,output,flags):
        target=folder/output
        result=subprocess.run(common+flags+['-c',str(source),'-o',str(target)],text=True,capture_output=True)
        if result.returncode:raise AssertionError(result.stdout+result.stderr)
        objects.append(str(target))
    side=ROOT/'tests/native_renderer/ppu_cost_cuts_side.cpp'
    for tag,ppu,flags in (('reference',reference,reference_flags),('candidate',CANDIDATE,['-DPPU_COST_CUTS_STATS','-DHARNESS_STATS'])):
        space=['-Dntr=ntr_'+tag]
        compile_one(ppu,tag+'_ppu.o',space+[f for f in flags if f!='-DHARNESS_STATS'])
        compile_one(PORT/'ntr/vram_banks.cpp',tag+'_banks.o',space)
        compile_one(PORT/'ntr/bitmap.cpp',tag+'_bitmap.o',space)
        compile_one(side,tag+'_side.o',space+['-DHARNESS_SIDE=harness_side_'+tag]+[f for f in flags if f=='-DHARNESS_STATS'])
    compile_one(ROOT/'tests/native_renderer/ppu_cost_cuts.cpp','driver.o',[])
    binary=folder/'ppu_cost_cuts'
    result=subprocess.run(common+objects+['-o',str(binary)],text=True,capture_output=True)
    if result.returncode:raise AssertionError(result.stdout+result.stderr)
    return binary


def run(binary,arguments=(),exact_cpu_objects=False):
    environment={k:v for k,v in os.environ.items() if not k.startswith('PPU_')}
    # The pinned CPU path keeps a per-sprite OBJ layer that extended OBJ palette
    # changes do not invalidate. Off, a refused publication's CPU composition
    # can be compared with the picture of a publication (disturbed phase).
    if exact_cpu_objects:environment['PPU_NO_SPRITE_INCREMENTAL']='1'
    result=subprocess.run([str(binary),*map(str,arguments)],text=True,capture_output=True,env=environment)
    if result.returncode:raise AssertionError(result.stdout+result.stderr)
    return result.stdout


class PpuCostCutTests(unittest.TestCase):
    def test_cuts_match_the_switched_off_paths(self):
        binary=build('cuts-off',CANDIDATE,['-DPPU_COST_CUTS_OFF'])
        for exact in (False,True):
            output=run(binary,exact_cpu_objects=exact)
            print(output,end='');self.assertIn('PASS:',output)

    def test_cuts_match_build_133(self):
        old=WORK/'old/ppu_build133.cpp'
        if not old.is_file():self.skipTest('no BUILD 133 copy of ppu.cpp in build/host-ppu/old')
        # BUILD135 bug fixes (prepare_integrated_native_renderer.ppu_bug_fixes_patch)
        # change outputs on purpose: the reference is BUILD 133 with the same fixes.
        sys.path.insert(0,str(ROOT/'tools'))
        from prepare_integrated_native_renderer import ppu_bug_fixes_patch
        fixed=WORK/'old/ppu_build133_fixed.cpp';fixed.write_text(ppu_bug_fixes_patch(old.read_text()))
        binary=build('build133',fixed,[])
        for exact in (False,True):
            output=run(binary,exact_cpu_objects=exact)
            print(output,end='');self.assertIn('PASS:',output)

    def test_sanitized_short_run(self):
        output=run(build('sanitized',CANDIDATE,['-DPPU_COST_CUTS_OFF'],sanitize=True),(36,120))
        print(output,end='');self.assertIn('PASS:',output)


if __name__=='__main__':unittest.main()
