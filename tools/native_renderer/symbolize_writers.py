#!/usr/bin/env python3
"""Map bounded native writer provenance to the exact local linked game's code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
ORIGIN=re.compile(r'^Fresh native writer: frame=(\d+) kind=(\d+) caller=([0-9a-f]+) target=([0-9a-f]+) width=(\d+) calls=(\d+) units=(\d+)$',re.M)


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,required=True)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--addr2line',type=Path)
    args=parser.parse_args();output=args.session/'native-writer-provenance.json'
    if not output.resolve().is_relative_to((ROOT/'build').resolve()):parser.error('Local provenance stays under ignored build/')
    tool=args.addr2line
    if tool is None:
        found=shutil.which('arm-none-eabi-addr2line')
        if found:tool=Path(found)
        elif os.environ.get('DEVKITARM'):tool=Path(os.environ['DEVKITARM'])/'bin/arm-none-eabi-addr2line'
    if tool is None or not tool.is_file():parser.error('Supply the installed SDK addr2line path or DEVKITARM')
    elf=args.build/'sm64ds.elf';image=args.build/'sm64ds.3dsx';manifest=json.loads((args.build/'build.json').read_text())['sha256']
    for file in [elf,image]:
        records=[digest for name,digest in manifest.items() if Path(name).name==file.name]
        if records!=[sha(file)]:parser.error('Build artifact does not match its original link receipt: '+file.name)
    run=json.loads((args.session/'result/run.json').read_text())
    if run['sha256']!=sha(image):parser.error('Session used a different binary from this build')
    log=(args.session/'result/log.txt').read_text();rows=[]
    for match in ORIGIN.finditer(log):
        frame,kind,caller,target,width,calls,units=match.groups();address=int(caller,16)
        # LR points after the call. Do not unwind ARM frames or use a foreign
        # symbol file. Thumb's final call halfword is two bytes before its LR.
        lookup=(address&~1)-(2 if address&1 else 4)
        rows.append(dict(frame=int(frame),kind=int(kind),caller=caller,target=target,width=int(width),calls=int(calls),units=int(units),lookup_address=lookup))
    addresses=sorted({r['lookup_address'] for r in rows})
    symbols={}
    if addresses:
        result=subprocess.run([str(tool),'-a','-f','-C','-i','-e',str(elf),*[hex(a) for a in addresses]],text=True,stdout=subprocess.PIPE,check=True)
        current=None;lines=[]
        def save():
            if current is not None:
                if len(lines)%2:raise ValueError('Unexpected local symbolizer output')
                symbols[current]=[dict(function=lines[i],location=lines[i+1]) for i in range(0,len(lines),2)]
        for line in result.stdout.splitlines():
            if re.fullmatch(r'0x[0-9a-fA-F]+',line):save();current=int(line,16);lines=[]
            else:lines.append(line)
        save()
    for row in rows:row['source']=symbols.get(row['lookup_address'],[])
    summaries=[]
    guards={int(f):int(n) for f,n in re.findall(r'^Fresh native frame=(\d+).*legacyWords=(\d+)',log,re.M)}
    for frame,origins,dropped in re.findall(r'^Fresh native writer summary: frame=(\d+) origins=(\d+) dropped=(\d+) diagnosticOnly=1$',log,re.M):
        selected=[r for r in rows if r['frame']==int(frame)];units=sum(r['units'] for r in selected if r['kind'] in (1,2,3))
        summaries.append(dict(frame=int(frame),origins=int(origins),dropped=int(dropped),guard_words=guards.get(int(frame)),traced_guard_units=units,
            complete=len(selected)==int(origins) and int(dropped)==0 and units==guards.get(int(frame))))
    known=all(r['source'] and r['source'][0]['function']!='??' for r in rows)
    report=dict(scope='Local code-origin provenance only; no command words or graphics interpretation',elf_sha256=sha(elf),image_sha256=sha(image),
        physical_hardware=False,performance_admissible=False,rows=rows,frames=summaries,all_origins_symbolized=known,
        application_result=run['result'],owned_process_group_stopped=run['owned_process_group_stopped'],
        passed=bool(rows and summaries) and known and all(s['complete'] for s in summaries) and run['result']=='PASS' and run['owned_process_group_stopped'] and run['unmapped_access_count']==0)
    output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=report['passed'],origins=len(rows),frames=summaries),indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
