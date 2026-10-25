#!/usr/bin/env python3
"""Check real CPU swizzle/cache lifetime with observed GPU allocation seams."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCES=('tests/test_native_screen_bindings.cpp','source/ctr/game_renderer.cpp','source/core/assets.cpp',
         'source/core/game_textures.cpp','source/vendor/ntr/texture.cpp')
with tempfile.TemporaryDirectory(prefix='sm64ds-screen-bindings-') as directory:
    temporary=Path(directory)
    flags=['-std=c++20','-O2','-D__3DS__','-UNDEBUG','-Wall','-Wextra',
           '-Wno-misleading-indentation','-ffunction-sections','-fdata-sections',
           '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
           '-I'+str(ROOT/'tests/support/native_renderer_host'),'-I'+str(ROOT/'include'),
           '-I'+str(ROOT/'build/3ds/source/ctr'),'-I/opt/devkitpro/libctru/include']
    objects=[]
    for index,source in enumerate(SOURCES):
        output=temporary/f'{index}.o'
        subprocess.run(['c++',*flags,'-c',str(ROOT/source),'-o',str(output)],check=True)
        objects.append(str(output))
    binary=temporary/'screen-bindings'
    subprocess.run(['c++',*flags,*objects,'-Wl,-dead_strip','-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],check=True,text=True,capture_output=True)
    assert not result.stderr,result.stderr
    report=json.loads(result.stdout)
report.update({'scope':'Real host CPU renderer texture binding and complete upload bytes; GPU calls observed, no PICA draw',
    'sanitizers':['address','undefined'],'Azahar_launched':False,'physical_performance_measured':False,
    'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in
        (*SOURCES,'include/sm64ds/game_renderer.hpp','tests/support/native_renderer_host/3ds.h')}})
target=ROOT/'build/native-performance-plan/screen-bindings-test.json'
target.parent.mkdir(parents=True,exist_ok=True)
target.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
