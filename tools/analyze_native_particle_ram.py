#!/usr/bin/env python3
"""Read captured particle resource/manager fields with their exact ELF aliases.

Checks full serialized bytes and every typed definition, behavior and texture
source pointer in complete acquired RAM. It neither restores RAM into a CPU nor
normalizes address-looking resource words. GPU cache coherence, simulation,
visible stage and console performance are outside this read-only check.
"""
import argparse
import json
from pathlib import Path

from analyze_native_acquisition import snapshot_paths
from test_native_title_fade_arm import Snapshot
import test_native_embedded_spa_arm as spa

base, ROOT = spa.base, spa.ROOT


def inspect(snap, names, entries, textures):
    resource = names['data_02075f14']
    raw = snap.read(resource, spa.SIZE)
    assert base.sha(raw) == spa.DIGEST, 'Serialized SPA differs from original ROM'
    begin, cursor, end = [snap.u32(names[n]) for n in (
        'data_0209ee80', 'data_0209ee78', 'data_0209ee7c')]
    item = dict(binary=snap.path.name, tick=snap.metadata['tick'],
        phase=snap.metadata['phase'], bytes=len(snap.raw),
        sha256=base.sha(snap.raw), SPA_bytes=spa.SIZE,
        SPA_sha256=base.sha(raw), entire_resource_identical_to_ROM=True,
        initialized_particle_arena=bool(begin))
    if not begin:
        item['manager_check']='No allocated particle arena at this checkpoint'
        return item
    assert begin <= cursor <= end
    assert snap.u32(begin) == names['func_02023178']
    definitions, records = snap.u32(begin+0x1c), snap.u32(begin+0x20)
    assert (snap.u16(begin+0x24), snap.u16(begin+0x26)) == (321, 46)
    def within(address, size):
        assert begin <= address and 0 <= size <= cursor-address, (hex(address), size)
        snap.read(address, size)
    within(begin, 0x3c); within(definitions, len(entries)*32)
    within(records, len(textures)*20)
    pointers, behaviors = 1, 0
    behavior_blocks=[]
    for entry in entries:
        at = definitions+entry['index']*32
        for slot, offset in enumerate([entry['offset'], *entry['parts']]):
            assert snap.u32(at+slot*4) == (0 if offset is None else resource+offset), (entry['index'], slot)
            pointers += 1
        count, block = snap.u16(at+28), snap.u32(at+24)
        assert count == len(entry['behaviors']) and bool(block) == bool(count)
        if count:
            within(block, count*8);behavior_blocks.append((block, count*8))
        for i, (name, offset) in enumerate(entry['behaviors']):
            assert snap.u32(block+i*8) == names[spa.behavior_name(name)]
            assert snap.u32(block+i*8+4) == resource+offset
            behaviors += 1;pointers += 2
    assert behaviors == 280
    for i, left in enumerate(behavior_blocks):
        for right in behavior_blocks[i+1:]:
            assert left[0]+left[1] <= right[0] or right[0]+right[1] <= left[0]
    for record in textures:
        assert snap.u32(records+record['index']*20) == resource+record['offset']
        pointers += 1
    item['manager']=dict(address=hex(begin), cursor=hex(cursor), end=hex(end),
        used_bytes=cursor-begin, remaining_bytes=end-cursor,
        definitions=len(entries), behavior_records=behaviors,
        textures=len(textures), typed_pointers_checked=pointers,
        behavior_allocations_disjoint=True,
        all_typed_entries_in_allocated_arena=True)
    return item


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt', type=Path, required=True)
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--report', type=Path, required=True)
    args=parser.parse_args();attempt=args.attempt.resolve()
    result=json.loads((attempt/'result.json').read_text())
    elf=attempt/'sm64ds-native.elf'
    assert base.sha(elf.read_bytes()) == result['elf_sha256']
    names=base.symbols(elf)
    for filename, expected in result['ram_snapshot_files'].items():
        raw=(attempt/filename).read_bytes()
        assert len(raw)==expected['bytes'] and base.sha(raw)==expected['sha256']
    cart=base.Cartridge(args.pack)
    start, _, image=cart.image(0xffffffff)
    raw=image[spa.SPA-start:spa.SPA-start+spa.SIZE]
    entries,textures=spa.parse(raw)
    complete, interrupted=snapshot_paths(attempt)
    rows=[inspect(Snapshot(binary), names, entries, textures) for binary in complete]
    assert rows
    report=dict(status='passed', attempt=str(attempt),
        ELF_sha256=result['elf_sha256'], snapshots=rows,
        all_archived_RAM_hashes_checked=True, interrupted_files=interrupted,
        analyzer_sha256=base.sha(Path(__file__).read_bytes()),
        schema_parser_sha256=base.sha(Path(spa.__file__).read_bytes()),
        scope=__doc__, limits=['Sequential CPU-visible capture; snapshot I/O bookkeeping may change.',
            'No GPU coherence, effect simulation, visible scene, player control or hardware/FPS claim.'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(status='passed', snapshots=len(rows),
        managers=sum(r['initialized_particle_arena'] for r in rows), report=str(args.report))))


if __name__=='__main__': main()
