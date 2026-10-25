#!/usr/bin/env python3
"""Check manual Stage vptr/ARM PMF dispatch against the captured native ABI.

The normal file-menu route is recorded by test_native_title_ram_arm.py. These
are separate actor-registry component fixtures on independent RAM clones: the
original func_02043098 receives controlled arguments (3,0,0,1). The candidate
changes only the manually stored Stage address point in a temporary compiled
constructor. No pending scene, lifecycle flags, InitResources results, IO
success, original source, immutable ELF, or live process is modified.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from test_native_title_ram_arm import FullRoute, Snapshot, shared
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_PC

ROOT=shared.ROOT
TOOL=shared.TOOL


def definitions(elf):
    rows={}
    for line in subprocess.check_output([TOOL+'nm','-S','--defined-only',elf],text=True).splitlines():
        c=line.split()
        if len(c)==4:rows[c[3]]=dict(address=int(c[0],16),bytes=int(c[1],16),kind=c[2])
        elif len(c)==3:rows[c[2]]=dict(address=int(c[0],16),bytes=0,kind=c[1])
    return rows


def without_comments(text):
    # Preserve offsets/line numbers when stripping comments for the parser.
    return re.sub(r'/\*.*?\*/|//[^\n]*',
        lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text,flags=re.S)


def visible_declarations(path,symbol):
    """Find explicit declarations in this file's local include closure."""
    seen=set();found=[]
    def visit(current):
        if current in seen:return
        seen.add(current);code=without_comments(current.read_text())
        for match in re.finditer(r'\bextern\s+([^;{}]*\b'+re.escape(symbol)+r'\b[^;{}]*);',code):
            found.append(dict(type=match[1].strip(),source=str(current.relative_to(ROOT)),
                line=code.count('\n',0,match.start())+1,
                sha256=shared.base.sha(current.read_bytes())))
        for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"',code,re.M):
            choices=[current.parent/name,ROOT/'upstream/include'/name]
            included=next((p.resolve() for p in choices if p.is_file()),None)
            if included:visit(included)
    visit(path.resolve())
    return found


