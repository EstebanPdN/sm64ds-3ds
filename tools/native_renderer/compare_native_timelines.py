#!/usr/bin/env python3
"""Compare explicit native CSV captures; never infer console performance from Azahar."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        raise ValueError('The requested phase has no samples')
    return dict(samples=len(ordered), mean=sum(ordered)/len(ordered),
                p95=ordered[math.ceil(len(ordered)*.95)-1],
                p99=ordered[math.ceil(len(ordered)*.99)-1], maximum=ordered[-1])


def read_capture(path):
    metadata = json.loads(path.with_name('native-renderer.json').read_text())
    if metadata['format'] != 'SM64_NATIVE_DIAGNOSTIC_1' or metadata['tick_frequency'] <= 0:
        raise ValueError(f'{path}: unsupported diagnostic metadata')
    if not metadata['timings']:
        raise ValueError(f'{path}: active wall requires measured GPU wait ticks')
    with path.open() as source:
        rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(source)]
    previous = None
    for row in rows:
        if not 0 <= row['gpu_wait_ticks'] <= row['end_tick']-row['start_tick']:
            raise ValueError(f'{path}: invalid timing interval')
        if previous is not None and row['end_tick'] < previous:
            raise ValueError(f'{path}: unordered timeline')
        previous = row['end_tick']
    return metadata, rows


def summarize(path, phases):
    metadata, rows = read_capture(path)
    ms = 1000/metadata['tick_frequency']
    result = dict(build=metadata['build'], source=str(path.resolve()),
                  csv_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), phases={})
    for name, start, end in phases:
        selected = [row for row in rows if start <= row['frame'] < end]
        intervals = [(row['end_tick']-previous['end_tick'])*ms
                     for previous, row in zip(rows, rows[1:]) if start <= row['frame'] < end]
        scenes = sorted({row['scene'] for row in selected})
        result['phases'][name] = dict(frame_range=[start, end], scenes=scenes,
            active_render_wall_ms=distribution([(r['end_tick']-r['start_tick']-r['gpu_wait_ticks'])*ms for r in selected]),
            presentation_interval_ms=distribution(intervals),
            base_update_wall_ms=distribution([r['update_ticks']*ms for r in selected if not r['endpoint']]),
            source_2d_prepare_wall_ms=distribution([r['source_2d_ticks']*ms for r in selected if not r['endpoint']]),
            previous_gpu_ms=distribution([r['previous_gpu_us']/1000 for r in selected]),
            draws=distribution([r['draws'] for r in selected]),
            intervals_over_25ms=sum(value > 25 for value in intervals),
            resource_load_samples=sum(r['bank_load_ticks'] > 0 for r in selected),
            error_samples=sum(r['error'] != 0 for r in selected))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--phase', action='append', required=True, help='name:start:end (native capture frames)')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    phases = []
    for text in args.phase:
        name, first, last = text.split(':')
        first, last = int(first), int(last)
        if not name or not 0 <= first < last:
            raise ValueError(f'Invalid phase: {text}')
        phases.append((name, first, last))
    output = dict(scope='One emulator run per variant; not physical hardware acceptance',
                  timing='Wall scopes can overlap; source2D includes blocking/preemption. Do not add nested phases.',
                  baseline=summarize(args.baseline, phases), candidate=summarize(args.candidate, phases))
    args.output.write_text(json.dumps(output, indent=2)+'\n')
    print(args.output)


if __name__ == '__main__':
    main()
