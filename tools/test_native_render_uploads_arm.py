#!/usr/bin/env python3
"""Compare the reached toon/FIFO/material/sprite block with the verified ROM.

Runs real native ARM11 providers and adapted original sprite bodies with DS
MMIO unmapped; a separate ARM9 oracle records original writes. Then sanitized
host execution verifies uploads, providers, canaries and the real GX decoder.
Temporary local fixtures never become distributed ROM bytes. This test proves
bounded SDK/ABI/stream contracts, not game execution, console behavior or FPS.
"""
import argparse
from collections import Counter
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib

from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP
import test_native_matrix_submit_arm as camera_test
import native_core
import native_scene_video

ROOT, TOOL, PIN = camera_test.ROOT, camera_test.TOOL, camera_test.PIN
BUFFER, MASK32, MASK64 = camera_test.BUFFER, camera_test.MASK32, camera_test.MASK64
import ndspy.rom
import ndspy.code
import ndspy.codeCompression

SITES = {
    'func_02049ee8':2, 'func_0204af3c':5,
    'func_0204be40':4, 'func_0204b81c':4, 'func_0204b244':3,
    'func_0204bbd8':4, 'func_0204b460':4, 'func_0204b028':3,
    'func_0204c24c':10, 'func_0204c194':10,
    'func_ov007_020c42f8':3,
}
PURE = ('func_0204a5c8','func_0204af38','func_0204c0a8','func_0204c0e8',
        'func_ov007_020c3abc','MulMat3x3Mat3x3',
        'MulMat4x3Mat4x3','MulVec3Mat3x3','MulVec3Mat4x3')
ASSEMBLY = ('Matrix3x3_LoadIdentity','Copy36Bytes')
COMMON = ('source/game_port/native_video_services.cpp','source/game_port/native_matrix_submit.cpp',
          'source/core/game_draw.cpp','source/game_port/native_video.cpp',
          'source/ctr/game_draw.cpp','source/ctr/game_math.cpp','source/core/fixed_math.cpp',
          'tests/native_render_upload_boundary.cpp','upstream/src/CrossVec3.c','upstream/src/DotVec3.c')
ARM_SOURCES = ('source/ctr/game_primitives.cpp','source/ctr/game_memory_primitives.S')
TEST_SOURCES = ('tools/test_native_render_uploads_arm.py','tools/test_native_matrix_submit_arm.py',
                'tests/test_native_render_uploads.cpp','tests/native_render_upload_boundary.cpp')
