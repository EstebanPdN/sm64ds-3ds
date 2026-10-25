#!/usr/bin/env python3
"""Camera CPU diagnostic with original Player resources and castle collision.

Only test files are produced. This extends the existing Player executor through
checked source substitutions, saving the complete derived executor for review.
Unimplemented game functions remain non-returning traps. The only host service
is the existing observer supplying verified local ROM file bytes.
"""
import argparse
import hashlib
import inspect
import json
from pathlib import Path
import struct
import sys
import re
import subprocess

import diagnose_player_arm as player_diag

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-camera-plan/world-fixture-projection'
MANUAL=False

def build_fixture(symbols,regions):
    """Reuse the Player builder with the current native LenVec3 owner."""
    # Prefer the newly enrolled original body. Old corpus reports may still
    # need an explicit diagnostic-only compilation; neither path edits corpus.
    corpus=json.loads((ROOT/'build/native-core/report.json').read_text())
    enrolled=[e for e in corpus['entries'] if e['source']=='src/func_02009e70.cpp' and e['compiled']]
    camera_extras=[]
    if not enrolled:
        command=[player_diag.TOOL+'g++',*corpus['flags'],'-std=gnu++17',
                 '-c',str(ROOT/'upstream/src/func_02009e70.cpp'),'-o',str(OUT/'camera_original.o')]
        compile_result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
        (OUT/'camera-compile.log').write_text(compile_result.stdout+compile_result.stderr)
        compile_result.check_returncode()
        camera_extras.append(str(OUT/'camera_original.o'))
    else:
        assert len(enrolled)==1
        (OUT/'camera-original-owner.json').write_text(json.dumps(enrolled[0],indent=2)+'\n')
    # The GP soft comparison has no recovered source in the corpus. Supply
    # the complete local provider, verified against the actual ROM separately.
    # Keep enabled exceptions linked to the diagnostic's non-returning trap;
    # no dispatcher success is invented here.
    math_gate=json.loads((ROOT/'build/native-soft-compare/report.json').read_text())
    compare_source=ROOT/'source/game_port/native_soft_compare.cpp'
    assert math_gate['status']=='pass'
    assert math_gate['source_sha256']['source/game_port/native_soft_compare.cpp']==hashlib.sha256(compare_source.read_bytes()).hexdigest()
    convert=next(e for e in corpus['entries'] if e['source']=='src/func_01ffa4bc.c')
    assert convert['compiled'] and convert['adaptations']==['mwcc-to-gas-soft-float-r0-bits']
    assert hashlib.sha256((ROOT/convert['object']).read_bytes()).hexdigest()==math_gate['original_convert_owner']['object_sha256']
    subprocess.run([player_diag.TOOL+'g++',*corpus['flags'],'-std=gnu++17',
                    '-c',str(compare_source),'-o',str(OUT/'soft_compare.o')],cwd=ROOT,check=True)
    camera_extras.append(str(OUT/'soft_compare.o'))
    (OUT/'soft-math-provider-gate.json').write_text(json.dumps({
        'report_sha256':hashlib.sha256((ROOT/'build/native-soft-compare/report.json').read_bytes()).hexdigest(),
        'compare_source_sha256':math_gate['source_sha256']['source/game_port/native_soft_compare.cpp'],
        'compare_object_sha256':hashlib.sha256((OUT/'soft_compare.o').read_bytes()).hexdigest(),
        'convert_original_owner':convert,'convert_object_sha256':math_gate['original_convert_owner']['object_sha256'],
        'enabled_dispatch_continuation_tested':False},indent=2)+'\n')
    group_replacements=[]
    # InitResources invokes real slot9/Render. The complete reviewed video
    # provider redirects only its two DS register sites into native storage.
    # No MMIO pages, virtual-slot rebasing or fabricated return are used.
    video_report=json.loads((ROOT/'build/native-runtime/scene-video/report.json').read_text())
    camera_video=next(e for e in video_report['entries'] if e['source']=='src/_ZN6Camera6RenderEv.cpp')
    assert camera_video['compiled'] and camera_video['only_reviewed_video_and_abi_edits']
    assert hashlib.sha256((ROOT/camera_video['object']).read_bytes()).hexdigest()==camera_video['sha256_object']
    camera_extras += [str(ROOT/camera_video['object']),
        'build/3ds/source/ctr/CMakeFiles/sm64ds_native_services.dir/__/game_port/native_video.o',
        'build/3ds/source/ctr/CMakeFiles/sm64ds_native_services.dir/__/game_port/native_matrix_submit.o',
        'build/3ds/CMakeFiles/sm64ds_game_draw.dir/source/core/game_draw.o']
    group_replacements += [camera_video['source'],
        'src/_ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3.cpp']
    (OUT/'camera-render-provider.json').write_text(json.dumps(camera_video,indent=2)+'\n')
    if MANUAL:
        import native_camera_body
        group=native_camera_body.build(OUT/'camera-group')
        camera_extras+=group['objects'];group_replacements+=group['replaced_sources']
        owner=enrolled[0]
        golden=OUT/'camera-original-golden.o'
        player_diag.native_operators.copy_isolated(player_diag.TOOL,ROOT/owner['object'],golden,ROOT)
        subprocess.run([player_diag.TOOL+'objcopy','--redefine-sym',
                        'func_02009e70=sm64ds_original_camera_body',str(golden)],check=True)
        camera_extras.append(str(golden))
        import native_camera_height_fixture
        camera_extras.append(str(native_camera_height_fixture.build(OUT,corpus['flags'])))
    source=inspect.getsource(player_diag.build)
    source=replace_once(source,
        "    replacements=json.loads((ROOT/'build/native-collision/symbols.json').read_text())['replaced_sources']",
        "    replacements=json.loads((ROOT/'build/native-collision/symbols.json').read_text())['replaced_sources']\n"
        "    replacements=list(replacements)+['src/LenVec3.c',\n"
        "        'src/_ZN4cstd16reciprocal_asyncE5Fix12IiE.c',\n"
        "        'src/_ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3.c']\n"
        "    replacements+=group_replacements")
    source=replace_once(source,"    for obj in extras: command+=['--extra-object',obj]",
        "    extras+=camera_extras\n"
        "    extras.append('build/3ds/source/ctr/CMakeFiles/sm64ds_native_services.dir/__/game_port/native_projection.o')\n"
        "    extras=[historical_bridge(obj) for obj in extras]\n"
        "    for obj in extras: command+=['--extra-object',obj]")
    (OUT/'derived_build.py').write_text(source)
    # Apply the same narrowly defined outer-mangling repair as the runtime
    # generator, but exclusively to the fixture's isolated object copies.
    original_copy=player_diag.native_operators.copy_isolated
    known=set(symbols)|set(player_diag.native_operators.SYMBOLS.values())
    fixes=[]
    def isolated_copy(tool,original,dest,cwd):
        original_copy(tool,original,dest,cwd)
        listing=subprocess.check_output([tool+'nm','-u',str(dest)],cwd=cwd,text=True)
        args=[]
        for line in listing.splitlines():
            name=line.split()[-1] if line.split() else ''
            m=re.match(r'_Z(\d+)',name)
            if not m: continue
            inner=name[m.end():m.end()+int(m[1])]
            if inner in known:
                args+=['--redefine-sym',name+'='+inner]
                fixes.append({'object':str(dest.relative_to(OUT)),'from':name,'to':inner})
        if args: subprocess.run([tool+'objcopy',*args,str(dest)],cwd=cwd,check=True)
    player_diag.native_operators.copy_isolated=isolated_copy
    namespace=dict(player_diag.__dict__)
    historical=[]
    def historical_bridge(obj):
        old_paths={'build/3ds/source/ctr/CMakeFiles/sm64ds_game_geometry_bridge.dir/game_geometry.o',
                   'build/3ds/CMakeFiles/sm64ds_game_files.dir/source/ctr/game_file_bridge.o'}
        if obj not in old_paths: return obj
        old=ROOT/'build/player-motion-isolated-diagnostic/objects'/(hashlib.sha256(obj.encode()).hexdigest()[:16]+'.o')
        if not old.is_file(): raise ValueError('Historical Player bridge is unavailable: '+str(old))
        historical.append({'original_path':obj,'fixture_copy':str(old.relative_to(ROOT)),
                           'sha256':hashlib.sha256(old.read_bytes()).hexdigest()})
        return str(old)
    namespace.update(OUT=OUT,ROOTS=player_diag.ROOTS,historical_bridge=historical_bridge,
                     camera_extras=camera_extras,group_replacements=group_replacements)
    exec(compile(source,str(OUT/'derived_build.py'),'exec'),namespace)
    try:
        namespace['build'](symbols,regions)
    finally:
        player_diag.native_operators.copy_isolated=original_copy
        (OUT/'isolated-abi-repairs.json').write_text(json.dumps(fixes,indent=2)+'\n')
        (OUT/'historical-platform-bridges.json').write_text(json.dumps(historical,indent=2)+'\n')

