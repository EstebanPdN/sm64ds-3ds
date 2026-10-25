#!/usr/bin/env python3
"""Verify the unencrypted, single-content CIA produced by package_cia.py.

The CIA/TMD/NCCH/ExeFS layouts follow the makerom source definitions. This
checks integrity and requested packaging policy, not retail signatures,
installation on hardware, game correctness, or frame rate.
"""
import argparse
import hashlib
import json
import pathlib
import struct

TITLE_ID = 0x000400000F646D00


def require(condition, message):
    if not condition:
        raise ValueError(message)


def align(n, boundary=64):
    return (n + boundary - 1) & -boundary


def sha(data):
    return hashlib.sha256(data).digest()


def verify(path, cxi_output=None):
    data = pathlib.Path(path).read_bytes()
    require(len(data) >= 0x2020, 'CIA header is truncated')
    header, kind, version, cert_n, ticket_n, tmd_n, meta_n, content_n = struct.unpack_from('<IHHIIIIQ', data)
    require(header == 0x2020 and kind == 0 and version == 0, 'Unexpected CIA header')
    cert_at = align(header)
    ticket_at = align(cert_at + cert_n)
    tmd_at = align(ticket_at + ticket_n)
    content_at = align(tmd_at + tmd_n)
    meta_at = align(content_at + content_n)
    require(meta_at + meta_n == len(data), 'CIA size or alignment mismatch')
    require(data[0x20] == 0x80 and not any(data[0x21:0x2020]), 'Expected only content index 0')
    ticket = data[ticket_at:ticket_at + ticket_n]
    tmd = data[tmd_at:tmd_at + tmd_n]
    require(len(ticket) >= 0x1e4 and len(tmd) >= 0xb34, 'Ticket or TMD truncated')
    require(struct.unpack_from('>I', ticket)[0] == 0x10004, 'Unexpected ticket signature type')
    require(struct.unpack_from('>I', tmd)[0] == 0x10004, 'Unexpected TMD signature type')
    require(struct.unpack_from('>Q', ticket, 0x1dc)[0] == TITLE_ID, 'Ticket title mismatch')
    require(struct.unpack_from('>Q', tmd, 0x18c)[0] == TITLE_ID, 'TMD title mismatch')
    require(struct.unpack_from('>HH', tmd, 0x1de) == (1, 0), 'Expected one boot content')
    require(sha(tmd[0x204:0xb04]) == tmd[0x1e4:0x204], 'TMD index hash mismatch')
    require(struct.unpack_from('>HH', tmd, 0x204) == (0, 1), 'TMD first index invalid')
    require(sha(tmd[0xb04:0xb34]) == tmd[0x208:0x228], 'TMD chunk table hash mismatch')
    content_id, content_index, content_flags, length = struct.unpack_from('>IHHQ', tmd, 0xb04)
    require(content_index == 0 and not (content_flags & 1), 'Unexpected encrypted content')
    require(length == content_n, 'Content size mismatch')
    cxi = data[content_at:content_at + length]
    require(sha(cxi) == tmd[0xb14:0xb34], 'CIA content SHA-256 mismatch')
    require(cxi[0x100:0x104] == b'NCCH', 'Missing NCCH')
    unit = 0x200 << cxi[0x18e]
    u32 = lambda pos: struct.unpack_from('<I', cxi, pos)[0]
    require(u32(0x104) * unit == len(cxi), 'NCCH size mismatch')
    require(struct.unpack_from('<Q', cxi, 0x118)[0] == TITLE_ID, 'NCCH program ID mismatch')
    require(cxi[0x18c] == 1 and (cxi[0x18f] & 4), 'Expected Old-compatible unencrypted NCCH')
    require(u32(0x180) == 0x400, 'Unexpected extended header size')
    ex = cxi[0x200:0x600]
    require(sha(ex) == cxi[0x160:0x180], 'Extended header hash mismatch')
    require(ex[0x0d] & 2, 'Application is not marked for SD')
    require(ex[0x20c] == 0 and ex[0x20d] == 0 and ex[0x20e] >> 4 == 0,
            'Packaging requests New 3DS exclusive CPU or memory modes')
    fs_access = int.from_bytes(ex[0x248:0x24f], 'little')
    require(fs_access == 0x8080, 'Unexpected filesystem permissions')
    services = [ex[off:off + 8].rstrip(b'\0').decode('ascii') for off in range(0x250, 0x360, 8)]
    services = [value for value in services if value]
    require('ir:rst' in services and 'fs:USER' in services, 'Missing input or SD service')
    require(u32(0x1b0) == 0 and u32(0x1b4) == 0, 'Unexpected RomFS assets')
    logo_at, logo_n = u32(0x198) * unit, u32(0x19c) * unit
    require(sha(cxi[logo_at:logo_at + logo_n]) == cxi[0x130:0x150], 'Logo hash mismatch')
    exe_at, exe_n, hash_n = u32(0x1a0) * unit, u32(0x1a4) * unit, u32(0x1a8) * unit
    require(exe_at + exe_n <= len(cxi), 'ExeFS outside NCCH')
    require(sha(cxi[exe_at:exe_at + hash_n]) == cxi[0x1c0:0x1e0], 'ExeFS header hash mismatch')
    exe = cxi[exe_at:exe_at + exe_n]
    files = {}
    for i in range(10):
        name, offset, size = struct.unpack_from('<8sII', exe, i * 16)
        if not name.rstrip(b'\0'):
            continue
        name = name.rstrip(b'\0').decode('ascii')
        require(name not in files, 'Duplicate ExeFS name')
        require(0x200 + offset + size <= len(exe), 'ExeFS file outside section')
        payload = exe[0x200 + offset:0x200 + offset + size]
        require(sha(payload) == exe[0x1e0 - 32*i:0x200 - 32*i], f'ExeFS hash mismatch: {name}')
        files[name] = {'size': size, 'sha256': sha(payload).hex()}
        if name == 'icon':
            require(payload[:4] == b'SMDH' and len(payload) == 0x36c0, 'Invalid SMDH icon')
            flags = struct.unpack_from('<I', payload, 0x2028)[0]
            require(flags & 1 and flags & 4 and not flags & 0x1000, 'Icon not visible, 3D capable, or Old-compatible')
            require(struct.unpack_from('<I', payload, 0x2018)[0] == 0x7fffffff, 'Icon not region free')
    require(set(files) == {'.code', 'icon', 'banner'}, 'Unexpected embedded files')
    if cxi_output is not None:
        pathlib.Path(cxi_output).write_bytes(cxi)
    return {'status': 'structurally_verified', 'cia': str(path), 'bytes': len(data),
            'sha256': sha(data).hex(), 'title_id': f'{TITLE_ID:016X}',
            'content_id': content_id, 'content_sha256': sha(cxi).hex(), 'exefs': files,
            'sd_path': '/3ds/sm64ds/rom.nds', 'system_mode': '64MB',
            'system_mode_ext': 'Legacy', 'cpu_speed': '268MHz', 'services': services,
            'romfs_present': False, 'physical_installation_tested': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cia', type=pathlib.Path)
    parser.add_argument('--extract-cxi', type=pathlib.Path)
    parser.add_argument('--report', type=pathlib.Path)
    args = parser.parse_args()
    try:
        result = verify(args.cia, args.extract_cxi)
    except (ValueError, struct.error, UnicodeError, OSError) as error:
        parser.exit(1, f'CIA verification failed: {error}\n')
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(text)
    print(text, end='')
