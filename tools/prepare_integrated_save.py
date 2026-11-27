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


LOAD_HEAD = 'bool load(unsigned char (&image)[Capacity]) {\n'
STORE_FN = ('bool store(const unsigned char (&image)[Capacity]) {\n'
            '    return integrated_storage::replace_file(savePath(),image,Capacity);\n}\n')
# BUILD139: the console spent hundreds of milliseconds per SaveDataToCart on
# the game thread (read the 8 KiB image, then write .tmp, sync and rename):
# the save box after the key ran at 12 FPS. On the 3DS the image is cached
# in memory (game thread only) and handed to the writer thread of
# native_renderer/save_writer.cpp, which performs the same checked
# replacement (with its .bak recovery) for the newest image. The host build
# stays synchronous, so the format tests see every write.
WRITE_BEHIND = r"""bool load_sd(unsigned char (&image)[Capacity]) {
"""
WRITE_BEHIND_STORE = r"""bool store_sd(const unsigned char (&image)[Capacity]) {
    return integrated_storage::replace_file(savePath(),image,Capacity);
}
#ifdef __3DS__
}
extern "C" int sm64ds_save_write_behind(const char*,const void*,unsigned) __attribute__((weak));
namespace {
unsigned char cache[Capacity];bool cache_valid=false;
bool load(unsigned char (&image)[Capacity]) {
    if(!cache_valid){if(!load_sd(cache))return false;cache_valid=true;}
    std::memcpy(image,cache,Capacity);return true;
}
bool store(const unsigned char (&image)[Capacity]) {
    if(!(sm64ds_save_write_behind&&sm64ds_save_write_behind(savePath(),image,Capacity))&&!store_sd(image))return false;
    std::memcpy(cache,image,Capacity);cache_valid=true;return true;
}
#else
bool load(unsigned char (&image)[Capacity]) {return load_sd(image);}
bool store(const unsigned char (&image)[Capacity]) {return store_sd(image);}
#endif
"""


def patch_write_behind(text):
    text = once(text, LOAD_HEAD, WRITE_BEHIND)
    return once(text, STORE_FN, WRITE_BEHIND_STORE)


def patch_cart(text):
    text = once(text, '#include "SaveData.h"',
                '#include "SaveData.h"\n#include "../platform/3ds/atomic_file.hpp"')
    text = once(text, 'FILE* file=std::fopen(savePath(),"rb");',
                'FILE* file=integrated_storage::open_read(savePath());')
    text = once(text, STORE,
                '    return integrated_storage::replace_file(savePath(),image,Capacity);')
    return patch_write_behind(text)


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
