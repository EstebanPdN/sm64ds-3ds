#!/usr/bin/env python3
"""Execute native ARM11 collision code against the supplied ROM's real castle.

Requires Unicorn 2.1.4 and outputs from build_game_collision.py and
test_collision_data assets.pak build/native-collision. This tests CPU behavior,
never 3DS performance. DS register space is deliberately not mapped.
"""
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
    UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,
    UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_SP,UC_ARM_REG_LR,
    UC_ARM_REG_PC,UC_ARM_REG_CPSR)

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/native-collision'
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'

def main():
    binary=(OUT/'runtime.bin').read_bytes()
    kcl=(OUT/'castle.kcl').read_bytes();clps=(OUT/'castle.clps').read_bytes()
    nm=subprocess.check_output([TOOL+'nm','-g','--defined-only',str(OUT/'runtime.elf')],text=True)
    syms={v[2]:int(v[0],16) for line in nm.splitlines() if len(v:=line.split())==3}
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    cpu.mem_map(0x100000,0x100000);cpu.mem_write(0x100000,binary)
    cpu.mem_map(0x200000,0x100000);cpu.mem_map(0x300000,0x10000);cpu.mem_map(0x400000,0x1000)
    COLLIDER,KCL,CLPS,POS,END,RESULT=0x200000,0x210000,0x230000,0x240000,0x240010,0x240020
    assert len(kcl)<CLPS-KCL
    cpu.mem_write(KCL,kcl);cpu.mem_write(CLPS,clps)
    saved=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
           UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
    count={'calls':0,'sqrt':0}
    def sqrt_hook(*_):count['sqrt']+=1
    cpu.hook_add(UC_HOOK_CODE,sqrt_hook,begin=syms['sm64ds_collision_sqrt'],
                 end=syms['sm64ds_collision_sqrt'])
    def call(name,*args):
        count['calls']+=1
        cpu.reg_write(UC_ARM_REG_CPSR,0x10)
        cpu.reg_write(UC_ARM_REG_SP,0x308000);cpu.reg_write(UC_ARM_REG_LR,0x400000)
        for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):
            cpu.reg_write(reg,value&0xffffffff)
        for i,reg in enumerate(saved):cpu.reg_write(reg,0x12340000+i)
        cpu.emu_start(syms[name],0x400000,count=2000000)
        assert cpu.reg_read(UC_ARM_REG_PC)==0x400000,(name,'instruction budget exhausted')
        assert cpu.reg_read(UC_ARM_REG_SP)==0x308000
        assert all(cpu.reg_read(reg)==0x12340000+i for i,reg in enumerate(saved))
        return cpu.reg_read(UC_ARM_REG_R0)
    def point(address,p):cpu.mem_write(address,struct.pack('<3i',*p))
    def ground(x,y,z,lower):
        point(POS,(x,y,z));cpu.mem_write(RESULT,bytes([0xcd])*20)
        found=call('native_collision_ground',COLLIDER,POS,lower,RESULT)
        if not found:
            assert bytes(cpu.mem_read(RESULT,20))==bytes([0xcd])*20
            return None
        return struct.unpack('<4iH2x',cpu.mem_read(RESULT,20))
    for storage,file,surface in [(0,KCL,CLPS),(COLLIDER+1,KCL,CLPS),
                                 (COLLIDER,KCL+1,CLPS),(COLLIDER,KCL,0)]:
        assert call('native_collision_init',storage,file,surface)==0
        assert bytes(cpu.mem_read(KCL,len(kcl)))==kcl
    assert call('native_collision_init',COLLIDER,KCL,CLPS)==1
    # The first four source entrance positions stand on the same horizontal
    # lawn at y=254; expected coordinates come from the ROM level records.
    entrances=[tuple(map(int,line.split(','))) for line in (OUT/'entrances.csv').read_text().splitlines()]
    for x,y,z in entrances[:4]:
        hit=ground(x*4096,(y+500)*4096,z*4096,-20000*4096)
        assert hit and hit[0]==y*4096 and hit[1:4]==(0,4096,0),(x,y,z,hit)
    assert ground(100000*4096,2000*4096,0,-20000*4096) is None
    assert ground(-1200*4096,254*4096,6800*4096,-20000*4096) is None
    assert ground(-1200*4096,1000*4096,6800*4096,300*4096) is None
    positions,normals,tris,octree=struct.unpack_from('<4I',kcl)
    origin=struct.unpack_from('<3i',kcl,0x14)
    extents=[((~v)&0xffffffff)+1 for v in struct.unpack_from('<3I',kcl,0x20)]
    top=(origin[1]+extents[1]*64+64)*64;lower=(origin[1]-256)*64
    hits=0;line_matches=0;line_differences=[];max_plane_error=0;sphere_contacts=0
    # Independent geometric check: returned height must lie on the reported
    # original prism's plane. This does not repeat the native octree algorithm.
    for gx in range(17):
        for gz in range(17):
            x=(origin[0]+(2*gx+1)*extents[0]*64//34)*64+17
            z=(origin[2]+(2*gz+1)*extents[2]*64//34)*64+29
            hit=ground(x,top,z,lower)
            if not hit:continue
            hits+=1;y,nx,ny,nz,tri=hit
            assert lower<y<top and ny>0 and 0<tri<(octree-tris)//16
            v,n=struct.unpack_from('<HH',kcl,tris+16*tri+4)
            vx,vy,vz=struct.unpack_from('<3i',kcl,positions+12*v)
            normal=struct.unpack_from('<3h',kcl,normals+6*n)
            assert (nx,ny,nz)==tuple(component*4 for component in normal)
            rawx=x>>6;rawz=z>>6
            plane=(vy-((rawx-vx)*normal[0]+(rawz-vz)*normal[2])/normal[1])*64
            delta=abs(y-plane);max_plane_error=max(delta,max_plane_error)
            # Original ground math truncates two products at >>10, then the
            # quotient at >>2. Its error bound grows on steep faces: less
            # than two numerator units / ny, plus one file-coordinate unit.
            plane_tolerance=(1+2048/normal[1])*64
            assert delta<=plane_tolerance,(gx,gz,y,plane,tri,plane_tolerance)
            # A local 200-unit ray keeps the original Q12 interpolation error
            # bounded. A ray through the entire 32K-unit octree quantizes its
            # interpolation fraction into multi-unit steps, as on DS.
            point(POS,(x,y+100*4096,z));point(END,(x,y-100*4096,z))
            got_line=call('native_collision_line',COLLIDER,POS,END,RESULT)
            if got_line:
                line=struct.unpack('<3i',cpu.mem_read(RESULT,12))
                if abs(line[1]-y)<=256+plane_tolerance:line_matches+=1
                else:line_differences.append((gx,gz,y,line[1]))
            else:line_differences.append((gx,gz,y,None))
            point(POS,(x,y+20*4096,z))
            if call('native_collision_sphere',COLLIDER,POS,40*4096,RESULT):
                sphere_contacts+=1
    assert hits>=50,hits  # The octree includes a large empty border around the map.
    assert line_matches>=hits*0.9,(line_matches,hits,line_differences[:8])
    assert sphere_contacts>=hits*0.9,(sphere_contacts,hits)
    # A sphere intersecting the flat lawn by 20 units must be pushed up 20;
    # exact contact has zero displacement; above the radius is a miss.
    for y,expected in [(274,20),(294,0),(314,None)]:
        point(POS,(-1200*4096,y*4096,6800*4096))
        found=call('native_collision_sphere',COLLIDER,POS,40*4096,RESULT)
        if expected is None:assert not found
        else:
            dx,dy,dz,flags=struct.unpack('<3iI',cpu.mem_read(RESULT,16))
            assert found and (dx,dy,dz)==(0,expected*4096,0) and flags&4
    geometry_sqrt_calls=count['sqrt']
    assert geometry_sqrt_calls>0,'Sphere edge/vertex paths never exercised raw sqrt'
    rng=random.Random(64)
    cases=[0,1,2,3,4,2**32-1,2**32,2**64-1]+[rng.getrandbits(64) for _ in range(100)]
    for value in cases:
        assert call('sm64ds_collision_sqrt',value&0xffffffff,value>>32)==math.isqrt(value)
    call('native_collision_destroy',COLLIDER)
    result={'status':'passed','scope':'ARM11 instructions in Unicorn; no 3DS FPS claim',
        'manifest_sha256':hashlib.sha256((OUT/'objects.json').read_bytes()).hexdigest(),
        'upstream_commit':json.loads((OUT/'objects.json').read_text())['upstream_commit'],
        'kcl_sha256':hashlib.sha256(kcl).hexdigest(),'clps_sha256':hashlib.sha256(clps).hexdigest(),
        'binary_sha256':hashlib.sha256(binary).hexdigest(),'ground_grid_queries':289,
        'ground_grid_hits':hits,'entrance_floor_checks':4,'negative_ground_queries':3,
        'invalid_initializations_rejected':4,
        'line_ground_agreements':line_matches,'line_ground_differences':line_differences,
        'sphere_contacts':sphere_contacts,'flat_sphere_checks':3,
        'max_ground_plane_error_fix12':max_plane_error,'raw_sqrt_geometry_calls':geometry_sqrt_calls,
        'raw_sqrt_exact_cases':len(cases),'calls':count['calls'],
        'DS_register_space_mapped':False,'callee_saved_registers_preserved':True,
        'line_provider_byte_matched':False}
    (OUT/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
