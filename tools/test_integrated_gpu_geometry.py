#!/usr/bin/env python3
"""Replay the vendor and generated GX GPU environment lookups side by side."""
import shutil, subprocess, sys, tempfile
from pathlib import Path
from prepare_integrated_gpu_geometry import prepare, SHADOWS_OLD, SHADOWS_NEW, FORCE_OLD, FORCE_NEW

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / 'third_party/sm64ds-3ds/port/platform/3ds/gx_gpu_backend.inc'

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static const char* values[2];static unsigned calls=0;
static const char* mock_getenv(const char* n){
 ++calls;
 if(!std::strcmp(n,"PLAYER_GPU_SHADOWS"))return values[0];
 if(!std::strcmp(n,"SM64DS_FORCE_3D"))return values[1];
 return nullptr;
}
static bool reference(float& slider){
@OLD_SHADOWS@
@OLD_FORCE@
 return true;
}
static bool generated(float& slider){
@NEW_SHADOWS@
@NEW_FORCE@
 return true;
}
int main(int argc,char** argv){
 values[0]=argc>1&&std::strcmp(argv[1],"-")?argv[1]:nullptr;
 values[1]=argc>2&&std::strcmp(argv[2],"-")?argv[2]:nullptr;
 unsigned generated_calls=0;
 for(unsigned frame=0;frame<600;++frame){
  float a=float(frame%7)/6.f,b=a;
  const bool ra=reference(a);
  const unsigned before=calls;const bool rb=generated(b);generated_calls+=calls-before;
  assert(ra==rb);
  if(ra)assert(std::memcmp(&a,&b,sizeof a)==0);
 }
 assert(generated_calls<=3);
 std::printf("ok shadows=%s force=%s generated_getenv_calls=%u\n",values[0]?values[0]:"-",values[1]?values[1]:"-",generated_calls);
}
'''


def body(snippet):
    # An early return from build() maps to the harness' "not built" result.
    return snippet.replace('std::getenv', 'mock_getenv')


def check():
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp) / 'port/platform/3ds'; base.mkdir(parents=True)
        shutil.copyfile(VENDOR, base / 'gx_gpu_backend.inc')
        assert prepare(tmp) == ['port/platform/3ds/gx_gpu_backend.inc']
        original = VENDOR.read_text(); generated = (base / 'gx_gpu_backend.inc').read_text()
        assert generated == original.replace(SHADOWS_OLD, SHADOWS_NEW).replace(FORCE_OLD, FORCE_NEW)
        start = generated.index('static bool build('); end = generated.index('static bool active_for(')
        per_frame = [l for l in generated[start:end].splitlines() if 'getenv' in l]
        assert per_frame and all(l.lstrip().startswith('static const') for l in per_frame), per_frame
        source = (HARNESS.replace('@OLD_SHADOWS@', body(SHADOWS_OLD)).replace('@OLD_FORCE@', body(FORCE_OLD))
                  .replace('@NEW_SHADOWS@', body(SHADOWS_NEW)).replace('@NEW_FORCE@', body(FORCE_NEW)))
        harness = Path(tmp) / 'harness.cpp'; harness.write_text(source)
        binary = Path(tmp) / 'harness'
        compiler = shutil.which('clang++') or shutil.which('c++'); assert compiler
        subprocess.run([compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', str(harness), '-o', str(binary)], check=True)
        results = []
        for shadows, force in (('-', '-'), ('1', '-'), ('', '-'), ('-', '1'), ('-', '0.5'),
                               ('-', '0'), ('-', 'abc'), ('-', '2.75'), ('1', '1')):
            out = subprocess.run([str(binary), shadows, force], check=True, capture_output=True, text=True).stdout
            results.append(out.strip())
        return dict(status='passed', cases=results)


if __name__ == '__main__':
    result = check()
    print('\n'.join(result['cases']))
    sys.exit(0)