def family_evidence(probe,elf,assignments,core):
    """Keep manual GCC stores distinct from restored bare actor/member tables."""
    definitions_by_name=definitions(elf)
    callable_addresses={r['address'] & ~1 for r in definitions_by_name.values() if r['kind'] in 'TtWw'}
    rtti_addresses={r['address'] for n,r in definitions_by_name.items() if n.startswith('_ZTI')}
    original={}
    for path in [ROOT/'upstream/config/arm9/symbols.txt',
            *sorted((ROOT/'upstream/config/arm9/overlays').glob('ov*/symbols.txt'))]:
        module=0xffffffff if path.parent.name=='arm9' else int(path.parent.name[2:])
        for line in path.read_text().splitlines():
            match=re.match(r'(\S+) kind:\S+ addr:0x([0-9a-f]+)',line)
            if match and match[1].startswith('_ZTV'):original[match[1]]=(module,int(match[2],16))
    companions=[]
    sources=['src/d_a_ply.cpp','src/_ZN6PlayerC1Ev.cpp','src/_ZN6PlayerD1Ev.cpp',
        'src/_ZN6PlayerD0Ev.cpp','src/d_s_dsmt.c','src/func_02017278.c']
    entries={r['source']:r for r in core['entries']}
    for source in sources:
        path=ROOT/'upstream'/source;code=without_comments(path.read_text())
        for match in re.finditer(r'[^;{}]*=[^;{}]*\b(data_(?:ov\d+_)?[0-9a-f]+)\b[^;{}]*;',code):
            symbol=match[1];assignment=code.index('=',match.start(),match.end())
            row=dict(source='upstream/'+source,line=code.count('\n',0,assignment)+1,
                source_sha256=shared.base.sha(path.read_bytes()),symbol=symbol,
                statement=' '.join(match[0].split()),enrolled_compiled=bool(entries.get(source,{}).get('compiled')),
                effectively_enrolled_object=entries.get(source,{}).get('object'))
            address=probe.names.get(symbol)
            if address is None:
                row['classification']='unretained_source_store_no_native_table_claimed'
            else:
                words=[probe.u32(address+i*4) for i in range(3)]
                gcc=words[0]==0 and words[1] in rtti_addresses
                bare=(words[0] & ~1) in callable_addresses and all(
                    word==0 or (word & ~1) in callable_addresses for word in words)
                row.update(native_address=hex(address),native_first_three_words=list(map(hex,words)),
                    native_has_GCC_prefix=gcc,first_three_entries_resolve_callable_or_null=bare,
                    classification='bare_restored_function_slot_array' if bare and not gcc else
                        'GCC_header' if gcc else 'other_data_pointer_not_classified_as_vptr',
                    preserve_bare_address_point=bare and not gcc)
            encoded=re.fullmatch(r'data_(?:ov(\d+)_)?([0-9a-f]+)',symbol)
            module=int(encoded[1]) if encoded[1] else 0xffffffff
            address=int(encoded[2],16)
            row['original_coaddressed_ZTV_symbols']=[n for n,a in original.items() if a==(module,address)]
            companions.append(row)
    parts=[r for r in core.get('split_translation_units',[]) if r['original_source']=='src/actors/Player.cpp']
    selected={'upstream/src/d_s_stage.cpp','upstream/src/d_camera.cpp',
        'upstream/src/d_meter.cpp','upstream/src/d_map.cpp'}
    return dict(Stage_Camera_HUD_Minimap_manual_stores=[r for r in assignments if r['source'] in selected],
        reachability_scope='Stage constructor was reached in the normal CPU route. Camera/HUD/Minimap '
            'are static Stage/player route families, not observed InitResources success or integrated actors.',
        HUD_minimap_source_route=dict(source='upstream/src/_ZN5Stage18LoadClsnAndObjectsER11LVL_OverlayjR7dBgW_Kc.cpp',
            HUD_actor_id='0x14e',HUD_condition='game mode is not 2 and intro is false',Minimap_actor_id='0x14f',
            Minimap_condition='game mode is not 2',
            intro_condition='game mode 0, save flag 0x80 absent, ContinueKuppaScriptIfNecessary returns 0'),
        Player_factory_calls_original_constructor=True,
        Player_constructor_uses_data_alias_not_compiler_ZTV=True,
        Player_split_original_source='upstream/src/actors/Player.cpp',Player_split_units=len(parts),
        Player_split_units_enrolled=sum(bool(entries.get(r['source'],{}).get('compiled')) for r in parts),
        Player_split_symbols=[r['symbol'] for r in parts],
        Player_cpp_literal_ZTV_assignments=sum(r['source']=='upstream/src/actors/Player.cpp' for r in assignments),
        bare_data_alias_companions=companions,
        genuine_GCC_member_constructors=list({'source':'upstream/src/'+name,
            'source_sha256':shared.base.sha((ROOT/'upstream'/name).read_bytes()),
            'enrolled_compiled':bool(entries.get(name,{}).get('compiled'))}
            for name in ['src/_ZN5ModelC1Ev.cpp','src/_ZN9ModelAnimC1Ev.cpp',
                'src/_ZN15TextureSequenceC1Ev.cpp','src/_ZN15MaterialChangerC1Ev.cpp',
                'src/_ZN10dCcAcPos_cC1Ev.cpp','src/_ZN10dBgCh_ActrC1Ev.cpp']),
        compiler_synthesized_member_vptrs_do_not_receive_manual_offset_fix=True,
        captured_registry_factory_identities=[dict(actor_id=hex(actor_id),
            profile_address=hex(probe.u32(probe.u32(probe.names['data_020a4bb8'])+actor_id*4)),
            constructor_address=hex(probe.u32(probe.u32(probe.u32(probe.names['data_020a4bb8'])+actor_id*4))),
            constructor_name=name,
            exact_pointer_matches_named_constructor=probe.u32(probe.u32(
                probe.u32(probe.names['data_020a4bb8'])+actor_id*4))==probe.names[name])
            for actor_id,name in [(3,'dScStage_c_classInit'),(0xbf,'daPly_c_classInit'),
                (0x14e,'dMeter_c_classInit'),(0x14f,'dMap_c_classInit')]])


