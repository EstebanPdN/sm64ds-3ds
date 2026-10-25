#!/usr/bin/env python3
"""Verify one unencrypted integrated CIA and its exact, linked ARM ELF.

Policy: experimental title 000400000F536400, Old 80 MiB, cores 0/1 (New 3DS:
124 MiB, 804 MHz with L2 cache, core 2), 1 MiB stack, direct SD access and the single filesystem
category bit needed to read the console's Home Menu DSP component.
This checks packaging and executable identity, not signatures, bootstrap
correctness, installation, game behavior, or physical hardware performance.

Layouts and ELF packing follow Project_CTR makerom commit
d52c2b0e788c2d085763d7c5dcc4b2edd9ffb7d8, src/{code.c,exheader.h,ncch.h}.
No ROM, save, SD resource, emulator, or network access is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


TITLE_ID = 0x000400000F536400
PREVIEW_TITLE_ID = 0x000400000F646D00
FS_ACCESS = 0x8084  # DirectSdmc, DirectSdmcWrite, CategoryFileSystemTool.
PAGE = 4096
SOURCE = ('https://github.com/3DSGuy/Project_CTR/tree/'
          'd52c2b0e788c2d085763d7c5dcc4b2edd9ffb7d8/makerom/src')
FS_SOURCE = ('https://www.3dbrew.org/w/index.php?title=Filesystem_services&oldid=23724#Archives')
REQUIRED_SERVICES = {'APT:U', 'cfg:u', 'dsp::DSP', 'fs:USER', 'gsp::Gpu',
                     'mcu::HWC', 'gsp::Lcd', 'hid:USER', 'ir:rst', 'ptm:u', 'ptm:sysm'}
# SVC instructions linked by the frozen integrated candidate 277c9139291b55c4.
# In particular 112 supplies the DS identity-alias fallback; 60 is used by
# CRT/NDSP/GPU invariant checks. Both are absent from the preview RSF.
REQUIRED_SVCS = {1, 2, 3, 8, 9, 10, 11, 20, 23, 24, 25, 31, 32, 33,
                 34, 35, 36, 37, 39, 40, 42, 45, 50, 56, 57, 58, 60, 112}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def align(n, boundary=64):
    return (n + boundary - 1) & -boundary


def sha(data):
    return hashlib.sha256(data).hexdigest()


def span(data, offset, size, label):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
            f'{label} outside input')
    return data[offset:offset + size]


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def unpack_code(payload, expected_size, compressed):
    """Bounded reverse LZ decoder; no appended ARM9 data or heuristic fallback."""
    if not compressed:
        require(len(payload) == expected_size, 'Uncompressed .code size mismatch')
        return payload
    require(len(payload) >= 4, 'Truncated .code compression footer')
    extra = u32(payload, len(payload) - 4)
    if extra == 0:
        # makerom BLZ's incompressible representation adds a zero word.
        require(len(payload) == expected_size + 4, 'Raw BLZ .code size mismatch')
        return payload[:-4]
    require(len(payload) >= 8 and len(payload) + extra == expected_size,
            'Compressed .code expanded size mismatch')
    footer = u32(payload, len(payload) - 8)
    header_size, encoded_size = footer >> 24, footer & 0xffffff
    require(8 <= header_size <= 11 and header_size < encoded_size <= len(payload),
            'Invalid .code compression bounds')
    require(all(x == 0xff for x in payload[len(payload) - header_size:-8]),
            'Invalid .code compression padding')
    boundary = len(payload) - encoded_size
    read, write = len(payload) - header_size, expected_size
    output = bytearray(expected_size)
    output[:boundary] = payload[:boundary]
    while write > boundary:
        require(read > boundary, 'Truncated .code flags')
        read -= 1
        flags = payload[read]
        for bit in range(7, -1, -1):
            if write == boundary:
                break
            if flags & (1 << bit):
                require(read - boundary >= 2, 'Truncated .code reference')
                token = payload[read - 1] << 8 | payload[read - 2]
                read -= 2
                count, distance = (token >> 12) + 3, (token & 0xfff) + 3
                require(count <= write - boundary and write - 1 + distance < expected_size,
                        'Invalid .code reference')
                for _ in range(count):
                    write -= 1
                    output[write] = output[write + distance]
            else:
                require(read > boundary, 'Truncated .code literal')
                read -= 1
                write -= 1
                output[write] = payload[read]
    require(read == boundary, 'Unused compressed .code bytes')
    return bytes(output)


def elf_image(raw):
    """Reconstruct makerom's padded text/rodata/data, excluding BSS/debug/REL."""
    require(len(raw) >= 52 and raw[:7] == b'\x7fELF\x01\x01\x01',
            'Expected ELF32 little endian')
    require(struct.unpack_from('<HHI', raw, 16) == (2, 40, 1),
            'Expected linked ARM executable')
    entry, phoff = struct.unpack_from('<II', raw, 24)
    phsize, phnum = struct.unpack_from('<HH', raw, 42)
    require(phsize == 32, 'Unsupported ELF program header size')
    span(raw, phoff, phnum * phsize, 'ELF program headers')
    loads = {}
    for i in range(phnum):
        kind, off, addr, _, file_n, mem_n, flags, _ = struct.unpack_from(
            '<8I', raw, phoff + i * phsize)
        if kind != 1:
            continue
        require(flags in (5, 4, 6) and flags not in loads,
                'Expected exactly one text, rodata and data PT_LOAD')
        require(0 < file_n <= mem_n and addr % PAGE == 0,
                'Unsupported ELF PT_LOAD size or alignment')
        loads[flags] = dict(address=addr, size=file_n, memory_size=mem_n,
                            pages=align(file_n, PAGE) // PAGE,
                            payload=span(raw, off, file_n, 'ELF load'))
    require(set(loads) == {5, 4, 6}, 'Missing ELF PT_LOAD')
    require(entry == loads[5]['address'], 'ELF entry is not the text start')
    chunks, segments = [], []
    for name, flags in (('text', 5), ('rodata', 4), ('data', 6)):
        row = loads[flags]
        payload = row['payload']
        chunks.append(payload + bytes(row['pages'] * PAGE - len(payload)))
        segments.append(dict(name=name, **{k: v for k, v in row.items() if k != 'payload'},
                             sha256=sha(payload)))
    require(segments[0]['address'] + segments[0]['pages'] * PAGE == segments[1]['address']
            and segments[1]['address'] + segments[1]['pages'] * PAGE == segments[2]['address'],
            'ELF virtual segment layout is not contiguous')
    return b''.join(chunks), segments, loads[6]['memory_size'] - loads[6]['size']