QUERY_HOOK = '''
    query_calls=[]; query_pending=[]; fixture_phase='player_setup'
    resolved_mode_calls=[]; mode_pending=[]
    manual_decisions=[]; manual_pending=[]; height_decisions=[]; manual_finishes=[]
    query_names={'_ZN9dBgCh_Lin10DetectClsnEv':('line',0x78),
                 '_ZN9dBgCh_Gnd10DetectClsnEv':('ground',0x50)}
    def query_hook(uc,address,size,user):
        if manual_pending and address==manual_pending[-1]['return_pc']:
            r=manual_pending.pop();r['accepted']=uc.reg_read(UC_ARM_REG_R0)
            r['requested_eye_fixed12']=struct.unpack('<3i',uc.mem_read(r['self']+0x8c,12))
            r['completed']=True
        if address==native.get('sm64ds_native_camera_prepare'):
            sp=uc.reg_read(UC_ARM_REG_SP)
            r={'phase':fixture_phase,'self':uc.reg_read(UC_ARM_REG_R0),'mode':uc.reg_read(UC_ARM_REG_R1),
                'view':uc.reg_read(UC_ARM_REG_R2),'secondary':uc.reg_read(UC_ARM_REG_R3),
                'radius_limits_fixed12':struct.unpack('<2i',uc.mem_read(sp,8)),
                'return_pc':uc.reg_read(UC_ARM_REG_LR),'completed':False}
            manual_decisions.append(r);manual_pending.append(r)
        if address==native.get('sm64ds_native_camera_height_range'):
            height_decisions.append({'phase':fixture_phase,'floor_fixed12':uc.reg_read(UC_ARM_REG_R1),
                'ceiling_fixed12':uc.reg_read(UC_ARM_REG_R2)})
        if address==native.get('sm64ds_native_camera_finish'):
            p=uc.reg_read(UC_ARM_REG_R0)
            manual_finishes.append({'phase':fixture_phase,'ownership':uc.reg_read(UC_ARM_REG_R1),
                'solved_eye_fixed12':struct.unpack('<3i',uc.mem_read(p+0x8c,12))})
        if mode_pending and address==mode_pending[-1]['return_pc']:
            record=mode_pending.pop()
            record['resolved_mode']=struct.unpack('<I',uc.mem_read(record['mode_slot'],4))[0]
            record['resolved_view']=struct.unpack('<I',uc.mem_read(record['view_slot'],4))[0]
            record['resolved_secondary']=struct.unpack('<i',uc.mem_read(record['secondary_slot'],4))[0]
            record['resolved_mode_names']=[name for name,entry in symbols.items()
                if name.startswith('data_02087') and storage(regions,entry['module'],entry['address'])==record['resolved_mode']]
            record['completed']=True
        if address==native.get('func_0200c66c'):
            record={'phase':fixture_phase,'self':uc.reg_read(UC_ARM_REG_R0),
                'return_pc':uc.reg_read(UC_ARM_REG_LR),'view_slot':uc.reg_read(UC_ARM_REG_R2),
                'mode_slot':uc.reg_read(UC_ARM_REG_R3),
                'secondary_slot':struct.unpack('<I',uc.mem_read(uc.reg_read(UC_ARM_REG_SP),4))[0],
                'completed':False}
            resolved_mode_calls.append(record);mode_pending.append(record)
        if query_pending and address==query_pending[-1]['return_pc']:
            q=query_pending.pop(); p=q['pointer']
            q['return_value']=uc.reg_read(UC_ARM_REG_R0)
            q['output_sha256']=hashlib.sha256(bytes(uc.mem_read(p,q['bytes']))).hexdigest()
            if q['kind']=='line':
                q['hit']=int(bytes(uc.mem_read(p+0x50,1))[0])
                q['hit_or_end_fixed12']=struct.unpack('<3i',uc.mem_read(p+0x54,12))
                q['distance_fixed12']=struct.unpack('<i',uc.mem_read(p+0x60,4))[0]
            else:
                q['hit']=int(bytes(uc.mem_read(p+0x48,1))[0])
                q['ground_y_fixed12']=struct.unpack('<i',uc.mem_read(p+0x44,4))[0]
            q['completed']=True
        for name,(kind,length) in query_names.items():
            if address!=native.get(name): continue
            p=uc.reg_read(UC_ARM_REG_R0)
            q={'function':name,'kind':kind,'phase':fixture_phase,'pointer':p,
               'bytes':length,'return_pc':uc.reg_read(UC_ARM_REG_LR),'completed':False,
               'input_sha256':hashlib.sha256(bytes(uc.mem_read(p,length))).hexdigest(),
               'start_or_probe_fixed12':struct.unpack('<3i',uc.mem_read(p+0x38,12))}
            if kind=='line': q['end_fixed12']=struct.unpack('<3i',uc.mem_read(p+0x44,12))
            query_calls.append(q); query_pending.append(q)
    cpu.hook_add(UC_HOOK_CODE,query_hook)
'''

