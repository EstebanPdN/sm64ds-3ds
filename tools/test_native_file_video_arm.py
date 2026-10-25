#!/usr/bin/env python3
"""Bounded production ARM file-to-video transfers with raw Stage assets.

FSFile opening supplies a real native_card list node/vector from verified
payload bytes. Allocators, cache flush observation and platform failures are
explicit CPU seams. FS_ReadFile, loader callers, native_video_copy/pointer and
DecompressLZ16 are never simulated as successful. No game/UI/GPU executes.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from unicorn import UcError

from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC,UC_ARM_REG_LR,UC_ARM_REG_SP,UC_ARM_REG_CPSR,UC_ARM_REG_R0
import test_native_audio_deferred_arm as shared

ROOT,TOOL=shared.ROOT,shared.TOOL
REPORT=ROOT/'build/native-file-video'
WORK,STACK,STOP=shared.WORK,shared.STACK,shared.STOP
INPUT,FILE,OUTPUT,HANDLES=WORK+0x1000,WORK+0x800,WORK+0x20000,WORK+0x10000
ARENA,ARENA_END=0x14000000,0x14800000
PIN='6230b2996636e3fd0e03cb178161c94cb4b73cc9'
ORIGINAL=('LoadFileAt','func_02018270','func_0201817c','func_0201818c',
    'func_020186c0','func_02018ac4','func_0201834c',
    'func_020184e0','func_02018d48','func_02018a24','FS_InitFile',
    'func_0206e28c','Deallocate')
PRODUCTION=('source/game_port/native_card.cpp','source/game_port/native_file_video.cpp',
    'source/game_port/native_video.cpp','source/ctr/game_memory_primitives.S')
TEST=('tests/test_native_file_video_arm.cpp','tools/test_native_file_video_arm.py')


def sha(data):return hashlib.sha256(data).hexdigest()


def lz_audit(data):
    assert data[0]==0x10 and len(data)>=4
    declared=int.from_bytes(data[:4],'little')>>8
    out=bytearray();position=4;literals=references=0
    while len(out)<declared:
        assert position<len(data);flags=data[position];position+=1
        for bit in range(7,-1,-1):
            if flags>>bit&1:
                assert position+2<=len(data)
                a,b=data[position:position+2];position+=2
                length=(a>>4)+3;distance=((a&15)<<8|b)+1
                assert distance<=len(out),'backreference precedes decoded bytes'
                for _ in range(length):out.append(out[-distance])
                references+=1
            else:
                assert position<len(data);out.append(data[position]);position+=1;literals+=1
            if len(out)>=declared:break
    return bytes(out),dict(declared_bytes=declared,logical_bytes_produced=len(out),
        halfword_bytes_stored=len(out)&~1,header_overshoot=len(out)-declared,
        bytes_consumed=position,raw_padding=len(data)-position,literals=literals,
        backreferences=references,output_sha256=sha(out))


def build():
    REPORT.mkdir(parents=True,exist_ok=True)
    flags=['-O2','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
        '-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-Wa,--noexecstack',
        '-I'+str(ROOT/'include'),'-I'+str(ROOT/'build/native-core/generated/include')]
    sources=[ROOT/'tests/test_native_file_video_arm.cpp',
        ROOT/'source/game_port/native_file_video.cpp',ROOT/'source/game_port/native_video.cpp',
        ROOT/'source/ctr/game_memory_primitives.S',
        *[ROOT/'build/native-core/generated/src'/(name+'.c') for name in ORIGINAL]]
    objects=[]
    for number,source in enumerate(sources):
        target=REPORT/('candidate-'+str(number)+'.o')
        c=source.suffix=='.c';assembly=source.suffix=='.S'
        command=[TOOL+('gcc' if c or assembly else 'g++'),*flags]
        if not assembly:command+=['-std=c11' if c else '-std=gnu++20']
        subprocess.run([*command,'-c',str(source),'-o',str(target)],check=True,cwd=ROOT)
        objects.append(str(target))
    elf=REPORT/'file-video-candidate.elf'
    roots=['test_file_video_initialize','test_file_video_set_payload','test_file_video_open_vector',
        'FS_ReadFile','func_0205d688','FS_CloseFile','func_0205d3f4','LoadFileAt','LoadCompressedFileAt',
        'DecompressLZ16','func_02018568','_ZN6sm64ds18native_video_resetEv',
        '_ZN6sm64ds20native_video_pointerEmj','_ZN6sm64ds18native_video_writeEmyj',
        '_ZN6sm64ds15native_video_okEv','_ZN6sm64ds24native_video_bad_addressEv']
    subprocess.run([TOOL+'g++',*flags,'-nostartfiles','-specs=nosys.specs',
        '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x01000000,-e,test_file_video_initialize',
        *['-Wl,-u,'+name for name in roots],*objects,'-o',str(elf)],check=True,cwd=ROOT)
    return elf,sources


class Probe(shared.Probe):
    def __init__(self,elf=None,cart=None):
        self.native=elf is not None
        self.next=ARENA;self.allocations={};self.events=[];self.calls=Counter();self.null_loader=False
        super().__init__('production ARM11 file/video' if elf else 'original ROM LZ16',elf=elf,cart=cart)
        self.cpu.mem_map(WORK+0x10000,0x30000)
        self.cpu.mem_map(ARENA,ARENA_END-ARENA)
        self.cpu.mem_map(0x08000000,0x10000)
        self.cpu.mem_map(0x03fff000,0x1000)
        if self.native:
            self.call('test_file_video_initialize',[])
        self.observed={self.names[name]:name for name in
            ('FS_ReadFile','func_0205d688','FS_CloseFile','func_0201834c',
             'func_0201817c','LoadFileAt','LoadCompressedFileAt','DecompressLZ16') if name in self.names}
        self.video=None

    def code(self,cpu,address,size,_):
        name=getattr(self,'observed',{}).get(address)
        if name:
            self.calls[name]+=1
            if name=='func_0201817c' and self.null_loader:
                self.null_loader=False;cpu.reg_write(UC_ARM_REG_R0,0)
                cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR));return
        if address==self.names.get('test_file_video_allocate'):
            count,alignment=(cpu.reg_read(reg) for reg in shared.REGS[:2])
            assert 0<alignment<=256 and not alignment&(alignment-1)
            assert count<=0x20000
            pointer=(self.next+32+alignment-1)&~(alignment-1)
            assert pointer+count+32<ARENA_END
            self.cpu.mem_write(pointer-32,bytes([0xa5])*32)
            self.cpu.mem_write(pointer,bytes([0xcd])*count)
            self.cpu.mem_write(pointer+count,bytes([0x5a])*32)
            self.allocations[pointer]=count;self.next=(pointer+count+32+31)&~31
            self.events.append(dict(operation='allocate',pointer=hex(pointer),size=count,alignment=alignment))
            cpu.reg_write(UC_ARM_REG_R0,pointer);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        elif address==self.names.get('test_file_video_free'):
            pointer=cpu.reg_read(shared.REGS[0])
            if pointer:
                self.check_allocation(pointer);count=self.allocations.pop(pointer)
                self.events.append(dict(operation='free',pointer=hex(pointer),size=count))
            cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        elif address==self.names.get('test_file_video_unexpected'):
            pointer=cpu.reg_read(shared.REGS[0]);text=bytes(cpu.mem_read(pointer,160)).split(b'\0')[0].decode(errors='replace')
            raise AssertionError('Unexpected CPU boundary: '+text)

    def check_allocation(self,pointer):
        size=self.allocations[pointer]
        assert bytes(self.cpu.mem_read(pointer-32,32))==bytes([0xa5])*32
        assert bytes(self.cpu.mem_read(pointer+size,32))==bytes([0x5a])*32

    def check_allocations(self):
        for pointer in self.allocations:self.check_allocation(pointer)

    def call(self,name,args,expect_fault=None):
        sp=STACK+0xf000;self.cpu.mem_write(sp-0x2000,bytes([0x6d])*0x3000)
        for reg,value in zip(shared.REGS,(*args,0,0,0,0)[:4]):self.cpu.reg_write(reg,value&0xffffffff)
        for number,value in enumerate(args[4:]):self.words(sp+number*4,value)
        self.cpu.reg_write(UC_ARM_REG_CPSR,0x10);self.cpu.reg_write(UC_ARM_REG_SP,sp)
        self.cpu.reg_write(UC_ARM_REG_LR,STOP)
        for number,reg in enumerate(shared.base.SAVED):self.cpu.reg_write(reg,0x12340000+number)
        incoming=bytes(self.cpu.mem_read(sp,64))
        try:self.cpu.emu_start(self.names[name],STOP,count=5000000)
        except UcError as error:
            if expect_fault is not None:
                assert len(self.faults)==1 and self.faults[0]['address']==hex(expect_fault),self.faults
                self.check_allocations();return None
            pc=self.cpu.reg_read(UC_ARM_REG_PC)
            symbol=min(self.names,key=lambda item:abs(self.names[item]-pc))
            raise AssertionError((name,hex(pc),symbol,hex(pc-self.names[symbol]),self.events[-8:])) from error
        assert expect_fault is None,'expected read fault was absent'
        assert not self.faults,(name,self.faults)
        assert self.cpu.reg_read(UC_ARM_REG_PC)==STOP,(name,'instruction bound')
        assert self.cpu.reg_read(UC_ARM_REG_SP)==sp
        assert all(self.cpu.reg_read(reg)==0x12340000+number for number,reg in enumerate(shared.base.SAVED))
        assert bytes(self.cpu.mem_read(sp,64))==incoming
        assert bytes(self.cpu.mem_read(sp-0x2000,32))==bytes([0x6d])*32
        self.check_allocations();return self.cpu.reg_read(UC_ARM_REG_R0)

    def pointer(self,address,size):return self.call('_ZN6sm64ds20native_video_pointerEmj',[address,size])
    def okay(self):return self.call('_ZN6sm64ds15native_video_okEv',[])
    def bad_address(self):return self.call('_ZN6sm64ds24native_video_bad_addressEv',[])
    def configure_video(self,banks=()):
        self.call('_ZN6sm64ds18native_video_resetEv',[])
        self.words(self.names['test_file_video_platform_failures'],0)
        for register,control in banks:
            # AAPCS aligns the uint64 value to r2/r3; the byte width is stacked.
            assert self.call('_ZN6sm64ds18native_video_writeEmyj',[0x04000000+register,0,control,0,1])==1
        self.video={name:(self.pointer(address,size),size) for name,address,size in
            [('registers',0x04000000,0x2000),('palette',0x05000000,0x800),
             ('vram',0x06800000,0xa4000),('oam',0x07000000,0x800)]}
        for name,(pointer,size) in self.video.items():
            if name!='registers':self.cpu.mem_write(pointer,bytes([0xa5])*size)
        assert self.okay()
    def video_bytes(self):return {name:bytes(self.cpu.mem_read(pointer,size)) for name,(pointer,size) in self.video.items()}
    def payload(self,fat_id,data):
        assert len(data)<0xe000
        self.cpu.mem_write(INPUT,data)
        self.call('test_file_video_set_payload',[fat_id,INPUT,len(data)])
    def open(self,fat_id,data):
        self.payload(fat_id,data);self.cpu.mem_write(FILE,bytes([0x9b])*0x44)
        assert self.call('test_file_video_open_vector',[FILE,fat_id])==1


def verify_video(probe,before,mutations):
    expected={name:bytearray(data) for name,data in before.items()}
    for name,offset,data in mutations:expected[name][offset:offset+len(data)]=data
    assert probe.video_bytes()=={name:bytes(data) for name,data in expected.items()}


def direct_reads(native,stage):
    """Exercise production reads; opening only supplies the real list/vector."""
    item=stage[0x23e];data=item['data'];results=[]
    def run(label,destination,count,position=0,expected=None,banks=(),changes=(),
            failure=None,asynchronous=False):
        native.configure_video(banks);native.open(item['fat_id'],data)
        native.words(FILE+0x28,position)
        fs_before=bytes(native.cpu.mem_read(FILE,0x44));before=native.video_bytes()
        payload_before=bytes(native.cpu.mem_read(INPUT,len(data)))
        scratch_start=OUTPUT-32;scratch_size=0x1000
        native.cpu.mem_write(scratch_start,bytes([0xa5])*scratch_size)
        result=native.call('func_0205d688' if asynchronous else 'FS_ReadFile',
            [FILE,destination,count,*([1] if asynchronous else [])])
        assert result==(expected&0xffffffff),(label,hex(result),expected)
        verify_video(native,before,changes)
        assert bytes(native.cpu.mem_read(INPUT,len(data)))==payload_before
        if expected<0:
            assert bytes(native.cpu.mem_read(FILE,0x44))==fs_before
        else:
            assert native.u32(FILE+0x28)==position+expected
            assert native.u32(FILE+0x10)==native.u32(FILE+0x14)==0
            expected_fs=bytearray(fs_before)
            for offset,value in ((0x28,position+expected),(0x10,0),(0x14,0),
                                 (0xc,struct.unpack_from('<I',fs_before,0xc)[0]&~1)):
                struct.pack_into('<I',expected_fs,offset,value)
            assert bytes(native.cpu.mem_read(FILE,0x44))==bytes(expected_fs)
        if destination==OUTPUT:
            expected_scratch=bytearray([0xa5])*scratch_size
            if expected>0:expected_scratch[32:32+expected]=data[position:position+expected]
            assert bytes(native.cpu.mem_read(scratch_start,scratch_size))==bytes(expected_scratch)
        if failure is not None:
            assert native.okay()==0 and native.bad_address()==failure
            assert native.u32(native.names['test_file_video_platform_failures'])==1
        else:
            assert native.okay()==1 and not native.u32(native.names['test_file_video_platform_failures'])
        assert native.call('func_0205d3f4',[FILE])==1
        assert native.call('FS_CloseFile',[FILE])==1
        assert native.call('FS_CloseFile',[FILE])==0
        assert native.call('func_0205d3f4',[FILE])==0
        assert not native.allocations
        results.append(dict(case=label,destination=hex(destination),count=count,
            initial_position=position,return_bytes=expected,final_position=native.u32(FILE+0x28),
            failure_address=hex(failure) if failure is not None else None,
            full_video_compare=True,FSFile_fields_checked=True,allocation_canaries=True,
            source_unchanged=True,asynchronous=asynchronous))
    run('sub OBJ palette full',0x05000600,512,expected=512,changes=[('palette',0x600,data)])
    run('ordinary memory full',OUTPUT,512,expected=512)
    run('short read at EOF',0x05000600,8,position=509,expected=3,changes=[('palette',0x600,data[509:])])
    run('asynchronous request completes',0x05000600,32,position=11,expected=32,
        changes=[('palette',0x600,data[11:43])],asynchronous=True)
    run('EOF validates no video span',0x07ffffff,4,position=512,expected=0)
    run('zero length with null destination',0,0,expected=0)
    run('zero length at invalid video destination',0x07ffffff,0,expected=0)
    run('negative count',0x05000600,-1,expected=-1)
    run('null destination with positive count',0,1,expected=-1)
    run('null destination at EOF',0,1,position=512,expected=-1)
    run('position exceeds file size',0x05000600,1,position=513,expected=-1)
    run('adjacent noncontiguous bank transfer',0x0601fff8,16,expected=16,
        banks=[(0x241,0x81),(0x240,0x89)],changes=[('vram',0x3fff8,data[:8]),('vram',0,data[8:16])])
    run('gap after transferred prefix',0x0601fff8,16,expected=-1,
        banks=[(0x240,0x81)],changes=[('vram',0x1fff8,data[:8])],failure=0x06020000)
    for label,address in [('register end',0x04002000),('palette end',0x05000800),
        ('unmapped BG',0x06000000),('video range end',0x07ffffff)]:
        run(label,address,1,expected=-1,failure=address)
    # Exact branch boundaries execute ordinary memcpy in mapped scratch RAM.
    for label,address in [('below DS video range',0x03ffffff),('above DS video range',0x08000000)]:
        native.cpu.mem_write(address,b'\xa5');run(label,address,1,expected=1)
        assert bytes(native.cpu.mem_read(address,1))==data[:1]
    assert native.call('FS_ReadFile',[FILE,OUTPUT,1])==0xffffffff
    results.append(dict(case='closed file rejects read',return_bytes=-1))
    return results


def compressed_boundaries(native,original,stage):
    results=[];item=stage[0x23d]
    # The compressed wrapper needs one contiguous destination span. This is a
    # stricter contract than the byte copy used by a raw FS read.
    for label,address,banks in (
        ('unmapped OBJ',0x06600000,()),
        ('palette span too small',0x050007fe,()),
        ('video end span',0x07ffffff,()),
        ('adjacent banks are not one pointer',0x0601fff8,[(0x241,0x81),(0x240,0x89)]),
        ('overlapping writable aliases',0x06000000,[(0x240,0x81),(0x241,0x81)])):
        native.configure_video(banks);native.payload(item['fat_id'],item['data'])
        before=native.video_bytes();calls=native.calls.copy()
        assert native.call('LoadCompressedFileAt',[0x23d,address])==0
        verify_video(native,before,[])
        assert not native.allocations
        assert native.calls['FS_ReadFile']-calls['FS_ReadFile']==1
        assert native.calls['DecompressLZ16']==calls['DecompressLZ16']
        assert not native.okay() and native.bad_address()==address
        results.append(dict(case=label,destination=hex(address),return_bytes=0,
            real_FS_reads=1,real_LZ16_calls=0,all_video_bytes_unchanged=True,source_freed=True))
    for address in (OUTPUT,0x08000000):
        native.configure_video();native.payload(item['fat_id'],item['data'])
        count=len(item['decoded']);native.cpu.mem_write(address,bytes([0xa5])*(count+32))
        before=native.video_bytes()
        assert native.call('LoadCompressedFileAt',[0x23d,address])==count
        assert bytes(native.cpu.mem_read(address,count+32))==item['decoded']+bytes([0xa5])*32
        verify_video(native,before,[]);assert not native.allocations and native.okay()
        results.append(dict(case='ordinary compressed destination',destination=hex(address),
            return_bytes=count,full_destination_and_canary_compare=True,all_video_bytes_unchanged=True))
    # Synthetic edges document the existing halfword writer, without treating
    # it as a malformed-stream validator or changing that original behavior.
    edges=(('odd declared length',b'\x10\x03\x00\x00\x00ABC'+bytes(4),b'AB',3),
           ('backreference exceeds header',b'\x10\x04\x00\x00\x20AB\x10\x01'+bytes(3),b'ABABAB',4))
    for label,data,stored,declared in edges:
        _,audit=lz_audit(data);native.configure_video();native.payload(item['fat_id'],data)
        before=native.video_bytes()
        assert native.call('LoadCompressedFileAt',[0x23d,0x05000400])==declared
        verify_video(native,before,[('palette',0x400,stored)])
        original.cpu.mem_write(INPUT,data);original.cpu.mem_write(OUTPUT-32,bytes([0xa5])*128)
        original.call('DecompressLZ16',[INPUT,OUTPUT])
        assert bytes(original.cpu.mem_read(OUTPUT-32,128))==bytes([0xa5])*32+stored+bytes([0xa5])*(96-len(stored))
        assert native.okay() and not native.allocations
        results.append(dict(case=label,declared_bytes=declared,actual_stored_bytes=len(stored),
            original_ROM_matches=True,full_video_compare=True,token_audit=audit,
            known_contract_limit=True))
    return results


def header_checks(native,original):
    results=[]
    for data in (b'LZ77',b'LZ7x',b'xZ77',b'\0\0\0\0'):
        original.cpu.mem_write(OUTPUT,data);reference=original.call('func_02018568',[OUTPUT])
        assert reference==int(data==b'LZ77')
        for destination in (OUTPUT,0x05000400,0x050007fc,0x06200000):
            native.configure_video([(0x242,0x84)])
            pointer=native.pointer(destination,4) if destination!=OUTPUT else OUTPUT
            native.cpu.mem_write(pointer,data);before=native.video_bytes()
            assert native.call('func_02018568',[destination])==reference
            verify_video(native,before,[])
            assert native.okay()
            results.append(dict(case='marker read',input_hex=data.hex(),destination=hex(destination),
                result=reference,original_ROM_matches=True,ABI_registers_stack_preserved=True))
    for destination,banks in ((0x050007fd,()),(0x06000000,()),
        (0x06000000,[(0x240,0x81),(0x241,0x81)]),(0x07ffffff,())):
        native.configure_video(banks);before=native.video_bytes()
        assert native.call('func_02018568',[destination])==0
        verify_video(native,before,[])
        assert native.okay()==0 and native.bad_address()==destination
        results.append(dict(case='invalid marker span',destination=hex(destination),result=0,
            full_video_unchanged=True,error_state_and_address_checked=True,
            overlapping_alias=bool(banks)))
    return results


def stage_sequence(native,stage):
    """The six calls in Stage::LoadGraphics2D's non-wireless branch, in order."""
    native.configure_video([(0x242,0x84),(0x249,0x82)])
    before=native.video_bytes();changes=[];results=[]
    for handle,destination,function,region,offset in (
        (0x239,0x06200000,'LoadCompressedFileAt','vram',0x40000),
        (0x23c,0x05000400,'LoadFileAt','palette',0x400),
        (0x23b,0x06208000,'LoadCompressedFileAt','vram',0x48000),
        (0x23a,0x0620a000,'LoadCompressedFileAt','vram',0x4a000),
        (0x23d,0x06600000,'LoadCompressedFileAt','vram',0xa0000),
        (0x23e,0x05000600,'LoadFileAt','palette',0x600)):
        item=stage[handle];native.payload(item['fat_id'],item['data'])
        expected=item['decoded'] if function=='LoadCompressedFileAt' else item['data']
        calls=native.calls.copy()
        assert native.call(function,[handle,destination])==len(expected)
        changes.append((region,offset,expected));verify_video(native,before,changes)
        assert native.okay() and not native.allocations
        assert native.calls['FS_ReadFile']-calls['FS_ReadFile']==1
        assert native.calls['FS_CloseFile']-calls['FS_CloseFile']==1
        assert native.calls['DecompressLZ16']-calls['DecompressLZ16']==int(function=='LoadCompressedFileAt')
        results.append(dict(handle=hex(handle),function=function,destination=hex(destination),
            physical_region=region,physical_offset=hex(offset),return_bytes=len(expected),
            cumulative_full_video_compare=True,ABI_and_canaries=True))
    return dict(calls=results,bank_controls={'C':'0x84','I':'0x82'},
        final_video_sha256={name:sha(data) for name,data in native.video_bytes().items()},
        total_raw_palette_bytes=1024,total_LZ_output_bytes=65536,
        original_BG0CNT='0xd000',original_BG1CNT='0xd400',
        scope='Six real loader chains in original call order; bank controls and open vectors are explicit fixture inputs. Stage state machine/GPU are not executed.')


