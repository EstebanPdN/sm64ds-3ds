#!/usr/bin/env python3
"""Check loading, ROM discovery and bounded DSP parsing before ARM launch."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/integrated-native'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def sources():
    names=['tools/test_integrated_setup.py','source/integrated_native/boot_allocator.hpp','source/integrated_native/loading_screen.hpp',
           'source/integrated_native/loading_screen_3ds.hpp','source/integrated_native/dsp_bootstrap.hpp',
           'source/integrated_native/loading_art.hpp','source/integrated_native/loading_locale.hpp','source/integrated_native/rom_art.hpp',
           'source/integrated_native/rom_discovery.hpp','source/integrated_native/rom_bootstrap.hpp',
           'build/integrated-native/rom-bootstrap/rom_install_manifest.hpp','tests/test_integrated_rom_discovery.cpp',
           'build/integrated-native/rom-bootstrap/rom_art_embedded.hpp','tools/generate_integrated_rom_art.py','build/integrated-native/rom-bootstrap/rom_art_manifest.hpp',
           'tools/generate_integrated_loading_style.py','build/integrated-native/rom-bootstrap/loading_style.hpp',
           'tests/test_integrated_rom_art.cpp','source/integrated_native/setup_power.hpp','tests/test_integrated_setup_power.cpp',
           'source/integrated_native/packed_assets.hpp','source/integrated_native/packed_assets_3ds.hpp',
           'source/integrated_native/packed_builder.hpp','tests/test_integrated_packed_builder.cpp',
           'source/integrated_native/rom_setup_3ds.hpp','tests/test_integrated_loading.cpp',
           'tests/test_integrated_dsp_bootstrap.cpp']
    return {name:sha(ROOT/name) for name in names}

def check(verify_only=False,rom=None):
    expected=sources();receipt=OUT/'setup-host-gate.json'
    if receipt.is_file():
        prior=json.loads(receipt.read_text())
        if prior['sources']==expected and (rom is None or prior['ROM_sha256']==sha(rom)):
            assert prior['status']=='passed' and prior['ASan'] and prior['UBSan']
            return prior
    assert not verify_only,'Loading/DSP sources changed since the host gate'
    assert rom is not None and rom.is_file(),'Exact local ROM needed to check decoded artwork'
    folder=OUT/('setup-host-'+str(time.time_ns()));folder.mkdir()
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    results={}
    for name in ['loading','dsp_bootstrap','rom_art','rom_discovery','setup_power','packed_builder']:
        test=folder/('test-'+name)
        command=[compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
                 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
                 '-I'+str(ROOT/'source/integrated_native'),
                 '-I'+str(OUT/'rom-bootstrap'),
                 *(['-DSM64DS_TEST_ROM_ART=1'] if name=='loading' else []),
                 str(ROOT/'tests'/('test_integrated_'+name+'.cpp')),'-o',str(test)]
        built=subprocess.run(command,capture_output=True,text=True)
        (folder/(name+'-build.log')).write_text(built.stdout+built.stderr);built.check_returncode()
        reference=Path(json.loads((OUT/'rom-bootstrap/manifest-report.json').read_text())['reference_sd'])/'sm64ds/repo'
        arguments=[str(rom),str(reference)] if name=='rom_discovery' else \
            [str(folder/'previews'),str(rom)] if name=='loading' else \
            [str(rom),str(OUT/'loading-authentic/rom-art-reference')] if name=='rom_art' else []
        if name=='packed_builder':
            import struct
            manifest=json.loads((OUT/'rom-bootstrap/manifest-report.json').read_text())
            golden=folder/'golden.sm64ds';private_work=folder/'builder-work';private_work.mkdir()
            with golden.open('wb') as output:
                output.write(struct.pack('<8sIIQ16s64s24s',b'SM64DSP1',1,manifest['file_count'],manifest['total_bytes'],
                    manifest['schema_id'].encode(),manifest['metadata_sha256'].encode(),bytes(24)))
                for entry in manifest['files']:
                    with (reference/entry['path']).open('rb') as source:
                        shutil.copyfileobj(source,output,length=65536)
            arguments=[str(rom),str(golden),str(reference),str(private_work)]
        command=[str(test),*arguments]
        ran=subprocess.run(command,capture_output=True,text=True)
        (folder/(name+'-test.log')).write_text(ran.stdout+ran.stderr);ran.check_returncode()
        result=json.loads(ran.stdout.splitlines()[-1]);assert result['status']=='passed'
        results[name]=result
    report=dict(status='passed',sources=expected,ASan=True,UBSan=True,tests=results,directory=str(folder),
        ROM_sha256=sha(rom),
        scope='Host bounds, allocation/lifetime and component parser tests; ARM linking and physical memory peaks remain separate')
    (folder/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    receipt.write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':print(json.dumps(check(rom=ROOT/'build/roms/europe.nds'),indent=2))
