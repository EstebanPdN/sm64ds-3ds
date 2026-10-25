#!/usr/bin/env python3
"""Audit one preserved acquisition without running the game or changing RAM.

All physical bytes and logical views are checked against the capture index and
the exact ELF section table. Known title/input/video fields are then observed;
this does not imply every resident object is live or every game defect is known.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from decode_native_title_ram import decode
from test_native_title_fade_arm import Snapshot
from test_native_audio_deferred_arm import base


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def elf_bytes(raw, address, size):
    if raw[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected ARM32 little-endian ELF')
    offset = struct.unpack_from('<I', raw, 28)[0]
    stride, count = struct.unpack_from('<HH', raw, 42)
    if stride != 32:
        raise ValueError('Unsupported program header')
    for i in range(count):
        kind, at, start, _, filesz, _, _, _ = struct.unpack_from('<8I', raw, offset+i*stride)
        if kind == 1 and start <= address and size <= filesz-(address-start):
            return raw[at+address-start:at+address-start+size]
    raise ValueError('ELF table is outside file-backed segments')


def records(path, prefix):
    result = []
    for line in path.read_text().splitlines():
        if line.startswith(prefix):
            row = dict(re.findall(r'(\w+)=(-?\w+)', line))
            result.append({k: int(v, 16 if k in ('ctx', 'valid') else 10) for k, v in row.items()})
    return result


def snapshot_paths(attempt):
    """Separate an interrupted descriptor write from complete RAM payloads.

    Only the observed combination of a valid format header, short binary and
    empty index is classified as interrupted. Other reader errors still fail.
    The caller verifies every archived file's hash, including partial files.
    """
    complete, interrupted = [], []
    for binary in sorted(attempt.glob('*.ram.bin')):
        index = binary.with_suffix('.json')
        with binary.open('rb') as stream:
            header = stream.read(32)
        if len(header) == 32 and header[:8] == b'SM64RAM1':
            version, payload, spans, views, total, reserved = struct.unpack_from('<6I',header,8)
            if (version == 1 and not reserved and 1 <= spans <= 5 and views <= 272
                    and payload == 32+16*spans+28*views and binary.stat().st_size < total
                    and index.is_file() and index.stat().st_size == 0):
                interrupted.append(dict(binary=binary.name,index=index.name,
                    actual_bytes=binary.stat().st_size,declared_total_bytes=total,
                    reason='Interrupted descriptor/payload write with empty index; not a RAM snapshot'))
                continue
        complete.append(binary)
    return complete, interrupted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    attempt = args.attempt.resolve()
    result = json.loads((attempt/'result.json').read_text())
    elf = attempt/'sm64ds-native.elf'
    elf_raw = elf.read_bytes()
    assert sha(elf_raw) == result['elf_sha256']
    assert sha((attempt/'sm64ds-native.3dsx').read_bytes()) == result['image_sha256']
    assert sha((attempt/'azahar-profile.ini').read_bytes()) == result['azahar_profile_sha256']
    names = base.symbols(elf)
    start, end = (names[n] for n in ('sm64ds_runtime_sections', 'sm64ds_runtime_sections_end'))
    assert end >= start and (end-start) % 20 == 0
    table = elf_bytes(elf_raw, start, end-start)
    sections = [struct.unpack_from('<5I', table, i) for i in range(0, len(table), 20)]
    checked_files = 3
    for group in ('original_capture_files', 'first_scene_frame_files', 'visible_scene_frame_files',
                  'ready_scene_frame_files', 'stage_requested_frame_files', 'ram_snapshot_files'):
        for filename, expected in result.get(group, {}).items():
            raw = (attempt/filename).read_bytes()
            assert len(raw) == expected['bytes'], filename
            assert sha(raw) == expected['sha256'], filename
            checked_files += 1
    inputs = records(attempt/'input.log', 'input ')
    titles = records(attempt/'title-state.log', 'title-state ')
    timings = records(attempt/'runtime.log', 'frame=')
    snapshots = []
    complete, interrupted = snapshot_paths(attempt)
    for binary in complete:
        snap = Snapshot(binary)
        metadata = snap.metadata
        original = [v for v in snap.views if v['name'] == 'original-section']
        assert len(original) == len(sections) == metadata['runtime_section_count']
        for view, (address, module, source, size, zero) in zip(original, sections):
            assert (view['address'], view['module'], view['original_address'],
                    view['size'], view['flags']) == (address, module, source, size, zero)
        root_header = [v for v in snap.views if v['name'] == 'original-root-header']
        root_heap = [v for v in snap.views if v['name'] == 'original-root-heap']
        assert len(root_header) == len(root_heap) == 1
        assert snap.u32(root_header[0]['address']+4) == root_heap[0]['address']
        assert snap.u32(root_header[0]['address']+8) == root_heap[0]['size']
        assert metadata['stack_available']
        assert any(v['name'] == 'mapped-stack-residency-region' and
                   Snapshot.contains(v, metadata['stack_probe_address'], 1) for v in snap.views)
        tls_available = metadata.get('thread_tls_available', False)
        assert type(tls_available) is bool
        tls_views = [v for v in snap.views if v['name'] == 'calling-thread-tls']
        tls = dict(captured=tls_available)
        if tls_available:
            address = metadata['thread_tls_address']
            assert type(address) is int and 0 < address <= 0xffffffff-512
            assert len(tls_views) == 1
            assert (tls_views[0]['address'], tls_views[0]['size'], tls_views[0]['module'],
                    tls_views[0]['original_address'], tls_views[0]['flags']) == (address, 512, 0xffffffff, 0, 0)
            payload = snap.read(address, 512)
            assert snap.u32(address) == 0x21545624
            reent = snap.u32(address+8)
            assert reent
            snap.read(reent, 4)
            tls.update(address=hex(address), bytes=512, sha256=sha(payload),
                       ThreadVars_magic_verified=True, reentrancy_pointer=hex(reent),
                       reentrancy_pointer_resident=True)
        else:
            assert not tls_views
        video = []
        for source, size in ((0x04000000, 0x2000), (0x05000000, 0x800),
                             (0x06800000, 0xa4000), (0x07000000, 0x800)):
            view = snap.video_view(source)
            assert view['size'] == size
            video.append(dict(source_address=hex(source), bytes=size,
                              sha256=sha(snap.read(view['address'], size))))
        regs = snap.video_view(0x04000000)['address']
        observed_regs = {f'{label}_{engine}': reader(regs+base_address+offset)
            for engine, base_address in (('A', 0), ('B', 0x1000))
            for label, offset, reader in (('DISPCNT', 0, snap.u32), ('BLDCNT', 0x50, snap.u16),
                                          ('BLDY', 0x54, snap.u16), ('MASTERBRIGHT', 0x6c, snap.u16))}
        snapshots.append(dict(**snap.summary(), section_table_exact=True,
            section_bytes=sum(v['size'] for v in original),
            section_sha256=[dict(module=v['module'], source_address=hex(v['original_address']),
                                 bytes=v['size'], sha256=sha(snap.read(v['address'], v['size'])))
                            for v in original],
            stack_probe_covered=True, root_header_matches_heap_view=True, thread_tls=tls,
            video=video, video_registers=observed_regs, title=decode(snap, names)))
    ready = next((r for r in titles if r.get('ready') == 1), None)
    touch = [r for r in inputs if r.get('touched') == 1]
    fresh_a = [r for r in inputs if r.get('pressed', 0) & 1]
    report = dict(status='passed_complete_captures_with_interrupted_artifact' if interrupted else 'passed',
        scope='Read-only archived file integrity and complete RAM capture analysis; interrupted artifacts are explicitly excluded',
        attempt=str(attempt), elf_sha256=result['elf_sha256'], image_sha256=result['image_sha256'],
        immutable_files_verified=checked_files, snapshots=snapshots,
        interrupted_snapshot_artifacts=interrupted, all_snapshots_complete=not interrupted,
        input_events=inputs, delivered_touch_events=touch, delivered_fresh_A_events=fresh_a,
        first_live_title_ready=ready,
        full_capture_after_A=bool(fresh_a and any(s['metadata']['tick'] >= fresh_a[0]['tick'] for s in snapshots)),
        two_fresh_A_events_delivered=len(fresh_a) >= 2,
        performance=dict(scope='Azahar counter samples; no physical timing or stable FPS claim',
            samples=len(timings), early=timings[:3],
            static_title_samples=[r for r in timings if r['frame'] in (180, 960)],
            ready_title_samples=[r for r in timings if ready and ready['tick'] <= r['ticks']][:3],
            final=timings[-1] if timings else None),
        stop_reason=result['stop_reason'], first_emulator_fault=result['first_emulator_fault'],
        physical_hardware=False, game_or_emulator_executed=False,
        limitations=['Coverage/integrity and observed fields do not establish liveness of every heap object.',
                    'RAM is sequential CPU-visible storage, with the captured consistency limitations.',
                    'No complete GPU correctness, playable session, sound, save, or physical 60 FPS verification.'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(status=report['status'], files=checked_files,
        snapshots=[dict(tick=s['metadata']['tick'], bytes=s['bytes'], views=s['view_count'],
                        title_ready=s['title'].get('touch_to_start_ready'),
                        stack_covered=s['stack_probe_covered']) for s in snapshots],
        interrupted=interrupted, first_ready_tick=ready['tick'] if ready else None, delivered_touch=touch,
        fresh_A=fresh_a, two_A_delivered=report['two_fresh_A_events_delivered'], report=str(args.report)), indent=2))


if __name__ == '__main__':
    main()
