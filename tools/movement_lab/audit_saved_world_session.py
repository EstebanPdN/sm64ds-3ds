#!/usr/bin/env python3
"""Run the complete host N64 session with original ARM DS queries from saved RAM.

Frozen world snapshot: no live actor/GPU/gameplay or console performance claim.
The host fixture forwards fixed-width records to a fresh Unicorn copy; it does
not use the old converted N64 collision mesh.
"""
from pathlib import Path
import argparse,ctypes,hashlib,json,math,struct,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from inspect_movement_colliders import inspect
from read_mapped_capture import MappedCapture
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_CPSR,UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC
ROOT=Path(__file__).resolve().parents[2];CORE=ROOT/'build/movement-lab/core'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--elf',required=True,type=Path);p.add_argument('--allow-incomplete',action='store_true');p.add_argument('--replay-report',type=Path,default=ROOT/'build/movement-adapter/captured-ground-gate-latest.json');p.add_argument('--baseline-report',type=Path);a=p.parse_args()
    inventory=inspect(a.capture,a.elf,a.allow_incomplete);assert inventory['ELF_anchor_bias']==0
    replay=json.loads(a.replay_report.read_text());assert replay['status']=='passed' and replay['offline_ray_code_redirected']
    assert replay['capture_sha256']==sha(a.capture) and replay['elf_sha256']==sha(a.elf)
    for name,digest in replay['source_sha256'].items():assert sha(ROOT/name)==digest,name
    manifest=json.loads((CORE/'host/sources.json').read_text())
    for name,digest in manifest.items():assert sha(Path(name))==digest,name
    overlay_path=Path(replay['directory'])/'query.elf';assert sha(overlay_path)==replay['overlay_sha256']
    out=ROOT/'build/movement-adapter'/('saved-world-session-'+str(time.time_ns()));out.mkdir(parents=True)
    commands=[]
    def run(command):
        commands.append(command);r=subprocess.run(command,capture_output=True,text=True)
        with (out/'build.log').open('a') as f:f.write(r.stdout+r.stderr)
        r.check_returncode();return r.stdout.strip()
    includes=['-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(CORE/'source/src'),'-I'+str(CORE/'source/src/decomp/include')]
    fixture=ROOT/'tests/movement_lab/saved_world_session.cpp';globals_fixture=ROOT/'tests/movement_lab/solver_contacts_fixture.c'
    run(['clang','-std=gnu11','-O2','-DGBI_FLOATS','-DVERSION_US','-DNO_SEGMENTED_MEMORY',*includes,'-c',str(globals_fixture),'-o',str(out/'globals.o')])
    run(['clang++','-std=c++17','-O2','-ffp-contract=off','-dynamiclib',*includes,str(fixture),str(out/'globals.o'),str(CORE/'host/libmotion64.a'),'-lm','-o',str(out/'session.dylib')])
    vm=MappedCapture(a.capture,allow_incomplete=a.allow_incomplete);cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);mapped=[]
    for region in vm.regions:
        if not region['captured_bytes'] or region['captured_bytes']!=region['size']:continue
        address=region['address'];size=region['size'];assert not address%4096 and not size%4096
        cpu.mem_map(address,size);cpu.mem_write(address,vm.read(address,size));mapped.append((address,size))
    for address,size in [(0xf000000,0x80000),(0xe000000,0x10000)]:
        assert all(address+size<=start or start+length<=address for start,length in mapped)
        cpu.mem_map(address,size)
    with overlay_path.open('rb') as f:
        binary=ELFFile(f);symbols={s.name:s['st_value'] for s in binary.get_section_by_name('.symtab').iter_symbols()}
        for section in binary.iter_sections():
            if not section['sh_flags']&2 or not section['sh_size']:continue
            address=section['sh_addr'];assert 0xf000000<=address and address+section['sh_size']<=0xf080000
            if section['sh_type']!='SHT_NOBITS':cpu.mem_write(address,section.data())
    cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);cpu.reg_write(UC_ARM_REG_FPEXC,1<<30)
    cpu.mem_write(symbols['_ZN7dBgW_Kc10DetectClsnER9dBgCh_Lin'],struct.pack('<2I',0xe51ff004,symbols['_ZN7dBgW_Kc14DetectClsnSafeER9dBgCh_Lin']))
    POS,HIT,PRISM,STOP,STACK=0xe000000,0xe000100,0xe000200,0xe000f00,0xe00f000
    counts={};errors=[]
    def call(name,*args):
        counts[name]=counts.get(name,0)+1
        cpu.reg_write(UC_ARM_REG_CPSR,0x10);cpu.reg_write(UC_ARM_REG_SP,STACK);cpu.reg_write(UC_ARM_REG_LR,STOP)
        for index,reg in enumerate([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]):cpu.reg_write(reg,(args[index] if index<len(args) else 0)&0xffffffff)
        cpu.emu_start(symbols[name],STOP,count=3000000)
        assert cpu.reg_read(UC_ARM_REG_PC)==STOP and cpu.reg_read(UC_ARM_REG_SP)==STACK,name
        raw=cpu.reg_read(UC_ARM_REG_R0);return raw if raw<0x80000000 else raw-0x100000000
    Bridge=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint)
    @Bridge
    def bridge(tag,input_ptr,input_size,output_ptr,output_size):
        try:
            data=ctypes.string_at(input_ptr,input_size);result=b''
            if tag==1:
                assert input_size==4 and not output_size
                return call('sm64ds_world_prism_count',struct.unpack('<I',data)[0])
            if tag==2:
                assert input_size==8 and output_size==152
                status=call('sm64ds_world_prism_at',*struct.unpack('<2I',data),HIT,PRISM)
                if status==1:result=bytes(cpu.mem_read(HIT,40))+bytes(cpu.mem_read(PRISM,112))
            elif tag==3:
                assert input_size==40 and output_size==112;cpu.mem_write(HIT,data)
                status=call('sm64ds_contact_prism',HIT,PRISM)
                if status==1:result=bytes(cpu.mem_read(PRISM,112))
            elif tag==4:
                assert input_size==48 and not output_size;cpu.mem_write(HIT,data[:40])
                actor=struct.unpack_from('<I',data,40)[0]
                return call('sm64ds_contact_pass_through',HIT,actor,data[44],data[45])
            elif tag in (5,6):
                assert input_size==16 and output_size==40;cpu.mem_write(POS,data[:12])
                actor=struct.unpack_from('<I',data,12)[0]
                status=call('sm64ds_motion_ground' if tag==5 else 'sm64ds_motion_ceiling',POS,actor,HIT)
                if status==1:result=bytes(cpu.mem_read(HIT,40))
            elif tag==7:
                assert input_size==32 and output_size==16;cpu.mem_write(POS,data)
                status=call('sm64ds_platform_displace',POS,POS+20,HIT)
                if status==1:result=bytes(cpu.mem_read(HIT,16))
            else:raise ValueError('Unknown bridge operation')
            if result:assert len(result)==output_size;ctypes.memmove(output_ptr,result,len(result))
            return status
        except Exception as error:
            errors.append({'tag':tag,'error':str(error),'pc':hex(cpu.reg_read(UC_ARM_REG_PC))});return -1
    library=ctypes.CDLL(str(out/'session.dylib'));library.fixture_saved_bridge.argtypes=[Bridge];library.fixture_saved_bridge(bridge)
    library.fixture_saved_create.argtypes=[ctypes.c_float]*3
    library.fixture_saved_step.argtypes=[ctypes.c_int]*6+[ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_uint32)]
    samples=[];maximum_y=-math.inf;wall_runs=[];wall_ticks=0
    try:
        assert library.fixture_saved_open()==1,errors
        assert not library.fixture_saved_reset_while_active()
        mario=library.fixture_saved_create(-1200,300,6800);assert mario>=0,(mario,errors,library.fixture_saved_faults())
        initial=None
        for tick in range(180):
            x=0 if tick<60 else 156 if tick<120 else 0
            y=0 if tick<120 else 156
            output=(ctypes.c_float*8)();action=ctypes.c_uint32()
            ok=library.fixture_saved_step(mario,x,y,int(90<=tick<100),0,0,output,ctypes.byref(action))
            assert ok and not errors,(tick,list(output),errors,library.fixture_saved_faults())
            assert all(math.isfinite(value) for value in output)
            if initial is None:initial=list(output[:3])
            maximum_y=max(maximum_y,output[1])
            if tick%15==0 or tick==179:samples.append({'tick':tick,'circle_pad':[x,y],'position':list(output[:3]),'velocity':list(output[3:6]),'action':action.value})
        assert not library.fixture_saved_faults()
        displacement=math.hypot(output[0]-initial[0],output[2]-initial[2]);assert displacement>100
        assert library.fixture_saved_remove(mario)==1
        records_path=Path(replay['directory'])/'wall-records.bin';assert sha(records_path)==replay['wall_records_sha256']
        raw_records=records_path.read_bytes();known_hits={raw_records[4+i*152+112:4+(i+1)*152] for i in range(struct.unpack_from('<I',raw_records)[0])}
        cases=(Path(replay['directory'])/'wall-cases.bin').read_bytes()
        assert struct.unpack_from('<I',cases)[0]==len(replay['world_wall_reference_samples'])
        for index,case in enumerate(replay['world_wall_reference_samples']):
            if not case['status']:continue
            offset=4+index*200;point=struct.unpack_from('<5f',cases,offset);assert list(point)==case['input']
            contact=cases[offset+40:offset+80];assert contact in known_hits
            normal=struct.unpack_from('<3i',contact,4)
            spawn=[point[0],point[1]-60,point[2]]
            cpu.mem_write(POS,struct.pack('<3i',*(math.trunc(value) for value in spawn)))
            if not call('sm64ds_motion_ground',POS,0,HIT):
                wall_runs.append({'case':index,'status':'no admitted floor at directed spawn','spawn':spawn});continue
            mario=library.fixture_saved_create(*spawn);assert mario>=0,(index,spawn,errors,library.fixture_saved_faults())
            x=round(-normal[0]*156/4096);y=round(normal[2]*156/4096)
            before=counts.get('sm64ds_contact_pass_through',0)
            for tick in range(30):
                output=(ctypes.c_float*8)();action=ctypes.c_uint32()
                ok=library.fixture_saved_step(mario,x,y,0,0,0,output,ctypes.byref(action))
                assert ok and not errors,(index,tick,spawn,errors,library.fixture_saved_faults())
                assert all(math.isfinite(value) for value in output);wall_ticks+=1
            hits=counts.get('sm64ds_contact_pass_through',0)-before
            wall_runs.append({'case':index,'status':'passed','spawn':spawn,'circle_pad':[x,y],'ticks':30,'native_wall_filter_calls':hits,'final_position':list(output[:3])})
            assert library.fixture_saved_remove(mario)==1
        assert sum(run.get('native_wall_filter_calls',0)>0 for run in wall_runs)>=2,wall_runs
        assert not library.fixture_saved_faults()
        ordinary_counts=dict(counts);delegation_checks=[]
        assert library.fixture_saved_close()==1
        cpu.mem_write(POS,struct.pack('<3i',-1200,300,6800))
        assert call('sm64ds_motion_ground',POS,0,HIT)==1
        contact=bytes(cpu.mem_read(HIT,40));collider=struct.unpack_from('<I',contact,24)[0]
        assert call('sm64ds_contact_prism',HIT,PRISM)==1
        attribute=struct.unpack_from('<H',cpu.mem_read(PRISM,112),40)[0]
        clps=struct.unpack('<I',cpu.mem_read(collider+0x24,4))[0]
        stride,count=struct.unpack('<2H',cpu.mem_read(clps+4,4));assert stride==8 and attribute<count
        entry=clps+8+attribute*8;original=bytes(cpu.mem_read(entry,8));assert original==contact[16:24]
        first,second=struct.unpack('<2I',original)
        for tag,word,reason in [('DS effect',(first&~(31<<19))|(6<<19),2),('unknown friction',(first&~(7<<12))|(6<<12),3)]:
            assert library.fixture_saved_open()==1
            mario=library.fixture_saved_create(-1200,300,6800);assert mario>=0
            cpu.mem_write(entry,struct.pack('<2I',word,second))
            try:
                output=(ctypes.c_float*8)(*([12345.0]*8));action=ctypes.c_uint32(0xdeadbeef)
                assert not library.fixture_saved_step(mario,0,0,0,0,0,output,ctypes.byref(action))
                assert list(output)==[12345.0]*8 and action.value==0xdeadbeef
                assert library.fixture_saved_delegation()==reason
                delegation_checks.append(dict(material=tag,reason=reason,staged_output_untouched=True))
            finally:
                cpu.mem_write(entry,original);assert library.fixture_saved_close()==1
                assert library.fixture_saved_delegation()==0
        counts=ordinary_counts
    finally:
        assert library.fixture_saved_close()==1
    sources=[Path(__file__),fixture,globals_fixture]+[ROOT/'source/integrated_native/movement'/name for name in ['ds_world_source.hpp','world_session.hpp','world_walls.hpp','stable_platforms.hpp','surface_description.hpp','material_profile.hpp','solver_contact_pins.hpp','stable_surfaces.hpp','collision_provider.h','collision_provider.c']]
    report={'status':'passed','delegation_checks':delegation_checks,'solver_ticks':180+wall_ticks,'walk_jump_ticks':180,'directed_wall_ticks':wall_ticks,'directed_wall_runs':wall_runs,'samples':samples,'maximum_y':maximum_y,'horizontal_displacement':displacement,'original_ARM_query_calls':counts,'bridge_errors':errors,'capture_sha256':sha(a.capture),'capture_complete':vm.complete,'elf_sha256':sha(a.elf),'replay_report_sha256':sha(a.replay_report),'overlay_sha256':sha(overlay_path),'host_core_sha256':sha(CORE/'host/libmotion64.a'),'source_sha256':{str(s.relative_to(ROOT)):sha(s) for s in sources},'directory':str(out),'limits':'Unsanitized host N64/DS-backend session with original ARM queries in a frozen saved-world CPU replay. No old converted mesh fallback. No live actors, GPU, player presentation/abilities, scene transitions, Azahar or physical FPS acceptance; not the final ten-scenario campaign.'}
    if a.baseline_report:
        baseline=json.loads(a.baseline_report.read_text())
        for key in ['capture_sha256','elf_sha256','host_core_sha256','solver_ticks','walk_jump_ticks','directed_wall_ticks','samples','directed_wall_runs','maximum_y','horizontal_displacement']:
            assert baseline[key]==report[key],key
        before=baseline['original_ARM_query_calls'];after=report['original_ARM_query_calls']
        assert after['sm64ds_world_prism_count']<before['sm64ds_world_prism_count'] and after['sm64ds_world_prism_at']<before['sm64ds_world_prism_at']
        for name in ['sm64ds_motion_ground','sm64ds_motion_ceiling','sm64ds_contact_prism','sm64ds_contact_pass_through']:assert after[name]==before[name],name
        report['all_saved_solver_outputs_equal_baseline']=True
        report['baseline_report_sha256']=sha(a.baseline_report)
        report['baseline_original_ARM_query_calls']=before
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(ROOT/'build/movement-adapter/saved-world-session-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('source_sha256','samples')},indent=2))
if __name__=='__main__':main()
