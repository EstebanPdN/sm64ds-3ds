#!/usr/bin/env python3
"""Read-only resource/lifetime audit of the exact pre-Stage RAM acquisition.

No game code is executed. Captured physical bytes and archived ELF read-only
data are distinct sources. A resident pointer is not declared a live object.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from test_native_title_fade_arm import Snapshot

ROOT=Path(__file__).resolve().parents[1]
ATTEMPT=ROOT/'build/native-runtime-azahar/attempt-1791624434443135000'
EXPECTED_ELF='cd3799f6752140172893ec61b384777078c614b21d9e726b20cff8538e4f3f29'
EXPECTED_RAM='f4ac712c7d5cbe623782f01cf40d5c2a9352a3882702bb85027ed134ba9aaac6'
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
OUT=ROOT/'build/native-integration-check/stage-requested-443444/resources'
PROFILES=[('Stage',3,'dScStage_c_classInit',0x9c8),
          ('Player',0xbf,'daPly_c_classInit',0x768),
          ('Camera',0x14c,'dCamera_c_classInit',0x1a8),
          ('HUD',0x14e,'dMeter_c_classInit',0x7c),
          ('Minimap',0x14f,'dMap_c_classInit',0x258)]
SLOTS=['InitResources','BeforeInitResources','AfterInitResources',
       'CleanupResources','BeforeCleanupResources','AfterCleanupResources',
       'Behavior','BeforeBehavior','AfterBehavior','Render','BeforeRender',
       'AfterRender','OnPendingDestroy','Virtual34','Virtual38','OnHeapCreated',
       'Destructor1','Destructor0']

def sha(data):return hashlib.sha256(data).hexdigest()

class ElfReadOnly:
    def __init__(self,path):
        self.raw=path.read_bytes();assert self.raw[:7]==b'\x7fELF\x01\x01\x01'
        self.loads=[]
        phoff=struct.unpack_from('<I',self.raw,28)[0]
        phsize,phcount=struct.unpack_from('<HH',self.raw,42)
        for i in range(phcount):
            kind,offset,address,_,filesz,memsz,flags,_=struct.unpack_from('<8I',self.raw,phoff+i*phsize)
            if kind==1:self.loads.append(dict(offset=offset,address=address,filesz=filesz,size=memsz,flags=flags))
    def read(self,address,size):
        for s in self.loads:
            if not s['flags']&2 and s['address']<=address and address+size<=s['address']+s['filesz']:
                start=s['offset']+address-s['address'];return self.raw[start:start+size]
        raise ValueError('Requested bytes are not archived ELF read-only file data')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot',type=Path,default=ROOT/'build/native-runtime-azahar/data/azahar-emu/sdmc/3ds/sm64ds/native/ram-stage-requested.ram.bin')
    args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    elf_path=ATTEMPT/'sm64ds-native.elf';elf=ElfReadOnly(elf_path)
    assert sha(elf.raw)==EXPECTED_ELF
    snap=Snapshot(args.snapshot,ATTEMPT/'ram-stage-requested.ram.json')
    assert sha(snap.raw)==EXPECTED_RAM
    assert snap.metadata['runtime_section_count']==226
    listing=subprocess.check_output([TOOL+'nm','-g','-S','--defined-only',str(elf_path)],text=True)
    symbols={};address_names={}
    for line in listing.splitlines():
        f=line.split()
        if len(f) not in (3,4):continue
        address=int(f[0],16);size=int(f[1],16) if len(f)==4 else 0;kind=f[-2];name=f[-1]
        symbols[name]=dict(address=address,size=size,kind=kind)
        address_names.setdefault(address,[]).append(name)
    addr=lambda n:symbols[n]['address']
    assert snap.spans[0]['address']==addr('sm64ds_diagnostic_rw_start')
    assert snap.spans[0]['address']+snap.spans[0]['size']==addr('sm64ds_diagnostic_rw_end')
    def native_read(address,size):
        if any(snap.contains(s,address,size) for s in snap.spans):return snap.read(address,size)
        return elf.read(address,size)
    def words(address,count):return struct.unpack('<'+'I'*count,native_read(address,count*4))
    disasm=subprocess.check_output([TOOL+'objdump','-d',str(elf_path)],text=True)
    functions={};current=None
    for line in disasm.splitlines():
        h=re.match(r'^([0-9a-f]+) <(.+)>:$',line)
        if h:
            current=int(h[1],16);functions[current]=dict(name=h[2],lines=[])
        elif current is not None and re.match(r'^\s+[0-9a-f]+:',line):functions[current]['lines'].append(line)
    unavailable={a for a,b in functions.items() if any('<sm64ds_native_missing>' in x and re.search(r'\bb\s',x) for x in b['lines'][:3])}
    def code_record(address):
        return dict(address=hex(address),names=address_names.get(address,[]),
                    linked_code=address in functions,unavailable_stub=address in unavailable)
    heaps={}
    def audit_heap(address):
        vptr,start,size,parent,flags,allocator=words(address,6)
        aw=words(allocator,14);assert aw[0]==0x45585048
        assert start<=allocator and allocator+0x38<=start+size
        lists={}
        for label,index,expected_tag in [('free',9,0x4652),('used',11,0x5544)]:
            node=aw[index];previous=0;visited=set();entries=[]
            while node:
                if len(visited)>=16384 or node in visited:raise ValueError('Node cycle or audit bound exceeded')
                visited.add(node)
                assert aw[6]<=node and node+16<=aw[7]
                tag,padding,nsize,prev,nxt=struct.unpack('<HH3I',snap.read(node,16))
                assert tag==expected_tag and prev==previous and node+16+nsize<=aw[7]
                entries.append(dict(address=hex(node),tag=hex(tag),flags=padding,size=nsize,
                    payload_start=node+16,payload_end=node+16+nsize))
                previous=node;node=nxt
            assert (previous==aw[index+1])
            lists[label]=entries
        return dict(address=hex(address),vptr=hex(vptr),vptr_is_native_expanding_heap=vptr==addr('_ZTV13ExpandingHeap')+8,
            start=start,size=size,end=start+size,parent=hex(parent),flags=hex(flags),fail_fast=bool(flags&0x4000),
            allocator=hex(allocator),allocator_magic=hex(aw[0]),allocator_data_start=aw[6],allocator_data_end=aw[7],
            free_bytes=sum(n['size'] for n in lists['free']),largest_raw_free_payload=max((n['size'] for n in lists['free']),default=0),
            lists=lists,bounded_lists_well_formed=True,actual_allocate_executed=False)
    root=snap.u32(addr('data_020a0e9c'));actor=snap.u32(addr('data_020a0eac'))
    heaps['root']=audit_heap(root);heaps['actor']=audit_heap(actor)
    def residency(pointer):
        if pointer==0:return dict(pointer=hex(pointer),classification='null')
        free=[label for label,h in heaps.items() for n in h['lists']['free'] if n['payload_start']<=pointer<n['payload_end']]
        resident=any(snap.contains(s,pointer,1) for s in snap.spans)
        return dict(pointer=hex(pointer),resident=resident,in_free_payloads=free,
            classification='residual pointer in free allocator payload; no live object' if free else 'resident only; liveness unverified')
    tls=snap.metadata['thread_tls_address'];tw=words(tls,8)
    assert tw[0]==0x21545624 and tw[2]==snap.u32(addr('_impure_ptr'))
    assert symbols['_impure_data']['size']==0x200 and tw[2]==addr('_impure_data')
    snap.read(tw[2],0x200)
    tls_report=dict(address=hex(tls),size=512,magic=hex(tw[0]),thread_pointer=hex(tw[1]),reent_pointer=hex(tw[2]),
        tls_data_pointer=hex(tw[3]),impure_ptr=hex(snap.u32(addr('_impure_ptr'))),
        complete_impure_data_512_bytes_resident=True,
        cpu_register_restorable_from_actual_metadata='TPIDRURW (C13_C0_3) only',
        no_fake_thread_vars=True,OS_handles_and_service_state_not_restored=True)
    table=snap.u32(addr('data_020a4bb8'));assert table==addr('ACTOR_SPAWN_TABLE')
    profiles=[];roots=[]
    for label,profile,factory,size in PROFILES:
        record=snap.u32(table+profile*4);function,behavior,render=struct.unpack('<IHH',snap.read(record,8))
        assert function==addr(factory)
        vtable=addr('_ZTV'+str(len(label))+label)
        # Player's still-flat recovered constructor stores the relocated raw
        # DS table; other typed factories store GCC's address point at +8.
        # Both tables must be inspected as linked/captured, not conflated.
        vptr=addr('data_ov002_0210a83c') if label=='Player' else vtable+8
        vptr_owner='_ZN6PlayerC1Ev' if label=='Player' else factory
        assert any(re.search(r'\b'+f'{vptr:08x}'+r'\s+\.word',line)
                   for line in functions[addr(vptr_owner)]['lines']),vptr_owner
        slots=[dict(index=i,slot=SLOTS[i],**code_record(p)) for i,p in enumerate(words(vptr,18))]
        assert slots[0]['address']==hex(addr('_ZN'+str(len(label))+label+'13InitResourcesEv'))
        profiles.append(dict(label=label,profile=hex(profile),record=hex(record),factory=code_record(function),
            behavior_priority=behavior,render_priority=render,object_bytes=size,vptr=hex(vptr),
            vtable_source=('captured relocated original Player table with native function aliases' if label=='Player'
                           else 'archived native ELF read-only GCC address-point data'),
            vptr_literal_owner=vptr_owner,vptr_literal_in_exact_factory_or_constructor=True,
            prospective_object_only_not_captured_live_instance=True,
            slots=slots,all_slots_linked=all(x['linked_code'] for x in slots),
            unavailable_slots=[x for x in slots if x['unavailable_stub']],
            factory_allocation_raw_free_payload_larger_than_object=heaps['actor']['largest_raw_free_payload']>size,
            factory_not_executed=True))
        roots += [factory,'_ZN'+str(len(label))+label+'13InitResourcesEv',
                  *[s['names'][0] for s in slots if s['index'] in (1,2,15)]]
    roots += ['func_02043098','func_0204335c','_ZN7fBase_cC2Ev','_ZN7fBase_cnwEj',
              '_ZN4Heap8AllocateEji','_ZN6Memory13operator_new2Ej','_ZN7fBase_c9Virtual34Ejj','_ZN7fBase_c9Virtual38Ejj',
              'func_0205d23c']
    def edges(address):
        result=[];dynamic=[]
        for line in functions.get(address,{}).get('lines',[]):
            ins=re.match(r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\w+)\s+(.*)$',line)
            if not ins:continue
            pc,op,arg=ins.groups();target=re.match(r'([0-9a-f]+) <([^>]+)>',arg)
            if re.fullmatch(r'(?:b|bl|blx)(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?',op) and target:
                dest=int(target[1],16)
                if dest in functions and dest!=address:result.append(dict(pc='0x'+pc,address=dest,target=functions[dest]['name'],op=op))
            elif op.startswith('blx') or (op.startswith('bx') and arg!='lr'):
                dynamic.append(dict(pc='0x'+pc,instruction=op+' '+arg))
        return result,dynamic
    closures={}
    selected=set()
    for name in roots:
        a=addr(name);seen={a:[]};queue=[(a,0)]
        while queue:
            pc,depth=queue.pop(0)
            if depth==4 or pc in unavailable:continue
            for edge in edges(pc)[0]:
                dest=edge['address']
                if dest in seen:continue
                seen[dest]=[*seen[pc],edge];queue.append((dest,depth+1))
        selected.update(seen)
        closures[name]=dict(**code_record(a),depth_limit=4,direct_calls=edges(a)[0],indirect_calls=edges(a)[1],
            unavailable_with_paths=[dict(**code_record(x),path=path) for x,path in seen.items() if x in unavailable],
            has_transitive_indirect_dispatch=any(edges(x)[1] for x in seen),full_dynamic_closure_not_claimed=True)
    globals_record={}
    for name,size in [('data_02092660',1),('data_02092664',2),('data_0209f5b8',4),('data_020a4b4c',1),('data_020a4b50',2),
        ('data_020a4b54',2),('data_02099e70',4),('data_02092110',1),('data_0209f2f8',1),('data_0209f2d8',1)]:
        globals_record[name]=dict(address=hex(addr(name)),bytes=size,value=int.from_bytes(snap.read(addr(name),size),'little'))
    archive_head=snap.u32(addr('data_020a8048'));archive_words=words(archive_head,8) if archive_head else []
    archive_registry=dict(global_address=hex(addr('data_020a8048')),head=residency(archive_head),
        first_words=[hex(x) for x in archive_words],
        first_next_resident_in_captured_spans=(any(snap.contains(s,archive_words[0],4) for s in snap.spans) if archive_words else False),
        owned_archive_not_established=True,
        live_stall_PC_not_inferred=True,
        loader_dependency='func0205d23c traverses data020a8048 and next pointers; stale registration is a concrete resource frontier')
    pmfs=[]
    for name,expected_offset in [('data_02099ebc',0),('data_02099ec4',4),('data_02099e94',8)]:
        pointer,adjustment=words(addr(name),2)
        assert pointer==expected_offset and adjustment==1
        pmfs.append(dict(name=name,address=hex(addr(name)),words=[pointer,adjustment],
            ARM_GCC_virtual_offset=pointer,ARM_GCC_adjustment_and_virtual_bit=adjustment))
    captured_objects=dict(current_scene=residency(snap.u32(addr('data_0209f5c0'))),
        stage_model_link_self_plus_8bc=residency(snap.u32(addr('data_0209f314'))),
        camera=residency(snap.u32(addr('data_0209f318'))),
        players=[residency(x) for x in words(addr('data_0209f394'),4)],
        title_context=residency(snap.u32(addr('data_ov007_0210342c'))),
        scene_manager_root_words=[hex(x) for x in words(addr('data_020a4b6c'),4)])
    malloc_lock=words(0x7fbc44,3)
    report=dict(schema=1,status='read_only_audit_complete',elf=str(elf_path),elf_sha256=EXPECTED_ELF,
        snapshot=snap.summary(),spans_and_views_validated=True,physical_span_count=len(snap.spans),logical_view_count=len(snap.views),
        tls=tls_report,stack=dict(probe=hex(snap.metadata['stack_probe_address']),mapped_app_heap=True,
            stack_size_global=snap.u32(addr('__stacksize__')),ctru_heap=hex(snap.u32(addr('__ctru_heap'))),
            fake_heap_start=hex(snap.u32(addr('fake_heap_start'))),fake_heap_end=hex(snap.u32(addr('fake_heap_end'))),
            exclusive_stack_bounds_not_queried=True,live_SP_PC_LR_not_captured=True),
        CPU_state=dict(captured='RAM bytes; TLS base identity; stack residency probe',
            not_captured=['live GPRs/SP/LR/PC','CPSR','FPSCR/VFP registers','other CP15 state','OS/service thread state'],
            replay='Explicit restart at chosen function with captured RAM and actual TPIDRURW, separate scratch stack; not resumption of a live PC'),
        globals=globals_record,captured_objects=captured_objects,heaps=heaps,
        native_malloc=dict(lock_address='0x007fbc44',light_word=malloc_lock[0],thread_tag=hex(malloc_lock[1]),recursion=malloc_lock[2],
            __getreent_uses_actual_TPIDRURW=True,lock_svc_dependency='Contended LightLock may call svcArbitrateAddressNoTimeout; OS arbitration state not in RAM'),
        profiles=profiles,init_process_pmfs=pmfs,archive_registry=archive_registry,static_frontiers=closures,
        resource_prerequisites=[
          'Stage scene request exists; Stage, Player, Camera, HUD and Minimap creation/init is still ahead of capture',
          'fBase factory uses actor heap; resource allocations may use temporary solid heaps/default heap and can fail-fast, so raw factory capacity does not prove Init success',
          'Stage before-init publishes current scene; old current-scene value inside free payload is residual and must not be treated as a live Stage',
          'HUD InitResources calls Player::GetHealth from players[current index]; Minimap calls GetMinimapID from same slot, so those are invalid as isolated initializers with the captured null Player array',
          'Stage LoadClsnAndObjects invokes original category/object loaders before Minimap/HUD factories; player/camera ordering must be established by actual loader execution',
          'Player factory constructs ModelAnim, array TextureSequence/MaterialChanger callbacks, Shadow and collision objects through real constructors; no init success inferred from linked code',
          'Archive/file/ROM reads and native video services remain resource dependencies, with OS effects outside this static audit'],
        limits=['No code executed; no runtime PC reconstructed; linked/vptr correctness is a static prerequisite only',
          'Static direct call closure depth4; indirect calls and captured future branch outcomes not proven',
          'Stream is sequential; snapshot I/O can mutate libc/IPC/bookkeeping and service threads can change memory',
          'No emulator/UI launch, production edits, Camera gate rerun, fake TLS or fake service result'],
        executions=0,production_changed=False,UI_launched=False,
        relevant_source_hashes={name:sha((ROOT/name).read_bytes()) for name in (
            'source/ctr/game_heap.cpp','source/game_port/native_snapshot.cpp','source/game_port/native_card.cpp',
            'upstream/src/func_0205d23c.c','upstream/src/func_02043098.c','upstream/src/func_0204335c.c',
            'upstream/src/_ZN7fBase_c7ProcessEMS_FivEMS_FbvEMS_FvjE.cpp','upstream/src/d_s_stage.cpp',
            'upstream/src/_ZN6PlayerC1Ev.cpp','upstream/src/d_camera.cpp','upstream/src/d_meter.cpp','upstream/src/d_map.cpp',
            'upstream/src/_ZN5Stage13InitResourcesEv.cpp','upstream/src/_ZN6Player13InitResourcesEv.cpp',
            'upstream/src/_ZN3HUD13InitResourcesEv.cpp','upstream/src/_ZN7Minimap13InitResourcesEv.cpp')},
        tool_sha256=sha(Path(__file__).read_bytes()))
    (OUT/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
    selected_text=[]
    for a in sorted(selected):
        if a in functions:selected_text.extend([f'\n{a:08x} <{functions[a]["name"]}>:',*functions[a]['lines']])
    for name in ('__syscall_getreent','__getreent','initThreadVars','__malloc_lock','__malloc_unlock','RecursiveLock_Lock','LightLock_Lock'):
        a=addr(name);selected_text.extend([f'\n{a:08x} <{name}>:',*functions[a]['lines']])
    (OUT/'selected-disassembly.txt').write_text('\n'.join(selected_text)+'\n')
    print(json.dumps(dict(status=report['status'],TLS=tls_report,actor_free_bytes=heaps['actor']['free_bytes'],
        factory_profiles=[p['label'] for p in profiles],
        unavailable_by_frontier={k:[x['names'] for x in v['unavailable_with_paths']] for k,v in closures.items()},
        report=str(OUT/'audit.json')),indent=2))

if __name__=='__main__':main()