def main():
    assert subprocess.check_output(['git','-C',str(ROOT/'upstream'),'rev-parse','HEAD'],text=True).strip()==PIN
    cart=shared.base.Cartridge(ROOT/'build/europe.pak');handle_address,indices=cart.handles()
    stage={}
    for handle in (0x239,0x23a,0x23b,0x23c,0x23d,0x23e,0x230,0x25c):
        entry=cart.entries[indices[handle]];data=cart.file(entry[4])
        item=dict(handle=hex(handle),fat_id=entry[0],path=entry[4],raw_bytes=len(data),raw_sha256=sha(data))
        if handle not in (0x23c,0x23e):
            decoded,audit=lz_audit(data);assert not audit['header_overshoot'] and not audit['declared_bytes']&1
            item['lz_audit']=audit;item['decoded']=decoded
        item['data']=data;stage[handle]=item
    source_hashes={path:sha((ROOT/path).read_bytes()) for path in (*PRODUCTION,*TEST)}
    elf,sources=build();native=Probe(elf=elf);original=Probe(cart=cart)
    table=[cart.entries[index][0] for index in indices]
    native.cpu.mem_write(HANDLES,struct.pack('<'+str(len(table))+'H',*table))
    native.words(native.names['data_0209d3b8'],HANDLES)
    # The full original caller chains are executed below. The open boundary is
    # the only file seam; it does not replace any read or decompression result.
    compressed=[]
    for handle,address,banks in (
        (0x239,0x06200000,[(0x242,0x84)]),
        (0x23a,0x0620a000,[(0x242,0x84)]),
        (0x23b,0x06208000,[(0x242,0x84)]),
        (0x23d,0x06600000,[(0x249,0x82)])):
        item=stage[handle];native.configure_video(banks);native.payload(item['fat_id'],item['data'])
        pointer=native.pointer(address,len(item['decoded']));before=native.video_bytes();events=len(native.events);calls=native.calls.copy()
        assert native.call('LoadCompressedFileAt',[handle,address])==len(item['decoded'])
        physical=next((name,base) for name,(base,size) in native.video.items() if base<=pointer<base+size)
        verify_video(native,before,[(physical[0],pointer-physical[1],item['decoded'])])
        assert native.okay() and not native.u32(native.names['test_file_video_platform_failures'])
        assert native.calls['FS_ReadFile']-calls['FS_ReadFile']==1
        assert native.calls['DecompressLZ16']-calls['DecompressLZ16']==1
        assert not native.allocations
        original.cpu.mem_write(INPUT,item['data']);original.cpu.mem_write(OUTPUT-32,bytes([0xa5])*(len(item['decoded'])+64))
        original.call('DecompressLZ16',[INPUT,OUTPUT])
        assert bytes(original.cpu.mem_read(OUTPUT,len(item['decoded'])))==item['decoded']
        assert bytes(original.cpu.mem_read(OUTPUT-32,32))==bytes([0xa5])*32
        assert bytes(original.cpu.mem_read(OUTPUT+len(item['decoded']),32))==bytes([0xa5])*32
        compressed.append(dict(handle=hex(handle),destination=hex(address),return_bytes=len(item['decoded']),
            destination_sha256=sha(item['decoded']),all_video_bytes_and_canaries_checked=True,
            real_FS_reads=1,real_LZ16_calls=1,source_freed=True,allocation_events=native.events[events:]))
    direct=direct_reads(native,stage)
    boundaries=compressed_boundaries(native,original,stage)
    headers=header_checks(native,original)
    sequence=stage_sequence(native,stage)
    raw=[]
    for handle,address in ((0x23c,0x05000400),(0x23e,0x05000600)):
        isolated=Probe(elf=elf);isolated.configure_video()
        isolated.cpu.mem_write(HANDLES,struct.pack('<'+str(len(table))+'H',*table))
        isolated.words(isolated.names['data_0209d3b8'],HANDLES)
        item=stage[handle];isolated.payload(item['fat_id'],item['data']);before=isolated.video_bytes()
        assert isolated.call('LoadFileAt',[handle,address])==len(item['data'])
        verify_video(isolated,before,[('palette',address-0x05000000,item['data'])])
        assert isolated.calls['FS_ReadFile']==isolated.calls['FS_CloseFile']==1
        assert not isolated.allocations
        assert isolated.okay() and not isolated.faults
        raw.append(dict(handle=hex(handle),destination=hex(address),
            raw_bytes_copied=len(item['data']),return_bytes=len(item['data']),
            destination_sha256=sha(item['data']),source_freed=True,CPU_faults=isolated.faults,
            full_video_compare=True,real_FS_reads=1,real_FS_closes=1,
            ABI_registers_stack_preserved=True))
    report=dict(status='passed',scope=__doc__,compressed_cases=compressed,
        direct_read_cases=direct,compressed_boundary_cases=boundaries,header_check_cases=headers,raw_LoadFileAt_cases=raw,
        stage_loader_sequence=sequence,
        stage_assets={hex(handle):{key:value for key,value in item.items() if key not in ('data','decoded')} for handle,item in stage.items()},
        ELF_sha256=sha(elf.read_bytes()),source_sha256=source_hashes,
        generated_original_source_sha256={str(path.relative_to(ROOT)):sha(path.read_bytes()) for path in sources[4:]},
        original_handle_table_address=hex(handle_address),upstream_pin=PIN,CPU_faults=native.faults,
        GPU_executed=False,Azahar_launched=False,production_changed=False)
    assert source_hashes=={path:sha((ROOT/path).read_bytes()) for path in (*PRODUCTION,*TEST)},'source changed during test'
    (REPORT/'file-video-arm-test.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'compressed_cases':len(compressed),
        'direct_read_cases':len(direct),'compressed_boundary_cases':len(boundaries),
        'header_check_cases':len(headers),'raw_LoadFileAt_cases':raw,'source_sha256':source_hashes},indent=2))


if __name__=='__main__':main()