def code_identity(payload, ex, raw_elf):
    expected, segments, bss = elf_image(raw_elf)
    for offset, row in zip((0x10, 0x20, 0x30), segments):
        actual = struct.unpack_from('<III', ex, offset)
        require(actual == (row['address'], row['pages'], row['size']),
                f"Exheader {row['name']} differs from ELF PT_LOAD")
    require(u32(ex, 0x3c) == bss, 'Exheader BSS differs from ELF')
    decoded = unpack_code(payload, len(expected), bool(ex[0x0d] & 1))
    require(decoded == expected, '.code bytes differ from the exact ELF')
    return dict(matched=True, compressed=bool(ex[0x0d] & 1),
                packed_bytes=len(payload), expanded_bytes=len(decoded),
                expanded_sha256=sha(decoded), expected_sha256=sha(expected),
                bss_bytes=bss, segments=segments)


def profile(ex):
    require(len(ex) == 0x400, 'Truncated extended header')
    require(struct.unpack_from('<Q', ex, 0x200)[0] == TITLE_ID,
            'Exheader title ID mismatch')
    require(ex[0x0d] & 2, 'CIA is not marked for SD')
    # New 3DS profile (console_profile.hpp): flag1 = L2 cache | 804 MHz,
    # flag2 = 124 MB application memory. An Old 3DS ignores both bytes.
    require(ex[0x20c] == 3 and ex[0x20d] == 1,
            'Expected New 3DS 124MB, 804MHz, L2 cache')
    ideal, affinity, mode = ex[0x20e] & 3, ex[0x20e] >> 2 & 3, ex[0x20e] >> 4
    # APPMEMTYPE 3 = 80 MB; 96 MB (2) leaves HOME Menu without SYSTEM memory.
    require((ideal, affinity, mode) == (0, 3, 3),
            'Expected ideal core 0, affinity 0/1, Old 80MB')
    require(u32(ex, 0x1c) == 0x100000, 'Expected 1 MiB stack')
    fs = int.from_bytes(ex[0x248:0x24f], 'little')
    # Title access archive 0x2345678A accepts any one bit of 0x1005;
    # CategoryFileSystemTool is the one selected bit for the local DSP reader.
    # Raw NAND archive permissions and FS Debug are not requested.
    require(fs == FS_ACCESS,
            'Expected DirectSdmc + DirectSdmcWrite + CategoryFileSystemTool (0x8084)')
    require(u32(ex, 0x3f0) == 0x300, 'Unexpected ARM9 mount permissions')
    require(struct.unpack_from('<Q', ex, 0x1c0)[0] == 0,
            'Unexpected title save archive')
    services = [ex[p:p + 8].rstrip(b'\0').decode('ascii') for p in range(0x250, 0x360, 8)]
    services = [s for s in services if s]
    require(len(services) == len(set(services)), 'Duplicate service permission')
    require(REQUIRED_SERVICES <= set(services),
            'Missing runtime services: ' + ', '.join(sorted(REQUIRED_SERVICES - set(services))))
    descriptors = struct.unpack_from('<28I', ex, 0x370)
    other = [x for x in descriptors if x >> 23 == 0x1fe]
    require(len(other) == 1 and (other[0] >> 8 & 15) == 1,
            'Expected Application memory type')
    require(other[0] & 0x2000, 'New 3DS profile must request core 2 for the presentation worker')
    # makerom IORegisterMapping range uses two static-map descriptors, with
    # the end page exclusive; the low 20 bits are the physical page index.
    ranges, index = [], 0
    while index < len(descriptors):
        start = descriptors[index]
        if start >> 21 != 0x7fc:
            index += 1
            continue
        require(index + 1 < len(descriptors) and descriptors[index + 1] >> 21 == 0x7fc,
                'Unpaired kernel range mapping')
        end = descriptors[index + 1]
        lo, hi = (start & 0xfffff) << 12, (end & 0xfffff) << 12
        require(lo < hi, 'Invalid kernel range mapping')
        ranges.append(dict(start=lo, end_exclusive=hi, read_only=bool(start & 0x100000),
                           io=not bool(end & 0x100000)))
        index += 2
    dsp_mapping = any(row['start'] <= 0x1ff00000 and row['end_exclusive'] >= 0x1ff80000
                      and not row['read_only'] and row['io'] for row in ranges)
    require(dsp_mapping, 'Missing read/write DSP RAM mapping 1ff00000-1ff7ffff')
    svcs = sorted({group * 24 + bit for word in descriptors if word >> 28 == 0xf
                   and word >> 27 != 0x1f for group in [(word >> 24) & 7]
                   for bit in range(24) if word & (1 << bit)})
    require(REQUIRED_SVCS <= set(svcs),
            'Missing integrated SVC permissions: ' + ', '.join(map(str, sorted(REQUIRED_SVCS - set(svcs)))))
    return dict(system_mode='80MB', system_mode_value=mode, system_mode_ext='124MB',
                cpu_speed='804MHz', l2_cache=True, memory_type='Application',
                ideal_processor=ideal, affinity_mask=affinity, stack_bytes=u32(ex, 0x1c),
                priority=ex[0x20f], filesystem_access=f'0x{fs:x}', services=services,
                filesystem_permissions=['DirectSdmc', 'DirectSdmcWrite', 'CategoryFileSystemTool'],
                arm9_access='0x300', raw_nand_mount_requested=False,
                special_memory_arrange=bool(other[0] & 0x1000),
                can_access_core2=True, dsp_ram_mapping=True, memory_mappings=ranges, svcs=svcs,
                kernel_descriptors=[f'{word:08x}' for word in descriptors])


