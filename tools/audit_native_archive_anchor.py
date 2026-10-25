#!/usr/bin/env python3
"""Read-only SDK archive-anchor contrast; no CPU, IO success seam or repair."""
import gc
import json
import struct
from pathlib import Path

import audit_native_family_virtuals as proof
from test_native_title_fade_arm import Snapshot

ROOT=proof.ROOT
ATTEMPT=ROOT/'build/native-runtime-azahar/attempt-1791624434443135000'
SHA='cd3799f6752140172893ec61b384777078c614b21d9e726b20cff8538e4f3f29'


def main():
    elf=proof.Elf(ATTEMPT/'sm64ds-native.elf')
    assert proof.sha(elf.raw)==SHA
    original=proof.Original(ROOT/'build/europe.pak')
    names=elf.names
    def address(n):return names[n]['address']
    functions=[]
    for name in ['func_02018aa4','func_0205d89c','func_0205d96c','func_0205cd34',
                 'func_0205cc80','func_0205cbe4','func_0205d23c','func_0205d304',
                 'func_0204ee10','func_0204ee40']:
        ds,sha,size=proof.disassemble_original(original,name)
        row=dict(name=name,original_ROM=dict(address=hex(original.names[name]['address']),
            code_sha256=sha,bytes=size,instructions=[l.strip() for l in ds.splitlines()
                if __import__('re').match(r'\s*[0-9a-f]+:\s+[0-9a-f]{8}\s',l)]),
            native_symbol=names.get(name))
        if name in names:
            row['native_instructions']=proof.disassemble_native(elf.path,name)
            row['native_code_sha256']=proof.sha(elf.read(address(name),names[name]['bytes']))
        functions.append(row)
    snapshots=[]
    for prefix in ['ram-title-180','ram-title-ready','ram-stage-requested']:
        snap=Snapshot(ATTEMPT/(prefix+'.ram.bin'),ATTEMPT/(prefix+'.ram.json'))
        head=snap.u32(address('data_020a8048'))
        key,next_,prev=struct.unpack('<3I',snap.read(head,12))
        snapshots.append(dict(index=proof.record(snap.index_path),
            binary=dict(path=str(snap.path.relative_to(ROOT)),sha256=proof.sha(snap.raw),bytes=len(snap.raw)),
            tick=snap.metadata['tick'],head_slot=hex(address('data_020a8048')),head=hex(head),
            cursor=snap.u32(address('data_020a804c')),head_key=hex(key),head_next=hex(next_),head_prev=hex(prev),
            head_key_has_nonzero_top8bits=bool(key>>24),
            head_next_inside_physical_capture=any(Snapshot.contains(s,next_,12) for s in snap.spans),
            original_rom_node=hex(address('data_020a8074')),
            original_rom_node_first0x50bytes_all_zero=not any(snap.read(address('data_020a8074'),0x50)),
            all13_archive_image_pointers_zero=all(snap.u32(address('data_0208ecf4')+i*20)==0 for i in range(13)),
            head_not_rom_static_node=head!=address('data_020a8074'),
            pointer_residency_not_allocation_liveness=True))
        del snap;gc.collect()
    runtime=(ROOT/'source/game_port/native_runtime.cpp').read_text()
    native_begin=runtime.index('extern "C" void func_02018aa4()')
    native_end=runtime.index('\n}',native_begin)+2
    provider=runtime[native_begin:native_end]
    assert 'data_0209d3b4=data_020a8074' in provider
    assert all(n not in provider for n in ['func_0205cd34','func_0205cc80','func_0205d89c','func_0205d96c'])
    rom_name=original.read(original.names['data_0209a048']['module'],original.names['data_0209a048']['address'],4)
    assert rom_name[:3]==b'rom'
    out=dict(status='completed_read_only_original_vs_native_anchor_contract',
        scope='Static original/immutable ELF and three physical RAM reads only. No ARM CPU replay, live PC, UI, Azahar, hardware, IO seam or production change.',
        tool=proof.record(Path(__file__).resolve()),elf=proof.record(elf.path),
        original_archive_name_symbol=dict(name='data_0209a048',address=hex(original.names['data_0209a048']['address']),
            represents='rom',bytes_sha256=proof.sha(rom_name)),
        sources=[proof.record(ROOT/p) for p in ['source/game_port/native_runtime.cpp','source/game_port/native_card.cpp',
            'upstream/src/func_02018aa4.c','upstream/src/func_0205d89c.c','upstream/src/func_0205d96c.c',
            'upstream/src/func_0205cd34.c','upstream/src/func_0205cc80.c','upstream/src/func_0205cbe4.c',
            'upstream/src/func_0205d23c.c','upstream/src/func_0205d304.c','upstream/src/func_0204ee40.c',
            'upstream/src/func_0204ee10.c','upstream/src/func_02018908.c']],
        functions=functions,snapshots=snapshots,
        original_registration_order='func18aa4→5d89c→5d96c: clears staticROMnode8074, registers("rom",3) via5cc80, THEN validates original FAT/FNT shared words and may configure archive callbacks via5c7c4/5cb68. Registration occurs before those FAT validity early returns.',
        native_registration_difference='native_runtime::func18aa4 publishes handles and data0209d3b4=data020a8074 but does not clear/register the permanent ROM node. All three captures preserve the ROM node zeros; head points elsewhere.',
        unregister_contract='Both original ROM5cbe4 and ELF5cbe4 update next.prev and prev.next when nonnull, then clear node.key/next/prev and flagsbit1. They do NOT update data020a8048 when removing a head withprev0. Cursor804c may be reset by reading the unchanged head8048. This is original behavior, not a decompilation omission.',
        permanent_anchor_implication='Original boot installs staticROM first. Later heapNARC records have an enduring predecessor and normal removal changes that predecessor.next; original normal archive unload never frees staticROM. Native missing initial node permits first heapNARC to become head and then be retired/freed while8048 still retains its address.',
        captured_inconsistency='Headkey has nonzero top8bits in every capture, unlike any name key produced by5d304(length<=3), and head.next is outside all captured physical spans. The bytes do not represent a valid retained archive list node. Freed allocation history is an inference; no live trace of that earlier retirement is available.',
        conditional_reuse_cycle='If native18934 allocates a new record at the retained head address, real4ee40→5cd34 zeros record.next/key. Lookup returnsnull; real5cc80 sees nonnullhead==thiz, appends thiz to itself and sets thiz.prev=thiz. Any different later key makes5d23c repeat through next==self. This is a static conditional derivation; separate exact replay supplies execution evidence.',
        not_claimed=['No live hang PC was acquired; this report does not independently establish that the integrated process was in5d23c.',
            'No ROM initialization with ARM7/DS FAT callbacks is executed or proposed as a native service.',
            'No unregister body, head pointer, IO result, node bytes or capture was modified.'])
    dest=ROOT/'build/native-stage-plan/fs-archive-anchor-static-contract.json'
    dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(out,indent=2)+'\n')
    print(json.dumps(dict(status=out['status'],report=str(dest),captured_ticks=[s['tick'] for s in snapshots],
        invalid_head_next_captures=sum(not s['head_next_inside_physical_capture'] for s in snapshots),CPU_replays=0)))


if __name__=='__main__':main()
