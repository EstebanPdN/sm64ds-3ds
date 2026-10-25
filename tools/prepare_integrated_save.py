"""Use checked, recoverable replacement in generated cart HAL copies only.

prepare(output) -> changed relative paths. Compatible with either SD prefix;
call after the optional ROM-only path rewriting. No build sources are added.
"""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
STORE = '''    const std::string path=savePath(), temporary=path+".tmp";
    FILE* file=std::fopen(temporary.c_str(),"wb");
    if (!file) return false;
    bool good=std::fwrite(image,1,Capacity,file)==Capacity;
    if (std::fflush(file)!=0) good=false;
    if (good && fsync(fileno(file))!=0) good=false;
    if (std::fclose(file)!=0) good=false;
    if (good && std::rename(temporary.c_str(),path.c_str())==0) return true;
    std::remove(temporary.c_str());
    return false;'''


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Save integration anchor changed: ' + old[:90])
    return text.replace(old, new, 1)


def patch_cart(text):
    text = once(text, '#include "SaveData.h"',
                '#include "SaveData.h"\n#include "../platform/3ds/atomic_file.hpp"')
    text = once(text, 'FILE* file=std::fopen(savePath(),"rb");',
                'FILE* file=integrated_storage::open_read(savePath());')
    return once(text, STORE,
                '    return integrated_storage::replace_file(savePath(),image,Capacity);')


def prepare(output):
    output = Path(output).resolve()
    if output.is_relative_to((ROOT / 'third_party').resolve()):
        raise ValueError('Pinned vendor tree is immutable')
    path = output / 'port/hal/save_cart.cpp'
    if not path.resolve().is_relative_to(output):
        raise ValueError('Generated save source must not escape output')
    text = patch_cart(path.read_text())  # Fail before mutation on any drift.
    header = output / 'port/platform/3ds/atomic_file.hpp'
    if not header.resolve().is_relative_to(output):
        raise ValueError('Generated storage header must not escape output')
    header.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'source/integrated_native/options/atomic_file.hpp', header)
    path.write_text(text)
    return ['port/hal/save_cart.cpp', 'port/platform/3ds/atomic_file.hpp']
