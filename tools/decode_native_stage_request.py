#!/usr/bin/env python3
"""Bounded read-only pre-Spawn RAM/static-loop observer. Never executes ARM."""
import json
from pathlib import Path
import re
import struct
import subprocess
from collections import deque

import audit_native_family_virtuals as proof
from test_native_title_fade_arm import Snapshot

ROOT = proof.ROOT
ATTEMPT = ROOT / 'build/native-runtime-azahar/attempt-1791624434443135000'
ELF_SHA = 'cd3799f6752140172893ec61b384777078c614b21d9e726b20cff8538e4f3f29'


def main():
    elf = proof.Elf(ATTEMPT / 'sm64ds-native.elf')
    assert proof.sha(elf.raw) == ELF_SHA
    snap = Snapshot(ATTEMPT / 'ram-stage-requested.ram.bin',
                    ATTEMPT / 'ram-stage-requested.ram.json')
    assert snap.metadata['tick'] == 3859 and snap.metadata['thread_tls_available'] is True
    original = proof.Original(ROOT / 'build/europe.pak')
    names = elf.names

    def addr(name): return names[name]['address']
    def word(name): return snap.u32(addr(name))
    def identify(address):
        return sorted(n for n, r in names.items() if r['address'] == address)
    def captured(name, size=4, signed=False):
        data = snap.read(addr(name), size)
        return dict(symbol=name, address=hex(addr(name)), bytes=size,
                    value=int.from_bytes(data,'little',signed=signed))

    # One disassembly in memory, then retain only bounded selected paths/cases.
    # CPU instructions are never executed and private cartridge payloads are not exported.
    disassembly = subprocess.check_output([proof.TOOL+'objdump','-d',str(elf.path)],text=True)
    graph, blocks, current = {}, {}, None
    for line in disassembly.splitlines():
        match = re.match(r'^([0-9a-f]+) <([^>]+)>:$',line)
        if match:
            current=match[2];graph.setdefault(current,[]);blocks[current]=[];continue
        if current: blocks[current].append(line)
        match = re.search(r'\s(?:bl|b(?:eq|ne|hi|ls|ge|gt|lt|le|cc|cs|pl|mi|vs|vc)?)\s+[0-9a-f]+ <([^>]+)>',line)
        if match and current:
            target=match[1].split('+')[0]
            if target!=current and target not in graph[current]:graph[current].append(target)

    def paths(root):
        queue=deque([[root]]); seen={}
        while queue:
            path=queue.popleft();name=path[-1]
            if name in seen:continue
            seen[name]=path
            for child in graph.get(name,[]):
                if child not in seen:queue.append(path+[child])
        return seen

    path_maps={n:paths(n) for n in ['_ZN5Stage13InitResourcesEv','dScStage_c_classInit',
        'func_0201a694','_ZN5Stage19BeforeInitResourcesEv','_ZN5Stage8BehaviorEv']}
    watched=['func_02019a58','func_0205f68c','func_0205f77c','func_0205fde8',
        'func_02055780','func_0205583c','func_020556d0','func_0201b6f8','func_0201b100',
        'func_020313d8','_ZN7Message13DisplaySavingEt','OS_SleepThread',
        'LoadTextNarcs','sm64ds_native_missing',
        '_ZN6sm64ds9AssetPack4readEjRSt6vectorIhSaIhEERNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE']

    def function(name):
        info=dict(name=name, native_symbol=names.get(name),
                  native_code_sha256=proof.sha(elf.read(addr(name),names[name]['bytes'])) if name in names else None,
                  native_direct_targets=graph.get(name,[]))
        backedges=[]
        for line in blocks.get(name,[]):
            match=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(b(?:eq|ne|hi|ls|ge|gt|lt|le|cc|cs|pl|mi|vs|vc)?)\s+([0-9a-f]+)\b',line)
            if match and int(match[3],16)<int(match[1],16):backedges.append(line.strip())
        info['native_backward_branch_candidates']=backedges
        if name in original.names:
            ds,digest,size=proof.disassemble_original(original,name)
            info['original_ROM']=dict(address=hex(original.names[name]['address']),
                code_sha256=digest, bytes=size,
                backward_branch_candidates=[line.strip() for line in ds.splitlines()
                    if (m:=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(b(?:eq|ne|hi|ls|ge|gt|lt|le|cc|cs|pl|mi|vs|vc)?)\s+0x([0-9a-f]+)\b',line))
                    and int(m[3],16)<int(m[1],16)])
        else:info['original_ROM']='No exact named original entry in symbol inventory; not inferred.'
        return info

    globals_read=[captured(n,size,signed) for n,size,signed in [
        ('data_02092660',1,False),('data_02092664',2,False),('data_0209f5b8',4,False),
        ('data_0209d50c',4,False),('data_020a4b4c',1,False),('data_020a4b50',2,False),
        ('data_02099f24',4,False),('data_0209f5c0',4,False),('data_0209d4a8',4,False),
        ('data_ov007_0210342c',4,False),('data_0209f2d8',1,False),
        ('data_02092110',1,True),('data_0209f2f8',1,True),('data_0209f26c',1,False),
        ('data_0209fc48',4,False),('data_0209fc68',4,False),('data_0209f278',1,False),
        ('data_02092130',4,True),('data_0208ee4c',4,True),('data_020a0f1c',2,False),
        ('data_020a8110',4,False),('data_02099fd0',4,True),('data_0209b480',1,False)]]
    assert snap.read(addr('data_02092660'),1)==b'\0' and snap.u16(addr('data_02092664'))==3
    assert word('data_0209d50c')==3 and word('data_ov007_0210342c')==0
    registry=word('data_020a4bb8'); profile=snap.u32(registry+3*4); factory=snap.u32(profile)
    assert factory==addr('dScStage_c_classInit')
    queues=[]
    for name in ['data_020a4ba8','data_020a4b88','data_020a4b78','data_020a4b98','data_020a4b6c']:
        a=addr(name); callback_at=a+(4 if name=='data_020a4b6c' else 8)
        cb,adj=struct.unpack('<II',snap.read(callback_at,8))
        queues.append(dict(symbol=name,address=hex(a),head=hex(snap.u32(a)),
            PMF_words=[hex(cb),hex(adj)], callback_names=identify(cb)))
    fader=word('data_0209f5bc');fader_values=struct.unpack('<iiH',snap.read(fader+4,10))
    vptr=snap.u32(fader)
    fader_slots=[dict(slot=i,target=hex(q),names=identify(q))
        for i,q in enumerate(struct.unpack('<10I',snap.read(vptr,40)))]
    initialized=snap.read(addr('_ZN12_GLOBAL__N_1L11initializedE'),104)
    ctors=[]
    for a in range(addr('sm64ds_runtime_ctors'),addr('sm64ds_runtime_ctors_end'),8):
        module,target=struct.unpack('<II',elf.read(a,8))
        if module==2:ctors.append(dict(module=module,target=hex(target),names=identify(target)))
    archives=[]
    for i in range(13):
        a=addr('data_0208ecf4')+i*20; fields=struct.unpack('<5I',snap.read(a,20))
        archives.append(dict(id=i,address=hex(a),image=hex(fields[0]),heap=hex(fields[1]),
            handle_first=fields[2]&65535,handle_end=fields[2]>>16,
            initial_unload_skips=fields[0]==0))
    level=snap.read(addr('data_02092110'),1)[0]
    planned_level_modules=[snap.s32(addr('data_020758c8')+level*4)]
    object_codes=snap.read(addr('data_02075998')+level*7,7)
    for i,code in enumerate(object_codes):
        if code:planned_level_modules.append(snap.u32(addr('data_02075804')+i*28+code*4))
    planned_level_modules += [98,102]  # original non-VS level1 branch
    assert level==1 and snap.read(addr('data_0209f2d8'),1)==b'\0'
    reg=snap.video_view(0x04000000)['address']
    registers={hex(offset):hex(snap.u32(reg+offset)) for offset in
        [0,0x1000,0x600,0x208,0x210,0x214,0x50,0x1050,0x6c,0x106c]}
    tls=snap.metadata['thread_tls_address'];reent=snap.u32(tls+8)
    assert snap.u32(tls)==0x21545624
    snap.read(reent,4)
    text_header, text_entries, text_image=[word(n) for n in
        ['data_0209d70c','data_0209d708','data_0209d6fc']]
    count=snap.u16(text_header+8);text=[]
    root=[v for v in snap.views if v['name']=='original-root-heap'][0]
    for i in range(0x280,0x283):
        assert i<count
        entry=text_entries+i*8
        pointer=text_image+0x28+snap.u32(text_header+4)+snap.u32(entry)
        assert Snapshot.contains(root,pointer,128)
        raw=snap.read(pointer,128);end=next((j for j,b in enumerate(raw) if b in (0xfd,0xff)),None)
        upto=raw[:end+1] if end is not None else raw
        text.append(dict(id=i,pointer=hex(pointer),first_FD_or_FF_offset=end,
            FE_before_delimiter=[j for j,b in enumerate(upto[:-1]) if b==0xfe],
            inspected_bytes_sha256=proof.sha(upto),
            physically_resident=True, allocation_liveness_or_future_pointer_not_claimed=True))

    loops=[
        dict(function='func_02055780',condition='GXSTAT bit0x08000000 remains1 after FIFO clear.',
            disposition='Native GX status derives stack state and sets bits25/26; it never sets busy bit27. Mirror80000000 is not the derived read value. Query failure records a platform error and returns0, not a busy spin.',phase='inside StageInit'),
        dict(function='func_0205583c',condition='GXSTAT bit0x08000000 remains1.',
            disposition='Same native GX provider contract as55780; this static comparison does not execute the pending command parser.',phase='inside StageInit'),
        dict(function='func_020556d0',condition='func55490 orfunc55464 returns nonzero.',
            disposition='Native providers return0 and publish queried position/projection, or publish0 plus explicit platform error. Matrix status is CPU command-stream state, not IRQ progress.',phase='inside StageInit'),
        dict(function='func_0201b6f8',condition='Sequential text has no0xfd/0xff terminator.',
            disposition='StageLoadGraphics2D Adventure branch calls LoadControllerModeText→1d590→1b6f8. Current captured IDs640..642 have delimiters and noFE; these old bytes are not a proof of future allocation liveness.',phase='inside StageInit'),
        dict(function='func_0201b100',condition='Text never reaches0xfd/0xff, or0xfe instruction length atcursor+1 is0.',
            disposition='Same controller-text path. Current inspected records have noFE before their short delimiters. No parser execution or future pointer identity asserted.',phase='inside StageInit'),
        dict(function='func_0205c864',condition='Archive flag+0x10 bit0x10 set,bit8 clear; sleep loop repeats whilebit0x40 stays set.',
            disposition='Static StageInit→UnloadArchive→18908→4ee10→5caa8 path exists, but all13 current archive image pointers are0. Initial unload2..5 returns before this branch.',phase='conditional archive retirement'),
        dict(function='func_0205caa8',condition='Linked request chain thiz[6] does not reachnull.',
            disposition='Archive retirement is guarded by thiz[4]&2; next comes fromp[1]. Initial current image pointers0 exclude initial retirement calls.',phase='conditional archive retirement'),
        dict(function='func_02019a58',condition='Neither5f650 nor5f66c(1) returns1.',
            disposition='Direct graph only reaches this from StageBehavior after Init. PM66c→68c requires unsigned VBlank count delta>7; C3D_FrameCounter0 captured14666, previous PMcount0. Count is driven by GSP callbacks, not game presentations; clone progress cannot be assumed.',phase='later Behavior'),
        dict(function='func_0205f77c',condition='5fc44(mode,arg1|arg2) repeatedly nonzero, followed byWaitForInterrupt.',
            disposition='Later StageBehavior→199a4→19ac4→19ed0 route. Not in direct factory/BeforeInit/Init graph. Native IPCSend missing service stops; no ARM7 ACK is supplied.',phase='later power/suspend'),
        dict(function='func_0205fc44',condition='data020a8108 remains0 after sending PM transaction.',
            disposition='Gated on5ff20 success, calls5f8b0/IPCSend. Native IPCSend is explicit unavailable boundary, not fake ACK.',phase='later PM protocol'),
        dict(function='func_0205ff08',condition='data020a8114 remainsnonzero waiting for callback.',
            disposition='Reached only viaPMfa78 afterfab4 success. No corresponding StageInit direct path; callback requires a real service.',phase='later PM callback'),
        dict(function='func_0205fde8',condition='PXI ARM7 channel8 readiness returns0.',
            disposition='Original SDK loop, but function is absent from exact ELF and absent from bounded direct paths. Native ARM7 readiness adapter stops at explicit unavailable service rather than spinning.',phase='not linked in this ELF'),
        dict(function='func_0201a4bc',condition='Game-thread queue waits fororiginal IRQ VBlank handler wake.',
            disposition='Main NativeBoot phase7 comes after Spawn/manager/behavior. Native OS_SleepThread records queue wait and returns to split scheduler; no mainthread busy loop here.',phase='after phase3/4/5'),
    ]
    for row in loops:
        row['static_paths']={root:pm[row['function']] for root,pm in path_maps.items() if row['function'] in pm}
        row['evidence']=function(row['function'])
        row['execution_after_checkpoint_not_claimed']=True
    ordered=['_ZN8dScene_c16SpawnIfNecessaryEv','func_02013edc','func_02042fe4',
        'func_02043098','func_02043060','func_0201a694','dScStage_c_classInit',
        'func_020433b8','func_0204335c','_ZN7fBase_c7ProcessEMS_FivEMS_FbvEMS_FvjE',
        '_ZN5Stage19BeforeInitResourcesEv','_ZN8dScene_c19ResetFadersAndSoundEv',
        '_ZN5Stage13InitResourcesEv','func_02044120','func_02043fdc','func_02019404']
    callback_slot=snap.u32(addr('data_02092830')+8)
    assert callback_slot==addr('func_020313d8')
    callback_code=proof.disassemble_native(elf.path,'func_020313d8')
    assert 'bl\t1c2df4 <func_02031b84>' in callback_code and 'b\t1c2df4 <func_02031b84>' in callback_code
    manifest=ROOT/'build/native-runtime/manifest.json';m=json.loads(manifest.read_text())
    assert 'func_020313d8' not in m['unavailable_functions']
    source_files=[ROOT/p for p in ['source/game_port/native_boot.cpp','source/game_port/native_runtime.cpp',
        'source/game_port/native_platform.cpp','source/game_port/native_platform_abi.cpp',
        'source/game_port/native_video.cpp','source/game_port/native_video_services.cpp',
        'source/game_port/native_card.cpp','source/core/assets.cpp','tools/native_scene_video.py',
        'tools/native_core.py','upstream/src/func_020433b8.c','upstream/src/func_02043098.c',
        'upstream/src/func_0204335c.c','upstream/src/func_0201a694.c','upstream/src/GetSceneOverlayID.c',
        'upstream/src/UnloadArchive.c','upstream/src/LoadControllerModeText.c',
        'upstream/src/_ZN5Stage13InitResourcesEv.cpp','upstream/src/d_s_stage.cpp',
        'upstream/src/_ZN5Stage19BeforeInitResourcesEv.cpp','upstream/src/_Z26LoadOrUnloadObjectOverlaysPFviEi.cpp']]
    out=dict(status='completed_read_only_decode_and_static_reachability',
        scope='Exact immutable pre-Spawn RAM+ELF. No CPU replay, reset, old tests, game frame, UI, Azahar launch or production edits.',
        tool=proof.record(Path(__file__).resolve()),elf=proof.record(elf.path), snapshot=snap.summary(),
        source_hashes=[proof.record(p) for p in source_files],
        captured_globals=globals_read, spawn_gate_eligible=True,
        factory_registry=dict(registry=hex(registry),profile=hex(profile),target=hex(factory),names=identify(factory)),
        actual_order_contract='phase3 Spawn→13edc→42fe4→43098→prehook1a694/LoadOverlay2→factory→433b8→4335c→Process(BeforeInit,Init,AfterInit) synchronously; only after returned actor does Spawn publish sentinel0x187/spawned1. If Init remains pending,433b8 enqueues init unless called while manager phase2, when it marks deferred. NativeBoot phase4 manager44120 follows Spawn return and may retry queued init; phase5behavior19404 follows manager; phase7VBlank follows these.',
        order_function_evidence=[function(n) for n in ordered],
        init_PMF_captured_words={n:[hex(q) for q in struct.unpack('<2I',snap.read(addr(n),8))]
            for n in ['data_02099ebc','data_02099ec4','data_02099e94']},
        Stage_current_GCC_address_point=dict(symbol=hex(addr('_ZTV5Stage')),address_point=hex(addr('_ZTV5Stage')+8),
            first_three_slots=[identify(q) for q in struct.unpack('<3I',elf.read(addr('_ZTV5Stage')+8,12))],
            static_table_read_only_not_factory_retest=True),
        scene_actor_pointer_is_residual_and_liveness_unknown=True, manager_queues=queues,
        current_fader=dict(address=hex(fader),vptr=hex(vptr),currInterp=fader_values[0],speed=fader_values[1],color=hex(fader_values[2]),
            IsAtStart=False,IsAtEnd=True,slots=fader_slots,
            SetFaders_on_BeforeInit_has_no_wait_loop=True),
        initialized_modules=[i for i,b in enumerate(initialized) if b],
        next_scene_module=2, next_scene_module_original_static_initializers=ctors,
        level_requested=level, conditional_planned_level_modules=planned_level_modules,
        archives=archives, native_register_mirror=registers,
        IRQ_power_order='The phase0xb aptMainLoop/power callback ran before the captured phase3 marker. IRQ masking/swap and phase7 VBlank are later; snapshot does not prove those later calls ran.',
        PM_clock=dict(C3D_FrameCounter0=snap.u32(addr('frameCounter')),C3D_FrameCounter1=snap.u32(addr('frameCounter')+4),
            previous_PM_count=word('data_020a8110'),native_count_source='C3D_FrameCounter(0), GSP callbacks',
            independent_of_game_presentation=True,callbacks_after_capture_unknown=True),
        TLS=dict(address=hex(tls),bytes=512,magic=hex(snap.u32(tls)),reentrancy_pointer=hex(reent),
            reentrancy_pointer_physically_captured=True,CP15_not_set_or_executed=True,OS_services_not_emulated=True),
        controller_text_captured=dict(header=hex(text_header),record_count=count,entries=text,
            no_private_text_bytes_exported=True, only_preSpawn_residency=True),
        loops_and_waits=loops,
        direct_graph=dict(roots={root:dict(reachable_functions=len(pm),selected_paths={n:pm[n] for n in watched if n in pm})
            for root,pm in path_maps.items()},runtime_conditions_and_indirect_edges_not_proved=True),
        actual_file_provider_contract='LoadArchive→native18934→AssetPack.read validates raw pack bytes/CRC, then realNARC parse, original heap allocation and4ee40 registration. FS reads complete synchronously in provider; ReadFileWait succeeds only for a resident open file with status0. fopen/fseek/fread/TLS/mutex/syscalls remain real libctru/newlib dependencies. Neither this observer nor a graph supplies an OS handle or acknowledgement.',
        callback313d8_ownership=dict(manifest=proof.record(manifest),listed_unavailable=False,
            exact_ELF_symbol=names['func_020313d8'],captured_table_third_slot=hex(callback_slot),
            linked_two_targets='func_02031b84', no_new_callback_CPU_gate=True),
        unknowns=['No post-Spawn CPU PC or returned frame was captured; a graph does not locate a live hang.',
            'Original root residency does not identify freed-vs-live allocations or future pointers.',
            'Indirect actor/resource callbacks and runtime OS/libc/GSP progress require the separate exact replay or later evidence.',
            'No Stage constructor/BeforeInit/Init execution is claimed solely from this pre-Spawn snapshot.'])
    dest=ROOT/'build/native-stage-plan/ram-stage-requested-static-contract.json'
    dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(out,indent=2)+'\n')
    print(json.dumps(dict(report=str(dest),status=out['status'],tick=3859,
        globals=len(globals_read),loops=len(loops),initialized=out['initialized_modules'],
        controller_text_delimiters=[r['first_FD_or_FF_offset'] for r in text],new_CPU_replays=0)))


if __name__=='__main__':main()
