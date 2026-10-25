#!/usr/bin/env python3
"""Validate a closed native-world laboratory capture using its exact ELF.

Optional retirement is restricted to newly analyzed VM files inside that owned
lab attempt. User-supplied dumps and integrated campaign receipts are untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from inspect_integrated_vm import VM,inspect
from inspect_integrated_allocator import observe
from elftools.elf.elffile import ELFFile

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def audit(attempt,retire=False):
    attempt=attempt.resolve()
    assert attempt.parent==ROOT/'build/movement-lab' and attempt.name.startswith('attempt-')
    result=json.loads((attempt/'result.json').read_text())
    assert result['fault'] is None and result['dumps'] and result['reason']=='requested-stop'
    pid=json.loads((attempt/'process.json').read_text())['pid']
    process=subprocess.run(['ps','-p',str(pid),'-o','command='],capture_output=True,text=True)
    assert str(attempt) not in process.stdout,'Owned emulator is still running'
    artifact=json.loads((attempt/'artifact.json').read_text());build=Path(artifact['build_directory']).resolve()
    assert build.parent==ROOT/'build/movement-lab'
    assert sha(build/'build.json')==artifact['source_receipt_sha256']
    receipt=json.loads((build/'build.json').read_text());elf=build/'sm64ds-movement-lab.elf'
    assert sha(elf)==receipt['sha256'][elf.name]
    assert sha(build/'sm64ds-movement-lab.3dsx')==artifact['image_sha256']
    for name,digest in receipt['source_sha256'].items():
        assert sha(build/'source-evidence'/name)==digest
    with elf.open('rb') as stream:
        image=ELFFile(stream)
        symbols={x.name:x['st_value'] for x in image.get_section_by_name('.symtab').iter_symbols()}
        sizes={x.name:x['st_size'] for x in image.get_section_by_name('.symtab').iter_symbols()}
        section=image.get_section_by_name('.text');code=section.data();base=section['sh_addr']
    def address(name):return symbols['_ZN12movement_lab12_GLOBAL__N_1L'+str(len(name))+name+'E']
    assert sizes['_ZN12movement_lab12_GLOBAL__N_1L5stateE']==60
    snapshots=[];paths=[]
    for path in sorted((attempt/'user/sdmc/3ds/sm64ds').glob('*.vm')):
        assert path.resolve().parent==attempt/'user/sdmc/3ds/sm64ds' and not path.is_symlink()
        vm=VM(path);meta=json.loads(path.with_suffix('.anchors.json').read_text())
        assert meta['anchors']['sm64ds_render60_source_epoch']==symbols['sm64ds_render60_source_epoch']
        assert vm.read(base,len(code))==code,'Captured code differs from laboratory ELF'
        for name,value in meta['anchors'].items():
            if name in symbols:assert value==symbols[name],name
        report=inspect(path,elf);report['ELF_text_bytes_verified']=len(code)
        report['allocator']=observe(vm,symbols)
        mode=vm.read(address('mode'),1)[0];owned=vm.read(address('owned'),1)[0]
        actor=vm.u32(meta['anchors']['data_0209f394']);session=address('world_session')
        solver=struct.unpack('<i',vm.read(address('mario'),4))[0]
        instance,source=struct.unpack('<iI',vm.read(session,8))
        position=struct.unpack('<3f',vm.read(address('state'),12))
        ds_position=struct.unpack('<3i',vm.read(actor+0x5c,12))
        assert owned==1 and solver>=0 and instance==solver and source==actor
        assert vm.u32(symbols['_ZN17movement_geometry20active_world_sessionE'])==session
        assert max(abs(a-b/4096) for a,b in zip(position,ds_position))<0.001
        report['movement_owner']=dict(mode=['Original','Hybrid','N64'][mode],
            character=vm.u32(address('character')),owned=True,solver_id=solver,
            actor=hex(actor),session=hex(session),DS_position_fixed=ds_position,
            solver_position=position,position_matches_DS=True)
        output=attempt/(path.stem+'.analysis.json')
        output.write_text(json.dumps(report,indent=2)+'\n')
        snapshots.append(dict(snapshot=path.name,bytes=path.stat().st_size,
            VM_sha256=sha(path),analysis=output.name,analysis_sha256=sha(output),
            movement_owner=report['movement_owner'],allocator=report['allocator']))
        paths.append(path)
    assert snapshots,'No raw captures available; inspect existing audit/retirement receipts'
    report=dict(status='complete_lab_captures_validated',attempt_result=result,
        image_sha256=artifact['image_sha256'],ELF_sha256=sha(elf),
        ELF_text_bytes_verified=len(code),inspector_sha256=sha(Path(__file__)),snapshots=snapshots,
        limits='Old/New-model Azahar functional evidence, not physical FPS or leak proof. '
        'Mapped process RAM includes non-atomic devices. Profile scopes overlap and cannot be summed.')
    output=attempt/'ram-audit.json';output.write_text(json.dumps(report,indent=2)+'\n')
    if retire:
        retirement=attempt/'ram-retirement.json';assert not retirement.exists()
        # Persist the analyzed identities before deletion; recheck every exact
        # file first. A failed check cannot cause a partial retirement.
        for path,item in zip(paths,snapshots):
            assert path.stat().st_size==item['bytes'] and sha(path)==item['VM_sha256']
            assert sha(attempt/item['analysis'])==item['analysis_sha256']
        receipt=dict(status='prepared',audit_sha256=sha(output),files=snapshots,
            scope='Only these analyzed lab .vm files; reports/trace/anchors and user originals retained')
        retirement.write_text(json.dumps(receipt,indent=2)+'\n')
        for path in paths:path.unlink()
        receipt['status']='retired_analyzed_lab_VM';retirement.write_text(json.dumps(receipt,indent=2)+'\n')
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('attempt',type=Path);parser.add_argument('--retire-memory',action='store_true')
    options=parser.parse_args();report=audit(options.attempt,options.retire_memory)
    print(json.dumps({k:v for k,v in report.items() if k!='snapshots'},indent=2))
    print('Validated captures:',len(report['snapshots']))
