#!/usr/bin/env python3
"""Compare CLPS extraction with original ARM accessors and DS/N64 slope code."""
from pathlib import Path
import argparse,ctypes,hashlib,json,random,struct,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elftools.elf.elffile import ELFFile
from read_mapped_capture import MappedCapture
from inspect_movement_colliders import inspect
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_PC,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_CPSR
ROOT=Path(__file__).resolve().parents[2];CORE=ROOT/'build/movement-lab/core';TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
GETTERS=['func_02037e58','func_02037e38','func_02037e84','func_02037e90','func_02037e68','func_02037e48','SurfaceInfo_TestFlag0x20','func_02037e2c','func_02037e20','func_02037e14']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path);p.add_argument('--elf',type=Path);p.add_argument('--allow-incomplete',action='store_true');a=p.parse_args()
    if bool(a.capture)!=bool(a.elf):p.error('capture and ELF are a matched pair')
    out=ROOT/'build/movement-adapter'/('materials-'+str(time.time_ns()));out.mkdir(parents=True)
    manifest=json.loads((CORE/'host/sources.json').read_text())
    for name,digest in manifest.items():assert sha(Path(name))==digest,name
    commands=[]
    def run(command):
        commands.append(command);r=subprocess.run(command,capture_output=True,text=True)
        with (out/'run.log').open('a') as f:f.write(r.stdout+r.stderr)
        r.check_returncode();return r.stdout.strip()
    includes=['-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(CORE/'source/src'),'-I'+str(CORE/'source/src/decomp/include')]
    defines=['-DGBI_FLOATS','-DVERSION_US','-DNO_SEGMENTED_MEMORY']
    fixtures=[ROOT/'tests/movement_lab/material_core_fixture.c',ROOT/'tests/movement_lab/solver_contacts_fixture.c',ROOT/'upstream/src/func_ov002_020f035c.c']
    test=ROOT/'tests/movement_lab/test_material_profile.cpp';library=out/'materials.dylib'
    for sanitized in [True,False]:
        flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitized else ['-O2']
        objects=[]
        for i,source in enumerate(fixtures):
            obj=out/(('san-' if sanitized else 'plain-')+str(i)+'.o');objects.append(str(obj))
            run(['clang','-std=gnu11',*flags,*defines,*includes,'-c',str(source),'-o',str(obj)])
        target=out/'test' if sanitized else library
        run(['clang++','-std=c++17',*flags,*(['-dynamiclib'] if not sanitized else []),*includes,str(test),*objects,str(CORE/'host/libmotion64.a'),'-lm','-o',str(target)])
        if sanitized:output=run([str(target)])
    host=ctypes.CDLL(str(library));host.fixture_material_fields.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32)]
    host.fixture_material_use.argtypes=[ctypes.c_uint32,ctypes.c_uint32];host.fixture_material_use.restype=ctypes.c_int
    sources=[ROOT/'upstream/src'/(name+'.c') for name in GETTERS+['func_ov002_020f035c']];objects=[]
    for i,source in enumerate(sources):
        obj=out/('arm-'+str(i)+'.o');objects.append(str(obj));run([TOOL+'gcc','-Os','-march=armv6k','-mfloat-abi=hard','-ffunction-sections','-fdata-sections','-c',str(source),'-o',str(obj)])
    roots=','.join('-u,'+name for name in GETTERS[1:]+['func_ov002_020f035c'])
    run([TOOL+'gcc','-march=armv6k','-mfloat-abi=hard','-nostartfiles','-Wl,--gc-sections,-e,'+GETTERS[0]+','+roots+',-Ttext=0x100000',*objects,'-o',str(out/'original.elf')])
    run([TOOL+'objcopy','-O','binary',str(out/'original.elf'),str(out/'original.bin')])
    nm=run([TOOL+'nm','-g','--defined-only',str(out/'original.elf')]);symbols={p[2]:int(p[0],16) for line in nm.splitlines() if len(p:=line.split())==3}
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);cpu.mem_map(0x100000,0x10000);cpu.mem_write(0x100000,(out/'original.bin').read_bytes());cpu.mem_map(0x200000,0x10000);cpu.mem_map(0x300000,0x1000)
    def call(name,*args):
        cpu.reg_write(UC_ARM_REG_CPSR,0x10);cpu.reg_write(UC_ARM_REG_SP,0x208000);cpu.reg_write(UC_ARM_REG_LR,0x300000)
        for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):cpu.reg_write(reg,value&0xffffffff)
        cpu.emu_start(symbols[name],0x300000,count=1000);assert cpu.reg_read(UC_ARM_REG_PC)==0x300000
        return cpu.reg_read(UC_ARM_REG_R0)
    rng=random.Random(6403);words=[0,0xffffffff]+[1<<bit for bit in range(32)]+[rng.getrandbits(32) for _ in range(128)]
    physical=[];capture_report=None
    if a.capture:
        capture_report=inspect(a.capture,a.elf,a.allow_incomplete);vm=MappedCapture(a.capture,allow_incomplete=a.allow_incomplete)
        with a.elf.open('rb') as f:sy={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
        table=struct.unpack('<24I',vm.read(sy['data_020a0c80']+capture_report['ELF_anchor_bias'],96))
        for slot,ptr in enumerate(table):
            if not ptr:continue
            block=vm.u32(ptr+36);count=struct.unpack('<H',vm.read(block+6,2))[0];assert count<=65536
            for index in range(count):physical.append((slot,index,*struct.unpack('<2I',vm.read(block+8+8*index,8))))
    samples=[(word,rng.getrandbits(32)) for word in words]+[(first,second) for _,__,first,second in physical]
    getter_checks=0
    for first,second in samples:
        cpu.mem_write(0x204000,struct.pack('<2I',first,second));actual=(ctypes.c_uint32*10)();host.fixture_material_fields(first,second,actual)
        expected=[call(name,0x204000) for name in GETTERS]
        expected[6:]=[int(bool(v)) for v in expected[6:]]
        assert list(actual)==expected,(first,second,list(actual),expected);getter_checks+=10
    report={'status':'passed','original_ARM_getter_comparisons':getter_checks,'DS_N64_slope_normal_cases':6*4097,'physical_CLPS_entries':len(physical),'host_output':output,'source_sha256':{str(f.relative_to(ROOT)):sha(f) for f in [Path(__file__),test,*fixtures,*sources,ROOT/'source/integrated_native/movement/material_profile.hpp',ROOT/'upstream/include/Player.h',ROOT/'upstream/notes/archive/n64-decomp-cross-reference.md']},'host_core_sha256':sha(CORE/'host/libmotion64.a'),'original_ARM_elf_sha256':sha(out/'original.elf'),'directory':str(out),'limits':'Ordinary locomotion class mapping and original field decoding only. Nonzero DS effects/toxic/water are explicitly not ordinary mapped ground. No main-game hazard/warp/progression delegation, full material integration, emulator or physical FPS proof.'}
    uses=['solid','water','needs_DS_effect','unknown_friction'];counts={name:0 for name in uses}
    for _,__,first,second in physical:counts[uses[host.fixture_material_use(first,second)]]+=1
    report['physical_material_use_counts']=counts
    if capture_report:report.update(capture_sha256=capture_report['VM_sha256'],capture_complete=capture_report['capture']['complete'],capture_executable_text_byte_exact=capture_report['executable_text_byte_exact'])
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(ROOT/'build/movement-adapter/material-profile-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':main()