READ='_ZN6sm64ds17native_video_readEmj'
WRITE='_ZN6sm64ds18native_video_writeEmyj'
POINTER='_ZN6sm64ds20native_video_pointerEmj'
RESET='_ZN6sm64ds18native_video_resetEv'
DEPENDENCIES = {
    'func_020555a4':['MultiCopyHalf: source r0 to TOON_TABLE r1, replaced by owned halfword upload'],
    'func_0205532c':['Copy36Bytes: FIFO aliases, replaced by ordered real FIFO words'],
    'func_ov007_020c42f8':['Matrix3x3_LoadIdentity','func_ov007_020c3abc','func_0205532c','MTX_SCALE'],
    'func_ov007_020c3abc':['func_02052550','func_0205256c','Matrix3x3_SetRotationZ','MulMat3x3Mat3x3'],
    'func_02049ee8':['DISP3DCNT','func_0204a5c8'],
    'func_0204a5c8':['func_0204af3c','func_0204af38','func_0204be40','func_0204b81c',
                      'func_0204b244','func_0204bbd8','func_0204b460','func_0204b028'],
    'func_0204be40 / func_0204bbd8':['MulVec3Mat4x3','func_02055388','func_0204c24c'],
    'func_0204b81c / func_0204b460':['CrossVec3','NormalizeVec3','Copy36Bytes: pure local memory',
        'MulVec3Mat3x3','MulVec3Mat4x3','func_02055388','func_0204c24c'],
    'func_0204b244 / func_0204b028':['data_02099fbc: func_0204c0a8 or func_0204c0e8',
        'func_020527e8','MulMat4x3Mat4x3','func_020553a4','func_02055388',
        'data_02099fb4: func_0204c24c or func_0204c194'],
    'func_020553a4 / func_02055388':['existing source/ctr/game_draw.cpp: command FIFO + 12 parameter words'],
    'func_0204c24c / func_0204c194':['BEGIN_VTXS','TEXCOORD','VTX_10','END_VTXS'],
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_native(temp, generated, tables, arm):
    flags=['-O2','-Wall','-Wextra','-Werror','-Wno-unknown-pragmas','-fno-strict-aliasing',
           '-ffunction-sections','-fdata-sections','-I'+str(ROOT/'include'),
           '-I'+str(ROOT/'build/native-core/generated/include')]
    if arm:
        flags+=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
                '-D__3DS__','-DSM64DS_RENDER_CPU_ORACLE','-Wa,--noexecstack']
    else:
        flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    sources=[*[ROOT/p for p in COMMON]]
    if arm:
        sources += list(generated.values())+[tables,*[ROOT/p for p in ARM_SOURCES],
            ROOT/'tests/native_menu_math_failure_stub.cpp']
    else:
        sources += [generated[name] for name in ('func_0204af3c','func_0204c24c','func_0204c194')]
        sources += [ROOT/'source/vendor/ntr/gx.cpp',ROOT/'tests/test_native_render_uploads.cpp']
    objects=[]
    for i,source in enumerate(sources):
        assembly=source.suffix=='.S'
        c=source.suffix=='.c'
        compiler=TOOL+('gcc' if c or assembly else 'g++') if arm else ('cc' if c else 'c++')
        obj=temp/(('arm-' if arm else 'host-')+str(i)+'.o')
        mode=['-x','assembler-with-cpp'] if assembly else ['-std=c11' if c else '-std=gnu++20']
        subprocess.run([compiler,*flags,*mode,'-c',str(source),'-o',str(obj)],check=True,cwd=ROOT)
        objects.append(str(obj))
    binary=temp/('render.elf' if arm else 'render-host')
    link=[TOOL+'g++' if arm else 'c++',*flags]
    if arm:
        roots=[*SITES,'func_020555a4','func_0205532c','sm64ds_draw_reset','sm64ds_draw_count',
               'sm64ds_draw_entries','sm64ds_draw_ok',READ,WRITE,POINTER,RESET]
        link += ['-nostartfiles','-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,func_0205532c',
                 *['-Wl,-u,'+name for name in roots]]
    subprocess.run([*link,*objects,'-o',str(binary)],check=True,cwd=ROOT)
    return binary


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,default=ROOT/'build/roms/europe.nds')
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-runtime/render-upload-test.json')
    args=parser.parse_args()
    assert subprocess.check_output(['git','-C',str(ROOT/'upstream'),'rev-parse','HEAD'],text=True).strip()==PIN
    reviewed={}
    for name in (*SITES,*PURE,*ASSEMBLY,'func_020555a4','func_0205532c','MultiCopyHalf'):
        relative='src/'+name+'.c'
        raw=(ROOT/'upstream'/relative).read_bytes()
        assert raw==subprocess.check_output(['git','-C',str(ROOT/'upstream'),'show',PIN+':'+relative]),relative+' changed'
        reviewed[relative]=hashlib.sha256(raw).hexdigest()
    raw=args.rom.read_bytes();assert zlib.crc32(raw)==0x29715dec,'Requires verified ASMP0 ROM'
    rom=ndspy.rom.NintendoDSRom(raw);assert rom.idCode==b'ASMP' and rom.version==0
    arm9=ndspy.codeCompression.decompress(rom.arm9);overlay=rom.loadArm9Overlays([7])[7]
    trig=struct.unpack_from('<8192h',arm9,0x02082214-rom.arm9RamAddress)
    original=camera_test.Cpu();original.map(rom.arm9RamAddress,len(arm9));original.write(rom.arm9RamAddress,arm9)
    for section in ndspy.code.MainCodeFile(rom.arm9,rom.arm9RamAddress).sections:
        if section.ramAddress<0x02000000 and section.data:
            original.map(section.ramAddress,len(section.data));original.write(section.ramAddress,section.data)
    original.map(overlay.ramAddress,len(overlay.data));original.write(overlay.ramAddress,overlay.data)
    original.map(0x04000000,4096,3)
    io=[];words=[];native_calls=[];audit={};counts=Counter();records=[]
    def read_io(uc,access,address,size,value,user):
        io.append(('read',address,size))
        if address in (0x040002a0,0x040002a4):
            numerator=struct.unpack('<q',uc.mem_read(0x04000290,8))[0]
            mode=struct.unpack('<H',uc.mem_read(0x04000280,2))[0]&3
            denominator=struct.unpack('<i' if mode==1 else '<q',uc.mem_read(0x04000298,4 if mode==1 else 8))[0]
            quotient=(-1 if numerator>=0 else 1) if denominator==0 else \
                (abs(numerator)//abs(denominator))*(-1 if (numerator<0)!=(denominator<0) else 1)
            uc.mem_write(0x040002a0,struct.pack('<Q',quotient&MASK64))
        elif address==0x040002b4:
            value=struct.unpack('<Q',uc.mem_read(0x040002b8,8))[0]
            if not struct.unpack('<H',uc.mem_read(0x040002b0,2))[0]&1:value&=MASK32
            uc.mem_write(address,struct.pack('<I',math.isqrt(value)))
    def write_io(uc,access,address,size,value,user):
        io.append(('write',address,size))
        if 0x04000400<=address<=0x040005c8:
            assert size==4 and not address&3
            words.append((0 if address<0x04000440 else (address-0x04000400)//4,value&MASK32))
    original.cpu.hook_add(UC_HOOK_MEM_READ,read_io,begin=0x04000000,end=0x04000fff)
    original.cpu.hook_add(UC_HOOK_MEM_WRITE,write_io,begin=0x04000000,end=0x04000fff)
    def note_io(name):
        audit[name]=[{'operation':op,'address':hex(address),'bytes':size,'count':count}
                     for (op,address,size),count in sorted(Counter(io).items())]
    def record(kind,payload):
        records.append(struct.pack('<III',kind,len(payload),len(words))+payload+
                       b''.join(struct.pack('<II',*entry) for entry in words))
    rng=random.Random(0x555a4)
    with tempfile.TemporaryDirectory(prefix='sm64ds-render-upload-') as directory:
        temp=Path(directory);generated={};sites={}
        for name,count in SITES.items():
            source=(ROOT/'upstream/src'/f'{name}.c').read_text()
            body,replacements=native_scene_video.adapt(source,count,native_scene_video.DATA_LITERAL_SITES.get('src/'+name+'.c',()))
            path=temp/(name+'.cpp');path.write_text('#include "sm64ds/native_register.hpp"\nextern "C" {\n'+body+'\n}\n')
            generated[name]=path;sites[name]=replacements
        for name in PURE:
            path=temp/(name+'.c');path.write_bytes((ROOT/'upstream/src'/path.name).read_bytes());generated[name]=path
        for name in ASSEMBLY:
            body,changes,language=native_core.adapt('src/'+name+'.c',(ROOT/'upstream/src'/f'{name}.c').read_text())
            assert language=='assembler' and changes==['mwcc-to-gas-memory-primitive']
            path=temp/(name+'.S');path.write_text(body);generated[name]=path
        tables=temp/'tables.cpp'
        tables.write_text('extern "C" { short data_02082214[8192]={'+','.join(map(str,trig))+'};\n'+
            'void func_0204c24c(int,int);void func_0204c194(int,int);\n'+
            'void func_0204c0a8(int,int,void*);void func_0204c0e8(int,int,void*);\n'+
            'void (*data_02099fb4[2])(int,int)={func_0204c24c,func_0204c194};\n'+
            'void (*data_02099fbc[2])(int,int,void*)={func_0204c0a8,func_0204c0e8}; }\n')
        expected_dispatch=(0x0204c24c,0x0204c194,0x0204c0a8,0x0204c0e8)
        assert struct.unpack_from('<4I',arm9,0x02099fb4-rom.arm9RamAddress)==expected_dispatch
        elf=compile_native(temp,generated,tables,True);native,symbols=camera_test.load_elf(elf)
        def note_write(uc,address,size,user):
            base=uc.reg_read(UC_ARM_REG_R0)
            low=uc.reg_read(UC_ARM_REG_R2);high=uc.reg_read(UC_ARM_REG_R3)
            width=struct.unpack('<I',native.read(uc.reg_read(UC_ARM_REG_SP),4))[0]
            native_calls.append((base,width,low|(high<<32)))
        native.cpu.hook_add(UC_HOOK_CODE,note_write,begin=symbols[WRITE],end=symbols[WRITE])
        def reset():
            native.invoke(symbols['sm64ds_draw_reset'])
            native.invoke(symbols[RESET],instructions=300000);native_calls.clear()
        def native_words():
            count=native.invoke(symbols['sm64ds_draw_count']);assert native.invoke(symbols['sm64ds_draw_ok'])==1
            pointer=native.invoke(symbols['sm64ds_draw_entries'])
            return list(struct.iter_unpack('<II',native.read(pointer,count*8)))
        def compare(name,address,arguments,image,label):
            reset();native.write(BUFFER,image);original.write(BUFFER,image)
            original.write(0x04000000,bytes(4096));io.clear();words.clear()
            native.invoke(symbols[name],arguments);original.invoke(address,arguments)
            assert native.read(BUFFER,len(image))==original.read(BUFFER,len(image)),('buffers/canaries',label)
            assert native_words()==words,('ordered GX words',label,native_words(),words)
            if name=='func_02049ee8':
                actual=native.invoke(symbols[READ],(0x04000060,2))
                assert actual==struct.unpack('<H',original.read(0x04000060,2))[0]
            counts[label]+=1
        for index in range(40):
            payload=rng.randbytes(64);image=bytearray([0xa5]*256);image[64:128]=payload
            reset();native.write(BUFFER,image);original.write(BUFFER,image)
            register_pointer=native.invoke(symbols[POINTER],(0x04000000,0x2000))
            native.write(register_pointer,bytes([0x5a])*0x2000);original.write(0x04000000,bytes([0x5a])*4096)
            io.clear();words.clear();native_calls.clear()
            native.invoke(symbols['func_020555a4'],(BUFFER+64,));original.invoke(0x020555a4,(BUFFER+64,))
            assert native.read(register_pointer,4096)==original.read(0x04000000,4096),('TOON owned registers/canaries',index)
            assert native.read(BUFFER,256)==image and original.read(BUFFER,256)==image
            assert native_calls==[(0x04000380+2*i,2,value[0]) for i,value in enumerate(struct.iter_unpack('<H',payload))]
            assert not native_words() and not words
            if index==0:note_io('TOON_TABLE original 64-byte upload')
            counts['TOON_TABLE32halfwords/register/source/canaries/ABI']+=1;record(0,payload)
        for index in range(48):
            values=[rng.randint(-40000,40000) for _ in range(12)]
            image=bytearray([0xa5]*256);struct.pack_into('<12i',image,64,*values)
            for translation in (100,64,76):
                compare('func_0205532c',0x0205532c,(BUFFER+64,BUFFER+translation),image,'FIFO4x3/aliases/source/canaries/ABI')
                payload=image[64:100]+image[translation:translation+12]
                assert len(words)==13 and words[0]==(0,0x19)
                if index==0 and translation==100:note_io('MTX_MULT4x3 original FIFO aliases')
                record(1,bytes(payload))
        for fmt,size_s,size_t,repeat,flip,transparent in itertools.product(range(8),(0,1),(0,2),(0,3),(0,3),(0,1)):
            parameter=fmt|(size_s<<4)|(size_t<<8)|(repeat<<12)|(flip<<14)|(transparent<<16)
            payload=struct.pack('<4I2H',0xa5a5a5a5,rng.randrange(0x10000)*8,rng.randrange(0x10000)*16,
                                parameter,8<<size_s,8<<size_t)
            image=bytearray([0xa5]*256);image[64:84]=payload
            compare('func_0204af3c',0x0204af3c,(BUFFER+64,),image,'material8words/bitfields/source/canaries/ABI')
            assert len(words)==8
            if fmt==repeat==flip==transparent==size_s==size_t==0:note_io('texture material original writes')
            record(2,payload)
        for style,a,b in itertools.product(range(2),range(4),range(4)):
            name='func_0204c194' if style else 'func_0204c24c'
            compare(name,int(name[5:],16),(a,b),bytes([0xa5])*256,'quad10words/source/canaries/ABI')
            assert len(words)==10;note_io(name+' original quad')
            record(3,struct.pack('<3i',style,a,b))
        for index in range(32):
            image=bytearray([0xa5]*256);self=BUFFER+32
            for offset in (0x14,0x18,0x1c,0x28,0x2c,0x30,0x3c,0x40):
                struct.pack_into('<i',image,32+offset,rng.randint(-40000,40000))
            struct.pack_into('<3H',image,32+0x34,*[rng.randrange(0x10000) for _ in range(3)])
            compare('func_ov007_020c42f8',0x020c42f8,(self,),image,'OV7transform16words/source/canaries/ABI')
            assert len(words)==16
            if index==0:note_io('OV007 sprite transform including FIFO matrix and scale')
            record(4,b'')
        # Construct real ARM32 records for the complete dispatch closure, not
        # test callbacks. Two node lists exercise all six production bodies.
        for kind,subkind,axis,style,enabled in itertools.product(range(3),range(3),range(2),range(2),range(2)):
            image=bytearray(0x3000)
            def put(offset,*values):struct.pack_into('<'+'I'*len(values),image,offset,*(v&MASK32 for v in values))
            put(4,BUFFER+0x400);put(0x20,BUFFER+0x2000);put(0x30,0x001f0000)
            put(0x408,BUFFER+0x800);put(0x410,BUFFER+0x880);put(0x418,BUFFER+0x500)
            put(0x474,1|(2<<6)|(2<<12))
            put(0x500,BUFFER+0x600);put(0x514,BUFFER+0x680)
            put(0x600,(kind<<4)|(1<<11)|(1<<16)|(axis<<17)|(style<<19))
            struct.pack_into('<h',image,0x620,4096);image[0x633]=0
            put(0x634,0x10000|(2<<24)|(1<<26))
            struct.pack_into('<H',image,0x680,(subkind<<7)|(axis<<9)|(style<<11))
            image[0x68f]=1;put(0x690,2|(1<<2))
            for offset in (0x800,0x880):
                put(offset+8,4096,8192,12288,1024,2048,3072)
                put(offset+0x20,4096,0,0);put(offset+0x30,4096)
                struct.pack_into('<2H',image,offset+0x34,4096,0x1230)
                struct.pack_into('<H',image,offset+0x3a,0x7fff)
                struct.pack_into('<H',image,offset+0x40,(31|(31<<5)) if enabled else 0)
                image[offset+0x42]=0
            put(0x1000,4096,0,0,0,4096,0,0,0,4096,4096,8192,12288)
            for offset in (0x2000,0x2014):
                put(offset,0,0,0,0);struct.pack_into('<2H',image,offset+16,16,16)
            compare('func_02049ee8',0x02049ee8,(BUFFER,BUFFER+0x1000),image,'full_sprite_dispatch/counters/6callbacks/ABI')
            if kind==subkind==axis==style==0 and enabled:note_io('complete sprite dispatch including both active lists')
            record(4,b'')
        # Invalid inputs stop explicitly before any destination or FIFO change.
        for source in (0,0x02001000):
            reset();native.invoke(symbols['func_020555a4'],(source,),True)
            assert not native_calls and not native_words();counts['invalid_TOON_source_explicit_failure']+=1
        for null in (0,1):
            reset();arguments=[BUFFER+64,BUFFER+100];arguments[null]=0
            native.invoke(symbols['func_0205532c'],arguments,True)
            assert not native_words();counts['null_FIFO_matrix_explicit_failure']+=1
        fixture=temp/'rom-render.bin';fixture.write_bytes(b'RUPL1\0\0\0'+struct.pack('<I',len(records))+b''.join(records))
        host=compile_native(temp,generated,tables,False)
        completed=subprocess.run([str(host),str(fixture)],text=True,capture_output=True,cwd=ROOT)
        if completed.returncode:raise RuntimeError(completed.stdout+completed.stderr)
        assert not completed.stderr,completed.stderr
        host_result=json.loads(completed.stdout);elf_sha=sha(elf)
    report={'status':'passed','scope':'ARM11/ARM9 SDK+complete sprite closure, sanitized host real GX decoder',
            'upstream_commit':PIN,'source_rom_crc32':'29715dec','source_rom_sha256':hashlib.sha256(raw).hexdigest(),
            'reviewed_original_source_sha256':reviewed,'native_source_sha256':{p:sha(ROOT/p) for p in (*COMMON,*ARM_SOURCES)},
            'test_sources_sha256':{p:sha(ROOT/p) for p in TEST_SOURCES},
            'generated_site_count':{n:len(s) for n,s in sites.items()},'original_IO_audit':audit,
            'audited_dependency_closure':DEPENDENCIES,
            'dispatch_tables_from_verified_ROM':{'quad':[hex(x) for x in expected_dispatch[:2]],'rotation':[hex(x) for x in expected_dispatch[2:]]},
            'comparisons':dict(counts),'host_sanitized_GX_checks':host_result,'native_elf_sha256':elf_sha,
            'ABI':'SP/r4-r11 and input/record/register canaries verified','native_DS_MMIO_mapping':False,
            'native_IRQ_or_bank_or_stack_query_boundary':'explicit failure','sanitizers':['address','undefined'],
            'Azahar_run':False,'physical_hardware':False,'playability_verified':False,'target_performance_verified':False}
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':'passed','comparisons':dict(counts),'host':host_result,'report':str(args.report)},indent=2))


if __name__=='__main__':main()