CAMERA_SUITE = '''
                # Original view loader consumes an actual OV009 object table.
                # This is a partial Stage fixture, not an execution of Stage boot.
                call('__sinit_02073a24')
                table_pointer=struct.unpack('<I',cpu.mem_read(level_pointer+4,4))[0]
                table_count=struct.unpack('<H',cpu.mem_read(table_pointer,2))[0]
                entry_pointer=struct.unpack('<I',cpu.mem_read(table_pointer+4,4))[0]
                assert 0<table_count<256
                view_tables=[];entrance_records=[]
                for index in range(table_count):
                    t=entry_pointer+index*8
                    packed,count=struct.unpack('<BB',cpu.mem_read(t,2))
                    if packed>>5: continue
                    source=struct.unpack('<I',cpu.mem_read(t+4,4))[0]
                    # Types verified against the original OV002 dispatch table.
                    if packed&31==4:
                        call('_Z15LoadViewObjectsRN11LVL_Overlay11ObjSubTableEij',t,-1,0)
                        view_tables.append({'native_table':t,'native_entries':source,'count':count,
                                            'sha256':hashlib.sha256(bytes(cpu.mem_read(source,count*14))).hexdigest()})
                    elif packed&31==1:
                        for j in range(count):
                            row=struct.unpack('<H6hH',cpu.mem_read(source+j*16,16))
                            entrance_records.append({'position':row[1:4],'rotation':row[4:7],
                                                     'parameter':row[7],'native_address':source+j*16})
                matching=[e for e in entrance_records if tuple(x*4096 for x in e['position'])==resource_position]
                assert len(matching)==1 and view_tables
                entrance=matching[0]
                # Factory reads the spawn record. Match the ROM entrance's yaw,
                # area and camera id instead of retaining the old arbitrary yaw.
                cpu.mem_write(player+0x8c,struct.pack('<3h',*entrance['rotation']))
                cpu.mem_write(player+0x92,struct.pack('<3h',*entrance['rotation']))
                cpu.mem_write(player+0xcc,struct.pack('<b',entrance['parameter']&7))
                write('data_0209f250',0,'<B')
                write('data_0209f394',player)
                write('data_020a4b60',(entrance['parameter']>>3)&15)
                camera=call('dCamera_c_classInit')
                assert heap_arena<camera<heap_arena+8*1024*1024
                write('data_0209f318',camera)
                write('data_020a4b54',0xbf,'<H')
                report['stage_fixture']={'full_stage_boot_executed':False,
                    'world_registry':'Original dBgW::Enable with real castle KCL/CLPS',
                    'visible_areas':1,'view_tables':view_tables,'selected_entrance':entrance,
                    'player_resources_original':True,'camera_factory_original':True,
                    'session_flags_enabled_after_player_resources':True,
                    'omissions':['Stage owner/lifecycle','other spawned world actors','normal audio/session boot']}
                session_flags=struct.unpack('<I',cpu.mem_read(data_address('data_0209caa0')+8,4))[0]
                cpu.mem_write(data_address('data_0209caa0')+8,struct.pack('<I',session_flags|0x80))
                write('data_0209f21c',1,'<B')
                report['camera_address']=camera
                vptr=struct.unpack('<I',cpu.mem_read(camera,4))[0]
                render_slot=struct.unpack('<I',cpu.mem_read(vptr+0x24,4))[0]
                assert render_slot==native['_ZN6Camera6RenderEv']
                report['camera_init_virtual_contract']={'vptr':vptr,'slot_bytes':0x24,
                    'slot_target':render_slot,'target_name':'_ZN6Camera6RenderEv',
                    'original_rom_contract':'build/native-camera-plan/init-virtual-contract.json'}
                def camera_sample():
                    b=bytes(cpu.mem_read(camera,0x1a8))
                    return {'sha256':hashlib.sha256(b).hexdigest(),
                        'look_at_fixed12':struct.unpack_from('<3i',b,0x80),
                        'eye_fixed12':struct.unpack_from('<3i',b,0x8c),
                        'state':struct.unpack_from('<I',b,0x138)[0],
                        'mode':struct.unpack_from('<I',b,0x13c)[0],
                        'flags':struct.unpack_from('<I',b,0x154)[0],
                        'yaw_pitch':struct.unpack_from('<2h',b,0x17c)}
                def player_bytes(): return bytes(cpu.mem_read(player,0x768))
                def controls(): return bytes(cpu.mem_read(data_address('data_0209f498'),0x20))
                fixture_phase='explicit_castle_collision_queries'
                report['phase']=fixture_phase
                report['explicit_collision_samples']=[]
                x,y,z=resource_position
                vectors=arena+0x8100;obj=arena+0x8000
                for kind,label,start,end,want in [
                        ('ground','entrance_floor',(x,y+400*4096,z),None,1),
                        ('ground','outside_world',(30000*4096,1000*4096,30000*4096),None,0),
                        ('line','entrance_floor_vertical',(x,y+400*4096,z),(x,y-200*4096,z),1),
                        ('line','outside_world',(30000*4096,1000*4096,30000*4096),
                                              (30000*4096,-1000*4096,30000*4096),0)]:
                    length=0x50 if kind=='ground' else 0x78
                    cpu.mem_write(obj-16,bytes([0xa5])*(length+32))
                    cpu.mem_write(vectors,struct.pack('<3i',*start))
                    p_before=player_bytes();g_before=controls()
                    if kind=='ground':
                        call('_ZN9dBgCh_GndC1Ev',obj)
                        call('_ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c',obj,vectors,0)
                        answer=call('_ZN9dBgCh_Gnd10DetectClsnEv',obj)
                    else:
                        cpu.mem_write(vectors+16,struct.pack('<3i',*end))
                        call('_ZN9dBgCh_LinC1Ev',obj)
                        call('_ZN9dBgCh_Lin13SetObjAndLineERK7Vector3S2_P8dActor_c',obj,vectors,vectors+16,0)
                        answer=call('_ZN9dBgCh_Lin10DetectClsnEv',obj)
                    sample={'label':label,'kind':kind,'expected_hit':want,'answer':answer,
                            'query':dict(query_calls[-1]),'player_unchanged':player_bytes()==p_before,
                            'control_unchanged':controls()==g_before,
                            'canaries_preserved':bytes(cpu.mem_read(obj-16,16))==bytes([0xa5])*16 and
                                                 bytes(cpu.mem_read(obj+length,16))==bytes([0xa5])*16}
                    report['explicit_collision_samples'].append(sample)
                    assert bool(answer)==bool(want) and sample['query']['hit']==want
                    if want and kind=='ground': assert sample['query']['ground_y_fixed12']==y
                    if want and kind=='line':
                        hit=sample['query']['hit_or_end_fixed12']
                        # Original line intersection uses reduced precision.
                        # Keep the measured difference rather than reporting an
                        # invented exact equality with the ground height.
                        sample['ground_height_difference_fixed12']=hit[1]-y
                        assert hit[0]==x and hit[2]==z and abs(hit[1]-y)<=256
                    assert sample['player_unchanged'] and sample['control_unchanged'] and sample['canaries_preserved']
                    call('_ZN9dBgCh_GndD1Ev' if kind=='ground' else '_ZN9dBgCh_LinD1Ev',obj)
                report['castle_native_line_ground_gate']='pass'
                fixture_phase='camera_init_resources'
                report['phase']=fixture_phase
                p_before=player_bytes()
                g_before=controls()
                report['camera_init_result']=call('_ZN6Camera13InitResourcesEv',camera)
                assert report['camera_init_result']==1
                assert player_bytes()==p_before,'Camera init modified Player'
                report['camera_after_init']=camera_sample()
                report['camera_original_behavior_samples']=[]
                for label,pause,direct in [('paused_behavior',1,False),('active_behavior',0,False),
                                           ('active_body',0,True),('active_behavior_next',0,False)]:
                    fixture_phase=label;report['phase']=label
                    write('data_0209fc48',pause)
                    p_before=player_bytes();g_before=controls();q_before=len(query_calls)
                    sample={'label':label,'before':camera_sample(),'queries_before':q_before}
                    report['camera_original_behavior_samples'].append(sample)
                    sample['result']=call('func_02009e70' if direct else '_ZN6Camera8BehaviorEv',camera)
                    assert sample['result']==1
                    sample['after']=camera_sample();sample['queries']=len(query_calls)-q_before
                    sample['player_unchanged']=player_bytes()==p_before
                    sample['control_unchanged']=controls()==g_before
                    assert sample['player_unchanged'] and sample['control_unchanged']
                    if pause: assert sample['queries']==0,'Paused camera performed collision work'
                report['status']='behavior_completed'
'''


