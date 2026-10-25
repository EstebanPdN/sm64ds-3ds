#!/usr/bin/env python3
"""Run native display-capture memory checks with ASan/UBSan and compile ARM11."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/native-capture'
OUT.mkdir(exist_ok=True)
sources=('tests/test_native_capture.cpp','source/game_port/native_capture.cpp',
         'source/game_port/native_video.cpp')
compiler=shutil.which('clang++') or shutil.which('c++')
if not compiler:
    raise RuntimeError('Host C++ compiler is unavailable')
executable=OUT/'test-native-capture'
subprocess.run([compiler,'-std=c++17','-O1','-g','-UNDEBUG','-Wall','-Wextra',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'include'),
    *(str(ROOT/name) for name in sources),'-o',str(executable)],check=True)
report=json.loads(subprocess.check_output([str(executable)],text=True))
arm_object=OUT/'native_capture.o'
subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=c++17',
    '-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-Os','-Wall','-Wextra',
    '-ffunction-sections','-fdata-sections','-Wa,--noexecstack','-Werror=trampolines',
    '-I',str(ROOT/'include'),'-c',str(ROOT/'source/game_port/native_capture.cpp'),
    '-o',str(arm_object)],check=True)
report.update({'host_sanitizers':'ASan and UBSan passed','ARM11_compilation':'passed; not executed on a console',
    'sources_sha256':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        for name in (*sources,'include/sm64ds/native_capture.hpp')},
    'host_executable_sha256':hashlib.sha256(executable.read_bytes()).hexdigest(),
    'ARM11_object_sha256':hashlib.sha256(arm_object.read_bytes()).hexdigest()})
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
