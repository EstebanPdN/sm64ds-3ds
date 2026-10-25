#!/usr/bin/env python3
"""Inventory actual ARM11 symbols and the original menu/adventure dependency frontier.

This is a static link frontier, not reachability proof for indirect ROM vtables.
It does not invent missing data, silently pick duplicate strong definitions,
or make objects safe to execute by changing their addresses.
"""
import collections
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream'
OUT = ROOT / 'build/native-core'
ROOTS = [
    '_ZN8dScene_c15SetSceneToSpawnEjj', '_ZN8dScene_c16SpawnIfNecessaryEv',
    'dScDSMT_c_classInit', '_ZN9dScDSMT_c13InitResourcesEv',
    '_ZN9dScDSMT_c8BehaviorEv', '_ZN9dScDSMT_c6RenderEv',
    'StartFile', '_ZN5Stage13InitResourcesEv', '_ZN5Stage8BehaviorEv',
    '_ZN6Player13InitResourcesEv', '_ZN6Player8BehaviorEv', '_ZN6Player6RenderEv',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--link-frontier', action='store_true',
                        help='Create a relocatable object with unresolved dependencies explicitly retained')
    parser.add_argument('--extra-object', action='append', default=[],
                        help='Add one compiled platform/data ELF object (repeatable; archives are not silently expanded)')
    parser.add_argument('--replace-source', action='append', default=[],
                        help='Explicitly remove one original provider in favor of a platform object (repeatable)')
    parser.add_argument('--root-symbol', action='append', default=[],
                        help='Include one additional native startup root (repeatable)')
    parser.add_argument('--roots-only', action='store_true',
                        help='Inspect only explicit roots, for bounded native engine integration')
    parser.add_argument('--output-directory', type=Path, default=OUT,
                        help='Keep an independent subset report without replacing the gameplay frontier')
    args = parser.parse_args()
    if args.roots_only and not args.root_symbol:
        parser.error('--roots-only requires at least one --root-symbol')
    output = args.output_directory.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = json.loads((OUT / 'report.json').read_text())
    entries = {e['source']: e for e in report['entries']}
    for source in args.replace_source:
        if source not in entries:
            parser.error(f'No enrolled/generated provider named {source}')
        del entries[source]
    for object_arg in args.extra_object:
        path = Path(object_arg).resolve()
        if not path.is_file() or path.read_bytes()[:4] != b'\x7fELF':
            parser.error(f'--extra-object requires a compiled ELF object: {path}')
        rel = os.path.relpath(path, ROOT)
        entries['platform:' + rel] = {'source': 'platform:' + rel, 'compiled': True,
                                      'object': rel, 'extra_platform_object': True}
    objects = {e['object']: e['source'] for e in entries.values() if e['compiled']}
    nm = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro')) / 'devkitARM/bin/arm-none-eabi-nm'
    definitions = collections.defaultdict(list)
    references = collections.defaultdict(set)
    object_names = list(objects)
    for start in range(0, len(object_names), 200):
        result = subprocess.run([str(nm), '-A', '-g', *object_names[start:start + 200]],
                                cwd=ROOT, capture_output=True, text=True, check=True)
        for line in result.stdout.splitlines():
            obj, detail = line.split(':', 1)
            fields = detail.split()
            kind, symbol = fields[-2:]
            source = objects[obj]
            if kind == 'U':
                references[source].add(symbol)
            elif kind not in {'w', 'v'}:  # undefined weak symbols need no provider
                definitions[symbol].append({'source': source, 'kind': kind})
    strong = {s: [d for d in ds if d['kind'] not in {'W', 'V'}] for s, ds in definitions.items()}
    conflicts = {s: ds for s, ds in strong.items() if len(ds) > 1}
    unresolved = {}
    for source, syms in references.items():
        for sym in syms:
            if sym not in definitions:
                unresolved.setdefault(sym, []).append(source)

    # Use enrolled ownership to distinguish failed compilation from an absent
    # data/runtime definition. Do not replace unresolved names with address stubs.
    sys.path.insert(0, str(UPSTREAM / 'tools'))
    import srcpath
    owners, _ = srcpath._enrolment()
    owners = dict(owners)
    for split in report.get('split_translation_units', []):
        owners[split['symbol']] = split['source']
    owner_equiv = report['equivalent_destructor_tus']
    def reason(symbol):
        owner = owners.get(symbol)
        owner = owner_equiv.get(owner, owner)
        if owner in entries and not entries[owner]['compiled']:
            return {'reason': 'provider-does-not-compile', 'source': owner}
        if (re.match(r'__(?:aeabi|cxa|gxx)_', symbol)
                or symbol.startswith(('_ZTVN10__cxxabiv1', '_ZTIN10__cxxabiv1'))
                or symbol in {'memcpy', 'memmove', 'memset', 'strlen'}):
            return {'reason': 'toolchain-runtime'}
        if symbol.startswith(('data_', '_ZTV', '_ZTI', '_ZTS')) or symbol.startswith('overlay_'):
            return {'reason': 'original-data-or-table-needs-safe-native-relocation'}
        return {'reason': 'platform-runtime-or-alias-definition-missing', 'enrolled_owner': owner}

    roots = ([] if args.roots_only else ROOTS) + args.root_symbol
    queue = collections.deque(roots)
    seen_symbols, closure = set(), set()
    closure_missing, closure_conflicts, weak_choices = {}, {}, {}
    while queue:
        symbol = queue.popleft()
        if symbol in seen_symbols:
            continue
        seen_symbols.add(symbol)
        ds = definitions.get(symbol, [])
        if not ds:
            closure_missing[symbol] = reason(symbol)
            continue
        providers = strong[symbol] or ds
        if len(strong[symbol]) > 1:
            closure_conflicts[symbol] = providers
        elif len(providers) > 1:
            weak_choices[symbol] = providers
        # Include every candidate here to expose dependency needs conservatively;
        # this does NOT authorize the linker to choose a strong definition.
        for provider in providers:
            source = provider['source']
            if source not in closure:
                closure.add(source)
                queue.extend(sorted(references[source]))

    unsafe_accesses = {}
    vtable_stores = {}
    for source in sorted(closure):
        if source.startswith('platform:'):
            continue
        lines = (OUT / 'generated' / source).read_text().splitlines()
        literal_io = []
        tables = []
        for number, line in enumerate(lines, 1):
            if re.search(r'0x0?4[0-9a-fA-F]{6}\b', line) and not line.lstrip().startswith(('//', '*', '/*')):
                literal_io.append({'line': number, 'text': line.strip()})
            if '_ZTV' in line and '=' in line and not line.lstrip().startswith(('//', '*', '/*')):
                tables.append({'line': number, 'text': line.strip()})
        if literal_io:
            unsafe_accesses[source] = literal_io
        if tables:
            vtable_stores[source] = tables
    result = {
        'scope': 'Conservative object dependency frontier, excluding unreferenced indirect vtable targets',
        'upstream_commit': report['upstream_commit'], 'defined_symbols': len(definitions),
        'extra_objects': args.extra_object, 'replaced_sources': args.replace_source,
        'strong_definition_conflicts': conflicts, 'unresolved_symbols': unresolved,
        'playable': {'roots': roots, 'sources': sorted(closure), 'unresolved': closure_missing,
                     'strong_conflicts': closure_conflicts, 'weak_comdat_candidates': weak_choices,
                     'literal_ds_io_needing_replacement': unsafe_accesses,
                     'vtable_stores_needing_address_point_review': vtable_stores},
    }
    (output / 'symbols.json').write_text(json.dumps(result, indent=2) + '\n')
    counts = {
        'compiled_objects': len(objects), 'defined_symbols': len(definitions),
        'strong_definition_conflicts': len(conflicts), 'unresolved_symbols': len(unresolved),
        'playable_sources': len(closure), 'playable_unresolved': len(closure_missing),
        'playable_conflicts': len(closure_conflicts),
        'playable_literal_ds_io_sources': len(unsafe_accesses),
        'playable_vtable_store_sources': len(vtable_stores),
    }
    # Publish the current inventory before trying the link. A failure must never
    # leave counts.json describing a previous successful build.
    if args.link_frontier:
        counts['relocatable_link_status'] = 'pending'
        counts['relocatable_link_is_executable'] = False
    (output / 'counts.json').write_text(json.dumps(counts, indent=2) + '\n')
    if args.link_frontier:
        if closure_conflicts:
            counts['relocatable_link_status'] = 'refused-strong-definition-conflicts'
            (output / 'counts.json').write_text(json.dumps(counts, indent=2) + '\n')
            (output / 'gameplay-frontier.o').unlink(missing_ok=True)
            raise RuntimeError('Refusing to link: playable frontier has ambiguous strong definitions')
        # ld -r preserves unresolved symbols; it is a real link check for duplicate
        # definitions and ARM object compatibility, never a runnable executable.
        linker = nm.with_name('arm-none-eabi-ld')
        response = output / 'frontier-objects.rsp'
        response.write_text('\n'.join('"' + entries[s]['object'].replace('\\', '\\\\').replace('"', '\\"') + '"'
                                     for s in sorted(closure)) + '\n')
        linked = output / 'gameplay-frontier.o'
        run = subprocess.run([str(linker), '-r', '--fatal-warnings', '-o',
                              str(linked), '@' + str(response)],
                             cwd=ROOT, capture_output=True, text=True)
        (output / 'frontier-link.txt').write_text(run.stdout + run.stderr)
        counts['relocatable_link_exit_code'] = run.returncode
        counts['relocatable_link_is_executable'] = False
        counts['relocatable_link_status'] = 'failed' if run.returncode else 'success'
        if run.returncode:
            linked.unlink(missing_ok=True)
            (output / 'counts.json').write_text(json.dumps(counts, indent=2) + '\n')
            raise RuntimeError('Relocatable frontier link failed; see frontier-link.txt')
        counts['relocatable_object_bytes'] = linked.stat().st_size
    (output / 'counts.json').write_text(json.dumps(counts, indent=2) + '\n')
    print(json.dumps(counts, indent=2))


if __name__ == '__main__':
    main()