def replace_once(text, original, replacement):
    if text.count(original) != 1:
        raise ValueError('Executor source drift at substitution: '+original[:120])
    return text.replace(original, replacement)


def derive_executor():
    text=inspect.getsource(player_diag.execute)
    baseline=text
    text=replace_once(text, '    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)',
        '    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)\n'
        '    from unicorn.arm_const import UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC\n'
        '    cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20)\n'
        '    cpu.reg_write(UC_ARM_REG_FPEXC,1<<30)')
    text=replace_once(text, '    cpu.hook_add(UC_HOOK_CODE,hook)',
                      QUERY_HOOK+'    cpu.hook_add(UC_HOOK_CODE,hook)')
    # Unicorn's until PC stops before executing the sentinel instruction;
    # finish direct-query observation from that already verified return state.
    text=replace_once(text,'        return cpu.reg_read(UC_ARM_REG_R0)',
        "        if query_pending and query_pending[-1]['return_pc']==stop:\n"
        "            query_hook(cpu,stop,4,None)\n"
        "        if manual_pending and manual_pending[-1]['return_pc']==stop:\n"
        "            query_hook(cpu,stop,4,None)\n"
        '        return cpu.reg_read(UC_ARM_REG_R0)')
    text=replace_once(text,'        cpu.reg_write(UC_ARM_REG_SP,stack+0xf000);cpu.reg_write(UC_ARM_REG_LR,stop)',
        '        cpu.reg_write(UC_ARM_REG_SP,stack+0xf000);cpu.reg_write(UC_ARM_REG_LR,stop)\n'
        "        if len(args)>4:cpu.mem_write(stack+0xf000,struct.pack('<'+'I'*(len(args)-4),*[v&0xffffffff for v in args[4:]]))")
    # The old executor's camera factory/fixed view and Player tick loop are
    # replaced by one bounded original-camera suite. Everything before it,
    # including data relocation, resources and FAT observation, stays intact.
    start="                write('data_020a4b54',0x14c,'<H')\n"
    end='    except Exception as error:\n'
    assert text.count(start)==1 and text.count(end)==1
    begin=text.index(start);finish=text.index(end,begin)
    text=text[:begin]+"                write('data_020a4b54',0x14c,'<H')\n"+CAMERA_SUITE+text[finish:]
    if MANUAL:
        text=replace_once(text,"                report['status']='behavior_completed'",
            "                report['status']='behavior_completed'\n"
            "                def set_fixture_phase(label):\n"
            "                    nonlocal fixture_phase\n"
            "                    fixture_phase=label;report['phase']=label\n"
            "                sys.path.insert(0,str(ROOT/'tests'))\n"
            "                import native_camera_world_cases\n"
            "                native_camera_world_cases.run(dict(locals()),set_fixture_phase)")
    text=replace_once(text, "    report['visited_function_count']=len(visited)",
        "    if 'camera_sample' in locals():\n"
        "        report['camera_at_boundary']=camera_sample()\n"
        "        if report.get('phase')=='camera_init_resources':\n"
        "            report['player_unchanged_at_camera_boundary']=player_bytes()==p_before\n"
        "            report['control_unchanged_at_camera_boundary']=controls()==g_before\n"
        "    report['query_calls']=query_calls\n"
        "    report['resolved_mode_calls']=resolved_mode_calls\n"
        "    report['manual_decisions']=manual_decisions\n"
        "    report['height_decisions']=height_decisions\n"
        "    report['manual_finishes']=manual_finishes\n"
        "    report['query_pending_at_stop']=len(query_pending)\n"
        "    report['boundary_registers']={name:cpu.reg_read(reg) for name,reg in\n"
        "        [('r0',UC_ARM_REG_R0),('r1',UC_ARM_REG_R1),('r2',UC_ARM_REG_R2),\n"
        "         ('r3',UC_ARM_REG_R3),('sp',UC_ARM_REG_SP),('lr',UC_ARM_REG_LR),('pc',UC_ARM_REG_PC)]}\n"
        "    report['scope']='ARM11 Camera/Player CPU fixture with real castle KCL/CLPS and original view data; partial Stage only'\n"
        "    report['visited_function_count']=len(visited)")
    if MANUAL:
        text=replace_once(text,"('constructor_passed','resources_passed','behavior_completed','draw_completed')",
            "('constructor_passed','resources_passed','behavior_completed','draw_completed','manual_completed')")
    (OUT/'derived_executor.py').write_text(text)
    namespace=dict(player_diag.__dict__)
    namespace.update(OUT=OUT,ROOTS=player_diag.ROOTS)
    exec(compile(text,str(OUT/'derived_executor.py'),'exec'),namespace)
    return namespace['execute'],baseline,text


