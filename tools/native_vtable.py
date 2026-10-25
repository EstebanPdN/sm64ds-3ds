"""Adapt exact reviewed manual vptr stores to GCC's ARM address point.

This changes generated source expressions, never vtable symbols, data aliases,
class layouts or compiler-emitted constructors. The checked recipe records the
original source hash and exact RHS range for every reviewed store. A table with
the original DS slot format must retain its unshifted address.
"""
from functools import lru_cache
import hashlib
import json
from pathlib import Path

PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
RECIPE = Path(__file__).with_name('native_vtable_rules.json')


@lru_cache(maxsize=1)
def rules():
    recipe = json.loads(RECIPE.read_text())
    if recipe['upstream_commit'] != PIN or recipe['offset_bytes'] != 8:
        raise ValueError('Manual vptr recipe has a different source/ABI pin')
    if len(recipe['rows']) != 352 or len(recipe['unchanged_controls']) != 59:
        raise ValueError('Manual vptr recipe coverage changed without review')
    grouped = {}
    seen = set()
    for row in recipe['rows']:
        location = (row['source'], row['rhs_start'], row['rhs_end'])
        if location in seen or row['offset_bytes'] != 8:
            raise ValueError('Duplicate or unreviewed manual vptr site')
        if row['declaration_kind'] not in ('array', 'scalar_symbol'):
            raise ValueError('Manual vptr declaration type is not reviewed')
        if row['proof'] == 'existing_reviewed_Stage_transient_repair':
            if row['source'] != 'src/d_s_stage.cpp' or row['symbol'] != '_ZTV8dScene_c':
                raise ValueError('Unreviewed transient Stage vptr repair')
        elif row['proof'] != 'retained_GCC_prefix':
            raise ValueError('Manual vptr store lacks GCC prefix evidence')
        seen.add(location)
        grouped.setdefault(row['source'], []).append(row)
    for rows in grouped.values():
        rows.sort(key=lambda row: row['rhs_start'])
        if any(a['rhs_end'] > b['rhs_start'] for a, b in zip(rows, rows[1:])):
            raise ValueError('Overlapping manual vptr sites')
    return grouped


def replacement(row):
    rhs = row['original_rhs']
    # +0 makes an array expression decay in typeof. Casts already present in
    # the original RHS retain their int/pointer type. The byte addition moves
    # char arrays, word arrays and addresses of scalar symbol declarations by
    # exactly eight bytes, rather than eight bytes only for word arrays.
    return f'((__typeof__(({rhs}) + 0))((unsigned char*)({rhs}) + 8))'


def adapt(name, original):
    rows = rules().get(name, ())
    if not rows:
        return original, []
    digest = hashlib.sha256(original.encode()).hexdigest()
    if any(row['source_sha256'] != digest for row in rows):
        raise ValueError('Reviewed manual vptr source changed: ' + name)
    result = original
    edits = []
    for row in reversed(rows):
        start, end = row['rhs_start'], row['rhs_end']
        before = row['original_rhs']
        if original[start:end] != before:
            raise ValueError('Reviewed manual vptr expression changed: ' + name)
        after = replacement(row)
        result = result[:start] + after + result[end:]
        edits.append(dict(symbol=row['symbol'], original=before, replacement=after,
                          original_offset=start, original_line=row['original_line'],
                          offset_bytes=8, proof=row['proof']))
    # Undo in increasing original-offset order. Each preceding length change
    # is removed before the next original offset is used.
    inverse = result
    for edit in reversed(edits):
        start = edit['original_offset']
        after = edit['replacement']
        if inverse[start:start + len(after)] != after:
            raise ValueError('Manual vptr repair cannot be inverted: ' + name)
        inverse = inverse[:start] + edit['original'] + inverse[start + len(after):]
    if inverse != original:
        raise ValueError('Manual vptr repair changed unrelated source: ' + name)
    return result, list(reversed(edits))
