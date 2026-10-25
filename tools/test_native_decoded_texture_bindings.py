#!/usr/bin/env python3
"""Gate immutable decoded-texture reuse against all captured C12 cache pixels."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCES=('tests/test_native_decoded_texture_bindings.cpp','source/ctr/game_renderer.cpp',
    'source/core/assets.cpp','source/core/game_textures.cpp','source/core/game_geometry.cpp',
    'source/vendor/ntr/texture.cpp')
HEADERS=('include/sm64ds/game_renderer.hpp','include/sm64ds/game_textures.hpp',
    'include/sm64ds/game_geometry.hpp','tests/support/native_renderer_host/3ds.h')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture-directory',type=Path,default=ROOT/'build/native-integration-check/render-cache-C12')
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-integration-check/decoded-texture-bindings-gate.json')
    args=parser.parse_args();manifest=json.loads((args.fixture_directory/'report.json').read_text())
    fixture=args.fixture_directory/'captured-textures.bin'
    assert manifest['status']=='passed' and sha(fixture)==manifest['fixture']['sha256']
    with tempfile.TemporaryDirectory(prefix='sm64ds-decoded-texture-bindings-') as directory:
        temp=Path(directory)
        flags=['-std=c++20','-O2','-D__3DS__','-UNDEBUG','-Wall','-Wextra',
            '-Wno-misleading-indentation','-ffunction-sections','-fdata-sections',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
            '-I'+str(ROOT/'tests/support/native_renderer_host'),'-I'+str(ROOT/'include'),
            '-I'+str(ROOT/'build/3ds/source/ctr'),'-I/opt/devkitpro/libctru/include']
        objects=[]
        for i,path in enumerate(SOURCES):
            obj=temp/f'{i}.o';subprocess.run(['c++',*flags,'-c',str(ROOT/path),'-o',str(obj)],check=True);objects.append(str(obj))
        binary=temp/'decoded-texture-bindings'
        subprocess.run(['c++',*flags,*objects,'-Wl,-dead_strip','-o',str(binary)],check=True)
        result=subprocess.run([str(binary),str(fixture)],text=True,capture_output=True)
        assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
        assert not result.stderr
        report=json.loads(result.stdout)
    assert sha(fixture)==manifest['fixture']['sha256']
    report.update(scope='Real production decoder and CPU GPU-texture cache; full acquired pixel comparison. GPU allocation/flush/delete observed; no PICA, game or FPS execution.',
        fixture_sha256=sha(fixture),fixture_manifest_sha256=sha(args.fixture_directory/'report.json'),
        original_captured_ELF_sha256=manifest['ELF_sha256'],
        sanitizers=['address','undefined'],physical_performance_measured=False,
        checks=['53 complete acquired textures decode byte-identically',
            'Repeated unchanged decoded versions perform zero complete-image CRC',
            'Texture generation change performs a new full CRC even for identical bytes',
            'Actual palette mutation keeps old/new versions distinct within the frame',
            'Unknown pointers retain full CRC and detect direct pixel mutations',
            'Separate decoder instances issue distinct lifetime identities',
            'Allocation failure and eviction do not leave stale certified GPU bindings'],
        source_sha256={p:sha(ROOT/p) for p in (*SOURCES,*HEADERS,'tools/test_native_decoded_texture_bindings.py')})
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