def write_world_gate(return_code):
    """Summarize the completed CPU run without replaying older suites."""
    execution=json.loads((OUT/'execution.json').read_text())
    passed=(return_code==0 and execution['status'] in ('behavior_completed','manual_completed'))
    if passed:
        assert execution['camera_init_result']==1
        assert execution['castle_native_line_ground_gate']=='pass'
        assert not execution['invalid_accesses'] and not execution['trap_hits']
        assert not execution['ds_io_mapped'] and not execution['original_ds_code_executable']
        assert execution['query_pending_at_stop']==0
        behavior=execution['camera_original_behavior_samples']
        assert len(behavior)==4
        assert [s['queries'] for s in behavior]==[0,9,9,9]
        assert all(s['result']==1 and s['player_unchanged'] and s['control_unchanged'] for s in behavior)
        assert len(execution['resolved_mode_calls'])>=3
        assert all(s['completed'] and s['resolved_mode_names']==['data_0208715c'] and
                   s['resolved_view']==0 and s['resolved_secondary']==0
                   for s in execution['resolved_mode_calls'][:3])
    elf=OUT/'diagnostic.elf'
    listing=subprocess.check_output([player_diag.TOOL+'nm','-S','-g','--defined-only',str(elf)],text=True)
    provider_names=['func_01ff98f4','func_01ffa4bc','func_02009e70','func_0207322c',
                    '_ZN4cstd16reciprocal_asyncE5Fix12IiE']
    providers={}
    for name in provider_names:
        lines=[line for line in listing.splitlines() if line.split()[-1]==name]
        assert len(lines)==1,(name,lines)
        fields=lines[0].split()
        providers[name]={'address':int(fields[0],16),'bytes':int(fields[1],16),'binding':fields[2]}
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    assert digest(elf)==execution['elf_sha256']
    owner=json.loads((OUT/'camera-original-owner.json').read_text())
    assert digest(ROOT/'upstream'/owner['source'])==owner['sha256_original']
    gate={'status':'pass' if passed else 'boundary','scope':execution['scope'],
        'original_camera_source_compiled_unchanged':not MANUAL,'original_camera_body_byte_matched_to_rom':False,
        'camera_original_owner':{**owner,'object_sha256':digest(ROOT/owner['object'])},
        'providers':providers,'collision_explicit_cases':len(execution['explicit_collision_samples']),
        'camera_init_result':execution.get('camera_init_result'),
        'behavior_samples':execution.get('camera_original_behavior_samples',[]),
        'resolved_mode_calls':execution.get('resolved_mode_calls',[]),
        'observed_real_queries':len(execution['query_calls']),
        'visited_function_count':execution['visited_function_count'],'verified_rom_file_requests':len(execution['file_requests']),
        'native_invalid_accesses':execution['invalid_accesses'],'native_trap_hits':execution['trap_hits'],
        'mmio_mapped':False,'manual_cstick_provider_implemented':MANUAL,
        'manual_camera_functional_proven':False,
        'full_stage_boot_executed':False,
        'limits':['Partial Stage: no Stage owner/lifecycle or other actors; session gates set explicitly after Player resources',
                  'Historical verified Player FS/geometry bridges, not current general-runtime bridge parity',
                  'Soft FP enabled exception dispatcher continuation is untested and remains a real non-returning diagnostic boundary',
                  'Single castle entrance/state sequence; no all-modes, cutscene, adversarial-world or integrated input proof',
                  'CPU fixture only; no emulator or physical console launch'],
        'hashes':{str(p.relative_to(ROOT)):digest(p) for p in [elf,OUT/'execution.json',OUT/'fixture.json',
            OUT/'derived_executor.py',OUT/'derived_build.py',OUT/'soft-math-provider-gate.json',
            OUT/'historical-platform-bridges.json',ROOT/'tools/test_native_camera_world.py',
            ROOT/'build/native-soft-compare/report.json']}}
    gate['camera_init_virtual_contract']=execution.get('camera_init_virtual_contract')
    gate['camera_render_provider']=json.loads((OUT/'camera-render-provider.json').read_text())
    if MANUAL:
        group=json.loads((OUT/'camera-group/camera-group.json').read_text())
        assert group['inverse_reconstruction_verified'] and group['inverse_sha256']==owner['sha256_original']
        assert digest(ROOT/'tools/native_camera_body.py')==group['generator_sha256']
        for obj in group['sources']:
            assert digest(Path(obj['source']))==obj['source_sha256']
            assert digest(Path(obj['object']))==obj['object_sha256']
        for obj in group['lifecycle_originals']:
            assert digest(Path(obj['isolated_object']))==obj['isolated_object_sha256']
        gate.update(provider_group=group,manual_cpu_cases=execution.get('manual_summary'),
            logical_clock_cadence=execution.get('logical_clock_cadence'),
            real_obstacle_proofs=execution.get('spatial_proofs'),
            spatial_cases=execution.get('spatial_cases'),
            isolated_exact_height_block_cases=execution.get('isolated_height_cases'),
            continuous_control_cpu_initial_mode_passed=passed,
            integrated_hid_game_loop_proven=False,physical_hardware_proven=False)
        gate['limits'] += [
            'Only resolved data_0208715c with no view/secondary is admitted; other resolver gates tested as helper arguments only',
            'Known obstacle precheck restores the entire original automatic yaw/radius/height solve; it can approach by rotation rather than hard-clamp instantly',
            'Two inverted, one equal and two valid intervals tested in exact ARM height blocks with original mode/math; inverted cases match full Camera bytes and original goal/clamp priority. Inputs are explicit block values, not collision hits; no natural inverted pair occurred in this partial castle world',
            'Previous-eye sweep uses controlled Camera/Player positions and a real controller acquisition at prepare; no collision results or service returns are changed']
        for relative in ('include/sm64ds/native_camera.hpp','source/game_port/native_camera.cpp',
                         'tools/native_camera_body.py','tests/native_camera_world_cases.py',
                         'tests/native_camera_world_geometry.py','tests/native_camera_clock.cpp',
                         'tools/test_native_camera_clock.py','tools/native_camera_height_fixture.py'):
            gate['hashes'][relative]=digest(ROOT/relative)
        for name in ('camera_height_block.cpp','camera_height_block.o','camera-height-block.json'):
            gate['hashes'][str((OUT/name).relative_to(ROOT))]=digest(OUT/name)
        orbit_gate=ROOT/'build/native-camera-plan/integrator-tests/report.json'
        previous=json.loads(orbit_gate.read_text())
        assert previous['status']=='passed'
        for relative in ('include/sm64ds/camera_orbit.hpp','source/core/camera_orbit.cpp'):
            assert digest(ROOT/relative)==previous['source_sha256'][relative]
        gate['portable_orbit_prior_report']={'path':str(orbit_gate.relative_to(ROOT)),
            'sha256':digest(orbit_gate),'reuse_reason':'Core and header identical; no arithmetic/LUT edits in this group',
            'source_hashes_verified':True,'status':previous.get('status')}
    (OUT/'gate.json').write_text(json.dumps(gate,indent=2)+'\n')
    return gate


