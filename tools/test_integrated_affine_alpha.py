#!/usr/bin/env python3
"""Check the semitransparent-OBJ affine slice against both full CPU PPU APIs."""
import argparse
import hashlib
import json
from pathlib import Path
from devkitpro_tools import arm_tool, sdk_tool
import shutil
import struct
import subprocess
import time
from inspect_integrated_vm import VM
from prepare_integrated_render60 import affine_alpha_source

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/integrated-native'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def sources():
    return {str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),
        ROOT/'tests/integrated_affine_alpha_replay.cpp',ROOT/'source/integrated_native/affine_alpha_objects.inc',
        ROOT/'source/integrated_native/affine_overlay_key.hpp',
        ROOT/'tools/inspect_integrated_vm.py',ROOT/'third_party/sm64ds-3ds/port/ntr/ppu.cpp']}
def verify():
    report=json.loads((OUT/'affine-alpha-gate-latest.json').read_text())
    assert report['status']=='pixel_exact_against_full_CPU_PPU' and report['sources']==sources()
    assert report['derived_ppu_sha256']==hashlib.sha256(affine_alpha_source(
        (ROOT/'third_party/sm64ds-3ds/port/ntr/ppu.cpp').read_text()).encode()).hexdigest()
    assert all(r['stats']['admitted']==192 and r['stats']['refused']==25 for r in report['fixtures'])
    if report.get('fixture_kind') == 'authored_synthetic':
        assert report['fixture_generator_sha256'] == sha(ROOT/'tools/test_integrated_ppu_synthetic.py')
        assert len(report['fixtures']) == 2
        for fixture in report['fixtures']:
            for name, digest in fixture['fixture_files'].items():
                assert sha(Path(fixture['fixture'])/name) == digest
    return report
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--snapshot',type=Path,required=True,action='append')
    p.add_argument('--elf',type=Path,required=True,action='append')
    a=p.parse_args();assert len(a.snapshot)==len(a.elf)
    directory=OUT/('affine-alpha-'+str(time.time_ns()));directory.mkdir()
    port=ROOT/'third_party/sm64ds-3ds/port'
    source=directory/'ppu-affine-alpha.cpp'
    source.write_text(affine_alpha_source((port/'ntr/ppu.cpp').read_text()))
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    exe=directory/'replay'
    command=[compiler,'-std=c++17','-O2','-g','-D__3DS__','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-fno-omit-frame-pointer','-I'+str(port/'ntr/include'),'-I'+str(ROOT/'source/integrated_native'),str(ROOT/'tests/integrated_affine_alpha_replay.cpp'),
        str(source),str(port/'ntr/vram_banks.cpp'),str(port/'ntr/bitmap.cpp'),'-o',str(exe)]
    built=subprocess.run(command,capture_output=True,text=True)
    (directory/'build.log').write_text(built.stdout+built.stderr);built.check_returncode()
    fixtures=[]
    for n,(snapshot,elf) in enumerate(zip(a.snapshot,a.elf)):
        vm=VM(snapshot);anchors=json.loads(snapshot.with_suffix('.anchors.json').read_text())['anchors']
        symbols={}
        for line in subprocess.check_output([arm_tool('nm'),str(elf)],text=True).splitlines():
            fields=line.split()
            if len(fields)==3:
                try:symbols[fields[2]]=int(fields[0],16)
                except ValueError:pass
        assert anchors['sm64ds_render60_source_epoch']==symbols['sm64ds_render60_source_epoch']
        assert anchors['data_0208751c']==symbols['data_0208751c']
        bias=anchors['data_0209f394']-symbols['data_0209f394']
        regions=struct.unpack('<8I',vm.read(symbols['_ZN3ntr8g_regionE']+bias,32))
        fixture=directory/('fixture-'+str(n));fixture.mkdir()
        for i,size in [(4,0x2000),(5,0x800),(6,0x900000),(7,0x800)]:
            (fixture/f'region-{i}.bin').write_bytes(vm.read(regions[i],size))
        output=fixture/'pixels.bin'
        ran=subprocess.run([str(exe),str(fixture),str(output)],capture_output=True,text=True)
        (fixture/'run.log').write_text(ran.stdout+ran.stderr);ran.check_returncode()
        stats=json.loads(ran.stdout.splitlines()[-1]);assert stats['admitted']==192 and stats['refused']==25
        fixtures.append(dict(snapshot_sha256=sha(snapshot),ELF_sha256=sha(elf),scene=vm.header['scene'],
            epoch=vm.header['epoch'],stats=stats,output_sha256=sha(output),bytes=output.stat().st_size))
    report=dict(status='pixel_exact_against_full_CPU_PPU',sources=sources(),fixtures=fixtures,
        derived_ppu_sha256=sha(source),directory=str(directory.resolve()),ASan=True,UBSan=True,
        limits='Offline final-pixel comparison against cached composition and direct CPU scanout. GPU sampling, texture upload identity, worker lifetime and physical timing require separate validation. Host times are diagnostic only.')
    (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'affine-alpha-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
