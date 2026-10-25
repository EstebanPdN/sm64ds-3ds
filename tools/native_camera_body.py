#!/usr/bin/env python3
"""Generate and compile local camera objects; never alter the upstream corpus.

The original source hash and checked, unique substitutions make the complete
inactive path reviewable. Lifecycle originals are isolated copied objects,
whose public definitions alone are renamed; their callable bodies remain full.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
import native_operators

ROOT=Path(__file__).resolve().parents[1]
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
BODY_SHA='fc5b3d2b01c2992d6408c65240f0ffd98d212a27278b0697c89fdf27c421ae61'
LIFECYCLE={
    'src/_ZN6Camera13InitResourcesEv.cpp':('_ZN6Camera13InitResourcesEv','sm64ds_original_camera_init'),
    'src/_ZN6Camera16CleanupResourcesEv.cpp':('_ZN6Camera16CleanupResourcesEv','sm64ds_original_camera_cleanup'),
    'src/_ZN6Camera16OnPendingDestroyEv.cpp':('_ZN6Camera16OnPendingDestroyEv','sm64ds_original_camera_pending_destroy'),
}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def replace_once(text,old,new):
    if text.count(old)!=1:raise ValueError('Pinned camera body drift: '+old[:100])
    return text.replace(old,new)
def generate_text():
    path=ROOT/'upstream/src/func_02009e70.cpp'
    assert sha(path)==BODY_SHA
    text=path.read_text()
    changes=[]
    def change(value,old,new):
        result=replace_once(value,old,new);changes.append((old,new));return result
    text=change(text,'#include "dBgCh_Gnd.h"',
        '#include "sm64ds/native_camera.hpp"\n#include "dBgCh_Gnd.h"')
    text=change(text,'s32 func_02009e70(char *self)\n{',
        's32 func_02009e70(char *self)\n{\n    bool manual_active = false;\n    bool manual_height_ok = false;')
    text=change(text,'    bool manual_height_ok = false;',
        '    bool manual_height_ok = false;\n    bool manual_collision_fallback = false;')
    before='    Vec3_Sub(&sp40, (struct Vector3 *)(self + 0x8c), (struct Vector3 *)(self + 0x80));'
    text=change(text,before,
        '    manual_active = sm64ds_native_camera_prepare(self, sp1c, sp20, sp24, fp, sl) != 0;\n'+before)
    text=change(text,'L_A534:\n    if (r5 != 0)',
        'L_A534:\n    if (manual_active) goto L_AAD0;\n    if (r5 != 0)')
    # func02008cb4's known-wall early returns depend on the automatic branch
    # having corrected this hit first. Retain that entire original path when
    # the real precheck hits; finish treats its solution as a constraint and
    # rebases orientation while retaining the desired (unshortened) radius.
    text=change(text,'    if (line.DetectClsn() == 0) goto L_A534;',
        '    if (line.DetectClsn() == 0) goto L_A534;\n'
        '    if (manual_active) {\n'
        '        manual_collision_fallback = true;\n'
        '        manual_active = false;\n'
        '    }')
    start='    r7h = sb;\n    if (sb < r6) r7h = r6;'
    end='    else if (sp40.y > t0) r7h = *(s32 *)(self + 0x84) + t0;'
    assert text.count(start)==1 and text.count(end)==1
    begin=text.index(start);finish=text.index(end,begin)+len(end)
    original=text[begin:finish]
    text=change(text,original,'    if (manual_active) {\n'
        '        const s32 manual_floor = static_cast<s32>(static_cast<u32>(sb) + 0x25bc8u);\n'
        '        const s32 manual_ceiling = r4 < manual_floor ? sp4c.y : r4;\n'
        '        manual_height_ok = sm64ds_native_camera_height_range(self, manual_floor, manual_ceiling) != 0;\n'
        '        if (!manual_height_ok) manual_active = false;\n'
        '    }\n'
        '    if (manual_active) {\n'
        '        r7h = *(s32 *)(self + 0x90); // Post-wall solved eye, before spatial clamps.\n'
        '    } else {\n'+original+'\n    }')
    approach='    Math_Function_0203b14c((s32 *)(self + 0x90), r7h, 0x300, 0x14000, 0x100);'
    text=change(text,approach,
        '    if (manual_active) {\n'
        '        if (manual_height_ok) *(s32 *)(self + 0x90) = (s32)r7h;\n'
        '    }\n    if (!manual_active || !manual_height_ok) {\n'+approach+'\n    }')
    text=change(text,'    return 1;\n}',
        '    sm64ds_native_camera_finish(self, manual_active ? 1 : (manual_collision_fallback ? 2 : 0));\n    return 1;\n}')
    inverse=text
    for old,new in reversed(changes):inverse=replace_once(inverse,new,old)
    assert inverse==path.read_text() and hashlib.sha256(inverse.encode()).hexdigest()==BODY_SHA
    return text
def normalize(tool,obj,known):
    # Same narrow repair as build_native_runtime: a recovered extern "C"
    # spelling may be wrapped in an outer C++ mangling by GCC.
    args=[];fixes=[]
    listing=subprocess.check_output([tool+'nm','-u',str(obj)],text=True)
    for line in listing.splitlines():
        name=line.split()[-1] if line.split() else ''
        match=re.match(r'_Z(\d+)',name)
        if not match:continue
        inner=name[match.end():match.end()+int(match[1])]
        if inner in known:
            args+=['--redefine-sym',name+'='+inner];fixes.append({'from':name,'to':inner})
    if args:subprocess.run([tool+'objcopy',*args,str(obj)],check=True)
    return fixes
def build(out,corpus_path=None):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    corpus_path=corpus_path or ROOT/'build/native-core/report.json'
    corpus=json.loads(Path(corpus_path).read_text())
    flags=corpus['flags']
    owners={e['source']:e for e in corpus['entries']}
    known=set()
    for file in (ROOT/'upstream/config/arm9').rglob('symbols.txt'):
        known.update(line.split()[0] for line in file.read_text().splitlines() if ' kind:' in line)
    known.update(native_operators.SYMBOLS.values())
    known.update({'sm64ds_native_camera_prepare','sm64ds_native_camera_height_range','sm64ds_native_camera_finish'})
    body=out/'camera_body.cpp';body.write_text(generate_text())
    (out/'camera_body.diff').write_text(''.join(difflib.unified_diff(
        (ROOT/'upstream/src/func_02009e70.cpp').read_text().splitlines(True),body.read_text().splitlines(True),
        fromfile='pinned-upstream/func_02009e70.cpp',tofile='local/camera_body.cpp')))
    sources=[ROOT/'source/game_port/native_camera.cpp',ROOT/'source/core/camera_orbit.cpp',body]
    objects=[];records=[]
    for index,source in enumerate(sources):
        obj=out/('camera-'+str(index)+'.o')
        subprocess.run([TOOL+'g++',*flags,'-std=gnu++17','-I'+str(ROOT/'include'),
                        '-c',str(source),'-o',str(obj)],cwd=ROOT,check=True)
        fixes=normalize(TOOL,obj,known)
        records.append({'source':str(source),'source_sha256':sha(source),'object':str(obj),
            'object_sha256':sha(obj),'abi_repairs':fixes})
        objects.append(obj)
    originals=[]
    for source,(public,private) in LIFECYCLE.items():
        owner=owners[source];assert owner['compiled']
        assert sha(ROOT/'upstream'/source)==owner['sha256_original']
        original=ROOT/owner['object'];obj=out/(private+'.o')
        native_operators.copy_isolated(TOOL,original,obj,ROOT)
        subprocess.run([TOOL+'objcopy','--redefine-sym',public+'='+private,str(obj)],check=True)
        fixes=normalize(TOOL,obj,known)
        definitions=subprocess.check_output([TOOL+'nm','-g','--defined-only',str(obj)],text=True)
        assert any(line.split()[-1]==private for line in definitions.splitlines())
        assert not any(line.split()[-1]==public for line in definitions.splitlines())
        objects.append(obj);originals.append({**owner,'original_object_sha256':sha(original),
            'isolated_object':str(obj),'isolated_object_sha256':sha(obj),'public':public,'private':private,'abi_repairs':fixes})
    # Core/controller/body must retain the platform's ordinary runtime ABI;
    # original lifecycle copies have already had only game allocator names
    # isolated. Body has no new allocation.
    manifest={'schema':1,'scope':'Local unintegrated camera provider group; no general link or emulator launch',
        'upstream_commit':subprocess.check_output(['git','-C',str(ROOT/'upstream'),'rev-parse','HEAD'],text=True).strip(),
        'body_original_sha256':BODY_SHA,'generated_body_sha256':sha(body),'generator_sha256':sha(Path(__file__)),
        'inverse_reconstruction_verified':True,'inverse_sha256':BODY_SHA,
        'objects':[str(p) for p in objects],'sources':records,'lifecycle_originals':originals,
        'replaced_sources':['src/func_02009e70.cpp',*LIFECYCLE],
        'logical_vblank_rate':{'numerator':60,'denominator':1},'initial_mode':'data_0208715c',
        'flags':flags,'upstream_modified':False}
    (out/'camera-group.json').write_text(json.dumps(manifest,indent=2)+'\n')
    cmake=['# Generated local camera group. Original source ownership must be excluded as a unit.',
           'set(NATIVE_CAMERA_OBJECTS']+['  "'+str(p)+'"' for p in objects]+[')',
           'set(NATIVE_CAMERA_REPLACED_SOURCES']+['  "'+s+'"' for s in manifest['replaced_sources']]+[')',
           'set_source_files_properties(${NATIVE_CAMERA_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE)',
           '']
    (out/'objects.cmake').write_text('\n'.join(cmake))
    return manifest
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=ROOT/'build/native-camera-plan/provider-group')
    parser.add_argument('--corpus',type=Path)
    parser.add_argument('--summary',action='store_true',help='Print a compact summary; keep the full JSON manifest on disk')
    args=parser.parse_args()
    manifest=build(args.out,args.corpus)
    if args.summary:
        result={key:manifest[key] for key in ('schema','scope','replaced_sources','inverse_sha256')}
        result['object_count']=len(manifest['objects'])
    else:result=manifest
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