def audit_assignments(probe,elf,cart):
    """Classify every literal _ZTV assignment, without a global +8 rule."""
    rows=definitions(elf);original={};images={}
    config=[ROOT/'upstream/config/arm9/symbols.txt',
        *sorted((ROOT/'upstream/config/arm9/overlays').glob('ov*/symbols.txt'))]
    for path in config:
        module=0xffffffff if path.parent.name=='arm9' else int(path.parent.name[2:])
        for line in path.read_text().splitlines():
            m=re.match(r'(\S+) kind:\S+ addr:0x([0-9a-f]+)',line)
            if m and m[1].startswith('_ZTV'):original[m[1]]=(module,int(m[2],16))
    core=json.loads((ROOT/'build/native-core/report.json').read_text())
    enrolled={r['source']:r for r in core['entries']}
    split=core.get('split_translation_units',[])
    manifest=json.loads((ROOT/'build/native-runtime/manifest.json').read_text())
    replacements=set(manifest['replaced_sources'])
    evidence=[]
    for path in sorted((ROOT/'upstream/src').rglob('*')):
        if path.suffix not in ('.c','.cpp'):continue
        text=path.read_text();code=without_comments(text)
        bodies=[]
        for function in re.finditer(r'\b([\w:~]+)\s*\([^;{}]*\)\s*(?::[^{};]+)?\{',code):
            if function[1] in ('if','while','switch','for','catch'):continue
            opening=function.end()-1;depth=1;end=opening+1
            while end<len(code) and depth:
                depth+=(code[end]=='{')-(code[end]=='}');end+=1
            if not depth:bodies.append((opening,end,function[1]))
        for match in re.finditer(r'[^;{}]*=[^;{}]*\b(_ZTV\w+)\b[^;{}]*;',code):
            symbol=match[1];statement=' '.join(match[0].split())
            assignment=code.index('=',match.start(),match.end())
            owners=[x for x in bodies if x[0]<assignment<x[1]]
            body=min(owners,key=lambda x:x[1]-x[0]) if owners else None
            function=body[2] if body else None
            class_init=[function] if function and re.search(r'_classInit(?:_|$)',function) else []
            source=str(path.relative_to(ROOT/'upstream'))
            compiled=enrolled.get(source)
            parts=[r for r in split if r['original_source']==source]
            compiled_parts=[r['source'] for r in parts if enrolled.get(r['source'],{}).get('compiled')]
            left=statement.split('=',1)[0].strip()
            declarations=visible_declarations(path,symbol)
            declared=declarations[0]['type'] if declarations else None
            declaration_kind=('array' if declared and re.search(re.escape(symbol)+r'\s*\[',declared)
                else 'scalar_symbol' if declared else 'included_header_or_local_alias')
            shifted=bool(re.search(re.escape(symbol)+r'\s*(?:\[\s*2\s*\]|\+\s*2\b)',statement))
            explicit_other=bool(re.search(re.escape(symbol)+r'\s*(?:\[\s*[1-9]\d*\s*\]|\+\s*[1-9]\d*\b)',statement))
            body_kind=('classInit_manual_vptr_store' if class_init else
                'manual_destructor_store' if function and ('~' in function or re.search(r'D[012]Ev$',function)) else
                'manual_constructor_store' if function and (re.search(r'C[012]Ev$',function) or
                    ('::' in function and function.split('::')[-1]==function.split('::')[-2])) else
                'other_manual_store_or_table_pointer')
            row=dict(source=str(path.relative_to(ROOT)),source_sha256=shared.base.sha(path.read_bytes()),
                class_init=class_init,body=function,body_kind=body_kind,symbol=symbol,statement=statement,
                code_offset=assignment,left_hand_side=left,declared_symbol_type=declared,
                line=code.count('\n',0,assignment)+1,visible_explicit_declarations=declarations,
                rhs_reference_start_chars=match.start(1),rhs_reference_end_chars=match.end(1),
                rhs_reference_start_bytes=len(text[:match.start(1)].encode('utf-8')),
                rhs_reference_end_bytes=len(text[:match.end(1)].encode('utf-8')),
                rhs_reference_line=text.count('\n',0,match.start(1))+1,
                rhs_reference_column=match.start(1)-text.rfind('\n',0,match.start(1)),
                original_RHS_reference=text[match.start(1):match.end(1)],
                original_RHS_expression=text[assignment+1:match.end()-1].strip(),
                declaration_kind=declaration_kind,explicit_index2_or_plus2=shifted,
                explicit_other_nonzero_offset=explicit_other and not shifted,
                enrolled_compiled=bool(compiled and compiled.get('compiled')),
                enrolled_object=compiled.get('object') if compiled else None,
                compiled_split_parts=compiled_parts,
                effectively_enrolled=bool((compiled and compiled.get('compiled')) or compiled_parts),
                current_manifest_replaces_source=source in replacements)
            native=rows.get(symbol)
            if native:
                address=native['address'];prefix=[probe.u32(address+i*4) for i in range(3)]
                rtti=probe.names.get('_ZTI'+symbol[4:])
                compiled_prefix=prefix[0]==0 and rtti is not None and prefix[1]==rtti
                row.update(native_address=hex(address),native_kind=native['kind'],
                    native_first_three_words=list(map(hex,prefix)),
                    native_RTTI=hex(rtti) if rtti is not None else None,
                    native_has_GCC_prefix=compiled_prefix,
                    required_manual_address_point=hex(address+(8 if compiled_prefix else 0)),
                    raw_assignment_requires_GCC_address_point=compiled_prefix and not (shifted or explicit_other))
                if function in rows:
                    f=rows[function];machine=bytes(probe.cpu.mem_read(f['address'],f['bytes']))
                    row['classInit_or_body_native_address']=hex(f['address'])
                    row['native_body_contains_raw_table_literal']=struct.pack('<I',address) in machine
                    row['native_body_contains_GCC_address_point_literal']=struct.pack('<I',address+8) in machine
                row['classification']=('already_explicit_plus2_or_index2' if shifted else
                    'explicit_other_address_point_do_not_shift' if explicit_other else
                    'raw_GCC_header_store' if compiled_prefix else 'ROM_bare_table_or_non_GCC_prefix')
            else:
                row['native_symbol_not_retained']=True
                row['classification']='symbol_not_retained_no_native_address_point_claimed'
            if symbol in original:
                module,address=original[symbol]
                if module not in images:images[module]=cart.image(module)
                base,_,image=images[module];offset=address-base
                if 0<=offset<=len(image)-12:
                    words=list(struct.unpack_from('<3I',image,offset))
                    row.update(original_module=hex(module),original_address=hex(address),
                        original_first_three_words=list(map(hex,words)),
                        original_address_point_starts_callable=all(0x02000000<=w<0x02400000 for w in words))
            evidence.append(row)
    # Source-level transient stores are kept distinct from final stores. The
    # native literal observations additionally expose optimized-away stores.
    for index,row in enumerate(evidence):
        transient=any(other['source']==row['source'] and other['body']==row['body'] and
            other['left_hand_side']==row['left_hand_side'] and other['code_offset']>row['code_offset']
            for other in evidence[index+1:]) if row['body'] else False
        row['transient_replaced_by_later_same_lhs_store']=transient
        if transient:row['body_kind_before_transient_classification']=row['body_kind'];row['body_kind']='transient_manual_store'
    raw=[r for r in evidence if r.get('raw_assignment_requires_GCC_address_point')]
    groups={name:[r for r in evidence if r.get('classification')==name] for name in
        ('already_explicit_plus2_or_index2','explicit_other_address_point_do_not_shift',
         'raw_GCC_header_store','ROM_bare_table_or_non_GCC_prefix',
         'symbol_not_retained_no_native_address_point_claimed')}
    return dict(assignments=evidence,
        total_literal_ZTV_assignments=len(evidence),
        assignments_with_retained_GCC_prefix_requiring_manual_adjustment=len(raw),
        classified_counts={k:len(v) for k,v in groups.items()},
        raw_GCC_header_stores=raw,
        already_plus2_stores=groups['already_explicit_plus2_or_index2'],
        bare_or_non_GCC_table_stores=groups['ROM_bare_table_or_non_GCC_prefix'],
        no_retained_symbol_stores=groups['symbol_not_retained_no_native_address_point_claimed'],
        declaration_counts={kind:sum(r['declaration_kind']==kind for r in evidence)
            for kind in ('array','scalar_symbol','included_header_or_local_alias')},
        body_kind_counts={kind:sum(r['body_kind']==kind for r in evidence)
            for kind in sorted({r['body_kind'] for r in evidence})},
        Stage_Player_family=family_evidence(probe,elf,evidence,core),
        raw_classInit_final_enrolled_stores=[r for r in raw if r['class_init'] and
            not r['transient_replaced_by_later_same_lhs_store'] and r['effectively_enrolled']],
        upstream_commit=core['upstream_commit'],
        native_core_report_sha256=shared.base.sha((ROOT/'build/native-core/report.json').read_bytes()),
        current_manifest_sha256=shared.base.sha((ROOT/'build/native-runtime/manifest.json').read_bytes()),
        global_ZTV_alias_shift_not_appropriate=True,
        real_GCC_constructors_already_publish_their_own_address_points=True,
        absence_of_retained_symbol_is_not_a_runtime_failure=True,
        all_original_tables_observed_read_only=True,no_original_table_bytes_exported=True)