def cia_parts(data):
    require(len(data) >= 0x2020, 'Truncated CIA header')
    head, kind, version, cert_n, ticket_n, tmd_n, meta_n, content_n = struct.unpack_from('<IHHIIIIQ', data)
    require((head, kind, version) == (0x2020, 0, 0), 'Unexpected CIA header')
    ticket_at = align(align(head) + cert_n)
    tmd_at = align(ticket_at + ticket_n)
    content_at = align(tmd_at + tmd_n)
    require(align(content_at + content_n) + meta_n == len(data), 'CIA size/alignment mismatch')
    require(data[0x20] == 0x80 and not any(data[0x21:0x2020]), 'Expected only content index 0')
    ticket = span(data, ticket_at, ticket_n, 'Ticket')
    tmd = span(data, tmd_at, tmd_n, 'TMD')
    require(len(ticket) >= 0x1e4 and len(tmd) == 0xb34, 'Unexpected Ticket/TMD size')
    require(struct.unpack_from('>I', ticket)[0] == 0x10004
            and struct.unpack_from('>I', tmd)[0] == 0x10004, 'Unexpected signature type')
    require(struct.unpack_from('>Q', ticket, 0x1dc)[0] == TITLE_ID
            and struct.unpack_from('>Q', tmd, 0x18c)[0] == TITLE_ID, 'CIA title ID mismatch')
    require(struct.unpack_from('>HH', tmd, 0x1de) == (1, 0), 'Expected one boot content')
    require(sha(tmd[0x204:0xb04]) == tmd[0x1e4:0x204].hex(), 'TMD index hash mismatch')
    require(struct.unpack_from('>HH', tmd, 0x204) == (0, 1), 'TMD index mismatch')
    require(sha(tmd[0xb04:0xb34]) == tmd[0x208:0x228].hex(), 'TMD chunk hash mismatch')
    content_id, index, flags, length = struct.unpack_from('>IHHQ', tmd, 0xb04)
    require(index == 0 and not flags & 1 and length == content_n, 'Unexpected CIA content')
    cxi = span(data, content_at, length, 'NCCH')
    require(sha(cxi) == tmd[0xb14:0xb34].hex(), 'CIA content hash mismatch')
    require(len(cxi) >= 0xa00 and cxi[0x100:0x104] == b'NCCH', 'Missing executable NCCH')
    require(cxi[0x18e] <= 8, 'Unsupported NCCH media unit')
    unit = 0x200 << cxi[0x18e]
    require(u32(cxi, 0x104) * unit == len(cxi), 'NCCH size mismatch')
    require(struct.unpack_from('<Q', cxi, 0x108)[0] == TITLE_ID
            and struct.unpack_from('<Q', cxi, 0x118)[0] == TITLE_ID, 'NCCH title ID mismatch')
    require(cxi[0x18c] == 1 and cxi[0x18f] & 4, 'Expected unencrypted CTR NCCH')
    require(u32(cxi, 0x180) == 0x400, 'Unexpected exheader size')
    ex = cxi[0x200:0x600]
    require(sha(ex) == cxi[0x160:0x180].hex(), 'Exheader hash mismatch')
    require(u32(cxi, 0x1b0) == u32(cxi, 0x1b4) == 0, 'Unexpected RomFS assets')
    logo_at, logo_n = u32(cxi, 0x198) * unit, u32(cxi, 0x19c) * unit
    if logo_n:
        require(sha(span(cxi, logo_at, logo_n, 'Logo')) == cxi[0x130:0x150].hex(),
                'NCCH logo hash mismatch')
    exe_at, exe_n, hash_n = (u32(cxi, p) * unit for p in (0x1a0, 0x1a4, 0x1a8))
    require(exe_at >= 0xa00 and 0x200 <= hash_n <= exe_n, 'Invalid ExeFS bounds')
    exe = span(cxi, exe_at, exe_n, 'ExeFS')
    require(sha(exe[:hash_n]) == cxi[0x1c0:0x1e0].hex(), 'ExeFS header hash mismatch')
    files, ranges = {}, []
    for i in range(10):
        name, offset, size = struct.unpack_from('<8sII', exe, i * 16)
        if not name.rstrip(b'\0'):
            continue
        name = name.rstrip(b'\0').decode('ascii')
        require(name not in files and offset % 0x200 == 0, 'Duplicate/unaligned ExeFS file')
        payload = span(exe, 0x200 + offset, size, 'ExeFS file')
        require(sha(payload) == exe[0x1e0 - i * 32:0x200 - i * 32].hex(),
                f'ExeFS hash mismatch: {name}')
        require(all(offset + size <= a or b <= offset for a, b in ranges), 'Overlapping ExeFS files')
        ranges.append((offset, offset + size))
        files[name] = payload
    require({'.code', 'icon', 'banner'} <= set(files) <= {'.code', 'icon', 'banner', 'logo'},
            'Unexpected ExeFS files')
    icon = files['icon']
    require(len(icon) == 0x36c0 and icon[:4] == b'SMDH', 'Invalid SMDH')
    require(u32(icon, 0x2028) & 1 and not u32(icon, 0x2028) & 0x1000,
            'SMDH hidden or New-only')
    require(u32(icon, 0x2018) == 0x7fffffff, 'SMDH is not region free')
    return ex, files, dict(content_id=content_id, content_sha256=sha(cxi),
                           title_version=struct.unpack_from('>H', tmd, 0x1dc)[0])


