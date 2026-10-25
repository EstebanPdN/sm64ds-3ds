#!/usr/bin/env python3
"""Read exact acquired texture banks and logged guest render intervals.

This does not execute game code, advance a scene or measure a physical 3DS.
Endpoint byte equality describes only the captured endpoints. Logged tick
intervals in Azahar are not wall-clock FPS or physical CPU/GPU timings.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import struct

from analyze_native_acquisition import snapshot_paths
from audit_native_gx_stream import FIELDS, FLAGS, TEXTURE_BYTES, PALETTE_BYTES
from test_native_menu_callbacks import symbols
from test_native_title_fade_arm import Snapshot


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def summarize(samples):
    keys = ('triangles', 'decode_us', 'scanout_us', 'submit_us',
            'texture_crc_bytes', 'decoded_texture_hits', 'screen_texture_hits')
    result = {'samples': len(samples)}
    for key in keys:
        values = [row[key] for row in samples if key in row]
        if values:
            result[key] = dict(samples=len(values), minimum=min(values),
                               median=statistics.median(values), maximum=max(values))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = json.loads((args.attempt/'result.json').read_text())
    elf = args.attempt/'sm64ds-native.elf'
    assert sha(elf.read_bytes()) == result['elf_sha256']
    assert sha((args.attempt/'sm64ds-native.3dsx').read_bytes()) == result['image_sha256']
    geometry = symbols(elf)['_ZN6sm64ds12_GLOBAL__N_1L8geometryE']
    rows = []
    complete, interrupted = snapshot_paths(args.attempt)
    for binary in complete:
        expected = result['ram_snapshot_files'][binary.name]
        assert sha(binary.read_bytes()) == expected['sha256']
        snap = Snapshot(binary)
        arrays = snap.read(geometry, TEXTURE_BYTES+PALETTE_BYTES)
        fields = struct.unpack('<8I3B', snap.read(
            geometry+TEXTURE_BYTES+PALETTE_BYTES, 35))
        rows.append(dict(binary=binary.name, tick=snap.metadata['tick'],
            phase=snap.metadata['phase'], bytes=len(snap.raw),
            snapshot_sha256=expected['sha256'], geometry_address=hex(geometry),
            geometry_metadata=dict(zip(FIELDS+FLAGS, fields)),
            texture_sha256=sha(arrays[:TEXTURE_BYTES]),
            palette_sha256=sha(arrays[TEXTURE_BYTES:])))
    assert rows, 'No complete acquired RAM'
    samples = []
    log = (args.attempt/'runtime.log').read_text()
    assert log == result['application_log'], 'Archived runtime log/result differ'
    for line in log.splitlines():
        if line.startswith('frame='):
            row = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}
            assert all(k in row for k in ('frame', 'ticks', 'triangles',
                                         'decode_us', 'scanout_us', 'submit_us'))
            samples.append(row)
    stage = next((r for r in rows if r['binary']=='ram-stage-presented.ram.bin'), None)
    stage_samples = [s for s in samples if stage and s['ticks'] > stage['tick']]
    final = next((r for r in rows if r['binary']=='ram-final.ram.bin'), None)
    comparisons = []
    if stage and final:
        comparisons.append(dict(from_binary=stage['binary'], to_binary=final['binary'],
            texture_bytes_equal=stage['texture_sha256']==final['texture_sha256'],
            palette_bytes_equal=stage['palette_sha256']==final['palette_sha256'],
            metadata_equal=stage['geometry_metadata']==final['geometry_metadata'],
            applies_only_to_captured_endpoints=True))
    report = dict(status='read_only_diagnostic_complete', scope=__doc__,
        attempt=str(args.attempt.resolve()), ELF_sha256=result['elf_sha256'],
        image_sha256=result['image_sha256'], snapshots=rows,
        interrupted_snapshots=interrupted,
        endpoint_comparisons=comparisons, all_logged_samples=summarize(samples),
        Stage_logged_samples=summarize(stage_samples), frame_records=samples,
        physical_performance_measured=False, game_executed=False,
        first_emulator_fault=result['first_emulator_fault'],
        stop_reason=result['stop_reason'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    assert not args.report.exists(), 'Preserve completed diagnostic evidence'
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(status=report['status'], snapshots=len(rows),
        Stage_logged_samples=report['Stage_logged_samples'], report=str(args.report))))


if __name__ == '__main__':
    main()
