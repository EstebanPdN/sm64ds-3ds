#!/usr/bin/env python3
"""Static Stage resource frontier of the frozen ARM11 candidate; no execution."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
ELF=ROOT/'build/native-integration-check/candidate-vptr-file-camera-tls/sm64ds-native.elf'
HASH='396ac3ff7f20af01d411357d97da9cdc4dccdee70f6fc53d1d8305a5804361c8'
OUT=ROOT/'build/native-stage-plan'
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
ROOTS=(
    '_ZN5Stage13InitResourcesEv',
    '_ZN5Stage18LoadClsnAndObjectsER11LVL_OverlayjR7dBgW_Kc',
    '_ZN6Camera13InitResourcesEv','_ZN3HUD13InitResourcesEv',
    '_ZN7Minimap13InitResourcesEv','_ZN6Player13InitResourcesEv',
    '_ZN5Stage8BehaviorEv','_ZN5Stage6RenderEv',
    '_ZN6Camera8BehaviorEv','_ZN6Camera6RenderEv',
    '_ZN3HUD8BehaviorEv','_ZN3HUD6RenderEv',
    '_ZN7Minimap8BehaviorEv','_ZN7Minimap6RenderEv','_ZN6Player6RenderEv',
    '_Z19LoadEntranceObjectsRN11LVL_Overlay11ObjSubTableEij',
    '_Z11LoadObjectsRN11LVL_Overlay8ObjTableEij',
    'dCamera_c_classInit','dMeter_c_classInit','dMap_c_classInit',
    '_ZN7dBase_c5SpawnEjP7fBase_cii','_ZN8dActor_c5SpawnEjjRK7Vector3PK10Vector3_16as')


def digest(data):return hashlib.sha256(data).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,default=ELF)
    parser.add_argument('--expected-sha256',default=HASH)
    parser.add_argument('--out',type=Path,default=OUT)
    parser.add_argument('--depth',type=int,choices=range(1,9),default=4)
    parser.add_argument('--root',action='append',default=[],help='Additional exact linked roots')
    options=parser.parse_args()
    elf=options.elf.resolve();out=options.out.resolve()
    assert digest(elf.read_bytes())==options.expected_sha256
    out.mkdir(parents=True,exist_ok=True)
    disassembly=subprocess.check_output([TOOL+'objdump','-d',str(elf)],text=True)
    functions={};name=None
    for line in disassembly.splitlines():
        header=re.match(r'^([0-9a-f]+) <(.+)>:$',line)
        if header:
            name=header[2];functions[name]=dict(address=int(header[1],16),lines=[])
        elif name and re.match(r'^\s+[0-9a-f]+:',line):functions[name]['lines'].append(line)
    # Classify stubs from this exact ELF, rather than trusting a mutable map or
    # the complete unavailable source list (GC can remove unused stubs).
    unavailable={name for name,body in functions.items()
        if any(re.search(r'\bb\s+[0-9a-f]+ <sm64ds_native_missing>',line)
               for line in body['lines'][:3])}
    calls={};indirect={}
    for name,body in functions.items():
        edges=[];dynamic=[]
        for line in body['lines']:
            instruction=re.match(r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+([a-z0-9.]+)\s+(.*)$',line)
            if not instruction:continue
            pc,opcode,args=instruction.groups()
            branch=re.match(r'([0-9a-f]+) <([^>]+)>',args)
            if re.fullmatch(r'(?:blx|bl|b)(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?',opcode) and branch:
                target=branch[2].split('+')[0]
                if target!=name:edges.append(dict(pc='0x'+pc,target=target,opcode=opcode))
            elif opcode.startswith('blx') or opcode.startswith('bx') and args!='lr':
                dynamic.append(dict(pc='0x'+pc,instruction=opcode+' '+args))
        calls[name]=edges;indirect[name]=dynamic
    stems={}
    for path in (ROOT/'upstream/src').rglob('*'):
        if path.suffix not in ('.c','.cpp','.S'):continue
        stems.setdefault(path.stem,[]).append(path)
    special={'dCamera_c_classInit':'d_camera','dMeter_c_classInit':'d_meter','dMap_c_classInit':'d_map'}
    def source_paths(name):
        stem=special.get(name,name.split('.')[0]);return stems.get(stem,[])
    def source_record(path):
        text=path.read_text();rows=[]
        for number,line in enumerate(text.splitlines(),1):
            literals=[]
            for match in re.finditer(r'\b0x[0-9a-fA-F]+\b',line):
                value=int(match[0],16)
                if 0x04000000<=value<0x08000000:literals.append(hex(value))
            if literals:rows.append(dict(line=number,text=line.strip(),DS_literals=literals))
        generated=ROOT/'build/native-core/generated'/path.relative_to(ROOT/'upstream')
        scene=ROOT/'build/native-runtime/scene-video'/path.name
        return dict(path=str(path.relative_to(ROOT)),sha256=digest(path.read_bytes()),DS_literals=rows,
            generated_source=str(generated.relative_to(ROOT)) if generated.exists() else None,
            generated_sha256=digest(generated.read_bytes()) if generated.exists() else None,
            scene_source=str(scene.relative_to(ROOT)) if scene.exists() else None,
            scene_sha256=digest(scene.read_bytes()) if scene.exists() else None)
    frontiers={};selected=set()
    for root in (*ROOTS,*options.root):
        assert root in functions,root
        seen={root:[]};queue=[(root,0)]
        while queue:
            name,depth=queue.pop(0)
            if depth==options.depth or name in unavailable:continue
            for edge in calls.get(name,[]):
                target=edge['target']
                if target in seen:continue
                seen[target]=[*seen[name],dict(caller=name,**edge)]
                if target in functions:queue.append((target,depth+1))
        selected.update(seen)
        frontiers[root]=dict(address=hex(functions[root]['address']),
            direct_calls=calls[root],indirect_calls=indirect[root],
            unavailable_with_paths=[dict(function=name,path=path) for name,path in seen.items() if name in unavailable],
            depth_limit=options.depth,static_reachable=list(seen))
    records={name:dict(address=hex(functions[name]['address']) if name in functions else None,
        unavailable=name in unavailable,direct_calls=calls.get(name,[]),indirect_calls=indirect.get(name,[]),
        sources=[source_record(path) for path in source_paths(name)]) for name in sorted(selected)}
    # Table values are original ROM data, not an assertion that native boot
    # populated the relocated aliases. Inspect only the reviewed fixed roots.
    import test_native_audio_deferred_arm as original
    cart=original.base.Cartridge(ROOT/'build/europe.pak')
    original_names={}
    for module,config in ((0xffffffff,ROOT/'upstream/config/arm9/symbols.txt'),
                          (2,ROOT/'upstream/config/arm9/overlays/ov002/symbols.txt')):
        for line in config.read_text().splitlines():
            match=re.match(r'(\S+) kind:function\([^)]*\) addr:0x([0-9a-f]+)',line)
            if match:original_names[int(match[2],16)]=match[1]
    def table(module,address,count,stride=4):
        base,_,image=cart.image(module);offset=address-base
        assert 0<=offset and offset+count*stride<=len(image)
        raw=image[offset:offset+count*stride];entries=[]
        for index in range(count):
            pointer=struct.unpack_from('<I',raw,index*stride)[0]
            name=original_names.get(pointer)
            entries.append(dict(index=index,original_pointer=hex(pointer),function=name,
                frozen_native_address=hex(functions[name]['address']) if name in functions else None,
                unavailable=name in unavailable if name else None))
        return dict(module=module,address=hex(address),stride=stride,sha256=digest(raw),entries=entries)
    profiles=[]
    for index in (3,0xbf,0x14c,0x14e,0x14f):
        slot=table(0xffffffff,0x02090864+index*4,1)
        pointer=int(slot['entries'][0]['original_pointer'],16)
        owner=[]
        for module in (0xffffffff,2):
            base,_,image=cart.image(module)
            if base<=pointer and pointer+4<=base+len(image):owner.append(module)
        assert len(owner)==1,(index,pointer,owner)
        profiles.append(dict(profile=hex(index),profile_slot=slot,
            factory_record=table(owner[0],pointer,1)))
    tables=dict(category_loaders=table(2,0x0210cbb8,15),factories=profiles)
    report=dict(scope=__doc__,ELF=str(elf.relative_to(ROOT)),ELF_sha256=options.expected_sha256,
        classification='Exact frozen ELF stub body; mutable runtime.map is not used',
        frontiers=frontiers,functions=records,original_static_tables=tables,linked_unavailable_functions=sorted(unavailable),
        limitations='Direct BL/BLX/B with condition suffixes. Indirect tables/vtables need manual source/table review. Source DS literals are candidates, not proof of an unadapted access in the binary. No function executed.',
        executions=0,UI_launched=False,production_changed=False)
    (out/'stage-static-closure.json').write_text(json.dumps(report,indent=2)+'\n')
    selected_text=[]
    for name in sorted(selected):
        if name in functions:selected_text.extend(['\n'+hex(functions[name]['address'])+' <'+name+'>:',*functions[name]['lines']])
    (out/'frontier-disassembly.txt').write_text('\n'.join(selected_text)+'\n')
    for root,entry in frontiers.items():
        print(root,entry['address'],'direct',len(entry['direct_calls']),
            'unavailable',[item['function'] for item in entry['unavailable_with_paths']])
    print('Linked unavailable',len(unavailable),'selected',len(selected),'report',out/'stage-static-closure.json')


if __name__=='__main__':main()