def compile_candidate(directory,names):
    source=ROOT/'upstream/src/d_s_stage.cpp'
    text=source.read_text().replace('dScStage_c_classInit()', 'stage_ctor_candidate()')
    # Adjust explicit manual stores; genuine GCC base/member constructors keep
    # their native ABI and metadata symbols. No global symbol alias is shifted.
    count=0
    for symbol in ('_ZTV7dBase_c','_ZTV8dScene_c','_ZTV5Stage'):
        old='= '+symbol+';';assert old in text
        text=text.replace(old,'= '+symbol+' + 2;');count+=1
    assert count==3
    copied=directory/'stage_candidate.cpp';copied.write_text(text)
    obj=directory/'stage_candidate.o';elf=directory/'stage_candidate.elf'
    flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-O2',
        '-ffreestanding','-fno-exceptions','-DSM64DS_PLATFORM_PC=1',
        '-I'+str(ROOT/'upstream/include'),'-Wa,--noexecstack']
    subprocess.run([TOOL+'g++',*flags,'-c',str(copied),'-o',str(obj)],check=True)
    unresolved=[]
    for line in subprocess.check_output([TOOL+'nm','-u',obj],text=True).splitlines():
        name=line.split()[-1];assert name in names,('candidate callee absent in exact ELF',name)
        unresolved.append(name)
    # Absolute callee/data definitions address the immutable original ELF's
    # existing memory. The candidate supplies only its constructor text.
    script=directory/'symbols.ld'
    script.write_text('\n'.join(n+' = '+hex(names[n])+';' for n in unresolved)+'\n')
    subprocess.run([TOOL+'g++',*flags,'-nostdlib','-Wl,-e,stage_ctor_candidate',
        '-Wl,-Ttext=0x17000000','-Wl,-T,'+str(script),str(obj),'-o',str(elf)],check=True)
    return elf,dict(candidate_source_sha256=shared.base.sha(copied.read_bytes()),
        original_source_sha256=shared.base.sha(source.read_bytes()),
        candidate_elf_sha256=shared.base.sha(elf.read_bytes()),
        only_explicit_manual_Stage_address_points_adjusted=True,
        unchanged_original_callees=unresolved,original_source_not_modified=True)


