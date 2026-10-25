#!/usr/bin/env python3
"""Test the portable orbit only: host sanitizers and bounded ARM11/ARM9 CPU.

No camera provider, Stage dispatch, game update, Azahar, or physical console
executes. Original LUT bytes stay in temporary local fixtures, never reports.
The ROM geometry oracle is a pure-memory rotation leaf. Original angle helpers
use an explicit integer divider/sqrt register model, only in the ARM9 oracle.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import zlib

from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC
from test_native_matrix_submit_arm import Cpu, ROOT, TOOL, BUFFER, STACK
from test_native_menu_constructors import original_symbols
import ndspy.rom
import ndspy.code
import ndspy.codeCompression

SOURCES = ['include/sm64ds/camera_orbit.hpp', 'source/core/camera_orbit.cpp',
           'tests/native_camera_orbit.cpp', 'tools/test_native_camera_orbit.py']
CASES = ['inactive_neutral', 'held_yaw_multiple_wraps', 'continuous_pitch_poles_reverse',
         'neutral_stops_and_retains_pose', 'same_time_30_and_60_partitions',
         'duplicate_and_stale_ticks', 'pause_no_catchup', 'lifecycle_identity_generation',
         'submillisecond_time_carry', 'geometry_overflow_is_transactional',
         'invalid_inputs_seed_identity_config_lut', 'original_lut_quadrants_pitch_sign',
         'collision_adapter_rebase_only', 'controller_and_input_canaries',
         'fractional_and_half_deflection', 'maximum_config_and_time_bounds']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def names(elf):
    return {parts[2]: int(parts[0], 16) for line in subprocess.check_output(
        [TOOL+'nm', '--defined-only', elf], text=True).splitlines()
        if len(parts := line.split()) == 3}


def load_elf(cpu, path):
    raw = path.read_bytes()
    assert raw[:7] == b'\x7fELF\x01\x01\x01'
    phoff = struct.unpack_from('<I', raw, 28)[0]
    width, count = struct.unpack_from('<HH', raw, 42)
    for i in range(count):
        kind, offset, address, _, filesz, memsz, _, _ = struct.unpack_from('<8I', raw, phoff+i*width)
        if kind == 1 and memsz:
            cpu.map(address, memsz)
            cpu.write(address, raw[offset:offset+filesz])


def signed(value, bits=32):
    value &= (1 << bits)-1
    return value-(1 << bits) if value & (1 << (bits-1)) else value


def oracle_math_ports(cpu):
    """Integer DS peripheral semantics for ORIGINAL math helpers only."""
    cpu.map(0x04000000, 4096, 3)
    accesses = []
    def read(uc, access, address, size, value, _):
        accesses.append([address, size])
        if address in (0x040002a0, 0x040002a4):
            numerator = struct.unpack('<q', uc.mem_read(0x04000290, 8))[0]
            mode = struct.unpack('<H', uc.mem_read(0x04000280, 2))[0] & 3
            denominator = (struct.unpack('<i', uc.mem_read(0x04000298, 4))[0] if mode == 1
                           else struct.unpack('<q', uc.mem_read(0x04000298, 8))[0])
            quotient = (-1 if numerator >= 0 else 1) if denominator == 0 else \
                (abs(numerator)//abs(denominator))*(-1 if (numerator < 0) != (denominator < 0) else 1)
            remainder = numerator if denominator == 0 else numerator-quotient*denominator
            uc.mem_write(0x040002a0, struct.pack('<Q', quotient & ((1 << 64)-1)))
            uc.mem_write(0x040002a8, struct.pack('<Q', remainder & ((1 << 64)-1)))
        elif address == 0x040002b4:
            radicand = struct.unpack('<Q', uc.mem_read(0x040002b8, 8))[0]
            if not struct.unpack('<H', uc.mem_read(0x040002b0, 2))[0] & 1:
                radicand &= 0xffffffff
            uc.mem_write(address, struct.pack('<I', math.isqrt(radicand)))
    cpu.cpu.hook_add(UC_HOOK_MEM_READ, read, begin=0x04000000, end=0x04000fff)
    cpu.cpu.ctl_flush_tb()
    return accesses


def geometry(native, entries, original, originals, trig):
    comparisons, rejected = [], []
    reads, writes = [], []
    native.cpu.hook_add(UC_HOOK_MEM_READ, lambda _, access, address, size, value, user:
                        reads.append((address, size)))
    native.cpu.hook_add(UC_HOOK_MEM_WRITE, lambda _, access, address, size, value, user:
                        writes.append((address, size)))
    # Earlier cases translated this callee. Instrument fresh blocks after
    # installing the memory hooks, including the cached core routine.
    native.cpu.ctl_flush_tb()
    rng = random.Random(0xca6e4a)
    samples = [(0, 0, 8192, (0,0,0)), (0x4000,0,8192,(0,0,0)),
               (-32768,0,8192,(0,0,0)), (-0x4000,0,8192,(0,0,0)),
               (0,-0x2000,8192,(0,0,0)), (0,0x2000,8192,(0,0,0))]
    samples += [(signed(i*16,16), signed((i*43)%30949-15474,16),
                 1+(i*7919)%0x2000000, (-1000000,2000000,-3000000)) for i in range(4096)]
    samples += [(rng.randrange(-32768,32768),rng.randrange(-15474,15475),
                 rng.randrange(1,0x10000000),tuple(rng.randrange(-0x20000000,0x20000000)
                  for _ in range(3))) for _ in range(128)]
    fx = lambda a,b: (a*b+2048)//4096
    for yaw,pitch,radius,target in samples:
        yi,pi = ((yaw&0xffff)>>4)*2,((pitch&0xffff)>>4)*2
        horizontal,dy = fx(radius,trig[pi+1]),fx(radius,-trig[pi])
        expected = (target[0]+fx(horizontal,trig[yi]),target[1]+dy,
                    target[2]+fx(horizontal,trig[yi+1]))
        native.write(BUFFER, bytes([0xa5])*0x200)
        native.write(BUFFER+16, struct.pack('<6i', *target,radius,yaw,pitch))
        native.write(BUFFER+128, struct.pack('<3i', 31,41,59))
        before = native.read(BUFFER,0x200);reads.clear();writes.clear()
        result = native.invoke(entries['camera_orbit_geometry'], [BUFFER+16,BUFFER+128])
        after = native.read(BUFFER,0x200)
        wanted = bytearray(before);wanted[128:140] = struct.pack('<3i',*expected)
        assert result == 1 and after == bytes(wanted), (yaw,pitch,radius,expected)
        touched = {at for address,size in reads if BUFFER<=address<BUFFER+0x200
                   for at in range(address,address+size)}
        allowed=set(range(BUFFER+16,BUFFER+40))|set(range(BUFFER+128,BUFFER+140))
        required=set(range(BUFFER+16,BUFFER+32))|set(range(BUFFER+32,BUFFER+34))|\
            set(range(BUFFER+36,BUFFER+38))|set(range(BUFFER+128,BUFFER+140))
        # The compiler may narrow an s32 angle field to its used low halfword.
        assert required<=touched<=allowed
        assert {(address,size) for address,size in writes if BUFFER<=address<BUFFER+0x200} == \
            {(BUFFER+128,4),(BUFFER+132,4),(BUFFER+136,4)}
        lut_reads = [(address,size) for address,size in reads
                     if entries['data_02082214']<=address<entries['data_02082214']+16384]
        actual_lut={at for address,size in lut_reads for at in range(address,address+size)}
        expected_lut={entries['data_02082214']+at for ix in (yi,yi+1,pi,pi+1) for at in (ix*2,ix*2+1)}
        assert actual_lut==expected_lut, (yaw,pitch,lut_reads,sorted(expected_lut))
        original.write(BUFFER, bytes([0xa5])*0x200)
        original.write(BUFFER+16,struct.pack('<3i',*target))
        original.write(BUFFER+64,struct.pack('<3i',0,dy,horizontal))
        original_before=original.read(BUFFER,0x200)
        original.invoke(originals['Vec3_RotateYAndTranslate'],
                        [BUFFER+128,BUFFER+16,yaw,BUFFER+64])
        wanted = bytearray(original_before);wanted[128:140] = struct.pack('<3i',*expected)
        assert original.read(BUFFER,0x200) == bytes(wanted), ('ROM rotation',yaw,pitch)
        if len(comparisons)<6:comparisons.append({'yaw':yaw,'pitch':pitch,'eye':list(expected)})
    for target,radius,yaw,pitch in [((0,0,0),0,0,0),((0,0,0),-1,0,0),
            ((0x7fffffff,0,0),0x7fffffff,0x4000,0),
            ((0,-0x80000000,0),0x7fffffff,0,0x4000)]:
        native.write(BUFFER,bytes([0xa5])*0x200)
        native.write(BUFFER+16,struct.pack('<6i',*target,radius,yaw,pitch))
        native.write(BUFFER+128,struct.pack('<3i',31,41,59))
        before=native.read(BUFFER,0x200)
        assert native.invoke(entries['camera_orbit_geometry'],[BUFFER+16,BUFFER+128]) == 0
        assert native.read(BUFFER,0x200) == before
        rejected.append({'radius':radius,'yaw':yaw,'pitch':pitch,'output_unchanged':True})
    return {'rom_rotation_comparisons':len(samples),'all_4096_yaw_lut_buckets':True,
            'python_fx12_comparisons':len(samples),'input_output_lut_extents_and_canaries':'passed',
            'quadrants':comparisons,'overflow_and_bad_radius':rejected}


def angle_conventions(original, symbols):
    accesses=oracle_math_ports(original)
    results=[]
    # Actual original wrappers, not a host atan2 substituting their return.
    for eye,yaw,pitch in [((0,0,8192),0,0),((8192,0,0),0x4000,0),
            ((0,0,-8192),-32768,0),((-8192,0,0),-0x4000,0),
            ((0,8192,8192),0,-0x2000),((0,-8192,8192),0,0x2000),
            ((8192,0,8192),0x2000,0),((-8192,0,-8192),-0x6000,0)]:
        original.write(BUFFER+16,struct.pack('<3i',0,0,0))
        original.write(BUFFER+64,struct.pack('<3i',*eye))
        before=original.read(BUFFER,0x100)
        actual_yaw=signed(original.invoke(symbols['Vec3_HorzAngle'],[BUFFER+16,BUFFER+64]),16)
        actual_pitch=signed(original.invoke(symbols['Vec3_VertAngle'],[BUFFER+16,BUFFER+64]),16)
        assert (actual_yaw,actual_pitch)==(yaw,pitch),(eye,actual_yaw,actual_pitch)
        assert original.read(BUFFER,0x100)==before
        results.append({'eye':list(eye),'yaw':actual_yaw,'pitch':actual_pitch})
    return {'original_helper_checks':results,'oracle_integer_math_port_reads':len(accesses),
            'native_math_ports_unmapped':True}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,default=ROOT/'build/roms/europe.nds')
    parser.add_argument('--out',type=Path,default=ROOT/'build/native-camera-plan/integrator-tests')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    raw=args.rom.read_bytes();assert zlib.crc32(raw)==0x29715dec
    rom=ndspy.rom.NintendoDSRom(raw);assert rom.idCode==b'ASMP' and rom.version==0
    arm9=ndspy.codeCompression.decompress(rom.arm9)
    lut_bytes=bytes(arm9[0x02082214-rom.arm9RamAddress:0x02086214-rom.arm9RamAddress])
    assert len(lut_bytes)==16384
    trig=struct.unpack('<8192h',lut_bytes)
    assert all(-4096<=v<=4096 for v in trig)
    base_flags=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include')]
    with tempfile.TemporaryDirectory(prefix='sm64ds-camera-orbit-') as temp:
        work=Path(temp);lut=work/'lut.bin';lut.write_bytes(lut_bytes)
        host=args.out/'host-tests'
        subprocess.run(['clang++',*base_flags,'-fsanitize=address,undefined',
            '-fno-omit-frame-pointer',ROOT/'source/core/camera_orbit.cpp',
            ROOT/'tests/native_camera_orbit.cpp','-o',host],check=True)
        host_output=subprocess.check_output([host,lut],text=True).strip()
        table=work/'original-lut.cpp'
        table.write_text('extern "C" { short data_02082214[8192]={'+','.join(map(str,trig))+'}; }\n')
        arm_flags=[*base_flags,'-marm','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard',
            '-mtp=soft','-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti',
            '-Wa,--noexecstack','-DCAMERA_ORBIT_ARM']
        objects=[]
        for source,name in [(ROOT/'source/core/camera_orbit.cpp','core'),
                (ROOT/'tests/native_camera_orbit.cpp','fixture'),(table,'lut')]:
            obj=(args.out if name!='lut' else work)/(name+'.o')
            subprocess.run([TOOL+'g++',*arm_flags,'-c',source,'-o',obj],check=True);objects.append(obj)
        elf=work/'orbit.elf'
        subprocess.run([TOOL+'g++',*arm_flags,'-nostartfiles',
            '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x01000000,-e,camera_orbit_test_entry',
            '-Wl,-u,camera_orbit_case,-u,camera_orbit_geometry',*objects,'-o',elf],check=True)
        native=Cpu();load_elf(native,elf);entries=names(elf)
        assert 'func_02009e70' not in entries
        assert 'sm64ds_original_camera_init' not in entries
        native.cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf << 20)
        native.cpu.reg_write(UC_ARM_REG_FPEXC,1 << 30)
        faults=[]
        native.cpu.hook_add(UC_HOOK_MEM_INVALID,lambda uc,access,address,size,value,user:
            faults.append({'pc':uc.reg_read(UC_ARM_REG_PC),'address':address,'size':size}) or False)
        results=[]
        for i,name in enumerate(CASES):
            native.write(STACK,bytes([0x6d])*0x8000)
            error=native.invoke(entries['camera_orbit_case'],[i],instructions=10000000)
            assert error==0,(name,'C++ assertion line',error)
            assert native.read(STACK,64)==bytes([0x6d])*64
            assert not faults,faults
            results.append({'case':name,'result':'passed'})
        original=Cpu();original.map(rom.arm9RamAddress,len(arm9));original.write(rom.arm9RamAddress,arm9)
        for section in ndspy.code.MainCodeFile(rom.arm9,rom.arm9RamAddress).sections:
            if section.ramAddress<0x02000000 and section.data:
                original.map(section.ramAddress,len(section.data));original.write(section.ramAddress,section.data)
        originals=original_symbols()
        geo=geometry(native,entries,original,originals,trig)
        conventions=angle_conventions(original,originals)
        assert not faults
        report={'schema':1,'status':'passed','scope':'portable integrator host/ARM CPU only; no Stage Camera provider or collision integration',
            'source_sha256':{source:sha(ROOT/source) for source in SOURCES},
            'harness_dependency_sha256':{name:sha(ROOT/name) for name in
                ['tools/test_native_matrix_submit_arm.py','tools/test_native_menu_constructors.py']},
            'rom_sha256':hashlib.sha256(raw).hexdigest(),'original_lut_sha256':hashlib.sha256(lut_bytes).hexdigest(),
            'lut_extent':{'address':0x02082214,'bytes':16384,'entries':8192,'observed_range':[-4096,4096]},
            'host':{'sanitizers':['address','undefined'],'result':host_output,'binary_sha256':sha(host)},
            'arm':{'strict_compile':True,'cases':results,'core_object_sha256':sha(objects[0]),
                'fixture_object_sha256':sha(objects[1]),'temporary_elf_sha256':sha(elf),
                'calling_convention_stack_and_registers':'passed','invalid_accesses':faults},
            'geometry':geo,'angle_conventions':conventions,
            'simulation_advanced':False,'production_integration':False,'azahar_launched':False,
            'time_quantum_us':1000,'yaw_lut_quantum_angle_units':16,
            'temporary_rom_fixture_removed_after_test':True,
            'remaining_gate':{'complete_provider':'not implemented; public func_02009e70 unchanged',
                'lifecycle_runtime_wrappers':'not implemented; opaque lifecycle tokens tested only',
                'real_camera_collision':'not tested; original query objects/world/Stage state needed',
                'gameplay':'not tested; current RAM Camera singleton is null',
                'physical_new3ds':'not tested; sensitivity/deadzone/render cadence remain unmeasured'}}
        (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({'status':report['status'],'host_cases':16,'arm_cases':len(results),
            'geometry_comparisons':geo['rom_rotation_comparisons'],
            'original_angle_helper_checks':len(conventions['original_helper_checks']),
            'report':str(args.out/'report.json')},indent=2))


if __name__=='__main__':main()
