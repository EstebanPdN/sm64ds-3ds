"""Bounded ROM-to-SM64DS converter, independent from GUI and game code."""
from __future__ import annotations
import base64
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import threading
import zlib

CHUNK = 65536
HEADER = struct.Struct('<8sIIQ16s64s24s')
MAGIC = b'SM64DSP1'
SCHEMA = '1f256505ddfbac65'
METADATA = '043895196e57eb8fef46fe80487dd570761f4f58db0b118da3fa350fb4125764'


class Cancelled(Exception):
    """The user cancelled before the output was committed."""


def recipes():
    data = json.loads(zlib.decompress(Path(__file__).with_name('recipes.json.zlib').read_bytes()))
    if (data['schema_id'] != SCHEMA or data['metadata_sha256'] != METADATA or
            data['file_count'] != 2450 or data['total_bytes'] != 15686745):
        raise ValueError('Invalid bundled extraction recipes')
    return data


def convert(source, destination, progress=None, cancel=None):
    """Validate ROM, write sibling temporary, verify all output, then atomic replace.

    Existing output is retained on cancellation or errors. The caller must ask
    before replacing an existing output. The input must never be the output.
    """
    source, destination = Path(source), Path(destination)
    progress = progress or (lambda phase, done, total: None)
    cancel = cancel or threading.Event()
    def check():
        if cancel.is_set():
            raise Cancelled('Conversion cancelled')
    check()
    if source.resolve() == destination.resolve() or (destination.exists() and os.path.samefile(source, destination)):
        raise ValueError('The output must be different from the ROM')
    if source.suffix.lower() != '.nds':
        raise ValueError('Choose a Nintendo DS ROM (.nds)')
    if destination.suffix.lower() != '.sm64ds':
        raise ValueError('The output must end in .sm64ds')
    manifest = recipes()
    temporary = None
    with source.open('rb') as rom:
        size = os.fstat(rom.fileno()).st_size
        if size != manifest['rom']['bytes']:
            raise ValueError('Unsupported ROM size. Use the original European SM64DS ROM (ASMP, revision 0).')
        digest, sha256, crc = hashlib.sha1(), hashlib.sha256(), 0
        done = 0
        while True:
            check()
            block = rom.read(CHUNK)
            if not block:
                break
            digest.update(block)
            sha256.update(block)
            crc = zlib.crc32(block, crc)
            done += len(block)
            progress('Checking ROM', done, size)
        if (digest.hexdigest() != manifest['rom']['sha1'] or
                sha256.hexdigest() != manifest['rom']['sha256'] or
                f'{crc & 0xffffffff:08x}' != manifest['rom']['crc32']):
            raise ValueError('Unsupported or damaged ROM. Use the original European SM64DS ROM (ASMP, revision 0).')
        check()
        descriptor, name = tempfile.mkstemp(prefix='.' + destination.name + '.', suffix='.part', dir=destination.parent)
        temporary = Path(name)
        try:
            with os.fdopen(descriptor, 'w+b') as output:
                output.write(HEADER.pack(MAGIC, 1, manifest['file_count'], manifest['total_bytes'],
                                         SCHEMA.encode('ascii'), METADATA.encode('ascii'), bytes(24)))
                done = 0
                for entry in manifest['files']:
                    check()
                    generated = base64.b64decode(entry['generated'], validate=True) if 'generated' in entry else None
                    if generated is None:
                        rom.seek(entry['offset'])
                    remaining, offset, crc = entry['size'], 0, 0
                    file_digest = hashlib.sha256()
                    while remaining:
                        check()
                        block = generated[offset:offset + min(CHUNK, remaining)] if generated is not None else rom.read(min(CHUNK, remaining))
                        if not block:
                            raise ValueError('ROM became truncated while reading')
                        output.write(block)
                        crc = zlib.crc32(block, crc)
                        file_digest.update(block)
                        remaining -= len(block)
                        offset += len(block)
                        done += len(block)
                        progress('Creating .sm64ds', done, manifest['total_bytes'])
                    if crc & 0xffffffff != entry['crc32'] or file_digest.hexdigest() != entry['sha256']:
                        raise ValueError('ROM changed or output recipe mismatch: ' + entry['path'])
                output.flush()
                os.fsync(output.fileno())
                output.seek(HEADER.size)
                done = 0
                for entry in manifest['files']:
                    remaining, crc = entry['size'], 0
                    file_digest = hashlib.sha256()
                    while remaining:
                        check()
                        block = output.read(min(CHUNK, remaining))
                        if not block:
                            raise ValueError('Output verification found a truncated file')
                        remaining -= len(block)
                        done += len(block)
                        crc = zlib.crc32(block, crc)
                        file_digest.update(block)
                        progress('Verifying .sm64ds', done, manifest['total_bytes'])
                    if crc & 0xffffffff != entry['crc32'] or file_digest.hexdigest() != entry['sha256']:
                        raise ValueError('Output verification failed')
                if output.read(1):
                    raise ValueError('Unexpected bytes after output payload')
                check()
            check()
            os.replace(temporary, destination)
            temporary = None
            # Directory sync is best effort on filesystems that do not support it.
            # macOS native GUI can stall opening an SD/save-picker directory after
            # commit. File fsync+atomic replacement are already complete here.
            if os.name == 'posix' and sys.platform != 'darwin':
                try:
                    directory = os.open(destination.parent, os.O_RDONLY)
                    try:
                        os.fsync(directory)
                    finally:
                        os.close(directory)
                except OSError:
                    pass
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    return {'path': str(destination), 'bytes': HEADER.size + manifest['total_bytes'], 'files': manifest['file_count']}
