#!/usr/bin/env python3
"""Remove only closed, analyzed campaign VM files, retaining signed-by-hash receipts.

Dry-run by default. Never follows symlinks, scans arbitrary folders, deletes
traces/renders/assets/saves, or alters the immutable acquisition result.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ACQUISITIONS = ROOT / 'build/integrated-native/acquisitions'


def sha(path):
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256')
    return digest.hexdigest()


def inventory(attempt):
    assert not attempt.is_symlink()
    attempt = attempt.resolve()
    assert attempt.parent == ACQUISITIONS.resolve() and attempt.name.startswith('attempt-')
    receipt = attempt / 'retired-VM-receipt.json'
    assert not receipt.exists(), 'An existing retirement receipt must be inspected, not overwritten'
    manual = (attempt / 'interactive-analysis.json').exists()
    required = [attempt / name for name in
                ('result.json', 'interactive-analysis.json' if manual else 'campaign-analysis.json', 'allocator-analysis.json')]
    assert all(p.is_file() and not p.is_symlink() for p in required)
    closed, campaign, allocator = [json.loads(p.read_text()) for p in required]
    assert closed['status'] == 'acquisition_closed' and closed['first_emulator_fault'] is None
    if manual:
        assert campaign['status'] == 'manual_capture_series_classified'
        assert allocator['status'] in ('read_only_captured_allocator_lists_validated', 'partial_allocator_capture_validation')
    else:
        allowed={
            'benchmark_three_complete_captures':'three_complete_original_API_baseline_captures_validated',
            'process_exit':'three_complete_captures_classified_unplanned_exit'}
        assert closed['stop_reason'] in allowed and campaign['status']==allowed[closed['stop_reason']]
        assert allocator['status'] == 'read_only_captured_allocator_lists_validated'
    assert closed['ELF_sha256'] == campaign['ELF_sha256'] == allocator['ELF_sha256']
    snapshots = {s['filename']: s['VM_sha256'] for s in campaign['snapshots']}
    if not manual:
        assert len(snapshots)==3
        extras=campaign.get('extra_diagnostics',[])
        assert len(extras)<=5
        for item in extras:
            assert item['complete'] and item['phase'] in ('before-stable-control','after-three-captures')
            assert item['filename'] not in snapshots
            snapshots[item['filename']]=item['VM_sha256']
    allocators = {s['snapshot']: s['VM_sha256'] for s in allocator['snapshots'] + allocator.get('rejected_snapshots', [])}
    assert (1<=len(snapshots)<=32 if manual else 3<=len(snapshots)<=8) and snapshots == allocators
    directory = attempt / 'user/sdmc/3ds/sm64ds'
    assert all(not p.is_symlink() for p in (directory, *directory.parents))
    paths = sorted(directory.glob('*.vm'))
    assert {p.name for p in paths} == set(snapshots)
    files = []
    for p in paths:
        assert p.is_file() and not p.is_symlink()
        relative = p.relative_to(attempt).as_posix()
        digest = sha(p)
        assert digest == snapshots[p.name] == closed['files'][relative]['sha256']
        assert p.stat().st_size == closed['files'][relative]['bytes']
        files.append(dict(path=relative, bytes=p.stat().st_size, sha256=digest))
    return receipt, dict(status='analysis_preserved_raw_VM_retirement_planned',
        authorization='User requested analyzing each RAM series and then deleting its dumps',
        attempt=str(attempt), files=files, ELF_sha256=closed['ELF_sha256'],
        preserved_reports={p.name: sha(p) for p in required},
        allocator_rejected_snapshots=allocator.get('rejected_snapshots', []),
        preserved='Original result, timing traces, anchors, renders, analysis, ELF, saves and private inputs',
        warning='Raw VM files cannot be replayed after retirement; original result hashes remain historical')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('attempts', type=Path, nargs='+')
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    # Validate the entire requested set before deleting the first byte.
    assert len({p.resolve() for p in args.attempts}) == len(args.attempts)
    plans = [inventory(p) for p in args.attempts]
    for receipt, report in plans:
        print(json.dumps(dict(attempt=report['attempt'], files=len(report['files']),
            bytes=sum(p['bytes'] for p in report['files']), apply=args.apply)))
        if not args.apply:
            continue
        # Persist the inventory first, so interruption cannot erase provenance.
        with receipt.open('x') as stream:
            stream.write(json.dumps(report, indent=2) + '\n')
        for item in report['files']:
            path = Path(report['attempt']) / item['path']
            assert not path.is_symlink() and sha(path) == item['sha256']
            path.unlink()
        report['status'] = 'analysis_preserved_raw_VM_retired'
        report['absent_files_verified'] = all(not (Path(report['attempt']) / f['path']).exists()
                                               for f in report['files'])
        receipt.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
