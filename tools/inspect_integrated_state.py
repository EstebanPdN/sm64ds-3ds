#!/usr/bin/env python3
"""Validate a private Azahar CST without restoring it or guessing virtual RAM.

The matching Azahar serialization stores cores/kernel/page tables and memory
in one Boost archive compressed after a 256-byte CST header. Decoding this
archive is not the same as decoding its native process virtual address map.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
from pathlib import Path

def sha(raw):return hashlib.sha256(raw).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('state',type=Path)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--keep-archive',action='store_true')
    args=parser.parse_args();raw=args.state.read_bytes()
    assert raw[:4]==b'CST\x1b' and raw[256:260]==b'\x28\xb5\x2f\xfd','Unexpected CST format'
    zstd=ctypes.CDLL(ctypes.util.find_library('zstd'))
    zstd.ZSTD_getFrameContentSize.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    zstd.ZSTD_getFrameContentSize.restype=ctypes.c_ulonglong
    zstd.ZSTD_decompress.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t]
    zstd.ZSTD_decompress.restype=ctypes.c_size_t
    zstd.ZSTD_isError.argtypes=[ctypes.c_size_t];zstd.ZSTD_isError.restype=ctypes.c_uint
    payload=raw[256:];size=zstd.ZSTD_getFrameContentSize(payload,len(payload))
    assert 128*1024*1024<size<1024*1024*1024,'Unbounded/unexpected archive size'
    decoded=ctypes.create_string_buffer(size)
    count=zstd.ZSTD_decompress(decoded,size,payload,len(payload))
    assert not zstd.ZSTD_isError(count) and count==size,'Incomplete ZSTD decode'
    archive=decoded.raw;output=args.output or args.state.with_suffix('.validation.json')
    report=dict(status='complete_archive_validated',format='Azahar CST full-system Boost archive',
        CST_sha256=sha(raw),compressed_bytes=len(raw),archive_sha256=sha(archive),
        decompressed_bytes=len(archive),ELF_sha256=sha(args.elf.read_bytes()),
        complete_zstd_decode=True,native_virtual_RAM_map_decoded=False,
        RAM_modified=False,state_restored=False,physical_hardware=False,
        limits='Whole emulated system archive includes memory and runtime state. Native virtual mapping and causal variable interpretation are not established by decompression.')
    if args.keep_archive:
        target=args.state.with_suffix('.full-system.bin');target.write_bytes(archive)
        report['decoded_archive']=str(target.resolve())
    output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
