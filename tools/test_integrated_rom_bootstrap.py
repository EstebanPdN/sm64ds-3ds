#!/usr/bin/env python3
"""Gate the exact streaming ROM installer; keep private fixtures off deliverables."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/integrated-native'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def identity(manifest):
    files=[Path(__file__),ROOT/'source/integrated_native/boot_allocator.hpp',ROOT/'source/integrated_native/rom_bootstrap.hpp',
           ROOT/'tests/test_integrated_rom_bootstrap.cpp',manifest]
    return {str(p.resolve()):sha(p) for p in files}

def check(rom,reference,manifest,verify_only=False):
    expected=identity(manifest)
    receipt=OUT/'rom-bootstrap/host-gate.json'
    if receipt.is_file():
        prior=json.loads(receipt.read_text())
        if prior['sources']==expected and prior['ROM_sha256']==sha(rom):
            assert prior['status']=='passed' and prior['ASan'] and prior['UBSan']
            return prior
    assert not verify_only,'ROM installer sources changed since the host gate'
    folder=OUT/('rom-bootstrap-host-'+str(time.time_ns()));folder.mkdir()
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    test=folder/'test'
    command=[compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
             '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
             '-I'+str(ROOT/'source/integrated_native'),'-I'+str(manifest.parent),
             str(ROOT/'tests/test_integrated_rom_bootstrap.cpp'),'-o',str(test)]
    built=subprocess.run(command,capture_output=True,text=True)
    (folder/'build.log').write_text(built.stdout+built.stderr);built.check_returncode()
    ran=subprocess.run([str(test),'--test',str(rom),str(folder/'fixtures'),
                        str(reference/'sm64ds/repo')],capture_output=True,text=True)
    (folder/'run.log').write_text(ran.stdout+ran.stderr);ran.check_returncode()
    stats=json.loads(ran.stdout.splitlines()[-1]);assert stats['status']=='passed'
    report=dict(status='passed',ASan=True,UBSan=True,sources=expected,ROM_sha256=sha(rom),
                tests=stats,directory=str(folder),
                scope='Exact installer: first boot, reuse, corruption, interruption and complete byte comparison; not physical SD durability')
    (folder/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    receipt.write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--reference-sd',type=Path,required=True)
    p.add_argument('--manifest',type=Path,default=OUT/'rom-bootstrap/rom_install_manifest.hpp')
    a=p.parse_args();r=check(a.rom.resolve(),a.reference_sd.resolve(),a.manifest.resolve())
    print(json.dumps({k:r[k] for k in ['status','tests','directory']},indent=2))
