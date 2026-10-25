#!/usr/bin/env python3
"""Exercise the pinned SD save HAL and retail menu transactions on host files.

Only generated test files in private build/temp directories are written.
No personal save, ROM, game state, or emulator is modified or restored.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
VENDOR=ROOT/'third_party/sm64ds-3ds'
REPO=VENDOR/'sm64ds-decomp'
OUT=ROOT/'build/integrated-native'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    out=OUT/('save-host-'+str(time.time_ns()));out.mkdir()
    lock=json.loads((ROOT/'reference-native.lock.json').read_text())
    relative=['port/hal/save_cart.cpp','port/tests/smoke_save_cart.cpp',
              'port/tests/menu_save_transactions.cpp','port/tools/menu_sources.py']
    for name in relative:assert sha(VENDOR/name)==lock['vendored_files_sha256'][name]
    pin=subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip()
    assert pin==lock['decomp_commit']
    before={name:sha(VENDOR/name) for name in relative}
    sources=['_ZN8SaveData16SetDefaultValuesEP12FileSaveData.cpp',
             '_ZN8SaveData12ReadFileDataEjP12FileSaveData.cpp',
             '_ZN8SaveData8SaveFileEjP12FileSaveData.cpp',
             '_ZN8SaveData13EraseSaveFileEjPc.cpp']
    before.update({str((REPO/'src'/name).relative_to(VENDOR)):sha(REPO/'src'/name) for name in
                   [*sources,'func_ov007_020cc600.c','func_02013c84.c']})
    compiler=shutil.which('clang++');c_compiler=shutil.which('clang');assert compiler and c_compiler
    common=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer','-I'+str(REPO/'include')]
    def run(command,label,**kwargs):
        result=subprocess.run([str(x) for x in command],capture_output=True,text=True,**kwargs)
        (out/(label+'.log')).write_text(result.stdout+result.stderr)
        result.check_returncode();return result
    run([compiler,'-std=c++17','-DSM64DS_PLATFORM_PC',*common,
         VENDOR/'port/tests/smoke_save_cart.cpp',VENDOR/'port/hal/save_cart.cpp',
         '-o',out/'cart-probe'],'cart-build')
    with tempfile.TemporaryDirectory(prefix='sm64ds-cart-host-') as temp:
        (Path(temp)/'sdmc:/sm64ds').mkdir(parents=True)
        cart=run([out/'cart-probe'],'cart-run',cwd=temp)
    spec=importlib.util.spec_from_file_location('pinned_menu_sources',VENDOR/'port/tools/menu_sources.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    generated=out/'dispatcher.c';module.generate(REPO/'src/func_ov007_020cc600.c',generated)
    objects=[]
    for i,source in enumerate([generated,REPO/'src/func_02013c84.c']):
        obj=out/(str(i)+'.o');objects.append(obj)
        run([c_compiler,'-c',*common,source,'-o',obj],'menu-C-'+str(i))
    run([compiler,'-std=c++17','-DSM64DS_PLATFORM_PC',*common,
         VENDOR/'port/tests/menu_save_transactions.cpp',VENDOR/'port/hal/save_cart.cpp',
         *[REPO/'src'/name for name in sources],*objects,'-o',out/'menu-probe'],'menu-build')
    with tempfile.TemporaryDirectory(prefix='sm64ds-menu-save-host-') as temp:
        menu=run([out/'menu-probe',Path(temp)/'save.sav'],'menu-run')
    assert all(sha(VENDOR/name)==expected for name,expected in before.items())
    report=dict(status='pinned_save_HAL_and_retail_menu_host_probes_passed',ASan=True,UBSan=True,
        decomp_commit=pin,sources={'tools/test_integrated_save.py':sha(Path(__file__)),
                                  **{str(Path('third_party/sm64ds-3ds')/name):expected for name,expected in before.items()}},
        cart_probe=cart.stdout,menu_transactions=menu.stdout,
        generated_dispatcher_sha256=sha(generated),directory=str(out.resolve()),
        real_save_modified=False,emulator_launched=False,physical_hardware=False,
        limits='Host files only. Original menu commands and save HAL exercised with isolated drawing/audio callbacks. This is not in-game save/reload, 3DS filesystem durability, full menu parity, or progression acceptance.')
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
