#!/usr/bin/env python3
"""Check the five-span reader and conditional TLS register restoration.

Uses the already built snapshot writer's host format fixture, never a new
Azahar launch. Its patterned TLS payload is a format fixture, not ThreadVars;
the semantic positive case is an explicitly separate synthetic copy.
"""
import argparse
import copy
import json
from pathlib import Path
import struct
import subprocess

from test_native_title_fade_arm import Snapshot,shared,snapshot_reader_selftest
from unicorn.arm_const import UC_ARM_REG_C13_C0_3

ROOT=shared.ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--old-snapshot',type=Path,required=True)
    args=parser.parse_args();args.directory.mkdir(parents=True,exist_ok=True)
    old_hash=shared.base.sha(args.old_snapshot.read_bytes())
    elf_hash=shared.base.sha(args.elf.read_bytes())
    fixture=args.directory/'host-fixture';fixture.mkdir(exist_ok=True)
    writer=ROOT/'build/native-integration-check/snapshot-tests-tls/snapshot-host'
    result=subprocess.run([str(writer),str(fixture)],check=True,capture_output=True,text=True)
    binary=fixture/'fixture.ram.bin';index=fixture/'fixture.ram.json'
    fixture_hash=shared.base.sha(binary.read_bytes());snap=Snapshot(binary,index)
    assert len(snap.spans)==5 and len(snap.views)==230
    cpu=shared.Uc(shared.UC_ARCH_ARM,shared.UC_MODE_ARM)
    snap.install(cpu)
    for span in snap.spans:
        assert bytes(cpu.mem_read(span['address'],span['size']))==snap.read(span['address'],span['size'])
    cpu.reg_write(UC_ARM_REG_C13_C0_3,0x12345000)
    try:snap.restore_tls(cpu)
    except ValueError as error:
        assert 'magic' in str(error)
    else:raise AssertionError('Pattern fixture was mistaken for real libctru TLS')
    assert cpu.reg_read(UC_ARM_REG_C13_C0_3)==0x12345000
    negatives=[]

    def rejected(label,raw,metadata,semantic=False):
        path=args.directory/(label+'.ram.bin');json_path=path.with_suffix('.json')
        path.write_bytes(raw);json_path.write_text(json.dumps(metadata))
        prior=cpu.reg_read(UC_ARM_REG_C13_C0_3)
        try:
            other=Snapshot(path,json_path)
            if semantic:other.restore_tls(cpu)
        except ValueError as error:negatives.append(dict(case=label,reason=str(error)))
        else:raise AssertionError('Invalid fixture accepted: '+label)
        assert cpu.reg_read(UC_ARM_REG_C13_C0_3)==prior

    raw=bytearray(snap.raw)
    six=raw[:];struct.pack_into('<I',six,16,6)
    rejected('six-physical-spans',six,snap.metadata)
    many=raw[:];struct.pack_into('<I',many,20,273)
    rejected('too-many-logical-views',many,snap.metadata)
    bad_index=copy.deepcopy(snap.metadata);bad_index['views'][-1]['size']=511
    rejected('TLS-view-index-mismatch',raw,bad_index)
    tls=snap.metadata['thread_tls_address'];reent=snap.spans[1]['address']+128
    tls_span=next(s for s in snap.spans if Snapshot.contains(s,tls,512))
    tls_offset=tls_span['file_offset']+tls-tls_span['address']
    synthetic=raw[:];struct.pack_into('<I',synthetic,tls_offset,0x21545624)
    struct.pack_into('<I',synthetic,tls_offset+8,reent)
    path=args.directory/'synthetic-valid-TLS.ram.bin';json_path=path.with_suffix('.json')
    path.write_bytes(synthetic);json_path.write_text(json.dumps(snap.metadata))
    positive=Snapshot(path,json_path)
    probe=shared.Probe('synthetic captured ThreadVars contract',elf=args.elf)
    positive.install(probe.cpu);restoration=positive.restore_tls(probe.cpu)
    assert restoration['restored'] and probe.cpu.reg_read(UC_ARM_REG_C13_C0_3)==tls
    # Run the supplied immutable ELF's real MRC/magic/reent read. This explicit
    # synthetic contract does not add TLS to the old real snapshot.
    actual_result=probe.call('__syscall_getreent',[])
    assert actual_result==reent and not probe.faults
    wrong_view=copy.deepcopy(snap.metadata);wrong_view['views'][-1]['name']='other-view'
    rejected('missing-named-TLS-view',synthetic,wrong_view,True)
    wrong_address=copy.deepcopy(snap.metadata);wrong_address['thread_tls_address']=tls+4
    rejected('TLS-metadata-address-mismatch',synthetic,wrong_address,True)
    wrong_flag=copy.deepcopy(snap.metadata);wrong_flag['thread_tls_available']=1
    rejected('non-boolean-TLS-availability',synthetic,wrong_flag,True)
    outside=synthetic[:];struct.pack_into('<I',outside,tls_offset+8,0x28000000)
    rejected('reent-outside-captured-spans',outside,snap.metadata,True)
    null=synthetic[:];struct.pack_into('<I',null,tls_offset+8,0)
    rejected('null-reent',null,snap.metadata,True)
    old=Snapshot(args.old_snapshot)
    assert len(old.spans)==3 and 'thread_tls_available' not in old.metadata
    old_cpu=shared.Uc(shared.UC_ARCH_ARM,shared.UC_MODE_ARM)
    old_cpu.reg_write(UC_ARM_REG_C13_C0_3,0x12345000)
    unavailable=old.restore_tls(old_cpu)
    assert not unavailable['restored'] and old_cpu.reg_read(UC_ARM_REG_C13_C0_3)==0x12345000
    assert old_hash==shared.base.sha(args.old_snapshot.read_bytes())
    assert elf_hash==shared.base.sha(args.elf.read_bytes())
    assert fixture_hash==shared.base.sha(binary.read_bytes())
    report=dict(status='passed',scope='Host format fixture and isolated ARM TLS contract; no game replay or OS emulation',
        format_fixture=dict(binary=str(binary.resolve()),sha256=fixture_hash,spans=5,views=230,
            every_physical_payload_restored=True,patterned_TLS_rejected_as_ThreadVars=True,
            writer_fixture_result=json.loads(result.stdout)),
        synthetic_TLS_contract=dict(synthetic_fixture=True,restoration=restoration,
            real_ELF_getreent_return=hex(actual_result),elf_sha256=elf_hash),
        negatives=negatives,old_actual_capture=dict(sha256=old_hash,spans=3,TLS=unavailable,
            CP15_left_unchanged=True),reader_base_selftest=snapshot_reader_selftest(),
        acquisitions_and_ELF_unchanged=True,no_Azahar_or_UI=True,
        source_sha256={str(p.relative_to(ROOT)):shared.base.sha(p.read_bytes()) for p in
            (Path(__file__),ROOT/'tools/test_native_title_fade_arm.py')})
    report_path=args.directory/'report.json';report_path.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='passed',report=str(report_path),negative_cases=len(negatives))))


if __name__=='__main__':main()
