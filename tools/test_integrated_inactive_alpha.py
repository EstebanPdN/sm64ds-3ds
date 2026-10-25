#!/usr/bin/env python3
"""Compare original and optimized PPU output using captured private 2D memory."""
import argparse
import hashlib
import json
from pathlib import Path
from devkitpro_tools import arm_tool, sdk_tool
import shutil
import subprocess
import time
from inspect_integrated_vm import VM
from prepare_integrated_render60 import optimize_inactive_alpha

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/integrated-native'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def sources():
    return {str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'tests/integrated_ppu_replay.cpp',Path(__file__),
        ROOT/'tools/inspect_integrated_vm.py',ROOT/'third_party/sm64ds-3ds/port/ntr/ppu.cpp']}
def verify():
    report=json.loads((OUT/'ppu-alpha-gate-latest.json').read_text())
    assert report['status']=='pixel_exact_against_pinned_original' and report['sources']==sources()
    raw=(ROOT/'third_party/sm64ds-3ds/port/ntr/ppu.cpp').read_text()
    assert report['optimized_ppu_sha256']==hashlib.sha256(optimize_inactive_alpha(raw).encode()).hexdigest()
    if report.get('fixture_kind') == 'authored_synthetic':
        assert report['fixture_generator_sha256'] == sha(ROOT/'tools/test_integrated_ppu_synthetic.py')
        assert len(report['fixtures']) == 2
        for fixture in report['fixtures']:
            for name, digest in fixture['fixture_files'].items():
                assert sha(Path(fixture['fixture'])/name) == digest
    return report
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--snapshot',type=Path,required=True)
    p.add_argument('--elf',type=Path,required=True);a=p.parse_args();vm=VM(a.snapshot)
    anchors=json.loads(a.snapshot.with_suffix('.anchors.json').read_text())['anchors'];symbols={}
    for line in subprocess.check_output([arm_tool('nm'),str(a.elf)],text=True).splitlines():
        parts=line.split()
        if len(parts)==3:
            try:symbols[parts[2]]=int(parts[0],16)
            except ValueError:pass
    assert anchors['sm64ds_render60_source_epoch']==symbols['sm64ds_render60_source_epoch']
    assert anchors['data_0208751c']==symbols['data_0208751c']
    bias=anchors['data_0209f394']-symbols['data_0209f394']
    import struct
    regions=struct.unpack('<8I',vm.read(symbols['_ZN3ntr8g_regionE']+bias,32))
    directory=OUT/('ppu-alpha-'+str(time.time_ns()));directory.mkdir()
    fixture=directory/'fixture';fixture.mkdir()
    for i,size in [(4,0x2000),(5,0x800),(6,0x900000),(7,0x800)]:
        (fixture/f'region-{i}.bin').write_bytes(vm.read(regions[i],size))
    port=ROOT/'third_party/sm64ds-3ds/port';original=port/'ntr/ppu.cpp'
    optimized=directory/'ppu-optimized.cpp';optimized.write_text(optimize_inactive_alpha(original.read_text()))
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    results={}
    for name,source in [('original',original),('optimized',optimized)]:
        exe=directory/name;output=directory/(name+'.pixels')
        command=[compiler,'-std=c++17','-O2','-g','-D__3DS__','-fsanitize=address,undefined',
            '-fno-omit-frame-pointer','-I'+str(port/'ntr/include'),str(ROOT/'tests/integrated_ppu_replay.cpp'),
            str(source),str(port/'ntr/vram_banks.cpp'),str(port/'ntr/bitmap.cpp'),'-o',str(exe)]
        built=subprocess.run(command,capture_output=True,text=True)
        (directory/(name+'-build.log')).write_text(built.stdout+built.stderr);built.check_returncode()
        ran=subprocess.run([str(exe),str(fixture),str(output)],capture_output=True,text=True)
        (directory/(name+'-run.log')).write_text(ran.stdout+ran.stderr);ran.check_returncode()
        results[name]=dict(output_sha256=sha(output),bytes=output.stat().st_size,log=ran.stdout)
    assert (directory/'original.pixels').read_bytes()==(directory/'optimized.pixels').read_bytes(),'PPU output differs'
    report=dict(status='pixel_exact_against_pinned_original',cases=64,ASan=True,UBSan=True,
        snapshot_sha256=sha(a.snapshot),ELF_sha256=sha(a.elf),captured_scene=vm.header['scene'],
        captured_epoch=vm.header['epoch'],optimized_ppu_sha256=sha(optimized),sources=sources(),results=results,
        directory=str(directory.resolve()),limits='Offline 2D equivalence, including active/inactive alpha and changed affine/scroll registers. Host times are diagnostic only; no physical timing or LCD acceptance.')
    (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'ppu-alpha-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