def install_candidate(probe,elf):
    data=elf.read_bytes();offset=struct.unpack_from('<I',data,28)[0]
    size,count=struct.unpack_from('<HH',data,42)
    rows=[struct.unpack_from('<8I',data,offset+i*size) for i in range(count)]
    rows=[r for r in rows if r[0]==1 and r[5]]
    begin=min(r[2] for r in rows)&~0xfff
    end=(max(r[2]+r[5] for r in rows)+0xfff)&~0xfff
    assert 0x16ff0000<=begin<end<=0x17010000
    probe.cpu.mem_map(begin,end-begin)
    for _,at,address,_,filesz,_,_,_ in rows:probe.cpu.mem_write(address,data[at:at+filesz])
    return shared.base.symbols(elf)['stage_ctor_candidate']


def component_case(snapshot,elf,candidate=None):
    replay=FullRoute(snapshot,elf,rom_fs=True);probe=replay.p
    process=probe.names['_ZN7fBase_c7ProcessEMS_FivEMS_FbvEMS_FvjE']
    stage_ctor=probe.names['dScStage_c_classInit']
    override=install_candidate(probe,candidate) if candidate else None
    calls=[];targets=[];ctor_entries=0;failure=None
    entry_before_init=probe.names['_ZN5Stage19BeforeInitResourcesEv']
    entry_init=probe.names['_ZN5Stage13InitResourcesEv']
    reached=CounterLike()
    def observe(cpu,address,size,_):
        nonlocal ctor_entries
        if address==stage_ctor:
            ctor_entries+=1
            if override:cpu.reg_write(UC_ARM_REG_PC,override)
        if address in (entry_before_init,entry_init):reached[hex(address)]+=1
        if address==process:
            args=[cpu.reg_read(r) for r in shared.REGS];sp=cpu.reg_read(UC_ARM_REG_SP)
            self=args[0];vptr=replay.view.u32(self)
            pmfs=[[args[1],args[2]],[args[3],probe.u32(sp)],[probe.u32(sp+4),probe.u32(sp+8)]]
            assert vptr in (probe.names['_ZTV5Stage'],probe.names['_ZTV5Stage']+8)
            calls.append(dict(self=hex(self),actor_id=replay.view.u16(self+0xc),
                vptr=hex(vptr),pmfs=pmfs,
                before_init_target=hex(probe.u32(vptr+pmfs[1][0])),
                expected_BeforeInitResources=hex(entry_before_init),
                target_is_real_BeforeInitResources=probe.u32(vptr+pmfs[1][0])==entry_before_init))
        # Exact immutable ARM disassembly has its first virtual BLX at +0x28.
        if address==process+0x28:
            targets.append(dict(pc=hex(address),target=hex(cpu.reg_read(shared.REGS[3])),
                self=hex(cpu.reg_read(shared.REGS[0]))))
    hook=probe.cpu.hook_add(UC_HOOK_CODE,observe)
    before_alias=snapshot.read(probe.names['data_0209b340'],14)
    try:result=replay.call_component('func_02043098',[3,0,0,1])
    except Exception as error:
        result=None;failure=dict(type=type(error).__name__,message=str(error),
            pc=hex(probe.cpu.reg_read(UC_ARM_REG_PC)),faults=probe.faults,
            last_entries=list(replay.last_entries),
            registers={str(i):hex(probe.cpu.reg_read(r)) for i,r in enumerate(shared.REGS)},
            integrated_fault_not_claimed=True)
    probe.cpu.hook_del(hook);probe.cpu.hook_del(replay.hook)
    assert ctor_entries==1 and calls and calls[0]['actor_id']==3
    assert targets and targets[0]['target']==calls[0]['before_init_target']
    linear=next(s for s in snapshot.spans if s['name']=='linear-heap')
    assert bytes(probe.cpu.mem_read(probe.names['data_0209b340'],14))==before_alias
    assert bytes(probe.cpu.mem_read(linear['address'],linear['size']))==snapshot.read(linear['address'],linear['size'])
    if candidate:
        assert calls[0]['target_is_real_BeforeInitResources']
        assert reached[hex(entry_before_init)] and reached[hex(entry_init)]
    else:
        assert not calls[0]['target_is_real_BeforeInitResources']
        assert not reached[hex(entry_before_init)] and not reached[hex(entry_init)]
        assert failure and failure['type']=='UcError' and not probe.faults
    return dict(label='compiled manual-address-point candidate' if candidate else 'immutable ELF baseline',
        controlled_original_registry_API='func_02043098(3,0,0,1)',
        constructor_entries=ctor_entries,Process_calls=calls,actual_first_virtual_BLX=targets,
        real_Stage_BeforeInitResources_reached=bool(reached[hex(entry_before_init)]),
        real_Stage_InitResources_reached=bool(reached[hex(entry_init)]),
        return_value=hex(result) if result is not None else None,component_boundary=failure,
        unchanged_original_no_save_alias=True,unchanged_linear_heap=True,
        no_scene_pending_spawn_flags_or_IO_success_forced=True,
        raw_FAT_provider_events=replay.fs.events,provider_negative_tests=replay.fs_checks,
        original_functions_executed=dict(sorted(replay.entries.items())),
        declared_observation_seams=FullRoute.SEAMS,
        scope='Independent actor registry API fixture, not the normal game-frame route or an integrated launch.')


