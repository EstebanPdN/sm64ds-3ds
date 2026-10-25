#!/usr/bin/env python3
"""Read-only job handoff summaries; timing is neither GPU submit nor LCD FPS."""
import argparse
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import re

def fields(line):
    result={}
    for key,value in re.findall(r'(\w+)=([-+]?\d+(?:\.\d+)?)',line):
        result[key]=float(value) if '.' in value else int(value)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('attempt',type=Path)
    p.add_argument('--mark',help='Record an observation boundary without touching the game')
    p.add_argument('--report',type=Path);a=p.parse_args();root=a.attempt
    log=root/'user/sdmc/sm64ds/user-test.log';raw=log.read_bytes();lines=raw.decode(errors='replace').splitlines()
    marks_path=root/'scenario-markers.json';marks=json.loads(marks_path.read_text()) if marks_path.exists() else []
    if a.mark:
        rows=[fields(line) for line in lines if line.startswith('Game vblank=')]
        marks.append(dict(label=a.mark,utc=datetime.now(timezone.utc).isoformat(),log_bytes=len(raw),
            after_line=len(lines),last_frame=rows[-1] if rows else None))
        marks_path.write_text(json.dumps(marks,indent=2)+'\n');print(json.dumps(marks[-1]));return
    windows=[];last_state=None;last_game=None;transitions=[];inputs=[];previous_window_line=0
    for index,line in enumerate(lines,1):
        if line.startswith('Game vblank='):last_game=fields(line)
        if line.startswith('Game scene transition'):transitions.append(dict(line=index,text=line))
        if line.startswith('Game HID'):
            match=re.search(r'update=(\d+) held=([0-9a-fA-F]+)',line)
            inputs.append(dict(line=index,text=line,update=int(match[1]),held=int(match[2],16)) if match else dict(line=index,text=line))
        if line.startswith(('Render60 submitted=','Render60 jobsAccepted=')):
            relevant=[m for m in marks if m['after_line']<index]
            mark=relevant[-1] if relevant else None
            w=dict(line=index,scenario=mark['label'] if mark else 'unmarked',
                game_context=last_game,window_start_line=previous_window_line,
                straddles_scenario_entry=bool(mark and previous_window_line<=mark['after_line']),**fields(line))
            previous_window_line=index
            interval=w.get('handoffMeanMs',w.get('submissionMeanMs',0))
            w['handoffMeanMs']=interval
            w['job_handoff_rate_per_second']=1000/interval if interval>0 else None
            w['timing_site']='game_thread_consumes_worker_result_after_wait_idle'
            w['is_submission_measurement']=False;w['physical_presentation_measurement']=False
            windows.append(w)
        if line.startswith(('Render60 sourceMidpoints=','Render60 midpointSelections=')):
            last_state=fields(line)
            if windows:windows[-1]['interpolation']=last_state
    groups={}
    for w in windows:groups.setdefault(w['scenario'],[]).append(w)
    summaries={}
    for label,rows in groups.items():
        admitted=[r for r in rows if not r['straddles_scenario_entry']]
        if not admitted:
            summaries[label]=dict(windows=len(rows),fully_contained_windows=0,note='No complete window after the observation boundary.');continue
        count=sum(r['samples'] for r in admitted)
        total=sum(r['handoffMeanMs']*r['samples'] for r in admitted)
        summaries[label]=dict(windows=len(rows),fully_contained_windows=len(admitted),sampled_intervals=count,
            mean_job_handoff_interval_ms=total/count,job_handoff_rate_per_second=1000*count/total,
            worst_window_p99_ms=max(r['p99'] for r in admitted),max_interval_ms=max(r['max'] for r in admitted),
            note='Observation boundaries can include idle time; HID spans below isolate recorded held input. Handoff is not GPU submit or physical presentation.')
    spans=[]
    for first,after in zip(inputs,inputs[1:]):
        if not first.get('held') or 'update' not in after:continue
        complete=[w for w in windows if w['window_start_line']>first['line'] and w['line']<after['line']]
        count=sum(w['samples'] for w in complete);duration=sum(w['handoffMeanMs']*w['samples'] for w in complete)
        spans.append(dict(held_hex=hex(first['held']),start_update=first['update'],end_update=after['update'],
            original_updates=after['update']-first['update'],start_line=first['line'],end_line=after['line'],
            fully_contained_windows=len(complete),job_handoff_rate_per_second=1000*count/duration if duration else None))
    result=dict(status='read_only_log_analysis',attempt=str(root.resolve()),log_sha256=hashlib.sha256(raw).hexdigest(),
        complete_acquisition=(root/'result.json').exists(),scope='Emulated job result handoff intervals and original update counters',
        windows=windows,scenarios=summaries,last_interpolation_counters=last_state,
        scene_transitions=transitions,HID_events=inputs,HID_spans=spans,FATAL_lines=[line for line in lines if line.startswith('FATAL')],
        original_logic_cadence_samples=[fields(line) for line in lines if line.startswith('Game vblank=')],
        physical_hardware=False,all_game_60FPS_acceptance=False)
    target=a.report or root/'render60-analysis.json';target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(report=str(target),scenarios=summaries,last_interpolation_counters=last_state),indent=2))

if __name__=='__main__':main()