def main():
    global OUT,MANUAL
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,default=ROOT/'build/roms/europe.nds')
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--manual',action='store_true')
    args=parser.parse_args()
    MANUAL=args.manual
    if MANUAL:OUT=ROOT/'build/native-camera-plan/manual-world-fixture'
    OUT.mkdir(parents=True,exist_ok=True)
    base=json.loads((ROOT/'build/player-motion-isolated-diagnostic/build.json').read_text())
    symbols,addresses,regions,relocs=player_diag.metadata()
    player_diag.OUT=OUT
    player_diag.ROOTS=list(base['roots'])
    more=['__sinit_02073a24','_ZN6Camera13InitResourcesEv','_ZN6Camera8BehaviorEv',
          'func_02009e70','func_02008cb4','_Z15LoadViewObjectsRN11LVL_Overlay11ObjSubTableEij',
          '_ZN9dBgCh_Lin10DetectClsnEv','_ZN9dBgCh_Gnd10DetectClsnEv',
          '_ZN9dBgCh_LinC1Ev','_ZN9dBgCh_LinD1Ev',
          '_ZN9dBgCh_Lin13SetObjAndLineERK7Vector3S2_P8dActor_c',
          '_ZN9dBgCh_GndC1Ev','_ZN9dBgCh_GndD1Ev',
          '_ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c']
    # Camera's virtual factory/init slots and state initializers are reached
    # indirectly. Select their compiled originals; missing leaves stay traps.
    more += [n for n,v in symbols.items() if n.startswith('_ZN6Camera') and v['kind'].startswith('function')]
    more += [n for n,v in symbols.items() if v['module']=='arm9' and
             v['kind'].startswith('function') and 0x02008a00<=v['address']<0x0200da00
             and n!='func_0200d8c8']
    if MANUAL:more+=['sm64ds_original_camera_body','sm64ds_native_camera_reset',
        'sm64ds_native_camera_publish_q15','sm64ds_native_camera_stats',
        'sm64ds_native_camera_height_range','_ZN6Camera16CleanupResourcesEv','_ZN6Camera16OnPendingDestroyEv',
        'sm64ds_draw_count','sm64ds_draw_entries','native_camera_height_golden','native_camera_height_candidate']
    player_diag.ROOTS += [n for n in dict.fromkeys(more) if n not in player_diag.ROOTS]
    manifest={'baseline_executor_sha256':hashlib.sha256(inspect.getsource(player_diag.execute).encode()).hexdigest(),
              'baseline_tool_sha256':hashlib.sha256(Path(player_diag.__file__).read_bytes()).hexdigest(),
              'baseline_build_sha256':hashlib.sha256((ROOT/'build/player-motion-isolated-diagnostic/build.json').read_bytes()).hexdigest(),
              'new_tool_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'production_edits':False,'original_ds_code_executable':False,'ds_io_mapped':False,
              'full_stage_boot':False,'original_camera_body_selected':True,
              'original_camera_body_sha256':hashlib.sha256((ROOT/'upstream/src/func_02009e70.cpp').read_bytes()).hexdigest(),
              'original_camera_body_byte_matched':False,
              'native_projection_source_sha256':hashlib.sha256((ROOT/'source/game_port/native_projection.cpp').read_bytes()).hexdigest(),
              'native_projection_object_sha256':hashlib.sha256((ROOT/'build/3ds/source/ctr/CMakeFiles/sm64ds_native_services.dir/__/game_port/native_projection.o').read_bytes()).hexdigest(),
              'query_layout_bytes':{'line':0x78,'ground':0x50}}
    (OUT/'fixture.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if not args.skip_build: build_fixture(symbols,regions)
    executor,baseline,derived=derive_executor()
    manifest['derived_executor_sha256']=hashlib.sha256(derived.encode()).hexdigest()
    (OUT/'fixture.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return_code=executor(args.rom,symbols,addresses,regions,relocs,1,True,False)
    write_world_gate(return_code)
    return return_code


if __name__=='__main__':
    sys.exit(main())
