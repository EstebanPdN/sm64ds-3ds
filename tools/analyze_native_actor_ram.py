#!/usr/bin/env python3
"""Observe the original intrusive actor list in one exact-ELF RAM acquisition.

Checks residency, prev/next/owner relationships, cycles and the list tail, then
records actor IDs, flags, positions and known mushroom members. Linked residency
does not establish completed initialization, control, rendered frames or FPS.
No execution or RAM restoration occurs; private payload bytes are not exported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from test_native_title_fade_arm import Snapshot
from test_native_menu_callbacks import symbols
from analyze_native_acquisition import elf_bytes, snapshot_paths

def sha(raw): return hashlib.sha256(raw).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args = parser.parse_args()
    result = json.loads((args.attempt/'result.json').read_text())
    elf = args.attempt/'sm64ds-native.elf'; raw = elf.read_bytes()
    assert sha(raw) == result['elf_sha256']
    names = symbols(elf)
    tables = {}
    for name,address in names.items():
        if not name.startswith('_ZTV'): continue
        try: prefix = struct.unpack('<2I',elf_bytes(raw,address,8))
        except ValueError: continue
        if prefix == (0,names.get('_ZTI'+name[4:])): tables[address+8] = name
        else: tables[address] = name
    if 'data_ov002_0210a83c' in names: tables[names['data_ov002_0210a83c']] = 'original Player table'
    observations = []
    complete, interrupted = snapshot_paths(args.attempt)
    for binary in sorted(args.attempt.glob('*.ram.bin')):
        expected = result['ram_snapshot_files'][binary.name]
        assert binary.stat().st_size == expected['bytes'] and sha(binary.read_bytes()) == expected['sha256']
        index = binary.with_suffix('.json')
        expected_index = result['ram_snapshot_files'][index.name]
        assert index.stat().st_size == expected_index['bytes'] and sha(index.read_bytes()) == expected_index['sha256']
        if binary not in complete:continue
        snap = Snapshot(binary)
        root = next(v for v in snap.views if v['name'] == 'original-root-heap')
        def resident(address,size):
            assert address and Snapshot.contains(root,address,size), ('outside original heap',hex(address),size)
        head,tail = [snap.u32(names['data_0209b468']+i) for i in (0,4)]
        node,prior,seen,actors = head,0,set(),[]
        while node:
            assert len(seen) < 1024 and node not in seen, ('actor list cycle/limit',hex(node))
            seen.add(node); resident(node,12)
            previous,next_node,owner = [snap.u32(node+i) for i in (0,4,8)]
            assert previous == prior and owner+0x50 == node, ('actor list linkage/owner',hex(node))
            resident(owner,0xd4)
            vptr = snap.u32(owner)
            row = {'address':hex(owner),'node':hex(node),'vptr':hex(vptr),
                'vtable':tables.get(vptr),'unique_id':snap.u32(owner+4),
                'param1':hex(snap.u32(owner+8)),'actor_id':snap.u16(owner+12),
                'alive_state':snap.read(owner+14,1)[0],'pause_flags':snap.read(owner+19,1)[0],
                'position_fix12':[snap.s32(owner+i) for i in (0x5c,0x60,0x64)],
                'actor_flags':hex(snap.u32(owner+0xb0))}
            if tables.get(vptr) == '_ZTV7da1up_c':
                resident(owner,0x398)
                row['mushroom'] = {'object_bytes':920,'object_sha256':sha(snap.read(owner,920)),
                    'type':snap.s32(owner+0x384),'model_file':hex(snap.u32(owner+0x304)),
                    'model_components':[hex(snap.u32(owner+0x308+i)) for i in range(0,20,4)],
                    'shadow_file':hex(snap.u32(owner+0x354)),
                    'shadow_components':hex(snap.u32(owner+0x358))}
            actors.append(row); prior,node = node,next_node
        assert prior == tail and bool(head) == bool(tail), 'actor tail mismatch'
        observations.append({'capture':binary.name,'tick':snap.metadata['tick'],
            'reason':snap.metadata.get('reason'),'list_head':hex(head),'list_tail':hex(tail),
            'linked_actor_count':len(actors),'acyclic_bidirectional_owner_links':True,'actors':actors})
    report = {'status':'PASS','scope':__doc__,'ELF_sha256':result['elf_sha256'],
        'observations':observations,'interrupted_snapshot_artifacts':interrupted,
        'all_snapshots_complete':not interrupted,'acquisition_unchanged':True,'game_executed':False,
        'initialization_or_liveness_of_every_object_proven':False,'physical_hardware':False}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':'PASS','captures':[{'tick':o['tick'],'actors':o['linked_actor_count'],
        'mushrooms':sum('mushroom' in a for a in o['actors'])} for o in observations],'report':str(args.report)}))

if __name__ == '__main__': main()