class CounterLike(dict):
    def __missing__(self,key):return 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot',type=Path,required=True)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--audit-only',action='store_true',help='Read immutable ELF/source/enrolment tables; do not repeat CPU fixtures')
    args=parser.parse_args();snapshot=Snapshot(args.snapshot)
    snapshot_hash=shared.base.sha(snapshot.raw);elf_hash=shared.base.sha(args.elf.read_bytes())
    probe=shared.Probe('read only symbol/table audit',elf=args.elf);snapshot.install(probe.cpu)
    cart=shared.base.Cartridge(ROOT/'build/europe.pak')
    audit=audit_assignments(probe,args.elf,cart)
    if args.audit_only:
        report=dict(status='passed',read_only_static_audit=True,elf=str(args.elf.resolve()),
            elf_sha256=elf_hash,snapshot_sha256=snapshot_hash,manual_assignment_audit=audit,
            source_sha256=shared.base.sha(Path(__file__).read_bytes()),
            no_game_component_replayed=True,no_Azahar_or_UI=True)
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
        recipe_path=args.report.with_name(args.report.stem+'-raw-stores.json')
        fields=('source','source_sha256','symbol','declaration_kind','declared_symbol_type',
            'rhs_reference_start_chars','rhs_reference_end_chars','rhs_reference_start_bytes',
            'rhs_reference_end_bytes','rhs_reference_line','rhs_reference_column',
            'original_RHS_reference','original_RHS_expression','statement','body','body_kind',
            'transient_replaced_by_later_same_lhs_store','effectively_enrolled','native_address',
            'native_kind','native_RTTI','native_has_GCC_prefix','required_manual_address_point')
        recipe=dict(read_only_recipe=True,parent_audit=str(args.report.resolve()),
            parent_audit_sha256=shared.base.sha(args.report.read_bytes()),elf_sha256=elf_hash,
            count=len(audit['raw_GCC_header_stores']),required_offset_bytes=8,
            reference_offsets_are_exclusive_end=True,
            source_offsets_preserve_original_comments_and_UTF8=True,
            rows=[{k:r[k] for k in fields} for r in audit['raw_GCC_header_stores']],
            excluded_classifications=audit['classified_counts'],no_production_changes=True)
        recipe_path.write_text(json.dumps(recipe,indent=2)+'\n')
        print(json.dumps(dict(status='passed',report=str(args.report),counts=audit['classified_counts'])))
        return
    baseline=component_case(snapshot,args.elf)
    with tempfile.TemporaryDirectory(prefix='sm64ds-stage-abi-') as temp:
        candidate,build=compile_candidate(Path(temp),probe.names)
        corrected=component_case(snapshot,args.elf,candidate)
    assert snapshot_hash==shared.base.sha(args.snapshot.read_bytes())
    assert elf_hash==shared.base.sha(args.elf.read_bytes())
    report=dict(status='passed',elf=str(args.elf.resolve()),elf_sha256=elf_hash,
        snapshot=snapshot.summary(),snapshot_sha256=snapshot_hash,
        immutable_baseline=baseline,compiled_candidate=corrected,candidate_build=build,
        manual_assignment_audit=audit,
        original_source_and_RAM_and_ELF_unchanged=True,
        independent_API_fixture_not_full_frame=True,
        no_global_ZTV_alias_shift=True,no_fake_IO_success_or_Stage_Init_return=True,
        source_sha256={str(p.relative_to(ROOT)):shared.base.sha(p.read_bytes()) for p in
            (Path(__file__).resolve(),ROOT/'tools/test_native_title_ram_arm.py')})
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='passed',report=str(args.report),
        baseline_boundary=baseline['component_boundary'],candidate_boundary=corrected['component_boundary'],
        raw_manual_GCC_assignments=audit['assignments_with_retained_GCC_prefix_requiring_manual_adjustment'])))


if __name__=='__main__':main()
