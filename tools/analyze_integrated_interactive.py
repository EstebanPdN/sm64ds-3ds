#!/usr/bin/env python3
"""Classify closed manual-input captures; never call mixed-menu traces steady gameplay."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import statistics
from elftools.elf.elffile import ELFFile
from inspect_integrated_vm import VM, trace_report, TRACE_RECORD_V2


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def analyze(attempt):
    closed=json.loads((attempt/'result.json').read_text())
    assert closed['status']=='acquisition_closed' and closed['first_emulator_fault'] is None
    def verified(p):
        assert p.is_file() and not p.is_symlink()
        assert sha(p)==closed['files'][p.relative_to(attempt).as_posix()]['sha256']
        return p
    elf=verified(attempt/'sm64ds-integrated.elf');assert sha(elf)==closed['ELF_sha256']
    with elf.open('rb') as f:symbols={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    notes=verified(attempt/('observations.json' if (attempt/'observations.json').exists() else 'manual-input.json'))
    terminal_transition=False
    if closed['stop_reason']=='benchmark_aborted_diagnostic_preserved':
        log=verified(attempt/'user/sdmc/3ds/sm64ds/user-test.log').read_text()
        terminal_transition=any(line.startswith('BENCHMARK event=aborted-diagnostic-capture-not-campaign ') and 'reason=stage-character-changed ' in line for line in log.splitlines())
    snapshots=[]
    for p in sorted((attempt/'user/sdmc/3ds/sm64ds').glob('*.vm')):
        verified(p);vm=VM(p)
        a=json.loads(verified(p.with_suffix('.anchors.json')).read_text())
        assert a['scene']==vm.header['scene'] and a['epoch']==vm.header['epoch'] and a['worker_drained']
        trace_path=verified(p.with_suffix('.trace'));trace=trace_report(trace_path)
        verified(p.with_suffix('.render'))
        item=dict(filename=p.name,VM_sha256=sha(p),header=vm.header,
            payload_bytes=sum(r['payload_bytes'] for r in vm.regions),regions=vm.regions,
            trace_records=trace['record_count'],trace_overwritten=trace['overwritten'],
            trace_scene_scopes=trace['scenes'],scope='mixed original gameplay/menu trace; not a steady gameplay FPS measurement')
        if vm.header['scene']==3:
            player=vm.u32(a['anchors']['data_0209f394'])
            if player:
                item.update(player=hex(player),position_fixed=list(struct.unpack('<3i',vm.read(player+0x5c,12))))
            else:
                assert terminal_transition,'Missing actor outside an explicitly preserved stage-change diagnostic'
                item.update(player_present=False,scope='terminal stage-change diagnostic; no gameplay actor or position, excluded from gameplay sample counts')
            item.update(
                original_level=vm.read(symbols['data_0209f2f8'],1)[0],
                pause=vm.read(symbols['data_0209f2c4'],1)[0],pause_state=vm.read(symbols['data_0209f248'],1)[0],
                actual_display=list(struct.unpack('<5IfB3x',vm.read(symbols['_ZL9g_display'],28))),
                upper_registers=vm.read(0x04000000,0x60).hex())
            assert item['actual_display'][:5]==[400,240,0,400,240] and item['actual_display'][6]==1
            assert abs(item['actual_display'][5]-.8)<.0001
        # Added context signature preserves the existingV2 binary layout. Old
        # reserved0 remains UNKNOWN rather than silently meaning unpaused.
        raw=trace_path.read_bytes();contexts=[];active=[];stable=0
        if raw[:8]==b'SM64PR2\0':
            for i,row in enumerate(trace['rows']):
                context=TRACE_RECORD_V2.unpack_from(raw,32+i*232)[5]
                if context&0xffff0000==0x534d0000:
                    contexts.append(dict(tick=row['tick'],pause=bool(context&1),control_disabled=bool(context&2),cutscene=bool(context&4)))
                    stable=stable+1 if row['scene']==3 and not context&7 and not row['overflow'] else 0
                    # Counter deltas may straddle the first state boundary.
                    if stable>=3:active.append(row)
                else:stable=0
        item['known_context_rows']=len(contexts)
        item['unpaused_control_rows']=sum(not(c['pause'] or c['control_disabled'] or c['cutscene']) for c in contexts)
        item['active_context_independent_scopes']={}
        if active:
            for name in active[0]['intervals_ms']:
                values=sorted(row['intervals_ms'][name] for row in active)
                if values[-1]:item['active_context_independent_scopes'][name]=dict(mean_ms=statistics.mean(values),p95_ms=values[(len(values)-1)*95//100],max_ms=values[-1])
        snapshots.append(item)
    assert snapshots
    game=[s for s in snapshots if s['header']['scene']==3 and 'position_fixed' in s]
    return dict(status='manual_capture_series_classified',ELF_sha256=closed['ELF_sha256'],model=closed['model'],
        observations=json.loads(notes.read_text()),snapshots=snapshots,
        distinct_gameplay_snapshot_positions=len({tuple(s['position_fixed']) for s in game}),
        game_captures=len(game),transition_only_captures=sum(s.get('player_present') is False for s in snapshots),physical_hardware_FPS_proof=False,
        sources_sha256={Path(__file__).name:sha(Path(__file__))},
        limits='Full readable process VM with aliases, not all physical console RAM or an atomic multi-thread snapshot. Paused captures after movement are not active movement snapshots; mixed-menu timing scopes are not gameplay frame-time percentiles.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('attempt',type=Path);args=parser.parse_args()
    report=analyze(args.attempt);output=args.attempt/'interactive-analysis.json';assert not output.exists()
    output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('status','game_captures','distinct_gameplay_snapshot_positions')},indent=2))
