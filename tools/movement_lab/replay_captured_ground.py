#!/usr/bin/env python3
"""Replay saved physical-capture collider state in Unicorn, never a live process.

Incomplete captures require opt-in and only wholly saved readable mappings
are installed. Exact ELF.text and independent anchors must match first.
This is a CPU collision replay, not a whole-RAM, physical runtime or FPS test.
"""
from pathlib import Path
import argparse,hashlib,json,math,struct,subprocess,sys,time
from fractions import Fraction
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from read_mapped_capture import MappedCapture
from inspect_movement_colliders import inspect
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_CPSR,UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC
ROOT=Path(__file__).resolve().parents[2];TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--elf',required=True,type=Path);p.add_argument('--allow-incomplete',action='store_true');p.add_argument('--safe-rays',action='store_true');p.add_argument('--baseline-report',type=Path);a=p.parse_args()
    inventory=inspect(a.capture,a.elf,a.allow_incomplete)
    vm=MappedCapture(a.capture,allow_incomplete=a.allow_incomplete)
    assert inventory['ELF_anchor_bias']==0,'Relocated symbol overlay requires explicit address rewriting'
    with a.elf.open('rb') as f:sy={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    out=ROOT/'build/movement-adapter'/('capture-ground-'+str(time.time_ns()));out.mkdir(parents=True)
    sources=[ROOT/'source/integrated_native/movement/ds_world_query.cpp',ROOT/'tests/movement_lab/captured_ground.cpp',ROOT/'source/integrated_native/movement/ds_world_walls.cpp']
    flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-mfpu=vfp','-Os','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-Wa,--noexecstack','-I'+str(ROOT/'build/native-core/generated/include'),'-I'+str(ROOT/'source/integrated_native/movement'),'-std=gnu++17']
    commands=[]
    def run(command):
        commands.append(command);r=subprocess.run(command,capture_output=True,text=True)
        with (out/'build.log').open('a') as log:log.write(r.stdout+r.stderr)
        r.check_returncode()
    objects=[]
    for index,source in enumerate(sources):
        obj=out/(str(index)+'.o');objects.append(str(obj));run([TOOL+'g++',*flags,'-c',str(source),'-o',str(obj)])
    safe_symbol='_ZN7dBgW_Kc14DetectClsnSafeER9dBgCh_Lin'
    if a.safe_rays:
        from prepare_integrated_collision import derive,RELATIVE
        ray_source=out/'safe-ray.cpp';ray_source.write_text(derive((ROOT/'third_party/sm64ds-3ds'/RELATIVE).read_text()))
        sources.append(ray_source);objects.append(str(out/'safe-ray.o'))
        run([TOOL+'g++',*flags,'-mfpu=vfp','-DDetectClsn=DetectClsnSafe','-Dg_walk_dbg=sm64ds_wide_ray_dbg','-c',str(ray_source),'-o',objects[-1]])
    extra_roots=',-u,'+safe_symbol if a.safe_rays else ''
    run([TOOL+'g++',*flags[:4],'-mfpu=vfp','-nostartfiles','-Wl,--gc-sections,-z,noexecstack,--fatal-warnings,-e,sm64ds_world_ground,-u,sm64ds_motion_ground,-u,fixture_collider_ground,-u,sm64ds_world_water_ground,-u,fixture_transform_collider,-u,sm64ds_world_line,-u,fixture_collider_line,-u,sm64ds_motion_ceiling,-u,sm64ds_contact_prism,-u,fixture_prism_contact,-u,sm64ds_world_prism_count,-u,sm64ds_world_prism_at,-u,sm64ds_contact_pass_through,-u,sm64ds_world_walls_reference,-u,fixture_wall_cache_build,-u,fixture_wall_cache_query,-u,fixture_wall_cache_size,-u,fixture_wall_cache_reads,-u,sm64ds_platform_displace,-u,fixture_original_platform_displace'+extra_roots+',-Ttext=0xf000000,--just-symbols='+str(a.elf),*objects,'-o',str(out/'query.elf')])
    run([TOOL+'objcopy','-O','binary',str(out/'query.elf'),str(out/'query.bin')])
    nm=subprocess.check_output([TOOL+'nm','-g','--defined-only',str(out/'query.elf')],text=True)
    overlay={p[2]:int(p[0],16) for line in nm.splitlines() if len(p:=line.split())==3}
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);mapped=[]
    for region in vm.regions:
        if not region['captured_bytes'] or region['captured_bytes']!=region['size']:continue
        address=region['address'];size=region['size'];assert not address%4096 and not size%4096
        cpu.mem_map(address,size);cpu.mem_write(address,vm.read(address,size));mapped.append((address,size))
    for address,size in [(0xf000000,0x80000),(0xe000000,0x10000)]:
        assert all(address+size<=start or start+length<=address for start,length in mapped)
        cpu.mem_map(address,size)
    code=(out/'query.bin').read_bytes();assert len(code)<0x10000;cpu.mem_write(0xf000000,code)
    cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);cpu.reg_write(UC_ARM_REG_FPEXC,1<<30)
    if a.safe_rays:
        # Redirect only this offline CPU copy; retained dump/ELF bytes stay intact.
        cpu.mem_write(sy['_ZN7dBgW_Kc10DetectClsnER9dBgCh_Lin'],struct.pack('<2I',0xe51ff004,overlay[safe_symbol]))
    POS,RESULT,STOP,STACK=0xe000000,0xe000020,0xe000100,0xe00f000
    calls=0;world_packets=[]
    def call(name,args):
        nonlocal calls
        calls+=1;cpu.reg_write(UC_ARM_REG_CPSR,0x10);cpu.reg_write(UC_ARM_REG_SP,STACK);cpu.reg_write(UC_ARM_REG_LR,STOP)
        for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):cpu.reg_write(reg,value&0xffffffff)
        try:cpu.emu_start(overlay[name],STOP,count=50000000 if name in ('sm64ds_world_walls_reference','fixture_wall_cache_build','fixture_wall_cache_query') else 3000000)
        except Exception as error:raise RuntimeError(f'{name} PC={cpu.reg_read(UC_ARM_REG_PC):#x}: {error}') from error
        assert cpu.reg_read(UC_ARM_REG_PC)==STOP and cpu.reg_read(UC_ARM_REG_SP)==STACK
        return cpu.reg_read(UC_ARM_REG_R0)
    def query(name,point,collider=None):
        cpu.mem_write(POS,struct.pack('<3i',*point));cpu.mem_write(RESULT,b'\xcd'*40)
        args=[POS,0,496*4096,RESULT] if collider is None else [collider,POS,496*4096,RESULT]
        found=call(name,args)
        return list(struct.unpack('<4i4Ii2H',cpu.mem_read(RESULT,40))) if found else None
    registry=struct.unpack('<24I',vm.read(sy['data_020a0c80'],96));samples=[];transform_samples=[];platform_samples=[];platform_rejections=0
    for collider in inventory['colliders']:
        if not collider.get('moving'):continue
        ptr=registry[collider['slot']];kcl=vm.u32(ptr+0x20);positions,normals,tris,octree=struct.unpack('<4I',vm.read(kcl,16));matrix=struct.unpack('<12i',vm.read(ptr+0x134,48))
        for tri in range(1,(octree-tris)//16):
            extent,vi,ni,e1,e2,e3,attribute=struct.unpack('<i6H',vm.read(tris+16*tri,16))
            anchor=[v/64 for v in struct.unpack('<3i',vm.read(positions+vi*12,12))]
            ns=[[v/1024 for v in struct.unpack('<3h',vm.read(normals+6*index,6))] for index in [ni,e1,e2,e3]]
            vertices=[anchor]
            for edge in ns[1:3]:
                direction=cross(ns[0],edge);factor=(extent/65536)/dot(direction,ns[3]);vertices.append([x+d*factor for x,d in zip(anchor,direction)])
            world=[[sum(matrix[row+3*col]*v[col] for col in range(3))/4096+matrix[9+row]/4096 for row in range(3)] for v in vertices]
            center=[sum(v[i] for v in world)/3 for i in range(3)]
            point=[round(center[0]*4096),math.ceil((max(v[1] for v in world)+20)*4096),round(center[2]*4096)]
            direct=query('fixture_collider_ground',point,ptr);global_hit=query('sm64ds_world_water_ground',point)
            assert direct,('moving triangle missed',tri,point)
            assert global_hit and global_hit[:6]==direct[:6] and global_hit[6]==ptr and global_hit[10]==collider['slot'],(direct,global_hit)
            assert direct[4]&0x20,'This fixture expects the saved moving water collider'
            solid=query('sm64ds_world_ground',point);assert not solid or solid[6]!=ptr
            samples.append({'slot':collider['slot'],'triangle':tri,'query_fix12':point,'height_fix12':direct[0],'normal_fix12':direct[1:4],'clps':direct[4:6],'ordinary_ground_excludes_water':True})
            # Controlled offline mutations through the original Transform
            # method. The saved capture and the user's running game are untouched.
            poses=[('translated',[4096,0,0,0,4096,0,0,0,4096,128*4096,64*4096,-96*4096],0),
                   ('quarter_yaw',[0,0,-4096,0,4096,0,4096,0,0,0,32*4096,0],16384),
                   ('pitched',[4096,0,0,0,3956,1060,0,-1060,3956,0,64*4096,0],0)]
            for label,pose,yaw in poses:
                previous_matrix=struct.unpack('<12i',cpu.mem_read(ptr+0x54,48));previous_yaw=struct.unpack('<h',cpu.mem_read(ptr+0x114,2))[0]
                cpu.mem_write(0xe000200,struct.pack('<12i',*pose));call('fixture_transform_collider',[ptr,0xe000200,yaw])
                transformed=[[sum(pose[row+3*col]*v[col] for col in range(3))/4096+pose[9+row]/4096 for row in range(3)] for v in vertices]
                mid=[sum(v[i] for v in transformed)/3 for i in range(3)]
                probe=[round(mid[0]*4096),math.ceil((mid[1]+20)*4096),round(mid[2]*4096)]
                direct=query('fixture_collider_ground',probe,ptr);world_hit=query('sm64ds_world_water_ground',probe)
                assert direct and world_hit and world_hit[:6]==direct[:6] and world_hit[6]==ptr,(label,tri,direct,world_hit)
                # Surface normal follows the original local-to-world rotation.
                expected_normal=[sum(pose[row+3*col]*ns[0][col] for col in range(3)) for row in range(3)]
                assert all(abs(a-b)<=1 for a,b in zip(direct[1:4],expected_normal)),(label,direct,expected_normal)
                # Ray interpolation is Fix12, so allow one world unit here;
                # retain the measured error rather than claiming exact geometry.
                height_error=abs(direct[0]/4096-mid[1]);assert height_error<1,(label,height_error)
                assert call('fixture_prism_contact',[collider['slot'],tri,RESULT])==1
                assert call('sm64ds_contact_prism',[RESULT,0xe000400])==1
                packet=bytes(cpu.mem_read(0xe000400,112))
                world_packets.append(packet+bytes(cpu.mem_read(RESULT+4,12)))
                assert packet[48:96]==struct.pack('<12i',*pose)
                expected_anchor=[sum(pose[row+3*col]*round(anchor[col]*4096) for col in range(3))//4096+pose[9+row] for row in range(3)]
                actual_anchor=struct.unpack_from('<3i',packet,100)
                assert all(abs(a-b)<=1 for a,b in zip(actual_anchor,expected_anchor)),(label,actual_anchor,expected_anchor)
                local_point=[sum(v[i] for v in vertices)/3 for i in range(3)]
                before=[sum(previous_matrix[row+3*col]*local_point[col] for col in range(3))+previous_matrix[9+row] for row in range(3)]
                expected=[sum(pose[row+3*col]*local_point[col] for col in range(3))+pose[9+row] for row in range(3)]
                cpu.mem_write(POS,struct.pack('<3i',*(round(v) for v in before)))
                reference=struct.pack('<3Ii2H',ptr,vm.u32(ptr+4),kcl,struct.unpack('<i',vm.read(ptr+8,4))[0],collider['slot'],1)
                cpu.mem_write(0xe000800,reference);cpu.mem_write(0xe000820,b'\xcd'*16)
                assert call('sm64ds_platform_displace',[0xe000800,POS,0xe000820])==1
                assert call('fixture_original_platform_displace',[ptr,POS,0xe000840])==1
                actual=bytes(cpu.mem_read(0xe000820,16));assert actual==bytes(cpu.mem_read(0xe000840,16))
                transported=struct.unpack('<4i',actual);expected_yaw=(yaw-previous_yaw+32768)%65536-32768
                error=max(abs(a-b)/4096 for a,b in zip(transported,expected))
                assert error<1 and transported[3]==expected_yaw,(label,transported,expected,error)
                platform_samples.append({'pose':label,'triangle':tri,'maximum_world_error':error,'yaw_delta':transported[3],'original_virtual_result_exact':True})
                if tri==1:
                    for offset,fmt,value in [(12,'i',-1234567),(8,'I',0),(16,'H',24),(18,'H',0)]:
                        damaged=bytearray(reference);struct.pack_into('<'+fmt,damaged,offset,value)
                        cpu.mem_write(0xe000800,bytes(damaged));cpu.mem_write(0xe000820,b'\xcd'*16)
                        assert not call('sm64ds_platform_displace',[0xe000800,POS,0xe000820])
                        assert bytes(cpu.mem_read(0xe000820,16))==b'\xcd'*16;platform_rejections+=1
                    cpu.mem_write(0xe000800,reference)
                transform_samples.append({'pose':label,'triangle':tri,'height_error_world':height_error,'normal_fix12':direct[1:4]})
            cpu.mem_write(0xe000200,struct.pack('<12i',*matrix));call('fixture_transform_collider',[ptr,0xe000200,0])
    assert samples,'No saved moving collider was tested'
    scale_y_samples=[]
    for original in inventory['colliders']:
        if original.get('vtable')!='_ZTV10dBgW_KcMbg':continue
        slot=original['slot'];ptr=registry[slot];clone=0xe100000
        assert all(clone+0x10000<=start or start+length<=clone for start,length in mapped)
        cpu.mem_map(clone,0x10000);cpu.mem_write(clone,vm.read(ptr,0x1c8)+bytes(8))
        cpu.mem_write(clone,struct.pack('<I',sy['_ZTV14dBgW_KcMbgSclY']+8))
        identity=[4096,0,0,0,4096,0,0,0,4096,0,0,0]
        for offset in [0x54,0x84,0xb4,0xe4,0x134,0x168,0x198]:cpu.mem_write(clone+offset,struct.pack('<12i',*identity))
        cpu.mem_write(clone+0x50,struct.pack('<i',4096));cpu.mem_write(clone+0x164,struct.pack('<i',4096))
        cpu.mem_write(sy['data_020a0c80']+slot*4,struct.pack('<I',clone))
        kcl=vm.u32(ptr+32);position_array,_,triangle_array,_=struct.unpack('<4I',vm.read(kcl,16))
        for scale in [2048,4096,8192]:
            cpu.mem_write(clone+0x1c8,struct.pack('<2i',scale,4096*4096//scale))
            point=samples[0]['query_fix12'].copy();point[1]=10*4096
            contact=query('sm64ds_world_water_ground',point);assert contact and contact[6]==clone
            packed=struct.pack('<6i4Ii2H',point[0],contact[0],point[2],*contact[1:4],*contact[4:8],*contact[8:11])
            cpu.mem_write(RESULT,packed);cpu.mem_write(0xe000300,struct.pack('<d',123.25))
            assert call('sm64ds_contact_plane_height',[RESULT,POS,0xe000300])==1
            actual=struct.unpack('<d',cpu.mem_read(0xe000300,8))[0]
            index=struct.unpack('<H',vm.read(triangle_array+16*contact[9]+4,2))[0]
            anchor_y=struct.unpack('<i',vm.read(position_array+12*index+4,4))[0]
            expected=(anchor_y*64*scale+0x800)>>12
            assert actual==expected,(scale,actual,expected)
            assert call('fixture_prism_contact',[slot,contact[9],RESULT])==1
            assert call('sm64ds_contact_prism',[RESULT,0xe000400])==1
            packet=bytes(cpu.mem_read(0xe000400,112))
            world_packets.append(packet+bytes(cpu.mem_read(RESULT+4,12)))
            assert struct.unpack_from('<H',packet,42)[0]==2
            assert struct.unpack_from('<i',packet,96)[0]==scale
            assert struct.unpack_from('<i',packet,104)[0]==expected
            scale_y_samples.append({'scale_fix12':scale,'plane_height_fix12':actual,'expected_height_fix12':expected})
        cpu.mem_write(sy['data_020a0c80']+slot*4,struct.pack('<I',ptr));break
    assert len(scale_y_samples)==3
    ceiling_samples=[];long_ray_samples=[];ceiling_admission=[];static=registry[0];kcl=vm.u32(static+32);positions,normals,tris,octree=struct.unpack('<4I',vm.read(kcl,16))
    for tri in range(1,(octree-tris)//16):
        extent,vi,ni,e1,e2,e3,attribute=struct.unpack('<i6H',vm.read(tris+16*tri,16))
        ns=[[v/1024 for v in struct.unpack('<3h',vm.read(normals+6*index,6))] for index in [ni,e1,e2,e3]]
        if ns[0][1]>=-.1:continue
        anchor=[v/64 for v in struct.unpack('<3i',vm.read(positions+vi*12,12))];vertices=[anchor]
        for edge in ns[1:3]:
            direction=cross(ns[0],edge);factor=(extent/65536)/dot(direction,ns[3]);vertices.append([x+d*factor for x,d in zip(anchor,direction)])
        center=[sum(v[i] for v in vertices)/3 for i in range(3)]
        start=[round(center[0]*4096),round((center[1]-10)*4096),round(center[2]*4096)];end=start.copy();end[1]+=20*4096
        cpu.mem_write(POS,struct.pack('<6i',*start,*end));cpu.mem_write(RESULT,b'\xcd'*48)
        found=call('fixture_collider_line',[static,POS,RESULT])
        direct=list(struct.unpack('<6i4Ii2H',cpu.mem_read(RESULT,48))) if found else None
        cpu.mem_write(RESULT,b'\xcd'*48);found=call('sm64ds_world_line',[POS,POS+12,0,RESULT])
        hit=list(struct.unpack('<6i4Ii2H',cpu.mem_read(RESULT,48))) if found else None
        assert bool(hit)==bool(direct),(tri,direct,hit)
        if direct:
            assert hit[:8]==direct[:8] and hit[8]==static and hit[12]==0
            ceiling_samples.append({'triangle':tri,'height_fix12':hit[1],'normal_fix12':hit[3:6]})
            long_end=end.copy();long_end[1]=100000*4096
            cpu.mem_write(POS,struct.pack('<6i',*start,*long_end));cpu.mem_write(RESULT,b'\xcd'*48)
            found=call('sm64ds_world_line',[POS,POS+12,0,RESULT])
            long_hit=list(struct.unpack('<6i4Ii2H',cpu.mem_read(RESULT,48))) if found else None
            long_ray_samples.append({'triangle':tri,'short_height_fix12':hit[1],'long_height_fix12':long_hit[1] if long_hit else None,'long_triangle':long_hit[11] if long_hit else None})
            if a.safe_rays:
                n=ns[0];expected=anchor[1]*4096-((start[0]-anchor[0]*4096)*n[0]+(start[2]-anchor[2]*4096)*n[2])/n[1]
                tolerance=64*(1+(abs(n[0])+abs(n[2]))/abs(n[1]))
                assert long_hit and abs(long_hit[1]-expected)<=tolerance,(tri,long_hit,expected,tolerance)
                long_ray_samples[-1]['plane_error_world']=abs(long_hit[1]-expected)/4096
                wx,wz=math.trunc(center[0]),math.trunc(center[2]);raw_normal=struct.unpack('<3h',vm.read(normals+6*ni,6));raw_anchor=struct.unpack('<3i',vm.read(positions+12*vi,12))
                exact=Fraction(raw_anchor[1]*64)-Fraction((wx*4096-raw_anchor[0]*64)*raw_normal[0]+(wz*4096-raw_anchor[2]*64)*raw_normal[2],raw_normal[1])
                boundary=exact//4096+78
                for wy in [boundary-1,boundary,boundary+1]:
                    cpu.mem_write(POS,struct.pack('<3i',wx,wy,wz));cpu.mem_write(RESULT,b'\xcd'*40)
                    answer=call('sm64ds_motion_ceiling',[POS,0,RESULT]);assert answer in (0,1),(tri,wy,hex(answer))
                    contact=list(struct.unpack('<4i4Ii2H',cpu.mem_read(RESULT,40))) if answer else None
                    if wy<=boundary:assert contact and abs(Fraction(contact[0])-exact)<=Fraction(501,1000),(tri,wy,contact,float(exact))
                    if contact:assert contact[0]>=(wy-78)*4096
                    else:assert bytes(cpu.mem_read(RESULT,40))==b'\xcd'*40
                    ceiling_admission.append({'triangle':tri,'input_y':wy,'hit':bool(answer),'height_fix12':contact[0] if contact else None})
        if len(ceiling_samples)>=20:break
    assert len(ceiling_samples)>=10,'Insufficient saved ceiling coverage'
    ceiling_negative_checks=0
    if a.safe_rays:
        for point,expected in [([100000,0,100000],0),([0,100078,0],0),([2147483647,0,0],0xffffffff),([0,-524287,0],0xffffffff)]:
            cpu.mem_write(POS,struct.pack('<3i',*point));cpu.mem_write(RESULT,b'\xcd'*40)
            assert call('sm64ds_motion_ceiling',[POS,0,RESULT])==expected
            assert bytes(cpu.mem_read(RESULT,40))==b'\xcd'*40;ceiling_negative_checks+=1
        assert call('sm64ds_motion_ceiling',[0,0,RESULT])==0xffffffff
        assert call('sm64ds_motion_ceiling',[POS,0,0])==0xffffffff
        ceiling_negative_checks+=2
    invalid_lines=0
    for start,end in [([0,0,0],[0,0,0]),([2147483647,0,0],[1,0,0]),([-2147483648,0,0],[2147483647,0,0]),([0,0,0],[2147483647,2147483647,0])]:
        cpu.mem_write(POS,struct.pack('<6i',*start,*end));cpu.mem_write(RESULT,b'\xcd'*48)
        assert not call('sm64ds_world_line',[POS,POS+12,0,RESULT]) and bytes(cpu.mem_read(RESULT,48))==b'\xcd'*48
        invalid_lines+=1
    prism_checks=0;prism_rejections=0;filter_checks=0;enumeration_checks=0;wall_records=[]
    PRISM=0xe000400
    for item in inventory['colliders']:
        slot=item['slot'];ptr=registry[slot]
        kcl=vm.u32(ptr+0x20);p,n,t,o=struct.unpack('<4I',vm.read(kcl,16))
        assert call('sm64ds_world_prism_count',[slot])==(o-t)//16-1
        for triangle in range(1,(o-t)//16):
            assert call('fixture_prism_contact',[slot,triangle,RESULT])==1
            contact=bytes(cpu.mem_read(RESULT,40))
            cpu.mem_write(PRISM,b'\xcd'*112)
            assert call('sm64ds_contact_prism',[RESULT,PRISM])==1,(slot,triangle)
            packet=bytes(cpu.mem_read(PRISM,112))
            cpu.mem_write(RESULT,b'\xcd'*40);cpu.mem_write(PRISM,b'\xcd'*112)
            assert call('sm64ds_world_prism_at',[slot,triangle,RESULT,PRISM])==1
            assert bytes(cpu.mem_read(RESULT,40))==contact and bytes(cpu.mem_read(PRISM,112))==packet
            enumeration_checks+=1
            extent,vi,ni,e1,e2,e3,attribute=struct.unpack('<i6H',vm.read(t+16*triangle,16))
            world_packets.append(packet+contact[4:16]);wall_records.append(packet+contact)
            assert packet[:4]==struct.pack('<i',extent)
            assert packet[4:16]==vm.read(p+12*vi,12)
            assert packet[16:40]==b''.join(vm.read(n+6*index,6) for index in [ni,e1,e2,e3])
            assert struct.unpack_from('<H',packet,40)[0]==attribute
            assert struct.unpack_from('<I',packet,44)[0]==kcl
            expected_kind=1 if item['moving'] else 0
            assert struct.unpack_from('<H',packet,42)[0]==expected_kind
            expected_matrix=vm.read(ptr+0x134,48) if expected_kind else struct.pack('<12i',4096,0,0,0,4096,0,0,0,4096,0,0,0)
            assert packet[48:96]==expected_matrix
            prism_checks+=1
            if triangle==1:
                # Same valid record with stale owner, properties, normal, slot,
                # zero/overflow triangle. Every failure preserves the output.
                for offset,fmt,value in [(32,'i',-1234567),(16,'I',0xffffffff),(4,'i',1234567),(38,'H',24),(36,'H',0),(36,'H',32768)]:
                    damaged=bytearray(contact);struct.pack_into('<'+fmt,damaged,offset,value)
                    cpu.mem_write(RESULT,bytes(damaged));cpu.mem_write(PRISM,b'\xcd'*112)
                    assert not call('sm64ds_contact_prism',[RESULT,PRISM])
                    assert bytes(cpu.mem_read(PRISM,112))==b'\xcd'*112
                    prism_rejections+=1
                cpu.mem_write(RESULT,contact)
                # Corrupt only the offline CPU copy. Reject invalid geometry
                # indices/header/type before any virtual surface lookup.
                clps=vm.u32(ptr+0x24)
                for address,damaged in [(t+16*triangle+4,struct.pack('<H',65535)),
                                        (t+16*triangle+6,struct.pack('<H',65535)),
                                        (t+16*triangle+8,struct.pack('<H',65535)),
                                        (t+16*triangle+14,struct.pack('<H',65535)),
                                        (clps+4,struct.pack('<H',4)),
                                        (kcl+4,struct.pack('<I',p)),
                                        (ptr,struct.pack('<I',0))]:
                    previous=bytes(cpu.mem_read(address,len(damaged)))
                    cpu.mem_write(address,damaged);cpu.mem_write(PRISM,b'\xcd'*112)
                    try:
                        assert not call('sm64ds_contact_prism',[RESULT,PRISM])
                        assert bytes(cpu.mem_read(PRISM,112))==b'\xcd'*112
                        prism_rejections+=1
                    finally:cpu.mem_write(address,previous)
    for slot,ptr in enumerate(registry):
        if not ptr:assert call('sm64ds_world_prism_count',[slot])==0
    assert call('sm64ds_world_prism_count',[24])==0xffffffff
    # Bob-omb Battlefield captures contain exact duplicate registry pointers
    # for moving platforms. Canonical storage must be enumerated once, while
    # original-query contacts through its alias still resolve the same prism.
    table=sy['data_020a0c80'];alias=23;owner=registry[0]
    previous=bytes(cpu.mem_read(table+alias*4,4));canonical=bytes(cpu.mem_read(table,4))
    alias_checks=0
    try:
        cpu.mem_write(table+alias*4,struct.pack('<I',owner))
        assert call('sm64ds_world_prism_count',[alias])==0;alias_checks+=1
        assert call('sm64ds_world_prism_at',[0,1,RESULT,PRISM])==1
        original_prism=bytes(cpu.mem_read(PRISM,112))
        assert call('sm64ds_world_prism_at',[alias,1,RESULT,PRISM])==1
        assert bytes(cpu.mem_read(PRISM,112))==original_prism
        assert call('sm64ds_contact_prism',[RESULT,PRISM])==1;alias_checks+=1
        cpu.mem_write(table,b'\0'*4)
        assert call('sm64ds_world_prism_count',[alias])==0xffffffff
        cpu.mem_write(RESULT,b'\xcd'*40);cpu.mem_write(PRISM,b'\xcd'*112)
        assert not call('sm64ds_world_prism_at',[alias,1,RESULT,PRISM])
        assert bytes(cpu.mem_read(RESULT,40))==b'\xcd'*40 and bytes(cpu.mem_read(PRISM,112))==b'\xcd'*112
        alias_checks+=1
    finally:
        cpu.mem_write(table,canonical);cpu.mem_write(table+alias*4,previous)

    for slot,triangle in [(24,1),(0,0),(0,32768),(0,32767)]:
        cpu.mem_write(RESULT,b'\xcd'*40);cpu.mem_write(PRISM,b'\xcd'*112)
        assert not call('sm64ds_world_prism_at',[slot,triangle,RESULT,PRISM])
        assert bytes(cpu.mem_read(RESULT,40))==b'\xcd'*40 and bytes(cpu.mem_read(PRISM,112))==b'\xcd'*112
    # Exercise original pass-through code with controlled CLPS mutations in the
    # offline copy. Independently decode the documented source flag clauses.
    ptr=registry[0];kcl=vm.u32(ptr+32);triangles=vm.u32(kcl+8)
    attribute=struct.unpack('<H',vm.read(triangles+16+14,2))[0]
    entry=vm.u32(ptr+36)+8+attribute*8;saved_clps=bytes(cpu.mem_read(entry,8))
    try:
        for bits in range(16):
            for effect in [0,17,20]:
                word=((bits&1)<<5)|(((bits>>1)&1)<<24)|(((bits>>2)&1)<<25)|(((bits>>3)&1)<<26)|(effect<<19)
                cpu.mem_write(entry,struct.pack('<2I',word,255))
                assert call('sm64ds_world_prism_at',[0,1,RESULT,PRISM])==1
                for wall in [0,1]:
                    for mask in range(128):
                        water=bool(word&32);toxic=bool(word&(1<<25));require_off=bool(word&(1<<24));require_on=bool(word&(1<<26));special=effect==17;wall_special=wall and effect==20
                        expected=(water and not mask&2) or (toxic and not mask&8) or (require_off and bool(mask&4)) or (require_on and not mask&4) or (special and bool(mask&32)) or (wall_special and bool(mask&64)) or (not(water or toxic or require_off or require_on or special or wall_special) and not mask&1)
                        assert call('sm64ds_contact_pass_through',[RESULT,0,mask,wall])==int(expected),(word,mask,wall,expected)
                        filter_checks+=1
    finally:cpu.mem_write(entry,saved_clps)
    for item in inventory['colliders']:
        slot=item['slot'];ptr=registry[slot];owner=vm.u32(ptr+4)
        if owner:
            assert call('sm64ds_world_prism_at',[slot,1,RESULT,PRISM])==1
            assert call('sm64ds_contact_pass_through',[RESULT,owner,3,1])==1
            filter_checks+=1
    assert call('sm64ds_contact_pass_through',[0,0,1,1])==0xffffffff
    assert prism_checks>=1245
    packets_path=out/'world-prisms.bin'
    packets_path.write_bytes(struct.pack('<I',len(world_packets))+b''.join(world_packets))
    triangle_test=ROOT/'tests/movement_lab/test_world_triangle.cpp'
    run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'source/integrated_native/movement'),str(triangle_test),'-o',str(out/'world-triangle-test')])
    run([str(out/'world-triangle-test'),str(packets_path)])
    run([TOOL+'g++',*flags,'-fsyntax-only',str(triangle_test)])
    wall_records_path=out/'wall-records.bin';wall_records_path.write_bytes(struct.pack('<I',len(wall_records))+b''.join(wall_records))
    wall_test=ROOT/'tests/movement_lab/test_ds_wall_reference.cpp';wall_cases_path=out/'wall-cases.bin'
    run(['clang++','-std=c++17','-O1','-g','-ffp-contract=off','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'source/integrated_native/movement'),str(wall_test),str(ROOT/'source/integrated_native/movement/ds_world_walls.cpp'),'-o',str(out/'wall-reference-test')])
    run([str(out/'wall-reference-test'),str(wall_records_path),str(wall_cases_path)])
    cases=wall_cases_path.read_bytes();wall_query_checks=struct.unpack_from('<I',cases)[0];wall_query_samples=[]
    assert len(cases)==4+wall_query_checks*200
    assert call('fixture_wall_cache_build',[])==1
    wall_cache_entries=call('fixture_wall_cache_size',[]);wall_cache_reads=[]
    for index in range(wall_query_checks):
        offset=4+index*200;input_bytes=cases[offset:offset+20];expected_status=struct.unpack_from('<i',cases,offset+20)[0];expected_result=cases[offset+24:offset+200]
        cpu.mem_write(POS,input_bytes);cpu.mem_write(0xe000500,b'\xcd'*176)
        status=call('sm64ds_world_walls_reference',[POS,0,1,0xe000500])
        assert status==expected_status,(index,status,expected_status)
        actual=bytes(cpu.mem_read(0xe000500,176))
        if status:
            ax,az,count,stored=struct.unpack_from('<2f2i',actual)
            ex,ez,ec,es=struct.unpack_from('<2f2i',expected_result)
            assert (count,stored)==(ec,es) and actual[16:]==expected_result[16:],(index,count,stored,ec,es)
            assert abs(ax-ex)<.001 and abs(az-ez)<.001,(index,ax,az,ex,ez)
        else:assert actual==b'\xcd'*176
        cpu.mem_write(0xe000600,b'\xcd'*176)
        cached_status=call('fixture_wall_cache_query',[POS,0,1,0xe000600])
        assert cached_status==status and bytes(cpu.mem_read(0xe000600,176))==actual
        reads=call('fixture_wall_cache_reads',[]);assert reads<len(wall_records)//10
        wall_cache_reads.append(reads)
        wall_query_samples.append({'input':list(struct.unpack('<5f',input_bytes)),'status':status,'contacts':struct.unpack_from('<i',actual,8)[0] if status else 0})
    # Rotate the originally horizontal saved moving water mesh into a wall.
    # The prebuilt cache must refresh movers rather than dropping former floors.
    moving_slot=next(item['slot'] for item in inventory['colliders'] if item['moving']);moving_ptr=registry[moving_slot]
    original_matrix=vm.read(moving_ptr+0x134,48)
    quarter_pitch=[4096,0,0,0,0,4096,0,-4096,0,0,0,0]
    cpu.mem_write(0xe000200,struct.pack('<12i',*quarter_pitch));call('fixture_transform_collider',[moving_ptr,0xe000200,0])
    probe=samples[0]['query_fix12'];moving_input=[probe[0]/4096,-probe[2]/4096,-85.,0.,30.]
    cpu.mem_write(POS,struct.pack('<5f',*moving_input))
    assert call('sm64ds_world_walls_reference',[POS,0,3,0xe000500])==1
    assert call('fixture_wall_cache_query',[POS,0,3,0xe000600])==1
    assert bytes(cpu.mem_read(0xe000500,176))==bytes(cpu.mem_read(0xe000600,176))
    moving_result=bytes(cpu.mem_read(0xe000500,176));moving_contacts=struct.unpack_from('<i',moving_result,12)[0]
    assert any(struct.unpack_from('<I',moving_result,16+40*i+24)[0]==moving_ptr for i in range(moving_contacts))
    cpu.mem_write(0xe000200,original_matrix);call('fixture_transform_collider',[moving_ptr,0xe000200,0])
    # Registry replacement must fail before exposing any old cached contact.
    saved_owner=bytes(cpu.mem_read(registry[0]+8,4));cpu.mem_write(registry[0]+8,struct.pack('<i',-1234567))
    cpu.mem_write(0xe000600,b'\xcd'*176)
    try:
        assert call('fixture_wall_cache_query',[POS,0,1,0xe000600])==0xffffffff
        assert bytes(cpu.mem_read(0xe000600,176))==b'\xcd'*176
    finally:cpu.mem_write(registry[0]+8,saved_owner)
    evidence=sources+[Path(__file__),ROOT/'source/integrated_native/movement/ds_world_query.h',ROOT/'tools/read_mapped_capture.py',ROOT/'tools/inspect_movement_colliders.py']
    if a.safe_rays:
        evidence.extend([ROOT/'tools/prepare_integrated_collision.py',ROOT/'source/integrated_native/movement/raycast_arithmetic.hpp',ROOT/'third_party/sm64ds-3ds'/RELATIVE])
    report={'status':'passed','registry_alias_checks':alias_checks,'capture_complete':vm.complete,'capture_sha256':sha(a.capture),'elf_sha256':sha(a.elf),'executable_text_byte_exact':True,'moving_triangle_samples':samples,'offline_transform_samples':transform_samples,'ceiling_line_samples':ceiling_samples,'long_ray_observations':long_ray_samples,'invalid_lines_rejected':invalid_lines,'calls':calls,'source_sha256':{str(s.relative_to(ROOT)):sha(s) for s in evidence},'overlay_sha256':sha(out/'query.elf'),'directory':str(out),'limits':'Saved physical-capture CPU replay plus controlled offline transforms only. Long-ray observations are diagnostic, not a passing full-range ceiling gate. No live process writes, whole-RAM integrity, platform velocity/lifetime, active gameplay or FPS acceptance.'}
    report['platform_transport_samples']=platform_samples
    report['invalid_platform_rejections']=platform_rejections
    report['world_triangle_decode_checks']=len(world_packets)
    report['world_triangle_fixture_sha256']=sha(packets_path)
    report['source_sha256'].update({str(f.relative_to(ROOT)):sha(f) for f in [triangle_test,ROOT/'source/integrated_native/movement/world_triangle.hpp',ROOT/'source/integrated_native/movement/kcl_prism.hpp']})
    with (out/'query.elf').open('rb') as f:
        cache_symbols=[symbol for symbol in ELFFile(f).get_section_by_name('.symtab').iter_symbols() if 'saved_wall_cache' in symbol.name]
        assert len(cache_symbols)==1
        report['wall_cache_ARM_bytes']=cache_symbols[0]['st_size']
    report['wall_cache_entries']=wall_cache_entries
    report['wall_cache_prism_reads_per_query']=wall_cache_reads
    report['wall_cache_rotated_floor_to_wall_refresh']=True
    report['world_wall_reference_samples']=wall_query_samples
    report['world_wall_reference_queries']=wall_query_checks
    report['wall_records_sha256']=sha(wall_records_path)
    report['source_sha256'][str(wall_test.relative_to(ROOT))]=sha(wall_test)
    report['source_sha256'].update({str(f.relative_to(ROOT)):sha(f) for f in [ROOT/'source/integrated_native/movement/wall_contact.hpp',ROOT/'source/integrated_native/movement/world_walls.hpp']})
    report['prism_enumeration_checks']=enumeration_checks
    report['original_DS_filter_checks']=filter_checks
    report['prism_extraction_checks']=prism_checks
    report['stale_prism_rejections']=prism_rejections
    report['offline_ray_code_redirected']=a.safe_rays
    report['ceiling_admission_checks']=ceiling_admission
    report['controlled_scale_y_plane_checks']=scale_y_samples
    report['ceiling_empty_invalid_checks']=ceiling_negative_checks
    report['long_ray_plane_gate_passed']=a.safe_rays
    if a.safe_rays:
        report['maximum_long_ray_plane_error_world']=max(s['plane_error_world'] for s in long_ray_samples)
        report['limits']='Saved capture CPU replay with offline raycode redirection. Ceiling, live-prism/filter and uncached-vs-cached wall fixtures pass. Cache geometry-read counts exclude original contact revalidation; static geometry needs scene-owned invalidation. Not full-game movement/platform/hazard integration, physical gameplay or FPS proof.'
    if a.baseline_report:
        baseline=json.loads(a.baseline_report.read_text())
        assert baseline['capture_sha256']==report['capture_sha256'] and baseline['elf_sha256']==report['elf_sha256']
        assert not baseline.get('offline_ray_code_redirected',False)
        for field in ['moving_triangle_samples','offline_transform_samples','ceiling_line_samples']:assert baseline[field]==report[field],field
        report['short_fixture_outputs_equal_original']=True;report['baseline_report_sha256']=sha(a.baseline_report)
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(ROOT/'build/movement-adapter/captured-ground-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'capture_complete':vm.complete,'moving_water_triangles':len(samples),'offline_transform_checks':len(transform_samples),'ceiling_line_contacts':len(ceiling_samples),'invalid_lines_rejected':invalid_lines,'calls':calls,'directory':str(out),'limits':report['limits']},indent=2))
if __name__=='__main__':main()
