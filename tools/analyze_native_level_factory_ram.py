#!/usr/bin/env python3
"""Read selected level object tables and preflight their named factory bodies.

The captured ELF names RAM addresses. A second ELF is inspected only for the
same symbolic factory names; no captured bytes are installed or executed there.
Standard, entrance, simple, door and exit tables are covered, respecting the
captured star filter and entrance index/count. Other category loaders and
procedural spawns remain explicitly outside this bounded inventory.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from test_native_title_fade_arm import Snapshot
from test_native_menu_callbacks import symbols
from analyze_native_acquisition import elf_bytes

def sha(raw):return hashlib.sha256(raw).hexdigest()

def missing_body(raw,address,missing):
    for i,word in enumerate(struct.unpack('<3I',elf_bytes(raw,address,12))):
        if word>>24 == 0xea:
            offset = word&0xffffff
            if offset&(1<<23):offset -= 1<<24
            if address+i*4+8+offset*4 == missing:return True
    return False

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt',type=Path,required=True)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args = parser.parse_args()
    result = json.loads((args.attempt/'result.json').read_text())
    elf = args.attempt/'sm64ds-native.elf';raw = elf.read_bytes();assert sha(raw) == result['elf_sha256']
    binary = args.attempt/'ram-stop.ram.bin';assert sha(binary.read_bytes()) == result['ram_snapshot_files'][binary.name]['sha256']
    snap = Snapshot(binary);names = symbols(elf);reverse = {a:n for n,a in names.items()}
    candidate_raw = args.candidate.read_bytes();candidate_names = symbols(args.candidate)
    level = snap.u32(names['data_0209f340']);assert level
    star_filter = snap.read(names['data_0209f220'],1)[0]
    tables = [(-1,snap.u32(level+4))]
    sub = snap.u32(level+16);sub_count = snap.read(level+20,1)[0];assert sub_count <= 127
    tables += [(i,snap.u32(sub+i*12)) for i in range(sub_count)]
    entries,uncovered = [],[]
    for area,table in tables:
        if not table:continue
        count,records = snap.u16(table),snap.u32(table+4);assert count <= 1024
        for i in range(count):
            row = records+i*8;packed = snap.read(row,1)[0];category,filter_id = packed&31,packed>>5
            if filter_id not in (0,star_filter):continue
            count,payload = snap.read(row+1,1)[0],snap.u32(row+4)
            if category not in (0,1,5,9,10):
                uncovered.append({'area':area,'category':category,'entry_count':count});continue
            start = snap.u32(names['data_0209f264']) if category == 1 else 0
            actual_count = snap.read(names['data_0209f21c'],1)[0] if category == 1 else count
            assert start+actual_count <= count
            stride = {0:16,1:16,5:8,9:12,10:14}[category]
            for j in range(start,start+actual_count):
                address = payload+j*stride
                if category in (0,1,5):
                    index = snap.u16(address)
                    if category == 5:
                        index &= 511
                        if index == 511:continue
                    assert index < 1024
                    actor = snap.u16(names['data_ov002_0210cbf4']+index*2)
                elif category == 9:
                    actor = snap.u16(names['data_ov002_0210cb88']+(snap.u16(address+10)&31)*2)
                else:actor = 0x15d
                entries.append((actor,area,category))
    groups = []
    for (actor,area,category),count in Counter(entries).items():
        profile = snap.u32(names['ACTOR_SPAWN_TABLE']+actor*4);assert profile
        factory = snap.u32(profile);symbol = reverse.get(factory);assert symbol
        assert symbol in candidate_names
        groups.append({'actor_id':actor,'area':area,'category':category,'entries':count,'factory':symbol,
            'captured_ELF_missing_body':missing_body(raw,factory,names['sm64ds_native_missing']),
            'candidate_missing_body':missing_body(candidate_raw,candidate_names[symbol],candidate_names['sm64ds_native_missing'])})
    report = {'status':'PASS','scope':__doc__,'captured_ELF_sha256':sha(raw),'candidate_ELF_sha256':sha(candidate_raw),
        'capture_sha256':sha(binary.read_bytes()),'capture_tick':snap.metadata['tick'],'star_filter':star_filter,
        'selected_encoded_actor_entries':len(entries),'distinct_factories':len({g['factory'] for g in groups}),
        'groups':groups,'uncovered_category_tables':uncovered,
        'old_RAM_imported_into_candidate':False,'game_executed':False,'initialization_or_playability_proven':False}
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':'PASS','entries':len(entries),'factories':report['distinct_factories'],
        'captured_missing':sorted({g['factory'] for g in groups if g['captured_ELF_missing_body']}),
        'candidate_missing':sorted({g['factory'] for g in groups if g['candidate_missing_body']}),'report':str(args.report)}))

if __name__ == '__main__':main()
