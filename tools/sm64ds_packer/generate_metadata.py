#!/usr/bin/env python3
"""Build distributable extraction recipes; includes filesystem catalogs, no ROM assets."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import zlib


def generate(report_path, header_path, destination):
    report = json.loads(Path(report_path).read_text())
    header_bytes = Path(header_path).read_bytes()
    if hashlib.sha256(header_bytes).hexdigest() != report['header_sha256']:
        raise ValueError('Manifest/header digest mismatch')
    header = header_bytes.decode('ascii')
    generated = {}
    for path, values in re.findall(r'inline constexpr unsigned char generated_\d+\[\] = \{ // ([^\n]+)\n(.*?)\n\};', header, re.S):
        generated[path] = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', values))
    files = []
    for original in report['files']:
        entry = {k: original[k] for k in ('path', 'offset', 'size', 'crc32', 'sha256')}
        if original['generated']:
            data = generated[entry['path']]
            if hashlib.sha256(data).hexdigest() != entry['sha256']:
                raise ValueError('Generated catalog digest mismatch')
            entry['generated'] = base64.b64encode(data).decode('ascii')
        files.append(entry)
    result = {k: report[k] for k in ('schema_id', 'metadata_sha256', 'total_bytes', 'file_count')}
    result['rom'] = {k: report['rom'][k] for k in ('bytes', 'sha1', 'sha256', 'crc32')}
    result['files'] = files
    raw = json.dumps(result, separators=(',', ':'), ensure_ascii=True).encode('ascii')
    Path(destination).write_bytes(zlib.compress(raw, 9))
    print(json.dumps({'metadata_bytes': len(raw), 'compressed_bytes': Path(destination).stat().st_size,
                      'file_count': len(files), 'includes_ROM_assets': False}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('report')
    parser.add_argument('header')
    parser.add_argument('destination')
    args = parser.parse_args()
    generate(args.report, args.header, args.destination)
