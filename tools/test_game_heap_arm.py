#!/usr/bin/env python3
"""Exercise the original heap's actual ARM11 code; no physical 3DS claims.

Requires a completed native-core frontier and test-only unicorn==2.1.4.
Only the DS fatal Crash screen is replaced, by a test trap that must never run.
The production bridge and every allocator/constructor/virtual body are real.
"""
import hashlib
import json
import pathlib
import random
import shlex
import struct
import subprocess
import native_operators

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_SP, UC_ARM_REG_LR,
    UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT/'build/native-heap'
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
CODE, ARENA, STACK, STOP = 0x100000, 0x200000, 0x300000, 0x400000
SAVED = [UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11]
INIT = '_ZN6sm64ds34initialize_game_root_heap_in_placeEPvj'


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    wrappers = OUT/'wrappers.cpp'
    wrappers.write_text('''#include "Heap.h"
extern "C" void* heap_allocate(Heap*h,unsigned n,int a){return h->Allocate(n,a);}
extern "C" void heap_free(Heap*h,void*p){h->Deallocate(p);}
extern "C" unsigned heap_left(Heap*h){return h->VMemoryLeft();}
extern "C" [[noreturn]] void __wrap_Crash(){__builtin_trap();}
''')
    flags = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
             '-Os', '-D__3DS__', '-DNINTENDO_3DS', '-ffunction-sections',
             '-fdata-sections', '-Wa,--noexecstack', '-Iinclude',
             '-Ibuild/native-core/generated/include']
    for src, obj in [(ROOT/'source/ctr/game_heap.cpp', 'bridge.o'), (wrappers, 'wrappers.o')]:
        run(TOOL+'g++', *flags, '-c', str(src), '-o', str(OUT/obj))
    report = json.loads((ROOT/'build/native-core/report.json').read_text())
    crash = next(e['object'] for e in report['entries'] if e['source']=='src/Crash.c')
    frontier = shlex.split((ROOT/'build/native-core/frontier-objects.rsp').read_text())
    objects = [o for o in frontier if o != crash]
    (OUT/'objects.rsp').write_text('\n'.join('"'+o+'"' for o in objects)+'\n')
    run(TOOL+'g++', *flags[:4], '-nostartfiles',
        '-Wl,--gc-sections,-z,noexecstack,--fatal-warnings,--wrap=Crash,-e,'+INIT+
        ',-u,heap_allocate,-u,heap_free,-u,heap_left,-Ttext=0x100000,-Map='+str(OUT/'heap.map'),
        str(OUT/'bridge.o'), str(OUT/'wrappers.o'), '@'+str(OUT/'objects.rsp'),
        '-o', str(OUT/'heap.elf'))
    run(TOOL+'objcopy', '-O', 'binary', str(OUT/'heap.elf'), str(OUT/'heap.bin'))
    nm = subprocess.check_output([TOOL+'nm', '-g', '--defined-only', str(OUT/'heap.elf')], text=True)
    symbols = {p[2]: int(p[0],16) for line in nm.splitlines() if len(p:=line.split())==3}
    # Retain only original objects that supply a symbol in this successful
    # final executable. This list deliberately excludes both test wrappers and
    # the DS Crash implementation; production must supply a native fatal path.
    source_for = {e['object']: e['source'] for e in report['entries'] if e.get('object')}
    input_nm = subprocess.check_output([TOOL+'nm', '-A', '-g', '--defined-only', *objects], text=True)
    chosen = {}
    for line in input_nm.splitlines():
        parts = line.split()
        if len(parts)==3 and parts[2] in symbols and parts[1] not in 'Uu':
            chosen.setdefault(parts[0].split(':')[0], []).append(parts[2])
    # Keep the game's deallocator away from the platform's std::vector/string
    # allocations. Change only symbols/relocations, never allocator bodies.
    isolated = OUT/'objects'
    isolated.mkdir(exist_ok=True)
    production_objects=[]
    manifest=[]
    for index,p in enumerate(sorted(chosen)):
        target=isolated/(str(index).zfill(3)+'-'+pathlib.Path(p).name)
        native_operators.copy_isolated(TOOL,p,target,ROOT)
        production_objects.append(str(target.relative_to(ROOT)))
        manifest.append({'object':str(target.relative_to(ROOT)), 'original_object':p,
                         'source':source_for.get(p,'platform-generated'),
                         'live_symbols':chosen[p]})
    (OUT/'production-objects.rsp').write_text('\n'.join('"'+p+'"' for p in production_objects)+'\n')
    (OUT/'objects.json').write_text(json.dumps({
        'scope':'Original heap closure from live ELF; excludes test wrappers and Crash',
        'bridge':'source/ctr/game_heap.cpp',
        'required_platform_symbol':'Crash (noreturn; native fatal abort required)',
        'isolated_game_operators':True,
        'symbol_isolation':native_operators.SYMBOLS,
        'objects':manifest}, indent=2)+'\n')
    # Verify the smaller list independently; no unresolved symbols are allowed.
    run(TOOL+'g++', *flags[:4], '-nostartfiles',
        '-Wl,--gc-sections,-z,noexecstack,--fatal-warnings,--wrap=Crash,-e,'+INIT+
        ',-u,heap_allocate,-u,heap_free,-u,heap_left,-u,sm64ds_game_operator_delete_sized,-Ttext=0x100000',
        str(OUT/'bridge.o'), str(OUT/'wrappers.o'), '@'+str(OUT/'production-objects.rsp'),
        '-o', str(OUT/'closure.elf'))
    assert not subprocess.check_output([TOOL+'nm','-u',str(OUT/'closure.elf')], text=True).strip()
    operator_nm=native_operators.verify_isolated(TOOL,production_objects,ROOT)
    (OUT/'operator-isolation.txt').write_text('\n'.join(line for line in operator_nm.splitlines()
        if 'sm64ds_game_operator' in line)+'\nNo scalar, array, sized or placement new/delete symbols remain.\n')
    run(TOOL+'objcopy','-O','binary',str(OUT/'closure.elf'),str(OUT/'closure.bin'))
    isolated_nm=subprocess.check_output([TOOL+'nm','-g','--defined-only',str(OUT/'closure.elf')],text=True)
    symbols={p[2]:int(p[0],16) for line in isolated_nm.splitlines() if len(p:=line.split())==3}
    binary = (OUT/'closure.bin').read_bytes()
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    cpu.mem_map(CODE, 0x80000); cpu.mem_write(CODE, binary)
    cpu.mem_map(ARENA, 0x20000); cpu.mem_map(STACK, 0x10000); cpu.mem_map(STOP, 0x1000)
    def trap(uc, address, size, user):
        if address == symbols['__wrap_Crash']: raise AssertionError('Original heap invoked Crash')
    cpu.hook_add(UC_HOOK_CODE, trap, begin=symbols['__wrap_Crash'], end=symbols['__wrap_Crash'])
    def call(name, a=0, b=0, c=0):
        cpu.reg_write(UC_ARM_REG_CPSR,0x10)
        for reg,value in [(UC_ARM_REG_R0,a),(UC_ARM_REG_R1,b),(UC_ARM_REG_R2,c),
                          (UC_ARM_REG_SP,STACK+0x8000),(UC_ARM_REG_LR,STOP)]:
            cpu.reg_write(reg,value & 0xffffffff)
        for i,reg in enumerate(SAVED): cpu.reg_write(reg,0x12340000+i)
        cpu.emu_start(symbols[name], STOP, count=1000000)
        assert cpu.reg_read(UC_ARM_REG_PC)==STOP
        assert cpu.reg_read(UC_ARM_REG_SP)==STACK+0x8000
        assert all(cpu.reg_read(reg)==0x12340000+i for i,reg in enumerate(SAVED))
        return cpu.reg_read(UC_ARM_REG_R0)
    # Validation must not alter heap globals before the first real init.
    assert call(INIT,0,0x10000)==0
    assert call(INIT,ARENA+1,0x10000)==0
    assert call(INIT,ARENA,0x60)==0
    assert call(INIT,ARENA,0x10000)==1
    assert call(INIT,ARENA,0x10000)==0
    for global_name in ('data_020a0e9c','_ZN6Memory14defaultHeapPtrE'):
        assert struct.unpack('<I',cpu.mem_read(symbols[global_name],4))[0]==ARENA
    baseline = call('heap_left',ARENA)
    assert 0xf000 < baseline < 0x10000
    rng = random.Random(64)
    allocations=0
    for round_index in range(12):
        active=[]
        for i in range(32):
            size = rng.randrange(1,513)
            alignment = (4,8,16,32,64,-4,-16,-32)[i%8]
            ptr=call('heap_allocate',ARENA,size,alignment)
            assert ARENA < ptr and ptr+size <= ARENA+0x10000
            assert ptr % abs(alignment)==0
            assert all(ptr+size <= p or p+n <= ptr for p,n,_ in active)
            data=bytes([(round_index*32+i)&255])*size
            cpu.mem_write(ptr,data); active.append((ptr,size,data)); allocations+=1
        rng.shuffle(active)
        while active:
            for p,n,data in active: assert bytes(cpu.mem_read(p,n))==data
            p,_,_=active.pop(); call('heap_free',ARENA,p)
        assert call('heap_left',ARENA)==baseline, 'Free nodes failed to coalesce'
    # Near-whole-arena reallocation proves fragmentation was recovered.
    ptr=call('heap_allocate',ARENA,baseline-64,4)
    cpu.mem_write(ptr,bytes([0x5a])*(baseline-64)); call('heap_free',ARENA,ptr)
    assert call('heap_left',ARENA)==baseline
    for private_delete in ('sm64ds_game_operator_delete','sm64ds_game_operator_delete_sized'):
        ptr=call('heap_allocate',ARENA,128,16)
        cpu.mem_write(ptr,bytes([0xc3])*128)
        call(private_delete,ptr,128)
        assert call('heap_left',ARENA)==baseline
    result={'status':'passed','scope':'Actual ARM11 instructions in Unicorn; not 3DS performance',
            'original_allocator':True,'production_bridge':'source/ctr/game_heap.cpp',
            'allocations':allocations+3,'coalescing_rounds':12,
            'invalid_init_cases':3,'reinitialization_rejected':True,
            'callee_saved_registers_preserved':True,'fatal_path_invocations':0,
            'game_delete_symbols_isolated_from_platform':True,
            'private_delete_variants_tested':['unsized','sized'],
            'test_only_override':'Crash becomes a trap, never a success return',
            'binary_sha256':hashlib.sha256(binary).hexdigest(),
            'heap_free_bytes':baseline}
    (OUT/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