def verify(cia, elf, expected_elf_sha256=None):
    raw, raw_elf = Path(cia).read_bytes(), Path(elf).read_bytes()
    elf_sha = sha(raw_elf)
    if expected_elf_sha256 is not None:
        require(re.fullmatch('[0-9a-fA-F]{64}', expected_elf_sha256) is not None,
                'Expected ELF SHA-256 must have 64 hexadecimal characters')
        require(elf_sha == expected_elf_sha256.lower(), 'ELF differs from frozen expected hash')
    ex, files, content = cia_parts(raw)
    return dict(status='integrated_cia_structurally_verified_exact_ELF',
                cia=str(Path(cia).resolve()), bytes=len(raw), sha256=sha(raw),
                elf=str(Path(elf).resolve()), ELF_sha256=elf_sha,
                verifier_sha256=sha(Path(__file__).read_bytes()),
                title_id=f'{TITLE_ID:016X}', distinct_from_preview=TITLE_ID != PREVIEW_TITLE_ID,
                profile=profile(ex), code=code_identity(files['.code'], ex, raw_elf),
                exefs={name: dict(bytes=len(payload), sha256=sha(payload))
                       for name, payload in files.items()}, **content,
                romfs_present=False, rom_bytes_read=False, emulator_launched=False,
                physical_installation_tested=False, source_layout=SOURCE,
                title_content_permission_source=FS_SOURCE,
                limits=['Integrity and requested packaging policy only; retail signatures not verified.',
                        'Matching .code and ELF does not prove ROM bootstrap or game behavior.',
                        'Installation, SD I/O, audio, RAM sufficiency and FPS on physical Old3DS remain unverified.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cia', type=Path)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--expected-elf-sha256')
    parser.add_argument('--report', type=Path, help='New report path; existing files are never replaced')
    args = parser.parse_args()
    try:
        result = verify(args.cia, args.elf, args.expected_elf_sha256)
        output = json.dumps(result, indent=2) + '\n'
        if args.report:
            with args.report.open('x') as stream:
                stream.write(output)
    except (ValueError, struct.error, UnicodeError, OSError) as error:
        parser.exit(1, f'Integrated CIA verification failed: {error}\n')
    print(output, end='')


if __name__ == '__main__':
    main()
