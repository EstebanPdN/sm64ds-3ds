#!/usr/bin/env python3
"""Generate a ROM-only install manifest for the pinned integrated native port.

This is a host build tool. ROM payloads are represented only by checked spans;
the resulting C++ header embeds catalogs, never game assets. A canonical private
SD tree is independently compared byte for byte before any output is written.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib


ROOT = Path(__file__).resolve().parents[1]
ARM9_BASE = 0x02004000
ARCHIVE_TABLE = 0x0208ECF4
HANDLE_TABLE = 0x020BD4B8
HANDLE_COUNT = 0x80A
ARCHIVE_COUNT = 13
KIND_BY_SUFFIX = {
    '.bca': 'animation', '.bmd': 'model', '.btp': 'texture-sequence',
    '.kcl': 'collision', '.narc': 'archive', '.sdat': 'sound-archive',
    '.bin': 'data',
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_span(blob: bytes, offset: int, size: int, label: str) -> bytes:
    require(0 <= offset <= 0xFFFFFFFF and 0 <= size <= 0xFFFFFFFF,
            f'{label}: span is not uint32')
    require(offset <= len(blob) and size <= len(blob) - offset,
            f'{label}: span outside source ({offset:#x}+{size:#x}/{len(blob):#x})')
    return blob[offset:offset + size]


def safe_path(path: str) -> str:
    require(bool(path) and path.isascii() and len(path) < 260,
            f'unsafe path length or encoding: {path!r}')
    require(all(32 <= ord(c) < 127 for c in path) and
            not any(c in path for c in '\\:*?"<>|'), f'unsafe path: {path!r}')
    require(all(part not in ('', '.', '..') for part in path.split('/')),
            f'unsafe path component: {path!r}')
    return path


def parse_fnt(fnt: bytes, file_count: int, label: str) -> dict[int, str]:
    """Parse the DS/NARC FNT with cycle, duplicate-ID and path bounds checks."""
    require(len(fnt) >= 8, f'{label}: missing root directory')
    directory_count = struct.unpack_from('<H', fnt, 6)[0]
    require(1 <= directory_count <= 4096 and directory_count * 8 <= len(fnt),
            f'{label}: invalid directory count')
    files: dict[int, str] = {}
    paths: set[str] = set()
    seen: set[int] = set()

    def walk(directory_id: int, prefix: str, depth: int) -> None:
        require(depth <= 32, f'{label}: directory depth exceeds 32')
        require(0xF000 <= directory_id < 0xF000 + directory_count,
                f'{label}: invalid directory ID {directory_id:#x}')
        index = directory_id - 0xF000
        require(index not in seen, f'{label}: repeated/cyclic directory')
        seen.add(index)
        offset, file_id, _ = struct.unpack_from('<IHH', fnt, index * 8)
        require(directory_count * 8 <= offset < len(fnt),
                f'{label}: subtable is outside the name area')
        while True:
            require(offset < len(fnt), f'{label}: unterminated directory')
            tag = fnt[offset]
            offset += 1
            if not tag:
                break
            length = tag & 0x7F
            require(length > 0 and length <= len(fnt) - offset,
                    f'{label}: truncated name')
            name = fnt[offset:offset + length].decode('ascii')
            offset += length
            require('/' not in name, f'{label}: slash in name')
            safe_path(name)
            path = safe_path(prefix + name)
            if tag & 0x80:
                require(offset + 2 <= len(fnt), f'{label}: missing child ID')
                child = struct.unpack_from('<H', fnt, offset)[0]
                offset += 2
                walk(child, path + '/', depth + 1)
            else:
                require(file_id < file_count and file_id not in files,
                        f'{label}: duplicate/out-of-range file ID {file_id}')
                require(path not in paths, f'{label}: duplicate path {path}')
                files[file_id] = path
                paths.add(path)
                file_id += 1

    walk(0xF000, '', 0)
    require(len(seen) == directory_count, f'{label}: unreachable directories')
    return files


def cstring(image: bytes, address: int, base: int, label: str) -> str:
    offset = address - base
    require(0 <= offset < len(image), f'{label}: invalid string pointer')
    end = image.find(b'\0', offset)
    require(end >= 0 and end - offset < 260, f'{label}: missing/long string')
    return image[offset:end].decode('ascii')


def tsv(header: tuple[str, ...], rows: list[tuple]) -> bytes:
    out = io.StringIO(newline='')
    writer = csv.writer(out, delimiter='\t', lineterminator='\n')
    writer.writerow(header)
    writer.writerows(rows)
    return out.getvalue().encode('ascii')


def kind(path: str) -> str:
    return KIND_BY_SUFFIX.get(Path(path).suffix.lower(), 'file')


def generate(args: argparse.Namespace) -> dict:
    try:
        import ndspy.codeCompression
    except ImportError as exc:
        raise ValueError('ndspy is required by this host generator; use the integrated-native venv') from exc

    lock_path = ROOT / 'reference-native.lock.json'
    lock = json.loads(lock_path.read_text())
    revision = subprocess.check_output(
        ['git', '-C', str(args.decomp), 'rev-parse', 'HEAD'], text=True).strip()
    require(revision == lock['decomp_commit'], 'decomp is not the pinned revision')
    rom = args.rom.read_bytes()
    require(len(rom) >= 0x200 and len(rom) <= 0xFFFFFFFF, 'invalid ROM size')
    rom_sha1 = hashlib.sha1(rom).hexdigest()
    require(rom_sha1 == lock['ROM_SHA1'], 'ROM is not the verified EU revision')
    require(rom[0x0C:0x10] == b'ASMP', 'ROM game code differs')
    rom_crc32 = zlib.crc32(rom) & 0xFFFFFFFF

    reference = args.reference_sd / 'sm64ds/repo'
    require(reference.is_dir(), f'missing canonical repo tree: {reference}')
    entries: dict[str, dict] = {}
    generated: dict[str, bytes] = {}

    def add_span(path: str, offset: int, size: int, origin: str) -> None:
        path = safe_path(path)
        require(path not in entries, f'duplicate manifest path: {path}')
        data = checked_span(rom, offset, size, path)
        entries[path] = dict(path=path, offset=offset, size=size,
                             crc32=zlib.crc32(data) & 0xFFFFFFFF,
                             sha256=sha256(data), origin=origin, generated=False)

    def add_generated(path: str, data: bytes, origin: str) -> None:
        path = safe_path(path)
        require(path not in entries, f'duplicate manifest path: {path}')
        generated[path] = data
        entries[path] = dict(path=path, offset=0, size=len(data),
                             crc32=zlib.crc32(data) & 0xFFFFFFFF,
                             sha256=sha256(data), origin=origin, generated=True)

    fnt_off, fnt_size, fat_off, fat_size = struct.unpack_from('<IIII', rom, 0x40)
    ovt9_off, ovt9_size, ovt7_off, ovt7_size = struct.unpack_from('<IIII', rom, 0x50)
    require(fat_size > 0 and fat_size % 8 == 0, 'invalid FAT size')
    require(ovt9_size > 0 and ovt9_size % 32 == 0, 'invalid ARM9 overlay table')
    require((ovt7_off == 0 and ovt7_size == 0) or
            (ovt7_off > 0 and ovt7_size > 0 and ovt7_size % 32 == 0),
            'invalid ARM7 overlay table')
    fnt = checked_span(rom, fnt_off, fnt_size, 'FNT')
    fat = checked_span(rom, fat_off, fat_size, 'FAT')
    ovt9 = checked_span(rom, ovt9_off, ovt9_size, 'OVT9')
    file_count = len(fat) // 8
    require(file_count <= 4096, 'FAT exceeds native MAX_FILES')
    file_spans = [struct.unpack_from('<II', fat, i * 8) for i in range(file_count)]
    for file_id, (first, end) in enumerate(file_spans):
        require(first <= end, f'FAT {file_id}: reversed span')
        checked_span(rom, first, end - first, f'FAT {file_id}')
    named = parse_fnt(fnt, file_count, 'ROM FNT')
    named_by_path = {path: file_id for file_id, path in named.items()}
    file_rows = []
    for file_id, path in sorted(named.items()):
        first, end = file_spans[file_id]
        add_span('extracted/dsd/files/' + path, first, end - first,
                 f'ROM FNT/FAT file {file_id}')
        file_rows.append((file_id, f'0x{file_id:04x}', path, kind(path), end - first))
    add_generated('build/assets/files.tsv',
                  tsv(('file_id', 'hex_id', 'path', 'kind', 'size'), file_rows),
                  'ROM FNT/FAT catalog')
    add_generated('extracted/dsd/files/files.txt',
                  ''.join(f'{r[0]} {r[4]} {r[2]}\n' for r in file_rows).encode('ascii'),
                  'ROM FNT/FAT directory manifest')

    ov0_records = [struct.unpack_from('<8I', ovt9, off)
                   for off in range(0, len(ovt9), 32)
                   if struct.unpack_from('<I', ovt9, off)[0] == 0]
    require(len(ov0_records) == 1, 'overlay zero is missing/duplicated')
    ov0 = ov0_records[0]
    require(ov0[6] < file_count, 'overlay zero FAT ID out of bounds')
    start, end = file_spans[ov0[6]]
    ov0_raw = checked_span(rom, start, end - start, 'overlay zero')
    overlay = ndspy.codeCompression.decompress(ov0_raw) if ov0[7] & 0x01000000 else ov0_raw
    require(len(overlay) == ov0[2], 'overlay zero decompressed length differs')
    overlay_path = args.decomp / 'extracted/overlays/overlay_0000.bin'
    require(overlay == overlay_path.read_bytes(), 'decomp overlay zero differs from ROM')
    handle_offset = HANDLE_TABLE - ov0[1]
    checked_span(overlay, handle_offset, HANDLE_COUNT * 4, 'handle table')
    handle_rows = []
    for handle in range(HANDLE_COUNT):
        pointer = struct.unpack_from('<I', overlay, handle_offset + handle * 4)[0]
        path = safe_path(cstring(overlay, pointer, ov0[1], f'handle {handle}'))
        require(path in named_by_path, f'handle {handle}: missing FNT path')
        file_id = named_by_path[path]
        first, end = file_spans[file_id]
        handle_rows.append((handle, f'0x{handle:04x}', file_id,
                            f'0x{file_id:04x}', path, kind(path), end - first))
    add_generated('build/assets/handles.tsv',
                  tsv(('handle', 'hex_handle', 'file_id', 'hex_file_id',
                       'path', 'kind', 'size'), handle_rows),
                  'ROM overlay zero handle pointers + FNT/FAT')

    arm9_off, _, arm9_base, arm9_size = struct.unpack_from('<IIII', rom, 0x20)
    require(arm9_base == ARM9_BASE, 'unexpected ARM9 RAM base')
    arm9_raw = checked_span(rom, arm9_off, arm9_size, 'ARM9')
    arm9 = ndspy.codeCompression.decompress(arm9_raw)
    arm9_path = args.decomp / 'extracted/arm9_dec.bin'
    require(arm9 == arm9_path.read_bytes(), 'decomp ARM9 differs from ROM')
    table = ARCHIVE_TABLE - ARM9_BASE
    checked_span(arm9, table, ARCHIVE_COUNT * 20, 'archive table')
    archive_rows = []
    archive_report = []
    for index in range(ARCHIVE_COUNT):
        loaded, heap, first_id, end_id, name_p, path_p = struct.unpack_from(
            '<IIHHII', arm9, table + index * 20)
        require(loaded == heap == 0 and first_id == 0x8000 + index * 0x400 and
                first_id < end_id <= first_id + 0x400, f'archive {index}: unexpected table entry')
        name = safe_path(cstring(arm9, name_p, ARM9_BASE, 'archive short name'))
        require('/' not in name, 'archive short name contains slash')
        narc_path = cstring(arm9, path_p, ARM9_BASE, 'archive ROM path')
        require(narc_path.startswith('/') and not narc_path.startswith('//'), 'archive path is not rooted')
        narc_path = safe_path(narc_path[1:])
        require(narc_path in named_by_path, f'archive {index}: missing ROM path')
        file_id = named_by_path[narc_path]
        start, end = file_spans[file_id]
        narc = checked_span(rom, start, end - start, narc_path)
        require(len(narc) >= 16 and narc[:8] == b'NARC\xfe\xff\x00\x01',
                f'{narc_path}: invalid NARC header')
        length, cursor, blocks = struct.unpack_from('<IHH', narc, 8)
        require(length == len(narc) and 16 <= cursor <= length,
                f'{narc_path}: invalid declared length/header size')
        sections = {}
        for _ in range(blocks):
            require(cursor + 8 <= length, f'{narc_path}: truncated block')
            tag, size = struct.unpack_from('<4sI', narc, cursor)
            require(size >= 8 and size <= length - cursor and tag not in sections,
                    f'{narc_path}: invalid/duplicate block')
            sections[tag] = (cursor + 8, size - 8)
            cursor += size
        require(cursor == length and set(sections) == {b'BTAF', b'BTNF', b'GMIF'},
                f'{narc_path}: missing/extra/trailing block')
        btaf_off, btaf_size = sections[b'BTAF']
        require(btaf_size >= 4, f'{narc_path}: short BTAF')
        count, reserved = struct.unpack_from('<HH', narc, btaf_off)
        require(reserved == 0 and count == end_id - first_id and btaf_size == 4 + count * 8,
                f'{narc_path}: member count differs from ARM9')
        btnf_off, btnf_size = sections[b'BTNF']
        members = parse_fnt(checked_span(narc, btnf_off, btnf_size, 'NARC FNT'), count, narc_path)
        require(set(members) == set(range(count)), f'{narc_path}: unnamed/duplicate members')
        gmif_off, gmif_size = sections[b'GMIF']
        for member in range(count):
            member_first, member_end = struct.unpack_from('<II', narc, btaf_off + 4 + member * 8)
            require(member_first <= member_end <= gmif_size,
                    f'{narc_path}: invalid member {member} span')
            rel = f'archives/{name}/{members[member]}'
            add_span('extracted/dsd/' + rel, start + gmif_off + member_first,
                     member_end - member_first, f'{narc_path} BTAF member {member}')
            archive_rows.append((first_id + member, f'0x{first_id + member:04x}', name, rel))
        archive_report.append(dict(index=index, path=narc_path, short_name=name,
                                   first_id=first_id, end_id=end_id, count=count))
    add_generated('build/assets/archives.tsv',
                  tsv(('id', 'hex_id', 'archive', 'path'), archive_rows),
                  'ROM ARM9 archive table + NARC BTAF/BTNF/GMIF')

    nitro_meta = [('fnt_offset', fnt_off), ('fnt_size', fnt_size),
                  ('fat_offset', fat_off), ('fat_size', fat_size),
                  ('ovt9_offset', ovt9_off), ('ovt9_size', ovt9_size),
                  ('ovt7_offset', ovt7_off), ('ovt7_size', ovt7_size)]
    add_generated('build/assets/nitrofs.tsv', tsv(('key', 'value'), nitro_meta), 'ROM header words')
    for name, offset, size in [('fnt', fnt_off, fnt_size), ('fat', fat_off, fat_size),
                               ('ovt9', ovt9_off, ovt9_size), ('ovt7', ovt7_off, ovt7_size)]:
        if size:
            add_span(f'build/assets/nitrofs_{name}.bin', offset, size, 'ROM header span')

    # Inventory independently; refuse symlinks and any omitted/extra file.
    actual = {}
    for path in reference.rglob('*'):
        require(not path.is_symlink(), f'canonical tree contains symlink: {path}')
        if path.is_file():
            relative = safe_path(path.relative_to(reference).as_posix())
            actual[relative] = path
    require(set(actual) == set(entries),
            f'canonical tree mismatch: missing={sorted(set(actual)-set(entries))}, '
            f'extra={sorted(set(entries)-set(actual))}')
    for path, entry in entries.items():
        candidate = generated[path] if entry['generated'] else checked_span(
            rom, entry['offset'], entry['size'], path)
        reference_bytes = actual[path].read_bytes()
        require(candidate == reference_bytes, f'canonical byte mismatch: {path}')
        require(zlib.crc32(reference_bytes) & 0xFFFFFFFF == entry['crc32'],
                f'canonical CRC mismatch: {path}')

    ordered = [entries[path] for path in sorted(entries)]
    identity = dict(format='sm64ds-integrated-rom-install-v1', rom_sha1=rom_sha1,
                    rom_size=len(rom), files=ordered)
    schema_sha256 = sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode('ascii'))
    metadata_digest = hashlib.sha256()
    for path, data in sorted(generated.items()):
        metadata_digest.update(path.encode('ascii') + b'\0' + struct.pack('<I', len(data)) + data)
    metadata_sha256 = metadata_digest.hexdigest()
    lines = [
        '// Generated from a user-supplied verified EU ROM. Private build artifact.',
        '// Game assets are ROM spans; only filesystem metadata is embedded.',
        '#pragma once', '#include <cstddef>', '#include <cstdint>',
        'namespace rom_install_manifest {',
        'struct File { const char* path; uint32_t offset, size, crc32; const unsigned char* generated; };',
        f'inline constexpr uint32_t rom_size = {len(rom)}u;',
        f'inline constexpr uint32_t rom_crc32 = 0x{rom_crc32:08x}u;',
        'inline constexpr uint8_t sha1[20] = {' + ','.join(f'0x{x:02x}' for x in bytes.fromhex(rom_sha1)) + '};',
        f'inline constexpr char schema_id[17] = "{schema_sha256[:16]}";',
        f'inline constexpr char metadata_sha256[65] = "{metadata_sha256}";',
    ]
    symbols = {}
    for index, (path, data) in enumerate(sorted(generated.items())):
        symbol = f'generated_{index}'
        symbols[path] = symbol
        lines.append(f'inline constexpr unsigned char {symbol}[] = {{ // {path}')
        for off in range(0, len(data), 24):
            lines.append('    ' + ','.join(f'0x{x:02x}' for x in data[off:off + 24]) + ',')
        lines.append('};')
    lines.append('inline constexpr File files[] = {')
    for entry in ordered:
        source = symbols[entry['path']] if entry['generated'] else 'nullptr'
        lines.append('    {' + json.dumps(entry['path']) +
                     f', {entry["offset"]}u, {entry["size"]}u, 0x{entry["crc32"]:08x}u, {source}' + '},')
    lines.extend(['};', 'inline constexpr std::size_t file_count = sizeof(files) / sizeof(files[0]);',
                  f'inline constexpr uint64_t total_bytes = {sum(e["size"] for e in ordered)}ull;',
                  '} // namespace rom_install_manifest', ''])
    header = '\n'.join(lines).encode('ascii')
    report = dict(status='passed_canonical_byte_equivalence', schema_id=schema_sha256[:16],
                  schema_sha256=schema_sha256, metadata_sha256=metadata_sha256,
                  rom=dict(path=str(args.rom.resolve()), bytes=len(rom), sha1=rom_sha1,
                           sha256=sha256(rom), crc32=f'{rom_crc32:08x}'),
                  decomp_commit=revision, reference_sd=str(args.reference_sd.resolve()),
                  header=str(args.output.resolve()), header_sha256=sha256(header), header_bytes=len(header),
                  file_count=len(ordered), total_bytes=sum(e['size'] for e in ordered),
                  generated_file_count=len(generated), generated_bytes=sum(map(len, generated.values())),
                  rom_span_file_count=len(ordered) - len(generated),
                  rom_span_bytes=sum(e['size'] for e in ordered if not e['generated']),
                  named_file_count=len(named), handle_count=len(handle_rows),
                  archive_member_count=len(archive_rows), archives=archive_report,
                  runtime_RAM_contract='Runtime copies ROM spans in bounded chunks; generated arrays are read-only metadata. No ROM payload is embedded.',
                  sources={str(Path(__file__).resolve()): sha256(Path(__file__).read_bytes()),
                           str(lock_path.resolve()): sha256(lock_path.read_bytes()),
                           str(arm9_path.resolve()): sha256(arm9),
                           str(overlay_path.resolve()): sha256(overlay)}, files=ordered)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--reference-sd', type=Path, required=True)
    parser.add_argument('--decomp', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = generate(args)
    except (ValueError, OSError, subprocess.CalledProcessError, UnicodeError) as exc:
        parser.exit(1, f'ROM manifest rejected: {exc}\n')
    print(json.dumps({key: report[key] for key in ['status', 'schema_id', 'file_count',
        'total_bytes', 'generated_bytes', 'rom_span_bytes', 'header_sha256']}, indent=2))


if __name__ == '__main__':
    main()
