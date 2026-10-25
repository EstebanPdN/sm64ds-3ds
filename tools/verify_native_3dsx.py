#!/usr/bin/env python3
"""Prove that a native 3DSX packages the supplied exact diagnostic ELF."""
import argparse
import hashlib
import json
from pathlib import Path
from devkitpro_tools import arm_tool, sdk_tool
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def verify(elf, image):
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix='sm64ds-image-pair-') as directory:
        directory=Path(directory)
        portable=directory/'portable.elf';expected=directory/'expected.3dsx'
        subprocess.run([sys.executable,str(ROOT/'tools/native_3dsx_elf.py'),
            str(elf),str(portable)],check=True,capture_output=True,text=True)
        subprocess.run([sdk_tool('3dsxtool'),str(portable),str(expected)],
            check=True,capture_output=True,text=True)
        actual_hash,expected_hash=digest(image),digest(expected)
        if actual_hash!=expected_hash:
            raise ValueError('3DSX does not package this ELF; build sm64ds-native_3dsx before acquiring RAM')
    return dict(status='PASS',scope=__doc__,ELF_sha256=digest(elf),image_sha256=actual_hash,
        independently_repackaged_image_sha256=expected_hash,game_executed=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf',type=Path);parser.add_argument('image',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args();result=verify(args.elf,args.image)
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

if __name__=='__main__':main()
