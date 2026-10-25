#!/usr/bin/env python3
"""Assemble the general ARM11 game, its original tables and native services.

This experimental target is separate from the distributed castle viewer.
Missing callable functions fail with their name; they never return success.
ROM data is loaded at run time. No cartridge code or assets are embedded here.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

import native_data_layout as layout
import native_operators

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/native-runtime'
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
FLAGS=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft']
OBJECT_REFERENCES={}
# Matching-only emission helpers deliberately define dummy key functions. GCC
# already emits their class metadata from the real method/destructor owner.
# Keep the real bodies, including BlendModelAnim's conditional resource free.
EMISSION_HELPERS={
    'src/_ZN13daObjMaruta_cD0Ev.cpp',
    'src/_ZN16daObjFallBlock_cD0Ev.cpp',
    'src/_ZN8SignPostD0Ev.cpp',
    'src/_ZThn80_N14BlendModelAnimD1Ev.cpp',
    'src/_ZNSt9type_infoD1Ev.cpp', # GCC/libstdc++ owns its RTTI runtime.
}
# These two recovered placeholders reference unnamed data with no owning ROM
# symbol. Their callable bodies remain explicit failures until that identity is
# recovered; do not manufacture zero-filled g0/g1/g2 or a guessed data alias.
INCOMPLETE_DATA_PROVIDERS={'src/func_ov006_020c0a48.c','src/func_ov006_020c221c.c',
                           'src/func_ov089_0213162c.c'}

# ASMP0's boot tiles are an LZ10 stream, not a pointer table. The pinned
# metadata misidentifies one compressed word as an OV085 data pointer.
# BootScene::InitResources passes data_020918c4 directly to DecompressLZ16.
# Replacing this word reproduces all 4,756 damaged bytes in the recorded VRAM
# dump. Keep exact, reviewed exceptions; do not infer exclusions from
# pointer-looking words in arbitrary assets or change the upstream checkout.
OPAQUE_DATA_RELOCATIONS = {
    ('arm9', 0x02091ae4, 'ov085', 0x0212ffff):
        'LZ10 boot tiles data_020918c4; compressed bytes are not an address',
    # Embedded SPA definitions 306/307 start with Flags1, not pointers.
    # Relocating 0x02083620 changes their optional-block strides and moves the
    # texture cursor into payload bytes. The exact captured-RAM replay reads
    # 0xfd6ed0d9 as a result. test_native_embedded_spa_arm.py verifies every
    # serialized byte, both manager capacities and retained real references.
    ('arm9', 0x0207c834, 'arm9', 0x02083620):
        'SPA data_02075f14 definition 306 Flags1; serialized flags are not an address',
    ('arm9', 0x0207c888, 'arm9', 0x02083620):
        'SPA data_02075f14 definition 307 Flags1; serialized flags are not an address',
    # The 724-byte original intro script data_02089af8 has byte-packed Kuppa
    # commands. These six aligned metadata words are times/parameters or
    # cross command boundaries, not pointer fields. __sinit_02073e6c owns
    # its three genuine unaligned pointers at +7/+11/+0x2cf. C14 RAM proves
    # that the first false fixup changes a command length from 11 to 116.
    ('arm9', 0x02089ba4, 'ov000', 0x020b0000):
        'Kuppa intro data_02089af8; numeric payload crosses the next command length/opcode',
    ('arm9', 0x02089bd0, 'arm9', 0x020a0244):
        'Kuppa intro data_02089af8; packed frame 580 and numeric operand, not a pointer',
    ('arm9', 0x02089c4c, 'arm9', 0x02091000):
        'Kuppa intro data_02089af8; numeric payload crosses the next command length/opcode',
    ('arm9', 0x02089cb0, 'arm9', 0x02090800):
        'Kuppa intro data_02089af8; numeric payload crosses the next command length/opcode',
    ('arm9', 0x02089cf0, 'ov007', 0x020f0000):
        'Kuppa intro data_02089af8; numeric payload crosses the next command length/opcode',
    ('arm9', 0x02089d78, 'ov007', 0x020f1000):
        'Kuppa intro data_02089af8; numeric payload crosses the next command length/opcode',
    # Reviewed NCSC SCRN and NCCG CHAR payloads consumed by the original menu.
    # tools/test_native_menu_assets.py verifies formats, hashes and exclusions.
    ('ov007', 0x020dc1f4, 'arm9', 0x02090208):
        'NCSC/SCRN graphics payload; bytes are not an address',
    ('ov007', 0x020dc1fc, 'ov007', 0x020d020c):
        'NCSC/SCRN graphics payload; bytes are not an address',
    ('ov007', 0x020dc200, 'ov007', 0x020f020e):
        'NCSC/SCRN graphics payload; bytes are not an address',
    ('ov007', 0x020dc204, 'ov002', 0x02110210):
        'NCSC/SCRN graphics payload; bytes are not an address',
    ('ov007', 0x020dc208, 'overlays(84,85)', 0x02130212):
        'NCSC/SCRN graphics payload; bytes are not an address',
    ('ov007', 0x020eddc8, 'ov007', 0x020d0f0f):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020ee580, 'ov002', 0x02110101):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020eeca8, 'ov002', 0x02110101):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020ef4b4, 'ov002', 0x02110101):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f4530, 'ov007', 0x020d2b5c):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f7bd8, 'arm9', 0x0208141f):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f7cc8, 'ov007', 0x020e2e08):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f7ce0, 'ov007', 0x020e1708):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f7cf0, 'ov006', 0x02142e02):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f8498, 'arm9', 0x02080b1f):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f84f8, 'ov007', 0x020e1702):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f85bc, 'ov007', 0x02102a1f):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f85e0, 'ov006', 0x02141402):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f8d1c, 'arm9', 0x02082a1f):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f8d20, 'ov007', 0x02102a0e):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f8d50, 'arm9', 0x02082a2a):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f8d80, 'arm9', 0x02082a28):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f9920, 'arm9', 0x02080210):
        'NCCG/CHAR graphics payload; bytes are not an address',
    ('ov007', 0x020f9a90, 'arm9', 0x02082a34):
        'NCCG/CHAR graphics payload; bytes are not an address',
}


def exclude_opaque_data_relocations(relocs):
    found = {row for row in relocs if row in OPAQUE_DATA_RELOCATIONS}
    if found != set(OPAQUE_DATA_RELOCATIONS):
        raise ValueError('Reviewed opaque resource relocation metadata changed')
    return [row for row in relocs if row not in found], [
        {'module': m, 'source': s, 'target_module': d, 'expected_word': t,
         'reason': OPAQUE_DATA_RELOCATIONS[(m, s, d, t)]}
        for m, s, d, t in sorted(found)]


def run(*args):
    subprocess.run(list(map(str,args)),cwd=ROOT,check=True)


def metadata():
    base=ROOT/'upstream/config/arm9'
    folders=[base,*sorted((base/'overlays').iterdir())]
    regions=[];symbols={};addresses={};relocs=[];ctors=[]
    for folder in folders:
        if not (folder/'symbols.txt').exists(): continue
        module='arm9' if folder==base else folder.name
        for line in (folder/'symbols.txt').read_text().splitlines():
            m=re.match(r'(\S+) kind:(\S+) addr:0x([0-9a-f]+)',line)
            if not m: continue
            record={'name':m[1],'kind':m[2],'module':module,'address':int(m[3],16)}
            symbols[m[1]]=record
            addresses.setdefault((module,record['address']),[]).append(record)
        ctor_ranges=[]
        for line in (folder/'delinks.txt').read_text().splitlines():
            if line and not line.startswith(' '): break
            m=re.match(r'    (\.\S+)\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+) kind:(\w+)',line)
            if not m: continue
            start,end=int(m[2],16),int(m[3],16)
            if m[1]=='.ctor': ctor_ranges.append((start,end))
            if m[1] in ('.rodata','.data','.bss') and end>start:
                regions.append(dict(module=module,section=m[1],start=start,end=end))
        for line in (folder/'relocs.txt').read_text().splitlines():
            m=re.match(r'from:0x([0-9a-f]+) kind:(\S+) to:0x([0-9a-f]+) module:(\S+)',line)
            if not m: continue
            source,target=int(m[1],16),int(m[3],16)
            dest='arm9' if m[4]=='main' else m[4]
            one=re.fullmatch(r'overlay\((\d+)\)',dest)
            if one:dest=f'ov{int(one[1]):03}'
            if any(a<=source<b for a,b in ctor_ranges):
                ctors.append((module,source,dest,target))
            elif layout.owner(regions,module,source):
                if m[2]!='load':raise ValueError('Unexpected data relocation '+line)
                relocs.append((module,source,dest,target))
    # ITCM contains compiler/SDK routines too; their recovered ARM11 providers
    # or named unavailable functions are used, never cartridge instructions.
    for memory in ('itcm','dtcm'):
        file=base/memory/'symbols.txt'
        if not file.exists():continue
        for line in file.read_text().splitlines():
            m=re.match(r'(\S+) kind:(\S+) addr:0x([0-9a-f]+)',line)
            if not m:continue
            record={'name':m[1],'kind':m[2],'module':'arm9','address':int(m[3],16)}
            symbols.setdefault(m[1],record)
            addresses.setdefault(('arm9',record['address']),[]).append(record)
    # Metadata's arm9 spellings alias the two overlay-1 shadow templates.
    for address in (0x020ad524,0x020ad560):
        overlay_name=f'data_ov001_{address:08x}'
        if overlay_name in symbols:
            name=f'data_{address:08x}'
            symbols[name]={**symbols[overlay_name],'name':name}
    return regions,symbols,addresses,relocs,ctors


def names(objects):
    definitions={};undefined=set()
    for start in range(0,len(objects),200):
        result=subprocess.check_output([TOOL+'nm','-A','-g',*map(str,objects[start:start+200])],cwd=ROOT,text=True)
        for line in result.splitlines():
            if ':' not in line:continue
            obj,detail=line.split(':',1);parts=detail.split()
            if len(parts)<2:continue
            kind,symbol=parts[-2:]
            if kind=='U':
                undefined.add(symbol);OBJECT_REFERENCES.setdefault(obj,set()).add(symbol)
            elif kind not in ('w','v'):
                previous=definitions.get(symbol)
                if previous and previous['kind'] not in ('W','V') and kind not in ('W','V'):
                    raise ValueError(f'Duplicate definition {symbol}: {previous["object"]}, {obj}')
                if not previous or previous['kind'] in ('W','V'):
                    definitions[symbol]={'kind':kind,'object':obj}
    return definitions,undefined


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extra-object',action='append',default=[])
    parser.add_argument('--extra-archive',action='append',default=[])
    parser.add_argument('--replace-source',action='append',default=[])
    parser.add_argument('--jobs',type=int,default=4)
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'objects').mkdir(exist_ok=True)
    report=json.loads((ROOT/'build/native-core/report.json').read_text())
    extras=[str(Path(p).resolve()) for p in args.extra_object]
    for path in args.extra_archive:
        archive=Path(path).resolve()
        directory=OUT/'providers'/hashlib.sha256(str(archive).encode()).hexdigest()[:16]
        directory.mkdir(parents=True,exist_ok=True)
        subprocess.run([TOOL+'ar','x',str(archive)],cwd=directory,check=True)
        extras.extend(str(p) for p in sorted(directory.glob('*.o')))
    extra_defs,_=names(extras)
    entries=[e for e in report['entries'] if e['compiled'] and
             e['source'] not in set(args.replace_source)|EMISSION_HELPERS|INCOMPLETE_DATA_PROVIDERS]
    # Selecting a replacement is explicit: a supplied native definition replaces
    # the complete original translation unit that owns it, never linker ordering.
    original_defs,_=names([e['object'] for e in entries])
    replaced_objects={d['object'] for n,d in original_defs.items() if n in extra_defs and d['kind'] not in ('W','V')}
    selected=[e for e in entries if e['object'] not in replaced_objects]
    def isolate(entry):
        source=ROOT/entry['object'];dest=OUT/'objects'/(hashlib.sha256(entry['object'].encode()).hexdigest()[:20]+'.o')
        stamp=dest.with_suffix('.sha256');source_bytes=source.read_bytes()
        source_digest=hashlib.sha256(source_bytes).hexdigest()
        digest=source_digest+'-native-abi3'
        if not dest.exists() or not stamp.exists() or stamp.read_text()!=digest:
            if not dest.exists() or not stamp.exists() or stamp.read_text() not in (source_digest,source_digest+'-native-abi2'):
                native_operators.copy_isolated(TOOL,source,dest,ROOT)
            # Some recovered C++ TUs declare an already mangled ROM symbol as
            # an ordinary function. Strip only the additional outer mangling;
            # argument words and the existing original callee remain unchanged.
            fixes=[]
            for name in OBJECT_REFERENCES.get(entry['object'],set()):
                m=re.match(r'_Z(\d+)',name)
                if not m:continue
                original=name[m.end():m.end()+int(m[1])]
                if original in original_defs or original in extra_defs:
                    fixes+=['--redefine-sym',name+'='+original]
            if fixes:run(TOOL+'objcopy',*fixes,dest)
            if b'.init_array' in source_bytes or b'.fini_array' in source_bytes:
                # The original .ctor table already owns these module initializers.
                # GCC's additional process-start list would call them before ROM
                # relocation/catalog setup, and call them twice on activation.
                run(TOOL+'objcopy','--remove-section=.init_array*',
                    '--remove-section=.fini_array*',dest)
            stamp.write_text(digest)
        return str(dest)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        objects=list(pool.map(isolate,selected))
    _,original_undefined=names(objects)
    definitions,undefined=names(objects+extras)
    regions,symbols,addresses,relocs,ctors=metadata()
    aliases={};unlocated={}
    for folder in (ROOT/'upstream/config/arm9/overlays').iterdir():
        if re.fullmatch(r'ov\d+',folder.name):aliases['overlay_'+str(int(folder.name[2:]))]=str(int(folder.name[2:]))
    for name,record in symbols.items():
        if not re.fullmatch(r'[A-Za-z_$][\w.$]*',name):continue
        if name in definitions or record['kind'].startswith('function'):continue
        if re.fullmatch(r'overlay_\d+',name):expression=str(int(name[8:]))
        else:expression=layout.expression(regions,record['module'],record['address'])
        if expression:aliases[name]=expression
        else:unlocated[name]=record
    missing=set();unresolved_pointers=[]
    def compiler_runtime(name):
        return name.startswith(('__aeabi_','__cxa_','__gxx_','_ZSt','_ZNSt','_ZNKSt','_ZNS',
                                '_Znw','_Zna','_Zdl','_Zda'))
    def resolve(module,address):
        # TCM code is recompiled with the main ARM11 corpus. Its original
        # module tag is a code-placement detail, not a separate runtime image.
        # Normalize before symbol lookup so collision-vtable entries resolve
        # to their real native bodies rather than becoming null data pointers.
        if module in ('itcm','dtcm'):module='arm9'
        records=addresses.get((module,address),[])
        functions=[r for r in records if r['kind'].startswith('function')]
        if functions:
            for record in functions:
                name=native_operators.SYMBOLS.get(record['name'],record['name'])
                if name in definitions or compiler_runtime(name):return name
            name=native_operators.SYMBOLS.get(functions[0]['name'],functions[0]['name'])
            missing.add(name);return name
        for record in records:
            name=record['name']
            if name in definitions and not name.startswith('_ZTV'):return name
        if module=='arm9' and address in (0x020ad524,0x020ad560):module='ov001'
        expression=layout.expression(regions,module,address)
        if expression:return expression
        # An unsupported data destination remains explicitly invalid, rather
        # than allocating fabricated game state. Report each exact reference.
        unresolved_pointers.append({'module':module,'address':address})
        return '0'
    def module_id(module):return 0xffffffff if module=='arm9' else int(module[2:])
    lines=[layout.assembly(regions),'.section .rodata.native_tables,"a",%progbits','.balign 4']
    def table(name,rows):
        lines.extend([f'.global {name}',f'{name}:'])
        for row in rows:lines.append('.word '+','.join(map(str,row)))
        lines.extend([f'.global {name}_end',f'{name}_end:'])
    table('sm64ds_runtime_sections',[(layout.section_name(i),module_id(r['module']),r['start'],r['end']-r['start'],int(r['section']=='.bss')) for i,r in enumerate(regions)])
    relocs, opaque_exclusions = exclude_opaque_data_relocations(relocs)
    fixed=[];dynamic=[];candidates=[]
    for origin,source,dest,target in relocs:
        i,r=layout.owner(regions,origin,source);offset=source-r['start']
        group=re.fullmatch(r'overlays\(([^)]+)\)',dest)
        if group:
            options=[f'ov{int(n):03}' for n in group[1].split(',')]
            resolved=[(module_id(m),resolve(m,target)) for m in options]
            unique=set(x[1] for x in resolved)
            if len(unique)==1:fixed.append((i,offset,target,resolved[0][1]))
            else:
                dynamic.append((i,offset,target,len(candidates),len(resolved)))
                candidates.extend(resolved)
                fixed.append((i,offset,target,0))
        else:fixed.append((i,offset,target,resolve(dest,target)))
    fixed.sort(key=lambda p:(p[0],p[1]));table('sm64ds_runtime_fixups',fixed)
    table('sm64ds_runtime_choices',dynamic);table('sm64ds_runtime_candidates',candidates)
    table('sm64ds_runtime_ctors',[(module_id(m),resolve(d,a)) for m,_,d,a in sorted(ctors)])
    lines.append('.section .note.GNU-stack,"",%progbits')
    (OUT/'native_tables.S').write_text('\n'.join(lines)+'\n')
    run(TOOL+'gcc',*FLAGS,'-c',OUT/'native_tables.S','-o',OUT/'native_tables.o')
    (OUT/'data-symbols.rsp').write_text('\n'.join('--defsym='+n+'='+v for n,v in sorted(aliases.items()))+'\n')
    # collect2 expands @response into argv on macOS; thousands of aliases would
    # exceed ARG_MAX before ld starts. A supplemental linker script stays small
    # on the command line and lets real platform definitions take precedence.
    (OUT/'data-symbols.ld').write_text('\n'.join('PROVIDE('+n+' = '+v+');' for n,v in sorted(aliases.items()))+'\n')
    # The final executable link first gets libc/libgcc and native providers;
    # named failure wrappers are only for game symbols unavailable in this build.
    known=set(definitions)|set(aliases)
    for name in undefined-known:
        if compiler_runtime(name):continue
        record=symbols.get(name)
        if record and record['kind'].startswith('function'):missing.add(name)
        elif name in original_undefined and name.startswith(('func_','_Z')) and not name.startswith(('_ZTV','_ZTI','_ZTS')):
            missing.add(name)
    traps=['.syntax unified','.arm']
    for index,name in enumerate(sorted(missing)):
        traps.extend([f'.section .text.unavailable_{index},"ax",%progbits',f'.global {name}',f'.type {name},%function',f'{name}:',f'ldr r0, 1f', 'b sm64ds_native_missing',f'1: .word 2f',f'.section .rodata.unavailable_{index},"a",%progbits',f'2: .asciz "{name}"'])
    traps.append('.section .note.GNU-stack,"",%progbits')
    (OUT/'unavailable.S').write_text('\n'.join(traps)+'\n')
    run(TOOL+'gcc',*FLAGS,'-c',OUT/'unavailable.S','-o',OUT/'unavailable.o')
    archive=OUT/'original.a'
    response=OUT/'objects.rsp';response.write_text('\n'.join('"'+o+'"' for o in objects)+'\n')
    signature=hashlib.sha256('\n'.join(o+':'+str(Path(o).stat().st_mtime_ns) for o in objects).encode()).hexdigest()
    archive_stamp=OUT/'archive.sha256'
    if not archive.exists() or not archive_stamp.exists() or archive_stamp.read_text()!=signature:
        archive.unlink(missing_ok=True)
        run(TOOL+'ar','rcs',archive,'@'+str(response));archive_stamp.write_text(signature)
    else:
        # Provider-only changes still regenerate tables and missing-function
        # ownership above. Complete the declared output timestamp without
        # rebuilding an unchanged original-source archive on every next build.
        archive.touch()
    manifest={'upstream_commit':report['upstream_commit'],'objects':len(objects),
        'sections':len(regions),'data_bytes':sum(r['end']-r['start'] for r in regions),
        'fixups':len(fixed),'overlay_choices':len(dynamic),'constructors':len(ctors),
        'unavailable_functions':sorted(missing),'unsupported_data_pointers':unresolved_pointers,
        'opaque_data_relocation_exclusions':opaque_exclusions,
        'replaced_sources':[e['source'] for e in entries if e['object'] in replaced_objects],
        'incomplete_data_providers':sorted(INCOMPLETE_DATA_PROVIDERS),
        'extra_objects':extras,'scope':'General experimental runtime, not verified playable'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({k:v for k,v in manifest.items() if k not in ('unavailable_functions','unsupported_data_pointers','replaced_sources','extra_objects')},indent=2))
    print('Unavailable functions:',len(missing),'unsupported data pointers:',len(unresolved_pointers))


if __name__=='__main__':main()
