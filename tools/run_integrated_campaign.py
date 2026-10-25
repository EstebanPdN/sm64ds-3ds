#!/usr/bin/env python3
"""Sequential diagnostic baselines; stop on faults, preserve every attempt.

This is an explicit test supervisor, not unattended gameplay/input automation.
Interactive dialogues are captured once and reported as needing real input.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from analyze_integrated_campaign import analyze

ROOT=Path(__file__).resolve().parents[1]
SCENARIOS=['castle-yoshi','bob-mario','whomp-luigi','bay-wario','clock-wario']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--cia',type=Path,required=True);p.add_argument('--cxi',type=Path,required=True)
    p.add_argument('--app',type=Path,required=True);p.add_argument('--asset-package',type=Path,required=True)
    p.add_argument('--scenarios',nargs='+',choices=SCENARIOS,required=True)
    args=p.parse_args();build=json.loads((args.candidate/'BUILD.json').read_text())
    out=ROOT/'build/integrated-native'/('campaign-'+str(time.time_ns()));out.mkdir()
    manifest={'ELF_sha256':build['ELF_sha256'],'runs':[],
              'scope':'Original-API spawn baselines. No movement/camera or physical hardware acceptance.'}
    def save():
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    save();print(json.dumps({'campaign':str(out)}),flush=True)
    for scenario in args.scenarios:
        for model in ['old','new']:
            existing=[]
            for path in (ROOT/'build/integrated-native/acquisitions').glob('*/result.json'):
                r=json.loads(path.read_text())
                if r['ELF_sha256']!=build['ELF_sha256'] or r['model']!=model:continue
                plan=json.loads((path.parent/'plan.json').read_text())
                if plan.get('benchmark',{}).get('profile')==scenario:existing.append(path.parent)
            assert len(existing)<=1,'Multiple attempts require manual classification'
            if existing:
                attempt=existing[0]
            else:
                command=[sys.executable,str(ROOT/'tools/run_integrated_native.py'),
                    '--candidate',str(args.candidate),'--cia',str(args.cia),'--cxi',str(args.cxi),
                    '--app',str(args.app),'--asset-package',str(args.asset_package),
                    '--model',model,'--benchmark-profile',scenario,'--seconds','600','--silent']
                result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
                (out/(scenario+'-'+model+'.log')).write_text(result.stdout+result.stderr)
                result.check_returncode()
                first=json.loads(result.stdout.splitlines()[0]);attempt=Path(first['instance'])
            r=json.loads((attempt/'result.json').read_text())
            entry={'scenario':scenario,'model':model,'attempt':str(attempt),'stop_reason':r['stop_reason']}
            if r['stop_reason']=='benchmark_three_complete_captures':
                analysis=analyze(attempt);report=attempt/'campaign-analysis.json'
                if report.exists():assert json.loads(report.read_text())==analysis
                else:report.write_text(json.dumps(analysis,indent=2)+'\n')
                entry.update(status='baseline_validated',captures=3,analysis=str(report))
            elif r['stop_reason']=='benchmark_aborted_diagnostic_preserved':
                log=(attempt/'user/sdmc/3ds/sm64ds/user-test.log').read_text()
                if 'reason=interactive-dialogue-needs-input' in log:
                    entry.update(status='needs_real_dialogue_input',captures=0)
                else:entry.update(status='diagnostic_failure',captures=0)
            else:entry.update(status='failed_or_incomplete',captures=0)
            manifest['runs'].append(entry);save();print(json.dumps(entry),flush=True)
            if entry['status']!='baseline_validated':
                print('Stopped: inspect the preserved diagnostic before another scenario.',flush=True)
                return
    print('Requested baseline subset complete; full campaign and hardware acceptance are separate.',flush=True)


if __name__=='__main__':main()
