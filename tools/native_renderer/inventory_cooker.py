#!/usr/bin/env python3
"""Measure host conversion admission, without writing extracted resources.

A successful conversion admits immutable resources only, not a live actor or
scene. Each refused model records its first blocking contract, not every one.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import time

try:
    from .cook_model import ROOT, cook, NeedsNativeFeature, SourceError
except ImportError:
    from cook_model import ROOT, cook, NeedsNativeFeature, SourceError


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-renderer/cooker-admission.json')
    parser.add_argument('--reader',type=Path,help='Independently validate each admitted bank with the C++ reader')
    args=parser.parse_args()
    if not args.output.resolve().is_relative_to((ROOT/'build').resolve()):parser.error('Private metadata stays under ignored build/')
    import ndspy.rom,ndspy.lz10
    source=args.rom.read_bytes();sha1=hashlib.sha1(source).hexdigest()
    if sha1!='1367529f2cb23e76ef295cb1727333ae8f0a6cd7':parser.error('Expected supported EU revision 0')
    rom=ndspy.rom.NintendoDSRom(source);records=[];counts=Counter();started=time.monotonic()
    for file_id,packed in enumerate(rom.files):
        name=rom.filenames.filenameOf(file_id)
        if not name or not name.lower().endswith('.bmd'):continue
        try:
            if packed.startswith(b'LZ77'):
                if len(packed)<8 or packed[4]!=0x10 or not 0<int.from_bytes(packed[5:8],'little')<=16*1024*1024:
                    raise SourceError('Invalid or oversized compressed model')
                data=ndspy.lz10.decompress(packed[4:])
            else:data=packed
            native,report,_=cook(data,name)
            reader_checked=False
            if args.reader:
                args.output.parent.mkdir(parents=True,exist_ok=True)
                with tempfile.TemporaryDirectory(prefix='native-admission-',dir=args.output.parent) as temporary:
                    path=Path(temporary)/'resource.nmd';path.write_bytes(native)
                    subprocess.run([str(args.reader.resolve(strict=True)),str(path)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                reader_checked=True
            records.append(dict(file_id=file_id,name=name,status='resource_admitted',cpp_reader_validated=reader_checked,
                native_bytes=len(native),meshes=report['meshes'],images=report['images'],parts=report['parts'],
                matrices=report['matrices'],game_behaviour_ready=False))
            counts['resource_admitted']+=1
        except NeedsNativeFeature as error:
            records.append(dict(file_id=file_id,name=name,status='native_feature_pending',first_blocker=str(error)))
            counts[str(error)]+=1
        except (SourceError,ValueError,struct.error) as error:
            records.append(dict(file_id=file_id,name=name,status='source_or_contract_refused',first_blocker=str(error)))
            counts['source_or_contract_refused: '+str(error)]+=1
    report=dict(scope='Immutable resource admission only; first blocker per model, no game/visual/hardware acceptance',
        rom_sha1=sha1,models=len(records),resource_admitted=counts['resource_admitted'],
        first_blockers=dict(counts),host_elapsed_seconds=time.monotonic()-started,records=records,
        cpp_reader_sha256=hashlib.sha256(args.reader.read_bytes()).hexdigest() if args.reader else None,
        game_behaviour_ready=False)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2))


if __name__=='__main__':main()
