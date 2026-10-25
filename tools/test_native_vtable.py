#!/usr/bin/env python3
"""Validate reviewed manual vptr adaptation/inversion and ARM32 expressions.

No native-core report or in-progress general bundle is read. Static checks use
the immutable ABI audit and original sources; a separate fixture compiles the
exact byte-offset expression with real reviewed declaration spellings.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import struct

import native_vtable
import test_native_audio_deferred_arm as shared
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PC,UC_ARM_REG_LR,UC_ARM_REG_SP,UC_ARM_REG_CPSR

ROOT=shared.ROOT


def sha(data):return hashlib.sha256(data).hexdigest()


def static_checks():
    recipe=json.loads(native_vtable.RECIPE.read_text())
    audit_path=ROOT/'build/native-title-video/manual-vptr-abi-audit.json'
    assert recipe['audit_sha256']==sha(audit_path.read_bytes())
    audit=json.loads(audit_path.read_text())['manual_assignment_audit']
    grouped=native_vtable.rules();sources={};changes=[];controls=[]
    assert len(grouped)==287
    for source,rows in grouped.items():
        path=ROOT/'upstream'/source;original=path.read_text()
        assert all(sha(path.read_bytes())==row['source_sha256'] for row in rows)
        adapted,edits=native_vtable.adapt(source,original)
        assert len(edits)==len(rows)
        # Verify literal unchanged gaps and reconstruct the original by slicing
        # forward through the generated text, independently of adapt's inverse.
        old_cursor=new_cursor=0;inverse=[]
        for row in rows:
            unchanged=original[old_cursor:row['rhs_start']]
            assert adapted[new_cursor:new_cursor+len(unchanged)]==unchanged
            inverse.append(unchanged);new_cursor+=len(unchanged)
            replaced=native_vtable.replacement(row)
            assert adapted[new_cursor:new_cursor+len(replaced)]==replaced
            inverse.append(row['original_rhs']);new_cursor+=len(replaced)
            old_cursor=row['rhs_end']
            matches=[r for r in audit['assignments'] if r['source']=='upstream/'+source and
                row['rhs_start']<=r['rhs_reference_start_chars']<row['rhs_end']]
            assert len(matches)==1 and matches[0]['symbol']==row['symbol']
            expected=('raw_GCC_header_store' if row['proof']=='retained_GCC_prefix' else
                'symbol_not_retained_no_native_address_point_claimed')
            assert matches[0]['classification']==expected
            changes.append(dict(source=source,symbol=row['symbol'],line=row['original_line'],
                proof=row['proof'],declaration_kind=row['declaration_kind'],
                declared_symbol_type=row['declared_symbol_type']))
        assert adapted[new_cursor:]==original[old_cursor:]
        inverse.append(original[old_cursor:])
        assert ''.join(inverse)==original
        sources[source]=dict(original=original,adapted=adapted,rows=rows,
            original_sha256=sha(path.read_bytes()),adapted_sha256=sha(adapted.encode()))
    for row in recipe['unchanged_controls']:
        source=row['source'];path=ROOT/'upstream'/source;original=path.read_text()
        assert sha(path.read_bytes())==row['source_sha256']
        assert original[row['rhs_start']:row['rhs_end']]==row['original_rhs']
        if source not in sources:
            adapted,edits=native_vtable.adapt(source,original)
            assert not edits and adapted==original
            rows=[]
        else:adapted=sources[source]['adapted'];rows=sources[source]['rows']
        assert not any(r['rhs_start']<row['rhs_end'] and row['rhs_start']<r['rhs_end'] for r in rows)
        delta=sum(len(native_vtable.replacement(r))-len(r['original_rhs'])
            for r in rows if r['rhs_start']<row['rhs_start'])
        assert adapted[row['rhs_start']+delta:row['rhs_end']+delta]==row['original_rhs']
        matches=[r for r in audit['assignments'] if r['source']=='upstream/'+source and
            row['rhs_start']<=r['rhs_reference_start_chars']<row['rhs_end']]
        assert len(matches)==1 and matches[0]['classification']==row['classification']
        controls.append(dict(source=source,symbol=row['symbol'],classification=row['classification'],
            source_sha256=row['source_sha256'],unchanged=True))
    assert len(changes)==352 and len(controls)==59
    assert Counter(r['proof'] for r in changes)==Counter(
        retained_GCC_prefix=351,existing_reviewed_Stage_transient_repair=1)
    assert Counter(r['classification'] for r in controls)==Counter(
        already_explicit_plus2_or_index2=41,ROM_bare_table_or_non_GCC_prefix=4,
        symbol_not_retained_no_native_address_point_claimed=14)
    for source,record in sources.items():
        assert sha((ROOT/'upstream'/source).read_bytes())==record['original_sha256']
    source=next(iter(grouped));original=sources[source]['original']
    try:native_vtable.adapt(source,original+'\n')
    except ValueError as error:assert 'source changed' in str(error)
    else:raise AssertionError('Changed source hash was accepted')
    arbitrary='extern int data_ov002_0210a83c[];\nint f(){return (int)data_ov002_0210a83c;}\n'
    result,edits=native_vtable.adapt('src/non_reviewed_fixture.cpp',arbitrary)
    assert result==arbitrary and not edits
    return recipe,dict(status='passed',reviewed_RHS=352,sources=287,unchanged_controls=controls,
        coverage_matches_all411_original_audit_sites=True,original_sources_unchanged=True,
        generated_source_only_contains_reviewed_RHS_changes=True,
        independent_inverse_equals_original_every_source=True,
        stale_source_hash_rejected=True,unreviewed_data_alias_source_unchanged=True,
        proof_counts=dict(Counter(r['proof'] for r in changes)),
        declaration_counts=dict(Counter(r['declaration_kind'] for r in changes)),
        char_array_rows=[r for r in changes if r['declared_symbol_type'].startswith('char ')],
        generated_hashes={source:r['adapted_sha256'] for source,r in sources.items()},
        audit_sha256=recipe['audit_sha256'])


def arm_expression_fixture(recipe,directory):
    selected=[]
    for label,symbol in [('char_array','_ZTV10dScEntry_c'),('word_array_with_int_cast','_ZTV5Whomp'),
            ('pointer_array','_ZTV5Stage'),('scalar_pointer_address','_ZTV6Cannon'),
            ('scalar_int_address','_ZTV11ChiefChilly')]:
        row=next(r for r in recipe['rows'] if r['symbol']==symbol)
        selected.append(dict(label=label,symbol=symbol,original=row['original_rhs'],
            adapted=native_vtable.replacement(row),offset=8,type=row['declared_symbol_type'],
            evidence_source=row['source'],evidence_sha256=row['source_sha256']))
    for label,symbol in [('already_index2','_ZTV10Scuttlebug'),('original_bare_alias','_ZTV6Dorrie')]:
        row=next(r for r in recipe['unchanged_controls'] if r['symbol']==symbol)
        selected.append(dict(label=label,symbol=symbol,original=row['original_rhs'],
            adapted=row['original_rhs'],offset=8 if label=='already_index2' else 0,
            type=row['declared_symbol_type'],evidence_source=row['source'],
            evidence_sha256=row['source_sha256']))
    path=ROOT/'upstream/src/_ZN6PlayerC1Ev.cpp';text=path.read_text()
    assert 'extern int data_ov002_0210a83c[];' in text
    assert '*(int*)p=(int)data_ov002_0210a83c;' in text
    selected.append(dict(label='Player_original_data_alias',symbol='data_ov002_0210a83c',
        original='(int)data_ov002_0210a83c',adapted='(int)data_ov002_0210a83c',offset=0,
        type='int data_ov002_0210a83c[]',evidence_source=str(path.relative_to(ROOT)),
        evidence_sha256=sha(path.read_bytes())))
    template=(ROOT/'tests/native_vtable_arm.cpp').read_text()
    for row in selected:
        # Ignore harmless spacing and retain the exact declaration type.
        declaration='extern '+row['type']+';'
        normalized=lambda s:re.sub(r'\s+','',s)
        assert normalized(declaration) in normalized(template),(row['label'],declaration)
    source=template.replace('// @TYPE_ASSERTIONS@','\n'.join(
        'static_assert(SameType<__typeof__(('+r['original']+') + 0),__typeof__('+
        r['adapted']+')>::value,"reviewed RHS type changed");' for r in selected))
    for marker,key in [('// @ORIGINAL_CASES@','original'),('// @ADAPTED_CASES@','adapted')]:
        source=source.replace(marker,'\n'.join('case '+str(i)+':return (unsigned)('+r[key]+');'
            for i,r in enumerate(selected)))
    cpp=directory/'fixture.cpp';cpp.write_text(source)
    assembly=directory/'tables.S'
    assembly.write_text('.section .data\n.balign 16\n'+''.join(
        '.global '+r['symbol']+'\n.type '+r['symbol']+', %object\n'+r['symbol']+
        ':\n.word 0, '+hex(0x11110000+i)+', '+hex(0x22220000+i)+', '+hex(0x33330000+i)+
        '\n.size '+r['symbol']+', .-'+r['symbol']+'\n.balign 16\n'
        for i,r in enumerate(selected)))
    obj=directory/'fixture.o';tables=directory/'tables.o';elf=directory/'fixture.elf'
    flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
        '-O2','-ffreestanding','-fno-exceptions','-fno-rtti','-Wa,--noexecstack']
    subprocess.run([shared.TOOL+'g++',*flags,'-c',str(cpp),'-o',str(obj)],check=True)
    subprocess.run([shared.TOOL+'gcc',*flags,'-c',str(assembly),'-o',str(tables)],check=True)
    subprocess.run([shared.TOOL+'g++',*flags,'-nostdlib','-Wl,-Ttext=0x01000000',
        '-Wl,-e,native_vtable_fixture_adapted',str(obj),str(tables),'-o',str(elf)],check=True)
    probe=shared.Probe('isolated reviewed ARM32 vptr expression types',elf=elf)
    results=[]
    tables_before={r['symbol']:bytes(probe.cpu.mem_read(probe.names[r['symbol']],16)) for r in selected}
    for i,row in enumerate(selected):
        old=probe.call('native_vtable_fixture_original',[i])
        new=probe.call('native_vtable_fixture_adapted',[i])
        base=probe.names[row['symbol']]
        assert new==base+row['offset']
        assert old==base+(8 if row['label']=='already_index2' else 0)
        assert not probe.faults
        results.append(dict(**row,original_value=hex(old),adapted_value=hex(new),
            symbol_address=hex(base),expected_address=hex(base+row['offset']),
            original_RHS_type_preserved_at_compile_time=True))
    assert all(bytes(probe.cpu.mem_read(probe.names[n],16))==before for n,before in tables_before.items())
    return dict(status='passed',scope='Isolated expression/declaration fixture; no factory/game lifecycle',
        ARM11_compile=True,ARM32_CPU=True,cases=results,fixture_tables_are_synthetic=True,
        tables_unchanged=True,callee_saved_registers_stack_and_canaries_checked_by_Probe=True,
        fixture_elf_sha256=sha(elf.read_bytes()),generated_fixture_source_sha256=sha(cpp.read_bytes()))


def candidate_factory_checks(elf):
    """Execute frozen factory code, with only its allocation boundary supplied.

    No captured runtime RAM is relocated or borrowed. Original data is restored
    from the ROM with this ELF's sections/fixups and real restore primitive.
    This is fixture initialization, not native_runtime_load or game boot proof.
    GCC PMF calls stop at the actual selected method entry, before resources/IO.
    """
    elf_hash=sha(elf.read_bytes())
    assert elf_hash=='396ac3ff7f20af01d411357d97da9cdc4dccdee70f6fc53d1d8305a5804361c8'
    initialized,initialization=fresh_original_data_fixture(elf)
    cases=[('Stage','dScStage_c_classInit','_ZTV5Stage',True),
        ('Camera','dCamera_c_classInit','_ZTV6Camera',True),
        ('HUD','dMeter_c_classInit','_ZTV3HUD',True),
        ('Minimap','dMap_c_classInit','_ZTV7Minimap',True),
        ('scalar_pointer_Cannon','daCnn_c_classInit','_ZTV6Cannon',True),
        ('scalar_int_ChiefChilly','daKing_Donketu_c_classInit','_ZTV11ChiefChilly',True),
        ('char_array_Entry','dScEntry_c_classInit_ENTRY','_ZTV10dScEntry_c',True),
        ('bare_Dorrie','daDossy_c_classInit','_ZTV6Dorrie',False),
        ('bare_Monkey','daMky_c_classInit_MONKEY_STAR','_ZTV7daMky_c',False),
        ('bare_BigBooIcon','daTrsIcon_c_classInit','_ZTV10BigBooIcon',False),
        ('bare_Player','daPly_c_classInit','data_ov002_0210a83c',False)]
    results=[]
    for label,factory,table,gcc in cases:
        probe=shared.Probe('frozen factory '+label,elf=elf)
        for address,payload in initialized:probe.cpu.mem_write(address,payload)
        assert factory in probe.names and table in probe.names,(label,factory,table)
        actor_id,selected_module,choice_count=prepare_factory_inputs(probe,factory)
        probe.cpu.mem_write(shared.WORK,bytes([0xa5])*0x10000)
        allocations=[];writes=[];entry_names={};cursor=shared.WORK+0x1000
        for name,address in probe.names.items():
            if name.endswith('_classInit') or name.endswith(('C1Ev','C2Ev')):
                entry_names.setdefault(address,[]).append(name)
        entries=[]
        def observe(cpu,address,size,_):
            nonlocal cursor
            if address in entry_names:entries.append(sorted(entry_names[address])[0])
            if address==probe.names['_ZN7fBase_cnwEj']:
                size=cpu.reg_read(shared.REGS[0]);assert 0<size<=0x5000
                pointer=cursor;cursor=(cursor+size+63)&~15
                assert cursor<shared.WORK+0xf000
                allocations.append(dict(address=pointer,size=size))
                cpu.reg_write(shared.REGS[0],pointer)
                cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        def written(cpu,access,address,size,value,_):
            if allocations and address==allocations[0]['address'] and size==4:
                writes.append(dict(pc=hex(cpu.reg_read(UC_ARM_REG_PC)),value=hex(value)))
        hook=probe.cpu.hook_add(UC_HOOK_CODE,observe)
        memory_hook=probe.cpu.hook_add(UC_HOOK_MEM_WRITE,written)
        # Shared helper checks return, stack, callee saved registers and guards.
        pointer=probe.call(factory,[])
        probe.cpu.hook_del(hook);probe.cpu.hook_del(memory_hook)
        assert allocations and pointer==allocations[0]['address']
        final=probe.u32(pointer);base=probe.names[table]
        expected=base+(8 if gcc else 0)
        assert final==expected,(label,hex(final),hex(expected))
        for allocation in allocations:
            assert bytes(probe.cpu.mem_read(allocation['address']-16,16))==bytes([0xa5])*16
            assert bytes(probe.cpu.mem_read(allocation['address']+allocation['size'],16))==bytes([0xa5])*16
        prefix=[probe.u32(base+i*4) for i in range(3)]
        dispatch=None
        if gcc:
            rtti=probe.names['_ZTI'+table[4:]]
            assert prefix[:2]==[0,rtti]
            before=probe.u32(final+4)
            names=sorted(n for n,a in probe.names.items() if a==before)
            if label=='Stage':assert before==probe.names['_ZN5Stage19BeforeInitResourcesEv']
            process=probe.names['_ZN7fBase_c7ProcessEMS_FivEMS_FbvEMS_FvjE']
            sp=shared.STACK+0xf000
            probe.cpu.mem_write(shared.STACK+0xe000,bytes([0x6d])*0x2000)
            # Itanium ARM32 PMFs: fn slot byte offset, (this adjustment<<1)|1.
            arguments=[pointer,0,1,4,1,8,1]
            for i,value in enumerate(arguments[:4]):probe.cpu.reg_write(shared.REGS[i],value)
            for i,value in enumerate(arguments[4:]):probe.words(sp+i*4,value)
            probe.cpu.reg_write(UC_ARM_REG_SP,sp);probe.cpu.reg_write(UC_ARM_REG_LR,shared.STOP)
            probe.cpu.reg_write(UC_ARM_REG_CPSR,0x10)
            reached=[]
            def method_entry(cpu,address,size,_):
                if address==before:
                    reached.append(dict(address=hex(address),this_pointer=hex(cpu.reg_read(shared.REGS[0]))))
                    cpu.emu_stop()
            stop=probe.cpu.hook_add(UC_HOOK_CODE,method_entry)
            object_before=bytes(probe.cpu.mem_read(pointer,allocations[0]['size']))
            probe.cpu.emu_start(process,shared.STOP,count=1000)
            probe.cpu.hook_del(stop)
            assert reached==[dict(address=hex(before),this_pointer=hex(pointer))]
            assert probe.cpu.reg_read(UC_ARM_REG_PC)==before and not probe.faults
            assert bytes(probe.cpu.mem_read(pointer,allocations[0]['size']))==object_before
            assert bytes(probe.cpu.mem_read(sp-0x1000,32))==bytes([0x6d])*32
            dispatch=dict(PMFs=[[0,1],[4,1],[8,1]],real_Process_executed=True,
                target_names=names,target=hex(before),reached= reached,
                stopped_before_selected_method_instructions=True,
                selected_method_body_or_InitResources_success_not_claimed=True,
                object_unchanged_by_dispatch=True,stack_guard_unchanged=True)
        # Allocation failure is a separate fixture and must return null without
        # changing object memory or substituting constructor success.
        null=shared.Probe('frozen factory allocation failure '+label,elf=elf)
        for address,payload in initialized:null.cpu.mem_write(address,payload)
        null_actor_id,_,_=prepare_factory_inputs(null,factory)
        assert null_actor_id==actor_id
        null.cpu.mem_write(shared.WORK,bytes([0xa5])*0x10000);null_calls=[]
        def fail_allocation(cpu,address,size,_):
            if address==null.names['_ZN7fBase_cnwEj']:
                null_calls.append(cpu.reg_read(shared.REGS[0]))
                cpu.reg_write(shared.REGS[0],0)
                cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        handle=null.cpu.hook_add(UC_HOOK_CODE,fail_allocation)
        null_result=null.call(factory,[]);null.cpu.hook_del(handle)
        assert null_result==0 and len(null_calls)==1
        assert bytes(null.cpu.mem_read(shared.WORK,0x10000))==bytes([0xa5])*0x10000
        results.append(dict(label=label,factory=factory,table=table,
            controlled_original_caller_inputs=dict(actor_id=hex(actor_id),parent=0,param=0,create_kind=1,
                setter='func_02043180',original_factory_module=hex(selected_module),
                exact_module_choice_slots=choice_count,module_constructors_not_run=True),
            real_factory_and_original_member_constructors_executed=True,constructor_entries=entries,
            allocations=[dict(address=hex(a['address']),size=a['size']) for a in allocations],
            allocation_only_seam='_ZN7fBase_cnwEj returns separate poisoned fixture storage',
            allocation_poison='0xa5',vptr_writes=writes,final_vptr=hex(final),
            table_symbol_address=hex(base),expected_address_point=hex(expected),
            retained_GCC_prefix=gcc,GCC_prefix_words=list(map(hex,prefix)) if gcc else None,
            bare_original_address_point_preserved=not gcc,
            original_ROM_table_prefix_words=list(map(hex,prefix)) if not gcc else None,
            allocation_canaries_unchanged=True,PMF_dispatch=dispatch,
            null_allocator_returns_null_and_object_canaries_unchanged=True))
    assert sha(elf.read_bytes())==elf_hash
    return dict(status='passed',elf=str(elf.resolve()),elf_sha256=elf_hash,cases=results,
        old_RAM_not_installed=True,fixture_initialization=initialization,
        no_game_heap_TLS_live_capture_or_scene_invented=True,
        factories_are_isolated_API_fixtures_not_game_stage_success=True,
        original_IO_audio_MMIO_not_seamed_into_success=True,
        no_Azahar_UI_or_production_changes=True)


def fresh_original_data_fixture(elf):
    """Real native-data restore, with ROM staging and exact ELF fixup identities."""
    probe=shared.Probe('fresh ROM data fixture for frozen ELF',elf=elf)
    cart=shared.base.Cartridge(ROOT/'build/europe.pak');images={}
    first=probe.names['sm64ds_runtime_sections'];last=probe.names['sm64ds_runtime_sections_end']
    assert (last-first)%20==0
    rows=[struct.unpack('<5I',probe.cpu.mem_read(at,20)) for at in range(first,last,20)]
    sources=[]
    for address,module,original,size,zero in rows:
        assert size and zero in (0,1)
        if zero:sources.append(None);continue
        if module not in images:images[module]=cart.image(module)
        base,_,image=images[module];offset=original-base
        assert 0<=offset and offset+size<=len(image)
        sources.append(bytearray(image[offset:offset+size]))
    fixes=[];deferred=0
    start=probe.names['sm64ds_runtime_fixups'];end=probe.names['sm64ds_runtime_fixups_end']
    assert (end-start)%16==0
    for at in range(start,end,16):
        section,offset,expected,replacement=struct.unpack('<4I',probe.cpu.mem_read(at,16))
        assert section<len(rows) and sources[section] is not None and offset+4<=rows[section][3]
        assert struct.unpack_from('<I',sources[section],offset)[0]==expected
        if replacement:fixes.append((section,offset,expected,replacement))
        else:
            # Mirrors native_runtime_load's explicitly deferred slot staging;
            # no module choice is guessed and no original source is modified.
            struct.pack_into('<I',sources[section],offset,0);deferred+=1
    scratch=0x34000000;probe.cpu.mem_map(scratch,0x2000000)
    section_array=scratch;fix_array=scratch+0x10000;cursor=scratch+0x100000
    for i,((address,module,original,size,zero),payload) in enumerate(zip(rows,sources)):
        source=0 if zero else cursor
        if payload:
            probe.cpu.mem_write(cursor,bytes(payload));cursor=(cursor+len(payload)+15)&~15
        assert cursor<scratch+0x2000000
        probe.cpu.mem_write(section_array+i*16,struct.pack('<4I',address,source,size,zero))
    probe.cpu.mem_write(fix_array,b''.join(struct.pack('<4I',*fix) for fix in fixes))
    assert fix_array+len(fixes)*16<scratch+0x100000
    # This primitive's copy/validation budget covers the full original-data
    # fixture; no SDK allocator, TLS, file-open or syscall is substituted.
    sp=shared.STACK+0xf000
    probe.cpu.mem_write(shared.STACK+0xe000,bytes([0x6d])*0x2000)
    for reg,value in zip(shared.REGS,[section_array,len(rows),fix_array,len(fixes)]):
        probe.cpu.reg_write(reg,value)
    probe.cpu.reg_write(UC_ARM_REG_CPSR,0x10);probe.cpu.reg_write(UC_ARM_REG_SP,sp)
    probe.cpu.reg_write(UC_ARM_REG_LR,shared.STOP)
    probe.cpu.emu_start(probe.names['sm64ds_restore_native_data'],shared.STOP,count=20000000)
    assert probe.cpu.reg_read(UC_ARM_REG_PC)==shared.STOP and probe.cpu.reg_read(shared.REGS[0])==0
    assert probe.cpu.reg_read(UC_ARM_REG_SP)==sp and not probe.faults
    restored=[(r[0],bytes(probe.cpu.mem_read(r[0],r[3]))) for r in rows]
    return restored,dict(original_ASMP0_ROM_data_fixture=True,same_ELF_native_section_identities=True,
        real_sm64ds_restore_native_data_executed=True,return_code=0,
        sections=len(rows),resolved_fixups=len(fixes),deferred_slots_kept_null=deferred,
        no_native_runtime_load_or_Boot_or_module_activation_claimed=True,
        no_ROM_bytes_exported=True,no_old_runtime_snapshot_used=True)


def prepare_factory_inputs(probe,factory):
    # Overlay ambiguity is resolved only to the original metadata module that
    # owns this selected factory. This is a declared data fixture operation;
    # loaded/initialized flags and module constructors are not fabricated.
    modules=[]
    for path in [ROOT/'upstream/config/arm9/symbols.txt',
            *sorted((ROOT/'upstream/config/arm9/overlays').glob('ov*/symbols.txt'))]:
        if any(line.startswith(factory+' kind:function') for line in path.read_text().splitlines()):
            modules.append(0xffffffff if path.parent.name=='arm9' else int(path.parent.name[2:]))
    assert len(modules)==1,(factory,modules)
    module=modules[0];selected=0
    for at in range(probe.names['sm64ds_runtime_choices'],probe.names['sm64ds_runtime_choices_end'],20):
        section,offset,expected,first,count=struct.unpack('<5I',probe.cpu.mem_read(at,20))
        for n in range(count):
            candidate_module,address=struct.unpack('<2I',probe.cpu.mem_read(
                probe.names['sm64ds_runtime_candidates']+(first+n)*8,8))
            if candidate_module==module:
                destination=probe.u32(probe.names['sm64ds_runtime_sections']+section*20)+offset
                probe.words(destination,address);selected+=1
    table=probe.names['ACTOR_SPAWN_TABLE']
    count=(probe.names['ACTOR_DEBUG_NAMES']-table)//4
    assert 0<count<=1024
    matches=[]
    for actor_id in range(count):
        profile=probe.u32(table+actor_id*4)
        if profile and probe.u32(profile)==probe.names[factory]:matches.append(actor_id)
    assert matches,(factory,'No original restored profile for selected factory')
    actor_id=matches[0]
    # This is NativeBoot's original table binding and the unchanged original
    # caller's explicit argument setter, separate from a running scene.
    probe.words(probe.names['data_020a4bb8'],table)
    probe.call('func_02043180',[actor_id,0,0,1])
    return actor_id,module,selected


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-title-video/vtable-recipe-test.json')
    parser.add_argument('--factory-elf',type=Path,help='Frozen new bundle; isolated factories only, no old RAM restoration')
    args=parser.parse_args()
    if args.factory_elf:
        report=candidate_factory_checks(args.factory_elf)
        report['tool_sha256']=sha(Path(__file__).read_bytes())
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(dict(status='passed',report=str(args.report),factory_cases=len(report['cases']))))
        return
    recipe,static=static_checks()
    with tempfile.TemporaryDirectory(prefix='sm64ds-vtable-expressions-') as temp:
        arm=arm_expression_fixture(recipe,Path(temp))
    report=dict(status='passed',static=static,isolated_ARM_expressions=arm,
        general_ELF_or_native_core_report_not_read=True,no_Azahar_UI_production_or_upstream_changes=True,
        source_sha256={str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in
            [Path(__file__),ROOT/'tests/native_vtable_arm.cpp',native_vtable.RECIPE,
             ROOT/'tools/native_vtable.py']})
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='passed',report=str(args.report),reviewed_sites=352,
        unchanged_controls=59,ARM_type_cases=len(arm['cases']))))


if __name__=='__main__':main()
