#!/usr/bin/env python3
"""Build the original KCL collision subset as ARM11 code, with audited providers.

The line query uses upstream's explicitly non-byte-matched port adoption.
Ground and sphere use the original decompilation; only the sphere's DS raw
sqrt/IRQ accesses are replaced. Never resolve missing operations with stubs.
"""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import native_core
import native_operators

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build/native-collision'
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
ROOTS = ['native_collision_init','native_collision_ground','native_collision_destroy',
         'native_collision_sphere','native_collision_line']
SPHERE = 'src/_ZN7dBgW_Kc10DetectClsnER12dBgCh_SphCrr.cpp'
LINE = 'port/unmatched/MeshCollider_DetectClsn_RaycastLine.cpp'

def run(*args):
    subprocess.run([str(a) for a in args],cwd=ROOT,check=True)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    report=json.loads((ROOT/'build/native-core/report.json').read_text())
    if report['upstream_commit']!=native_core.PIN:
        raise ValueError('Native source corpus does not match the pinned upstream')
    flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
           '-D__3DS__','-Os','-fno-strict-aliasing','-ffunction-sections',
           '-fdata-sections','-Wa,--noexecstack','-Iinclude',
           '-Ibuild/native-core/generated/include']
    original=(ROOT/'upstream'/SPHERE).read_text()
    sphere,changes,_=native_core.adapt(SPHERE,original)
    (OUT/'sphere.cpp').write_text(sphere)
    sources=[(OUT/'sphere.cpp','sphere.o'),(ROOT/'upstream'/LINE,'line.o'),
             (ROOT/'source/ctr/game_collision.cpp','bridge.o'),
             (ROOT/'tests/game_collision_arm.cpp','wrappers.o')]
    for source,name in sources:
        run(TOOL+'g++',*flags,'-std=gnu++17','-c',source,'-o',OUT/name)
    extras=[
        'build/3ds/CMakeFiles/sm64ds_math.dir/source/core/fixed_math.o',
        'build/3ds/source/ctr/CMakeFiles/sm64ds_game_math.dir/game_math.o',
        'build/native-globals/game_bss.o',
        *[str((OUT/name).relative_to(ROOT)) for _,name in sources],
    ]
    replacements=[SPHERE,'src/_ZN4cstd3divEii.cpp','src/_ZN4cstd4fdivEii.cpp',
        'src/_ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE.c','src/_ZN4cstd11fdiv_resultEv.cpp',
        'src/_ZN4cstd4ldivEii.cpp','src/_ZN4cstd11ldiv_resultEv.cpp','src/_ZN4cstd4sqrtEy.cpp',
        'src/func_02053274.c','src/NormalizeVec3.c','src/func_02049018.c']
    command=[sys.executable,'tools/native_core_inventory.py','--roots-only',
             '--output-directory',str(OUT),'--link-frontier']
    for root in ROOTS: command+=['--root-symbol',root]
    for obj in extras: command+=['--extra-object',obj]
    for source in replacements: command+=['--replace-source',source]
    run(*command)
    objects=shlex.split((OUT/'frontier-objects.rsp').read_text())
    isolated=OUT/'objects';isolated.mkdir(exist_ok=True)
    production=[];tests=[];manifest=[]
    source_for={e['object']:e['source'] for e in report['entries'] if e.get('object')}
    for obj in objects:
        if obj.endswith('/wrappers.o'):
            tests.append(obj);continue
        dest=isolated/(hashlib.sha256(obj.encode()).hexdigest()[:12]+'.o')
        native_operators.copy_isolated(TOOL,obj,dest,ROOT)
        relative=str(dest.relative_to(ROOT));production.append(relative)
        manifest.append({'object':relative,'original_object':obj,
            'source':source_for.get(obj,'platform-adapter'),
            'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
    native_operators.verify_isolated(TOOL,production,ROOT)
    rsp=OUT/'production-objects.rsp'
    rsp.write_text('\n'.join('"'+p+'"' for p in production)+'\n')
    elf=OUT/'runtime.elf'
    link='--gc-sections,-z,noexecstack,--fatal-warnings,-e,'+ROOTS[0]
    link+=''.join(',-u,'+root for root in ROOTS[1:])
    link+=',-Ttext=0x100000,-Map='+str(OUT/'runtime.map')
    run(TOOL+'g++',*flags[:4],'-nostartfiles','-Wl,'+link,*tests,'@'+str(rsp),'-o',elf)
    unresolved=subprocess.check_output([TOOL+'nm','-u',str(elf)],text=True)
    if unresolved.strip(): raise ValueError(unresolved)
    run(TOOL+'objcopy','-O','binary',elf,OUT/'runtime.bin')
    result={'scope':'Original static collider: linked ARM11; execution verified separately',
        'upstream_commit':native_core.PIN,'isolated_game_operators':True,
        'symbol_isolation':native_operators.SYMBOLS,
        'objects':manifest,'sphere_adaptations':changes,
        'line_provider':LINE,'line_provider_byte_matched':False,
        'line_sha256':hashlib.sha256((ROOT/'upstream'/LINE).read_bytes()).hexdigest(),
        'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest()}
    (OUT/'objects.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Collision ARM11 executable linked; allocator symbols isolated. Run test_game_collision_arm.py.')

if __name__=='__main__': main()
