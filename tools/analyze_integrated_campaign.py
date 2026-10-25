#!/usr/bin/env python3
"""Validate a closed original-API baseline and analyze its three complete dumps.

Never counts a title/failed/partial capture or infers physical console FPS.
Timing scopes overlap: each is reported independently, never added together.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import struct
from elftools.elf.elffile import ELFFile
from inspect_integrated_vm import VM, trace_report, dump_files, find_dumps, capture_media_report


def companion(path,suffix):
    """Legacy suffix -> file of the same dump (legacy stem or dump folder)."""
    key={'.lcd':'lcd','.anchors.json':'anchors','.trace':'trace','.render':'render','.upper':'upper','.lower':'lower'}[suffix]
    return dump_files(path)[key]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def analyze(attempt, classify_unplanned_exit=False):
    closed=json.loads((attempt/'result.json').read_text())
    plan=json.loads((attempt/'plan.json').read_text())
    if classify_unplanned_exit:
        assert closed['stop_reason']=='process_exit','Explicit classification is only for an unplanned process exit'
    else:
        assert closed['stop_reason']=='benchmark_three_complete_captures'
    assert closed['first_emulator_fault'] is None and plan['benchmark']['captures_requested']==3
    def verified(relative):
        p=attempt/relative
        assert p.is_file() and sha(p)==closed['files'][relative]['sha256'],relative
        return p
    elf=verified('sm64ds-integrated.elf')
    assert sha(elf)==closed['ELF_sha256']
    with elf.open('rb') as f:
        symbols={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    log=verified('user/sdmc/3ds/sm64ds/user-test.log').read_text()
    manual=None;note=attempt/'manual-input.json'
    if note.exists():
        assert note.is_file() and not note.is_symlink()
        manual=json.loads(note.read_text())
        if manual['status']=='main_physics_modes_exercised':
            # Notes written while the owned run was alive are frozen by its
            # closing inventory; do not rewrite them into a post-close receipt.
            verified('manual-input.json')
        else:
            assert manual['status']=='manual_CUA_input_during_original_API_scenario'
            assert manual['ELF_sha256']==closed['ELF_sha256'] and manual['closed_result_sha256']==sha(attempt/'result.json')
            assert manual['game_log_sha256']==closed['files']['user/sdmc/3ds/sm64ds/user-test.log']['sha256']
            assert manual['scenario']==plan['benchmark']['profile']
    events=[]
    for line in log.splitlines():
        if line.startswith('BENCHMARK event='):
            events.append(dict(re.findall(r'(\w+)=([^ ]+)',line)))
    requests=[e for e in events if e['event']=='request-full-RAM-dump']
    finished=[e for e in events if e['event'] in ('dump-completed-clock-resumed','complete-three-dumps')]
    assert len(requests)==len(finished)==3 and finished[-1]['event']=='complete-three-dumps'
    assert [int(e['dumpsCompleted']) for e in finished]==[1,2,3]
    for e in requests+finished:
        assert e['scene']=='3' and e['level']==e['expectedLevel'] and e['character']==e['expectedCharacter']
        assert e['player']==e['bodyModel']=='1' and e['scenario']==plan['benchmark']['profile']
    # Exact measured intervals: exclude intro, suspended control, dump I/O and
    # the GPU readbacks. The first row of each interval is also excluded since
    # its counter delta may begin before that interval.
    windows=[];start=None
    for e in events:
        tick=int(e['ticks'])
        if e['event'] in ('measurement-start','dump-completed-clock-resumed'):
            start=tick
        elif e['event'] in ('request-full-RAM-dump','player-model-validated-settling'):
            if start is not None and tick>start:windows.append((start,tick))
            start=None
    snapshots=[];extra_diagnostics=[];rows={};selected=set()
    vm_paths=find_dumps(attempt/'user/sdmc/3ds/sm64ds')
    assert len(vm_paths)>=3
    hitch_events=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in log.splitlines()
                  if line.startswith('HITCH captureRequested=')]
    auto_hitches=plan.get('automatic_hitch_capture',{})
    assert len(hitch_events)<=auto_hitches.get('maximum',0)
    if len(vm_paths)>3:
        assert (manual and plan['benchmark'].get('post_capture_inspection_seconds',0)>0) or auto_hitches,'Unexpected diagnostic captures; classify before counting'
        assert len(vm_paths)<=8,'Unbounded diagnostic series requires separate classification'
    for path in vm_paths:
        verified(path.relative_to(attempt).as_posix());vm=VM(path)
        assert vm.header['scene']==3 and vm.header['complete']
        media=None
        if vm.header['version']>=2:
            verified(companion(path,'.lcd').relative_to(attempt).as_posix())
            metadata=json.loads(verified(companion(path,'.anchors.json').relative_to(attempt).as_posix()).read_text())
            media=capture_media_report(vm,path,metadata)
        matches=[i for i,(a,b) in enumerate(zip(requests,finished))
                 if int(a['ticks'])<=vm.header['begin_tick']<=vm.header['end_tick']<=int(b['ticks'])]
        if not matches:
            hitch=[e for e in hitch_events if 0<=vm.header['begin_tick']-int(e['ticks'])<=268111856]
            exclusions=(manual or {}).get('diagnostic_exclusions',{})
            if hitch:
                phase='automatic-gameplay-hitch'
            elif vm.header['end_tick']<windows[0][0]:
                assert path.name in exclusions and exclusions[path.name]['phase']=='before-stable-control'
                phase='before-stable-control'
            else:
                assert vm.header['begin_tick']>int(finished[-1]['ticks']),'Diagnostic overlaps a measured interval'
                phase='after-three-captures'
            for suffix in ('.anchors.json','.render','.trace','.upper'):
                verified(companion(path,suffix).relative_to(attempt).as_posix())
            trace=trace_report(verified(companion(path,'.trace').relative_to(attempt).as_posix()))
            for row in trace['rows'][1:]:rows[(row['tick'],row['frame'])]=row
            extra_diagnostics.append(dict(filename=path.name,VM_sha256=sha(path),scene=vm.header['scene'],epoch=vm.header['epoch'],complete=True,phase=phase,capture_media=media,scope='Explicitly classified diagnostic outside measured windows; not counted toward the three measured samples'))
            continue
        assert len(matches)==1 and matches[0] not in selected
        selected.add(matches[0])
        anchors_path=verified(companion(path,'.anchors.json').relative_to(attempt).as_posix())
        anchors=json.loads(anchors_path.read_text())
        assert anchors['scene']==3 and anchors['epoch']==vm.header['epoch'] and anchors['worker_drained']
        display=struct.unpack('<5IfB3x',vm.read(symbols['_ZL9g_display'],28))
        assert display[:5]==(400,240,0,400,240) and display[6]==1,'Native+Wide was not actual at capture'
        assert abs(display[5]-.8)<.0001
        render=verified(companion(path,'.render').relative_to(attempt).as_posix()).read_bytes()
        assert len(render)==897040 and struct.unpack_from('<4I',render)==(0x34524d53,1,256,192)
        trace=trace_report(verified(companion(path,'.trace').relative_to(attempt).as_posix()))
        for row in trace['rows'][1:]:
            rows[(row['tick'],row['frame'])]=row
        player_address=vm.u32(anchors['anchors']['data_0209f394'])
        assert player_address
        logical_character=vm.read(player_address+0x6d9,1)[0]
        render_body_character=vm.read(player_address+0x6db,1)[0]
        assert logical_character==render_body_character==int(requests[matches[0]]['expectedCharacter'])
        snapshots.append(dict(index=matches[0]+1,filename=path.name,epoch=vm.header['epoch'],
            begin_tick=vm.header['begin_tick'],end_tick=vm.header['end_tick'],
            VM_sha256=sha(path),readable_payload_bytes=sum(r['payload_bytes'] for r in vm.regions),
            regions=len(vm.regions),scan_end=vm.header['scan_end'],complete=True,capture_media=media,
            geometry=[400,240],projection_x=display[5],player=hex(player_address),
            player_position_fixed=list(struct.unpack('<3i',vm.read(player_address+0x5c,12))),
            logical_character=logical_character,render_body_character=render_body_character,
            trace_records=trace['record_count'],trace_overwritten=trace['overwritten']))
    assert selected=={0,1,2}
    ordered=sorted(rows.values(),key=lambda r:r['tick']);steady=[]
    for begin,end in windows:
        part=[r for r in ordered if begin<=r['tick']<=end and r['scene']==3 and not r['overflow']]
        steady.extend(r for r in part[1:] if not r.get('context') or
                      not any(r['context'].get(flag) for flag in ('pause','dialogue','cutscene')))
    assert steady,'No measured timing rows'
    stats={}
    for name in steady[0]['intervals_ms']:
        values=sorted(r['intervals_ms'][name] for r in steady)
        if values[-1]:stats[name]=dict(mean_ms=statistics.mean(values),
            p95_ms=values[(len(values)-1)*95//100],max_ms=values[-1])
    camera_sectors={}
    for row in steady:
        sector=(row.get('context') or {}).get('camera_sector')
        if sector is not None:camera_sectors.setdefault(str(sector),[]).append(row)
    sector_costs={}
    for sector,items in camera_sectors.items():
        sector_costs[sector]={'samples':len(items),'independent_overlapping_scopes':{}}
        for name in items[0]['intervals_ms']:
            values=sorted(r['intervals_ms'][name] for r in items)
            if values[-1]:sector_costs[sector]['independent_overlapping_scopes'][name]=dict(
                mean_ms=statistics.mean(values),p95_ms=values[(len(values)-1)*95//100],max_ms=values[-1])
    return dict(status='three_complete_captures_classified_unplanned_exit' if classify_unplanned_exit else 'three_complete_original_API_baseline_captures_validated',
        run_acceptance='unplanned emulator exit; runtime acceptance remains unresolved' if classify_unplanned_exit else 'bounded capture run closed as planned',
        attempt=str(attempt.resolve()),ELF_sha256=closed['ELF_sha256'],model=closed['model'],
        scenario=plan['benchmark']['profile'],snapshots=sorted(snapshots,key=lambda s:s['index']),
        extra_diagnostics=extra_diagnostics,
        analysis_sources_sha256={p.name:sha(p) for p in (Path(__file__),Path(__file__).with_name('inspect_integrated_vm.py'))},
        measured_windows_ticks=windows,unique_trace_rows=len(rows),steady_rows=len(steady),
        independent_overlapping_scopes=stats,manual_input='recorded ordinary HID via CUA' if manual else 'none; automated spawn baseline',
        camera_sectors=sector_costs,automatic_hitch_events=hitch_events,
        manual_notes_sha256=sha(note) if manual else None,manual_notes=manual,
        distinct_snapshot_positions=len({tuple(s['player_position_fixed']) for s in snapshots}),
        physical_hardware_FPS_proof=False,full_physical_RAM=False,
        limits='Readable process virtual memory, not atomic GPU/OS/DSP FCRAM. Manual notes and position differences document only the stated route; they do not establish whole-level/camera acceptance. Emulator timings are diagnostic only.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('attempt',type=Path)
    p.add_argument('--classify-unplanned-exit',action='store_true',help='Analyze complete capture files without accepting an unexpectedly closed runtime')
    args=p.parse_args();report=analyze(args.attempt,args.classify_unplanned_exit)
    out=args.attempt/'campaign-analysis.json';assert not out.exists(),'Preserve an existing report'
    out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('status','model','scenario','steady_rows')},indent=2))
    print(out)
