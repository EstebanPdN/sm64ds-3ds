#!/usr/bin/env python3
"""Execute the production world-query bridge and original registry walk on ARM.

An independently linked overlay calls the preserved collision subset's actual
ARM instructions. No live game state is modified and DS MMIO stays unmapped.
This gate covers static registry dispatch/metadata/exclusion, not moving
platform geometry or a production N64 controller.
"""
from pathlib import Path
import hashlib,json,random,struct,subprocess,time
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_CPSR

ROOT=Path(__file__).resolve().parents[2]
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
DS=ROOT/'build/native-collision'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def symbols(path):
    output=subprocess.check_output([TOOL+'nm','-g','--defined-only',str(path)],text=True)
    return {p[2]:int(p[0],16) for line in output.splitlines() if len(p:=line.split())==3}

def main():
    directory=ROOT/'build/movement-adapter'/('world-ground-'+str(time.time_ns()))
    directory.mkdir(parents=True)
    sources=[ROOT/'source/integrated_native/movement/ds_world_query.cpp']
    names=['_ZN9dBgCh_Gnd10DetectClsnEv.cpp',
           '_ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c.cpp',
           'func_02037464.c','func_020374d4.c','func_02035354.c','func_020393b4.c',
           'func_020393ac.c','func_0203939c.c','func_0203938c.c',
           'func_02037fec.c','func_02037f5c.c','Vec3_HorzDist.c']
    sources += [ROOT/'build/native-core/generated/src'/name for name in names]
    flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
           '-D__3DS__','-Os','-fno-strict-aliasing','-ffunction-sections',
           '-fdata-sections','-Wa,--noexecstack',
           '-I'+str(ROOT/'build/native-core/generated/include'),
           '-I'+str(ROOT/'source/integrated_native/movement')]
    commands=[]
    def run(command):
        commands.append(command)
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
        with (directory/'build.log').open('a') as log:log.write(result.stdout+result.stderr)
        result.check_returncode()
    objects=[]
    for index,source in enumerate(sources):
        obj=directory/(str(index)+'.o');objects.append(str(obj))
        cpp=source.suffix=='.cpp'
        run([TOOL+('g++' if cpp else 'gcc'),*flags,
             '-std='+('gnu++17' if cpp else 'gnu11'),'-c',str(source),'-o',str(obj)])
    overlay=directory/'query.elf'
    run([TOOL+'g++',*flags[:4],'-nostartfiles',
         '-Wl,--gc-sections,-z,noexecstack,--fatal-warnings,-e,sm64ds_world_ground,-u,sm64ds_motion_ground,-Ttext=0x180000,--just-symbols='+str(DS/'runtime.elf'),
         *objects,'-o',str(overlay)])
    run([TOOL+'objcopy','-O','binary',str(overlay),str(directory/'query.bin')])
    sy=symbols(DS/'runtime.elf');overlay_symbols=symbols(overlay);entry=overlay_symbols['sm64ds_world_ground']
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    base_binary=(DS/'runtime.bin').read_bytes();overlay_binary=(directory/'query.bin').read_bytes()
    assert len(base_binary)<0x80000 and len(overlay_binary)<0x80000
    cpu.mem_map(0x100000,0x100000);cpu.mem_write(0x100000,base_binary)
    cpu.mem_write(0x180000,overlay_binary)
    cpu.mem_map(0x200000,0x100000);cpu.mem_map(0x300000,0x10000);cpu.mem_map(0x400000,0x1000)
    COLLIDER,KCL,CLPS,POS,OUT,ACTOR=0x200000,0x210000,0x230000,0x240000,0x240020,0x250000
    cpu.mem_write(KCL,(DS/'castle.kcl').read_bytes());cpu.mem_write(CLPS,(DS/'castle.clps').read_bytes())
    calls=0
    def call(address,*args):
        nonlocal calls
        calls+=1;cpu.reg_write(UC_ARM_REG_CPSR,0x10)
        cpu.reg_write(UC_ARM_REG_SP,0x308000);cpu.reg_write(UC_ARM_REG_LR,0x400000)
        for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):cpu.reg_write(reg,value&0xffffffff)
        cpu.emu_start(address,0x400000,count=2000000)
        assert cpu.reg_read(UC_ARM_REG_PC)==0x400000 and cpu.reg_read(UC_ARM_REG_SP)==0x308000
        return cpu.reg_read(UC_ARM_REG_R0)
    assert call(sy['native_collision_init'],COLLIDER,KCL,CLPS)==1
    registry=sy['data_020a0c80'];cpu.mem_write(registry,bytes(24*4))
    def register(slot):
        cpu.mem_write(registry,bytes(24*4))
        if slot is not None:cpu.mem_write(registry+4*slot,struct.pack('<I',COLLIDER))
    def query(point,actor=0,probe=100*4096):
        cpu.mem_write(POS,struct.pack('<3i',*point));cpu.mem_write(OUT,b'\xcd'*40)
        found=call(entry,POS,actor,probe,OUT)
        if not found:
            assert bytes(cpu.mem_read(OUT,40))==b'\xcd'*40
            return None
        return struct.unpack('<4i4Ii2H',cpu.mem_read(OUT,40))
    point=(-1200*4096,1000*4096,6800*4096)
    assert query(point) is None
    # Exercise every registry slot through the original virtual ground query.
    for slot in range(24):
        register(slot);hit=query(point)
        assert hit and hit[:4]==(254*4096,0,4096,0)
        assert hit[6:9]==(COLLIDER,0,-1) and hit[10]==slot
        # The bridge preserves the source CLPS entry without reinterpretation.
        tri=hit[9];raw=(DS/'castle.kcl').read_bytes();tris=struct.unpack_from('<I',raw,8)[0]
        attribute=struct.unpack_from('<H',raw,tris+tri*16+14)[0]
        assert hit[4:6]==struct.unpack_from('<2I',(DS/'castle.clps').read_bytes(),8+attribute*8)
    register(0)
    rng=random.Random(6403)
    points=[(-1200,10000,6800)]+[(x,10000,z) for x in range(-6000,6001,500) for z in range(-6000,7501,500)]+[(rng.randrange(-4000,2000),10000,rng.randrange(2500,7000)) for _ in range(250)]
    floor_samples=[]
    for x,y,z in points:
        fix=(x*4096,y*4096,z*4096);cpu.mem_write(POS,struct.pack('<3i',*fix))
        found=call(sy['native_collision_ground'],COLLIDER,POS,-20000*4096,OUT)
        expected=struct.unpack('<4iH2x',cpu.mem_read(OUT,20)) if found else None
        hit=query(fix)
        assert bool(hit)==bool(expected),(x,z,expected,hit)
        if hit:
            assert hit[:4]==expected[:4] and hit[9]==expected[4]
            floor_samples.append((x,z,expected))
    slope_checks=0;slope_triangles=set();admission_differences=[]
    for x,z,expected in floor_samples:
        if not (expected[1] or expected[3]):continue
        threshold=-((-expected[0])//4096)-78
        for y in [threshold-1,threshold,threshold+1]:
            cpu.mem_write(POS,struct.pack('<3i',x,y,z));cpu.mem_write(OUT,b'\xcd'*40)
            found=call(overlay_symbols['sm64ds_motion_ground'],POS,0,OUT)
            actual=struct.unpack('<4i4Ii2H',cpu.mem_read(OUT,40)) if found else None
            if actual:assert actual[0]<=(y+78)*4096
            if y>=threshold and (not actual or actual[:4]!=expected[:4]):
                admission_differences.append({'position':[x,y,z],'expected':expected,'actual':actual})
            slope_checks+=1;slope_triangles.add(expected[4])
    (directory/'slope-admission.json').write_text(json.dumps(admission_differences,indent=2)+'\n')
    assert not admission_differences,('slope admission differs',len(admission_differences),str(directory))
    register(23)
    # Flat castle lawn is exactly Y254. Original N64 floor admission starts
    # at input Y176 (78 below it), including the equality boundary.
    admission_checks=0
    for y in [174,175,176,177,253,254,255,1000]:
        cpu.mem_write(POS,struct.pack('<3i',-1200,y,6800));cpu.mem_write(OUT,b'\xcd'*40)
        found=call(overlay_symbols['sm64ds_motion_ground'],POS,0,OUT)
        assert bool(found)==(y>=176),(y,found)
        if found:assert struct.unpack('<i',cpu.mem_read(OUT,4))[0]==254*4096
        else:assert bytes(cpu.mem_read(OUT,40))==b'\xcd'*40
        admission_checks+=1
    for values in [(2147483647,1000,0),(0,2147483647,0),(0,-2147483648,0),(0,-12000,0)]:
        cpu.mem_write(POS,struct.pack('<3i',*values));cpu.mem_write(OUT,b'\xcd'*40)
        assert not call(overlay_symbols['sm64ds_motion_ground'],POS,0,OUT)
        assert bytes(cpu.mem_read(OUT,40))==b'\xcd'*40
    # Test-only actor identity fixture, not a fabricated live game state.
    cpu.mem_write(ACTOR+4,struct.pack('<I',73))
    cpu.mem_write(COLLIDER+4,struct.pack('<Ii',ACTOR,73))
    hit=query(point);assert hit and hit[7:9]==(ACTOR,73)
    assert query(point,ACTOR) is None
    # Exclusion uses the owning actor pointer, not merely its numeric ID.
    cpu.mem_write(ACTOR+0x204,struct.pack('<I',73));assert query(point,ACTOR+0x200)
    cpu.mem_write(ACTOR+4,struct.pack('<I',74));assert query(point,ACTOR) is None
    assert query((100000*4096,1000*4096,0)) is None
    assert query(point,probe=-1) is None
    cpu.mem_write(OUT,b'\xcd'*40)
    assert not call(entry,0,0,0,OUT) and bytes(cpu.mem_read(OUT,40))==b'\xcd'*40
    assert not call(entry,POS,0,0,0)
    report={'status':'passed','scope':'ARM original registry dispatch and production bridge; static collider fixture only',
            'registry_slots':24,'differential_floor_queries':len(points),'differential_mismatches':0,
            'flat_floor_78_unit_admission_checks':admission_checks,'overflow_or_below_lower_limit_rejections':4,
            'slope_admission_checks':slope_checks,'slope_triangles':len(slope_triangles),
            'owner_identity_exclusion':True,'distinct_actor_same_id_retained':True,
            'raw_clps_and_world_normal_preserved':True,'miss_leaves_output_unchanged':True,
            'DS_MMIO_mapped':False,'calls':calls,'sources_sha256':{str(p.relative_to(ROOT)):sha(p) for p in sources},
            'bridge_header_sha256':sha(ROOT/'source/integrated_native/movement/ds_world_query.h'),
            'audit_script_sha256':sha(Path(__file__)),
            'base_elf_sha256':sha(DS/'runtime.elf'),'base_binary_sha256':sha(DS/'runtime.bin'),
            'overlay_sha256':sha(overlay),'castle_kcl_sha256':sha(DS/'castle.kcl'),'castle_clps_sha256':sha(DS/'castle.clps'),
            'directory':str(directory),'limits':'No moving-platform geometry, stable N64 surface cache, live Hybrid/N64 actor, GPU or physical FPS acceptance.'}
    (directory/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (ROOT/'build/movement-adapter/world-ground-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='sources_sha256'},indent=2))

if __name__=='__main__':main()
