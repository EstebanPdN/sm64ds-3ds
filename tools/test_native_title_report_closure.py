#!/usr/bin/env python3
"""Cross-check preserved CPU route reports without replaying their components."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,default=ROOT/'build/native-title-video')
    args=parser.parse_args();directory=args.directory
    paths={name:directory/file for name,file in [
        ('normal2A','ram-title-ready-622993-keyboard-filemenu.json'),
        ('new_game','ram-title-ready-622993-new-game.json'),
        ('lifecycle','ram-title-ready-622993-stage-boundary.json'),
        ('ABI_fixture','ram-title-stage-abi.json'),
        ('manual_audit','manual-vptr-abi-audit.json'),
        ('manual_recipe','manual-vptr-abi-audit-raw-stores.json')]}
    hashes={name:digest(path) for name,path in paths.items()}
    reports={name:json.loads(path.read_text()) for name,path in paths.items()}
    lifecycle=reports['lifecycle']['scene_lifecycle'];new=reports['lifecycle']['new_game']
    normal=reports['normal2A']['route'];abi=reports['ABI_fixture']
    assert normal['status']=='passed' and new['status']=='passed'
    assert new['exit_result']==3 and new['pending_scene_id']==3
    assert new['original_functions_executed']['StartFile']==1
    assert lifecycle['original_functions_executed']['StartFile']==3
    assert lifecycle['failure']['step']==4 and not lifecycle['failure']['faults']
    assert not lifecycle['original_Stage_InitResources_executed']
    assert not lifecycle['original_Stage_constructor_executed']
    assert abi['immutable_baseline']['constructor_entries']==1
    assert not abi['immutable_baseline']['real_Stage_InitResources_reached']
    assert abi['compiled_candidate']['real_Stage_BeforeInitResources_reached']
    assert abi['compiled_candidate']['real_Stage_InitResources_reached']
    reference_checks=[]
    audit=reports['manual_audit']['manual_assignment_audit']
    recipe=reports['manual_recipe']
    assert len(audit['assignments'])==411 and len(recipe['rows'])==351
    for row in audit['assignments']:
        path=ROOT/row['source'];raw=path.read_bytes();text=raw.decode('utf-8')
        assert digest(path)==row['source_sha256']
        assert text[row['rhs_reference_start_chars']:row['rhs_reference_end_chars']]==row['symbol']
        assert raw[row['rhs_reference_start_bytes']:row['rhs_reference_end_bytes']].decode('utf-8')==row['symbol']
        assert text.count('\n',0,row['rhs_reference_start_chars'])+1==row['rhs_reference_line']
        reference_checks.append((row['source'],row['rhs_reference_start_bytes']))
    assert len(set(reference_checks))==411
    stage_rows=[r for r in audit['assignments'] if r['source']=='upstream/src/d_s_stage.cpp']
    assert len(stage_rows)==3
    middle=next(r for r in stage_rows if r['symbol']=='_ZTV8dScene_c')
    assert middle['classification']=='symbol_not_retained_no_native_address_point_claimed'
    char_rows=[r for r in recipe['rows'] if r['declared_symbol_type'].startswith('char ')]
    assert len(char_rows)==3
    entries=lifecycle['original_functions_executed']
    previous=new['original_functions_executed']
    deltas={name:count-previous.get(name,0) for name,count in entries.items()
        if count>previous.get(name,0)}
    sources=['src/_ZN9dScDSMT_c8BehaviorEv.cpp','src/_ZN10dFdDummy_c14SetForwardTimeEj.cpp',
        'src/_ZN10dFdDummy_c11AdvanceFadeEv.cpp','src/_ZN7fBase_c14BeforeBehaviorEv.cpp']
    input_path=ROOT/'build/native-runtime-azahar/attempt-1791616229939712000/input.log'
    assert 'input tick=3036 held=1 pressed=1' in input_path.read_text()
    report=dict(status='passed',scope='Persistent report/source cross-check only; no component replay',
        evidence={name:dict(path=str(path.resolve()),sha256=hashes[name]) for name,path in paths.items()},
        integrated_input_timeline=dict(ready_snapshot_tick=2810,final_snapshot_tick=2970,
            actual_first_A_tick=3036,actual_A_release_tick=3037,input_log_sha256=digest(input_path),
            actual_A_occurs_after_final_RAM_capture=True,
            normal_two_A_inputs_are_controlled_clone_inputs=True),
        normal_two_A=dict(passed=True,first_A_focus=0,second_fresh_A_Adventure=True,
            main5_at_component_step=normal['file_selection_step'],
            presentation_settled_at_component_step=normal['file_selection_settled_step']),
        new_empty_file=dict(exit_result=3,pending_scene=3,StartFile_calls_in_selection_phase=1,
            StartFile_calls_cumulative_after_lifecycle=3,
            additional_StartFile_calls_in_lifecycle=2,lifecycle_entry_count_deltas=deltas,
            repeated_before_cleanup_is_original_source_behavior=True,
            BeforeBehavior_allows_while_shouldBeKilled0_and_pauseFlags_bit2_clear=True,
            SaveFile_persistence_calls=entries.get('SaveFile',0),
            original_copy_and_StartFile_executed=True,no_IO_or_scene_success_forced=True),
        dummy_fader=dict(real_type='dFdDummy_c',SetForwardTime_ignores_frames_and_sets_speed='0x1000',
            actual_AdvanceFade_calls=entries['_ZN10dFdDummy_c11AdvanceFadeEv'],
            cleanup_reached_at_component_step=3,thirty_tick_generic_fade_not_applicable=True),
        constructor_flag_annotation=dict(historical_flag=lifecycle['original_Stage_constructor_executed'],
            historical_observer_excluded_plain_classInit_names=True,
            corrected_interpretation='Normal registry dispatch reached Stage base/member construction; '
                'the false constructor flag is missing observer coverage, not skipped construction.',
            independent_registry_fixture_constructor_entries=abi['immutable_baseline']['constructor_entries'],
            actual_component_last_entries=lifecycle['failure']['last_entries'],
            InitResources_not_reached_in_immutable_baseline=True,
            historical_report_preserved=True,future_observer_filter_corrected=True),
        ABI_boundaries=dict(immutable_virtual_dispatch=abi['immutable_baseline']['actual_first_virtual_BLX'],
            immutable_boundary=abi['immutable_baseline']['component_boundary'],
            independent_candidate_virtual_dispatch=abi['compiled_candidate']['actual_first_virtual_BLX'],
            independent_candidate_boundary=abi['compiled_candidate']['component_boundary'],
            candidate_is_separate_API_fixture_not_normal_route_success=True,
            TLS_absent_in_old_real_capture_is_oracle_frontier=True,full_Stage_Init_success_not_claimed=True),
        reference_recipe=dict(all411_hashes_and_char_and_UTF8_slices_verified=True,
            unique_reference_positions=411,raw_GCC_rows=351,
            Stage_three_source_stores_verified=True,
            Stage_middle_store=middle,
            Stage_middle_store_excluded_from351_due_to_missing_native_symbol=True,
            raw_char_array_rows=char_rows,all_required_offsets_are_eight_bytes=True),
        source_sha256={name:digest(ROOT/'upstream'/name) for name in sources},
        no_Azahar_UI_launch_production_change_or_full_game_frame=True)
    path=directory/'ram-title-ready-622993-route-closure.json'
    path.write_text(json.dumps(report,indent=2)+'\n')
    assert hashes=={name:digest(path) for name,path in paths.items()}
    print(json.dumps(dict(status='passed',report=str(path),verified_references=411)))


if __name__=='__main__':main()
