#!/usr/bin/env python3
"""Replay only the original intro gate/clock over copied SM64RAM1 bytes.

ELF code and all native RAM identities are restored, with an isolated oracle
stack. No constructor/init/reset runs. Inputs below are controlled writes to
the cloned input objects; graphics/audio services are explicit observations.
No live process, game frame, Azahar/UI or physical hardware is exercised.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import re
import zlib
from collections import Counter,deque

import test_native_audio_deferred_arm as shared
from test_native_title_fade_arm import Snapshot,inspect_snapshot
from test_native_title_start_arm import IntroFixture
from decode_native_title_ram import decode
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR,UC_ARM_REG_SP,UC_ARM_REG_CPSR

ROOT = shared.ROOT


def source_path(name):
    return next((ROOT/'upstream/src'/(name+ext) for ext in ('.c','.cpp')
        if (ROOT/'upstream/src'/(name+ext)).is_file()),None)


def preflight_callers(probe,elf,extra_roots=()):
    """Follow direct ELF calls/tail branches before replaying the component chain.

    Indirect callbacks remain explicit runtime observations. Literal DS bus
    values in a source are recorded for provider review, never called failures.
    """
    raw=subprocess.check_output([shared.TOOL+'nm','-S','--defined-only',elf],text=True)
    functions={}
    for line in raw.splitlines():
        columns=line.split()
        if len(columns)==4 and columns[2] in 'TtWw':
            address,size=int(columns[0],16),int(columns[1],16)
            if size:functions[address]=(columns[3],size)
    disassembly=subprocess.check_output([shared.TOOL+'objdump','-d',elf],text=True)
    direct={};owner=None
    for line in disassembly.splitlines():
        header=re.match(r'^([0-9a-fA-F]+) <[^>]+>:',line)
        if header:
            owner=int(header[1],16);direct.setdefault(owner,[])
        elif owner is not None:
            branch=re.search(r'\b(?:blx?|b(?:\.w)?)\s+(?:0x)?([0-9a-fA-F]+)\s*<',line)
            if branch:direct[owner].append(int(branch[1],16))
    roots=['func_ov007_020b7090','func_ov007_020b7040','func_ov007_020b6eb4',*extra_roots]
    queue=deque(probe.names[n] for n in roots)
    entries=[];seen=set()
    manifest=ROOT/'build/native-runtime/manifest.json'
    replacements=set(json.loads(manifest.read_text())['replaced_sources'])
    boundaries={'sm64ds_draw_fifo','sm64ds_draw_port','sm64ds_draw_list',
        'func_ov007_020bdeb0','func_ov007_020bde70',
        '_ZN6sm64ds27native_platform_defer_audioEPKc',
        '_ZN6sm64ds20native_platform_failEPKc','sm64ds_native_missing'}
    while queue:
        address=queue.popleft()
        if address in seen or address not in functions:continue
        seen.add(address);name,size=functions[address]
        path=source_path(name)
        calls=[]
        if name not in boundaries:
            for target in direct.get(address & ~1,[]):
                if target in functions and target != address:
                    calls.append(functions[target][0]);queue.append(target)
        source=path.read_text() if path else ''
        entries.append(dict(name=name,address=hex(address),bytes=size,direct_calls=sorted(set(calls)),
            original_source=str(path.relative_to(ROOT)) if path else None,
            original_source_sha256=shared.base.sha(path.read_bytes()) if path else None,
            source_is_native_replacement=bool(path and str(path.relative_to(ROOT/'upstream')) in replacements),
            source_DS_bus_literals=sorted(set(re.findall(r'0x0?4[0-9a-fA-F]{6}\b',source)))))
        if len(seen)>4096:raise ValueError('Call graph exceeded bounded component preflight')
    return dict(roots=roots,entries=entries,functions=len(entries),
        indirect_callbacks_require_runtime_observation=True,
        DS_bus_literals_are_not_fault_evidence=True,current_manifest_context_sha256=shared.base.sha(manifest.read_bytes()))


class CpuView:
    def __init__(self, snapshot, cpu):
        self.snapshot, self.cpu = snapshot, cpu
        self.views = snapshot.views

    def read(self, address, size):
        if not any(Snapshot.contains(s,address,size) for s in self.snapshot.spans):
            raise ValueError('CPU clone observer read outside captured spans')
        return bytes(self.cpu.mem_read(address,size))

    def u32(self,address): return struct.unpack('<I',self.read(address,4))[0]
    def s32(self,address): return struct.unpack('<i',self.read(address,4))[0]
    def u16(self,address): return struct.unpack('<H',self.read(address,2))[0]
    def s16(self,address): return struct.unpack('<h',self.read(address,2))[0]


class Replay:
    def __init__(self, snapshot, elf):
        self.snapshot = snapshot
        self.p = shared.Probe('copied real title RAM ARM11',elf=elf)
        assert all(not (s['address'] < shared.STACK+0x20000 and
            shared.STACK < s['address']+s['size']) for s in snapshot.spans)
        assert all(not (s['address'] < shared.WORK+0x10000 and
            shared.WORK < s['address']+s['size']) for s in snapshot.spans)
        snapshot.install(self.p.cpu)
        self.tls_restoration=snapshot.restore_tls(self.p.cpu)
        self.view = CpuView(snapshot,self.p.cpu)
        self.before = decode(self.view,self.p.names)
        assert not self.before['unavailable'],self.before['unavailable']
        self.ctx = int(self.before['context'],16)
        self.sub = int(self.before['sub']['address'],16)
        self.wrapper = int(self.before['intro_wrapper']['address'],16)
        self.model = int(self.before['intro_model']['address'],16)
        self.animation = int(self.before['intro_animation']['address'],16)
        self.touch = int(self.before['touch']['address'],16)
        self.keys = int(self.before['keys']['address'],16)
        self.parent = self.view.u32(self.ctx+0xf4)
        self.other = self.view.u32(self.ctx+0x30)+0x44
        self.callback = self.p.names['func_ov007_020b0da0']
        self.initial_counter = self.before['sub']['ticks']
        self.events,self.counter = [],self.initial_counter
        self.hook = self.p.cpu.hook_add(UC_HOOK_CODE,self.observe)

    def observe(self,cpu,address,size,_):
        for name,count in IntroFixture.SEAMS.items():
            if address == self.p.names[name]:
                args = [cpu.reg_read(reg) for reg in shared.REGS[:count]]
                if name == 'RequestScreenFade' and args[2]:
                    args[2] = next((n for n,a in self.p.names.items() if a == args[2]),hex(args[2]))
                if name in ('RequestScreenFade','func_ov007_020bdeb0','func_ov007_020bde70'):
                    self.events.append(dict(counter=self.counter,service=name,arguments=args))
                cpu.reg_write(shared.REGS[0],0)
                cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
                return
        for name in IntroFixture.MATH:
            if address == self.p.names[name]:
                a,b = [IntroFixture.signed(cpu.reg_read(r)) for r in shared.REGS[:2]]
                assert b != 0
                numerator = a << 12 if name.endswith('4fdivEii') else a
                quotient = abs(numerator)//abs(b)
                if (numerator < 0) != (b < 0): quotient = -quotient
                value = a-quotient*b if name.endswith('3modEii') else quotient
                cpu.reg_write(shared.REGS[0],value & 0xffffffff)
                cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
                return

    def half(self,address,value):
        self.p.cpu.mem_write(address,struct.pack('<H',value & 0xffff))

    def check_canaries(self):
        comp = self.snapshot.u32(self.model)
        model_file = self.snapshot.u32(comp)
        bone_count = self.snapshot.u32(model_file+4)
        assert bone_count <= 256
        matrix = self.snapshot.u32(self.model+4)
        auxiliary = self.snapshot.u32(self.snapshot.u32(self.snapshot.u32(self.ctx+0xc4))+4)
        scrolls = [self.snapshot.u32(self.snapshot.u32(self.ctx+offset)+4) for offset in (0xcc,0xe0)]
        mutable = [(self.sub,16),(self.wrapper+0x18,4),(self.model+0x40,0x4c),
            (self.model+0x94,8),(self.animation,0x1c),(comp+0xc,4),
            (matrix,bone_count*0x30),(self.touch+8,0xe),(self.keys,4),(auxiliary+2,2),
            *[(scroll+0x20,16) for scroll in scrolls],
            (self.p.names['data_ov007_02103340'],4),(self.p.names['data_ov007_02103344'],4)]
        checked = []
        for span in self.snapshot.spans:
            expected = bytearray(self.snapshot.read(span['address'],span['size']))
            for address,size in mutable:
                if Snapshot.contains(span,address,size):
                    offset = address-span['address']
                    expected[offset:offset+size] = self.p.cpu.mem_read(address,size)
            actual = bytes(self.p.cpu.mem_read(span['address'],span['size']))
            assert actual == expected,(span['name'],'unexpected mutation outside documented gate/input fields',
                [(hex(span['address']+i),a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a != b][:16])
            checked.append(dict(name=span['name'],bytes=span['size'],all_unlisted_bytes_unchanged=True))
        return checked

    def run(self,name,touch=None,key=0,steps=400):
        detach,completion,checkpoints = None,None,[]
        for step in range(1,steps+1):
            self.p.call('func_ov007_020c92d0',[self.sub])
            self.counter = self.view.u32(self.sub+12)
            assert self.counter == self.initial_counter+step
            if touch is not None:
                self.half(self.touch+8,touch[0]);self.half(self.touch+10,touch[1])
            self.half(self.touch+12,int(step == 1 and touch is not None))
            self.half(self.touch+20,0)
            self.half(self.keys,key if step == 1 else 0);self.half(self.keys+2,0)
            self.p.call('func_ov007_020b0a20',[])
            if self.view.s16(self.sub+2) == 10:
                completion = decode(self.view,self.p.names)
                break
            # Actual caller arguments come from the restored title context.
            self.p.call('func_ov007_020be9ac',[self.wrapper,self.parent,self.other])
            if detach is None and not self.view.u32(self.wrapper+0x18): detach = self.counter
            if step in (1,4,20,60,100,200,300,steps):
                state = decode(self.view,self.p.names)
                checkpoints.append(dict(step=step,counter=self.counter,flags=state['sub']['flags'],
                    callback=state['intro_wrapper']['callback'],animation=state['intro_animation'],
                    pending=state['sub']['pending']))
        after = decode(self.view,self.p.names)
        canaries = self.check_canaries()
        self.p.cpu.hook_del(self.hook)
        assert not self.p.faults
        return dict(case=name,controlled_touch=touch,controlled_first_key=key,
            steps=step,initial_counter=self.initial_counter,callback_detach=detach,
            completion_counter=completion['sub']['ticks'] if completion else None,
            before=self.before,after=after,checkpoints=checkpoints,events=self.events,
            restored_span_canaries=canaries,normal_returns_preserve_stack_and_registers=True,
            instruction_or_memory_faults=self.p.faults)


class RomFileObserver:
    """Optional raw FAT I/O provider over independently verified cartridge bytes.

    This replaces the *whole* native_card FAT open/read/close/wait boundary,
    not allocation, decompression, game callbacks, state, or file IDs. The CPU
    still executes the original caller/heap/parser. No target FILE*, libc TLS,
    OS service or std::list node is fabricated. Each request can fail normally.
    """
    NAMES={'func_0205d568','FS_ReadFile','func_0205d688','FS_CloseFile','func_0205d3f4'}

    def __init__(self,route):
        self.route=route;self.open={};self.events=[]

    def valid(self,address,size):
        return any(Snapshot.contains(s,address,size) for s in self.route.snapshot.spans) or (
            shared.STACK<=address and address+size<=shared.STACK+0x20000)

    def words(self,object,offset,value):
        assert self.valid(object+offset,4),('FS object range',hex(object),offset)
        self.route.p.words(object+offset,value)

    def invoke(self,name,args):
        route=self.route;p=route.p;object=args[0]
        if not object:return 0xffffffff if name in ('FS_ReadFile','func_0205d688') else 0
        assert self.valid(object,0x44),('FS object span',hex(object))
        if name=='func_0205d568':
            archive,id=args[1:3]
            if archive!=p.names['data_020a8074'] or id>=0x8000:
                self.events.append(dict(function=name,file_id=id,result=0,
                    reason='No controlled NARC mount; request not invented'))
                return 0
            matches=[(i,e) for i,e in enumerate(route.cart.entries) if e[0]==id]
            if len(matches)!=1:return 0
            index,entry=matches[0]
            # Reuse the actual captured C++ pack entry, including path/CRC.
            service=route.view.u32(p.names['_ZN6sm64ds12_GLOBAL__N_1L12native_filesE'])
            pack=route.view.u32(service);start=route.view.u32(pack+40)
            at=start+index*40
            values=tuple(route.view.u32(at+i*4) for i in range(4))
            path=route.view.read(route.view.u32(at+16),route.view.u32(at+20)).decode()
            assert (*values,path)==entry,('Captured FAT metadata differs',id)
            data=route.cart.file(path)
            assert len(data)==entry[2] and zlib.crc32(data)==entry[3]
            if object in self.open:self.invoke('FS_CloseFile',[object,0,0,0])
            if len(self.open)>=64:return 0
            self.open[object]=data
            flags=(p.u32(object+12)|0x10)&~0x21
            for offset,value in ((8,archive),(12,flags),(16,6),(20,0),(32,0),
                    (36,len(data)),(40,0),(44,archive),(48,id)):
                self.words(object,offset,value)
            self.events.append(dict(function=name,object=hex(object),archive=hex(archive),
                file_id=id,path=path,bytes=len(data),crc32=hex(entry[3]),
                payload_sha256=shared.base.sha(data),result=1,
                captured_metadata_and_original_payload_verified=True))
            return 1
        if name in ('FS_ReadFile','func_0205d688'):
            destination,count=args[1:3]
            if count&0x80000000 or object not in self.open or (not destination and count):return 0xffffffff
            position=p.u32(object+40);data=self.open[object]
            if position>len(data):return 0xffffffff
            amount=min(count,len(data)-position)
            if amount:
                assert self.valid(destination,amount),('FS copy range',hex(destination),amount)
                p.cpu.mem_write(destination,data[position:position+amount])
            for offset,value in ((40,position+amount),(16,0),(20,0),(12,p.u32(object+12)&~1)):
                self.words(object,offset,value)
            self.events.append(dict(function=name,object=hex(object),destination=hex(destination),
                requested=count,position=position,result=amount,
                copied_original_payload=True,asynchronous_argument=args[3] if name=='func_0205d688' else None))
            return amount
        if name=='FS_CloseFile':
            if object not in self.open:return 0
            del self.open[object]
            for offset,value in ((8,0),(16,12),(12,p.u32(object+12)&~0x31)):
                self.words(object,offset,value)
            self.events.append(dict(function=name,object=hex(object),result=1))
            return 1
        if name=='func_0205d3f4':return int(object in self.open and p.u32(object+20)==0)
        raise AssertionError(name)

    def self_test(self):
        """Exercise failure/partial-read rules only in disjoint oracle scratch."""
        p=self.route.p;obj=shared.STACK+0x17000;destination=shared.STACK+0x18000
        archive=p.names['data_020a8074'];p.cpu.mem_write(obj,bytes(0x44))
        checks=[]
        for name,args,result in [('func_0205d568',[0,archive,222,0],0),
            ('func_0205d568',[obj,0,222,0],0),('func_0205d568',[obj,archive,0x7fff,0],0),
            ('func_0205d568',[obj,archive,0x8000,0],0),('FS_ReadFile',[obj,destination,4,0],0xffffffff),
            ('FS_CloseFile',[obj,0,0,0],0),('func_0205d3f4',[obj,0,0,0],0)]:
            assert self.invoke(name,args)==result,(name,args)
            checks.append(dict(function=name,arguments=list(map(hex,args)),expected=hex(result)))
        assert self.invoke('func_0205d568',[obj,archive,222,0])==1
        assert self.invoke('FS_ReadFile',[obj,destination,0xffffffff,0])==0xffffffff
        assert self.invoke('FS_ReadFile',[obj,0,4,0])==0xffffffff
        assert self.invoke('func_0205d3f4',[obj,0,0,0])==1
        data=self.open[obj]
        assert self.invoke('FS_ReadFile',[obj,destination,4,0])==4
        assert bytes(p.cpu.mem_read(destination,4))==data[:4]
        self.words(obj,40,len(data)-3)
        assert self.invoke('func_0205d688',[obj,destination,8,1])==3
        assert bytes(p.cpu.mem_read(destination,3))==data[-3:]
        assert p.u32(obj+40)==len(data)
        assert self.invoke('FS_ReadFile',[obj,destination,8,0])==0
        rejected=False
        self.words(obj,40,0)
        try:self.invoke('FS_ReadFile',[obj,shared.STACK+0x1fffe,4,0])
        except AssertionError:rejected=True
        assert rejected
        assert self.invoke('FS_CloseFile',[obj,0,0,0])==1
        assert self.invoke('FS_CloseFile',[obj,0,0,0])==0
        for span in self.route.snapshot.spans:
            assert bytes(p.cpu.mem_read(span['address'],span['size']))==self.route.snapshot.read(span['address'],span['size'])
        self.events=[]
        return dict(status='passed',negative_cases=checks,negative_count_and_null_destination=True,
            partial_read_and_eof=True,asynchronous_read_finishes_synchronously=True,
            out_of_span_destination_rejected=True,wait_and_double_close=True,
            all_captured_spans_unchanged=True,no_original_payload_exported=True)


class FullRoute(Replay):
    """Original input/state/render-provider callbacks, over the same RAM clone."""
    SEAMS={'sm64ds_draw_fifo':1,'sm64ds_draw_port':2,'sm64ds_draw_list':2,
        'func_ov007_020bdeb0':1,'func_ov007_020bde70':2,
        '_ZN6sm64ds27native_platform_defer_audioEPKc':1}

    def __init__(self,snapshot,elf,rom_fs=False):
        super().__init__(snapshot,elf)
        self.entries=Counter();self.last_entries=deque(maxlen=32)
        self.entry_names={a:n for n,a in self.p.names.items() if
            n.startswith(('func_','_ZN')) or n in self.SEAMS or n.startswith('sm64ds_')
            or n.endswith('_classInit')
            or n in ('__syscall_getreent','RequestScreenFade','StartFile','LoadLevel',
                'LoadLevelNoReturn','SetPlayerGlobals','SetNumPlayers','LoadOverlay',
                'LoadArchive','LoadTextNarcs') or n in RomFileObserver.NAMES}
        self.entry_names[self.p.names['RequestScreenFade']]='RequestScreenFade'
        self.preflight=preflight_callers(self.p,elf)
        self.regbase=snapshot.video_view(0x04000000)['address']
        self.file_observations=[]
        self.cart=shared.base.Cartridge(ROOT/'build/europe.pak')
        self.cpp_layout=shared.base.layout_contract()
        self.fs=RomFileObserver(self) if rom_fs else None
        self.fs_checks=self.fs.self_test() if self.fs else None

    def inspect_file_boundary(self,name,args):
        """Read objects at the actual provider; never return success or inject data."""
        if name=='func_0205d568':
            self.file_observations.append(dict(function=name,object=hex(args[0]),
                archive=hex(args[1]),file_id=args[2],
                real_rom_archive=args[1]==self.p.names['data_020a8074']))
        elif name.startswith('_ZN6sm64ds9AssetPack4readE'):
            pack,index,output,error=args
            # Offsets are asserted by the target compiler layout_contract,
            # and checked again against this immutable ELF's read body.
            start,end,capacity=(self.view.u32(pack+40+4*i) for i in range(3))
            assert start<=end<=capacity and (end-start)%40==0
            count=(end-start)//40
            assert index<count
            at=start+40*index
            values=tuple(self.view.u32(at+i*4) for i in range(4))
            pointer,length=self.view.u32(at+16),self.view.u32(at+20)
            assert length<256
            path=self.view.read(pointer,length).decode()
            assert self.cart.entries[index]==(*values,path),('captured pack entry differs',index,path)
            # Local vector is on the isolated original caller stack, which is
            # intentionally outside captured spans. It is observed via CPU.
            vector=tuple(self.p.u32(output+i*4) for i in range(3))
            assert vector==(0,0,0) or vector[0]<=vector[1]<=vector[2]
            data=self.cart.file(path)
            self.file_observations.append(dict(function=name,pack=hex(pack),
                pack_file=hex(self.view.u32(pack)),index=index,entry_count=count,
                entry_address=hex(at),file_id=values[0],packed_offset=values[1],
                size=values[2],crc32=hex(values[3]),path=path,
                original_payload_sha256=shared.base.sha(data),
                captured_entry_matches_verified_pack=True,vector_address=hex(output),
                vector_words=list(map(hex,vector)),vector_structurally_valid=True,
                error_address=hex(error),no_payload_injected=True))

    def observe(self,cpu,address,size,_):
        name=self.entry_names.get(address)
        if name:
            self.entries[name]+=1;self.last_entries.append(name)
        if name=='func_0205d568' or (name and name.startswith('_ZN6sm64ds9AssetPack4readE')):
            self.inspect_file_boundary(name,[cpu.reg_read(r) for r in shared.REGS])
        if self.fs and name in self.fs.NAMES:
            value=self.fs.invoke(name,[cpu.reg_read(r) for r in shared.REGS])
            cpu.reg_write(shared.REGS[0],value & 0xffffffff)
            cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
            return
        if name in ('sm64ds_native_missing','_ZN6sm64ds20native_platform_failEPKc'):
            raise RuntimeError('Reached production unavailable/fail boundary: '+name)
        if name in self.SEAMS:
            count=self.SEAMS[name]
            args=[cpu.reg_read(r) for r in shared.REGS[:count]]
            if not name.startswith('sm64ds_draw_'):
                self.events.append(dict(counter=self.counter,service=name,arguments=args))
            cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
            return
        if name in ('RequestScreenFade','func_ov007_020b16f0','func_ov007_020b1604',
                    'func_ov007_020af34c','func_ov007_020af434','func_ov007_020af580',
                    'func_ov007_020b63e4','StartFile'):
            args=[cpu.reg_read(r) for r in shared.REGS[:3]] if name=='RequestScreenFade' else []
            if name=='func_ov007_020b63e4':args=[hex(cpu.reg_read(shared.REGS[0]))]
            if name=='StartFile':args=[cpu.reg_read(r) for r in shared.REGS[:2]]
            if name=='RequestScreenFade' and args[2]:
                args[2]=next((n for n,a in self.p.names.items() if a==args[2]),hex(args[2]))
            self.events.append(dict(counter=self.counter,original=name,arguments=args))

    def clocks(self, touch=None, key=0):
        values=(*touch,1,0) if touch is not None else (0,0,0,0)
        result=self.call_component('func_ov007_020b7090',[*values,key])
        self.counter=self.view.u32(self.sub+12)
        # Original renderer updates model/cursor/palette clocks and publishes
        # data03430. Only final void geometry packet leaves are observed.
        self.call_component('func_ov007_020b7040',[])
        self.call_component('func_ov007_020b6eb4',[])
        return result

    def call_component(self,name,arguments):
        # Full menu reload executes original decompression and many allocations.
        # Keep a finite 64M instruction component budget and the same
        # ARM ABI/stack guards; exceeding it remains a reported oracle limit.
        cpu=self.p.cpu;stack=shared.STACK;sp=stack+0xf000
        cpu.reg_write(UC_ARM_REG_CPSR,0x10)
        cpu.mem_write(stack,bytes([0x6d])*0x10000)
        for index,value in enumerate((*arguments,0,0,0,0)[:4]):
            cpu.reg_write(shared.REGS[index],value & 0xffffffff)
        for index,value in enumerate(arguments[4:]):self.p.words(sp+4*index,value)
        incoming=bytes(cpu.mem_read(sp,64))
        cpu.reg_write(UC_ARM_REG_SP,sp);cpu.reg_write(UC_ARM_REG_LR,shared.STOP)
        for index,reg in enumerate(shared.base.SAVED):cpu.reg_write(reg,0x12340000+index)
        cpu.emu_start(self.p.names[name],shared.STOP,count=64000000)
        assert not self.p.faults,(name,self.p.faults)
        assert cpu.reg_read(UC_ARM_REG_PC)==shared.STOP,(name,'64M instruction component bound')
        assert cpu.reg_read(UC_ARM_REG_SP)==sp,(name,'stack pointer',hex(cpu.reg_read(UC_ARM_REG_SP)))
        changed=[(index,hex(cpu.reg_read(reg))) for index,reg in enumerate(shared.base.SAVED)
            if cpu.reg_read(reg)!=0x12340000+index]
        assert not changed,(name,'callee-saved registers',changed)
        assert bytes(cpu.mem_read(sp,64))==incoming,(name,'caller stack slots')
        assert bytes(cpu.mem_read(stack,32))==bytes([0x6d])*32,(name,'oracle stack lower guard')
        return cpu.reg_read(shared.REGS[0])

    def state(self):
        title=decode(self.view,self.p.names)
        fade=inspect_snapshot(self.snapshot,self.p.names,self.p.cpu)
        return dict(title=title,fade_root=fade['fade_root'],effect=fade.get('effect'),display=fade['display'])

    def menu_item(self,index):
        element=self.view.u32(self.ctx+0x114+index*4)
        object,placement=self.view.u32(element),self.view.u32(element+4)
        item=self.view.u16(object);controller=self.view.u32(object+4)
        rect=self.view.u32(object+0x28)
        x,y=self.view.s32(placement+4)>>12,self.view.s32(placement+8)>>12
        box=[x+self.view.s32(rect+4),y+self.view.s32(rect+8),
            x+self.view.s32(rect+12),y+self.view.s32(rect+16)]
        flags=self.view.u32(self.p.names['data_ov007_020d77dc']+item*12)
        opacity=self.view.s32(object+0x10);state=self.view.s16(controller)
        info=self.view.u32(self.p.names['data_ov007_02102ddc']+4)
        selectable=state==0 and (opacity>=4096 or flags&2) and not flags&1 and info==4
        return dict(index=index,id=item,element=hex(element),object=hex(object),
            controller_state=state,controller_pending=self.view.s16(controller+2),
            opacity=opacity,static_flags=hex(flags),info_state=info,
            box=box,selectable=bool(selectable),
            controlled_touch=((max(0,box[0])+min(255,box[2]))//2,
                              (max(0,box[1])+min(191,box[3]))//2))

    def protected_canaries(self, assets_alive=True):
        regions=[]
        for span in self.snapshot.spans:
            if span['name']=='linear-heap': regions.append((span['address'],span['size'],'linear heap'))
        if 'data_02082214' in self.p.names:regions.append((self.p.names['data_02082214'],0x4000,'original trig table'))
        regions.append((self.p.names['data_0209b340'],14,'actual no-save data alias'))
        if assets_alive:
            table=self.snapshot.u32(self.model+0x14)
            for index in (0,1):
                bca=self.snapshot.u32(table+index*4)
                # Header and relocated BCA channel pointers are immutable
                # throughout the introduction; no constructor is rerun.
                regions.append((bca,0x18,'BCA header '+str(index)))
        output=[]
        for address,size,label in regions:
            before=self.snapshot.read(address,size)
            assert bytes(self.p.cpu.mem_read(address,size))==before,(label,'protected memory changed')
            output.append(dict(region=label,address=hex(address),bytes=size,unchanged=True))
        return output

    def run_empty_file(self,elf,steps=240):
        """Continue the same file-menu clone through the original result/caller."""
        roots=('_ZN9dScDSMT_c8BehaviorEv','StartFile','_ZN8dScene_c16SpawnIfNecessaryEv',
            '_ZN5Stage19BeforeInitResourcesEv','_ZN5Stage13InitResourcesEv')
        audit=preflight_callers(self.p,elf,roots)
        hook=self.p.cpu.hook_add(UC_HOOK_CODE,self.observe)
        before=self.state();items=[self.menu_item(i) for i in (14,15,16)]
        assert before['title']['main']['current']==5 and before['title']['sub']['current']==0
        assert before['title']['no_save_predicate']['slots']==[0,0,0]
        selected=items[0];assert selected['selectable'],selected
        transitions=[];failure=None;result=0;prior=None;behavior=None
        scene=self.view.u32(self.p.names['data_0209f5c0'])
        # This is the actual captured scene, checked against its OV007 vtable.
        assert self.view.u32(scene)==self.p.names['data_ov007_021032e8']
        destination=self.view.u32(self.p.names['data_0209b33c'])
        source=self.p.names['data_0209caa0']
        template_before=self.view.read(source,0x44)
        dest_before=self.view.read(destination,0x44)
        try:
            for step in range(1,steps+1):
                result=self.clocks(selected['controlled_touch'] if step==1 else None,0)
                state=self.state();title=state['title']
                pair=(title['main']['current'],title['sub']['current'],title['input_lock'])
                if pair!=prior:
                    transitions.append(dict(step=step,b7090_return=result,**state));prior=pair
                if result in (3,4,5):
                    assert result==3 and title['exit_flag']==1
                    # Original Behavior calls b7090 itself, observes the real
                    # result and invokes SaveData copy/StartFile. No result,
                    # actor ID, pending scene, or StartFile argument is forced.
                    behavior=self.call_component('_ZN9dScDSMT_c8BehaviorEv',[scene])
                    break
            else:raise RuntimeError('Empty-file exit did not complete in controlled bound')
        except Exception as error:
            failure=dict(type=type(error).__name__,message=str(error),step=step,
                pc=hex(self.p.cpu.reg_read(UC_ARM_REG_PC)),faults=self.p.faults,
                last_entries=list(self.last_entries),component_oracle=True)
        after=self.state();pending=self.view.u16(self.p.names['data_02092664'])
        self.p.cpu.hook_del(hook)
        dest_after=self.view.read(destination,0x44)
        if not failure:
            try:
                assert dest_after==template_before and behavior==1 and pending==3
                assert self.entries['StartFile']==1
            except Exception as error:
                failure=dict(type=type(error).__name__,message=str(error),step=step,
                    pc=hex(self.p.cpu.reg_read(UC_ARM_REG_PC)),faults=self.p.faults,
                    last_entries=list(self.last_entries),component_oracle=True)
        return dict(status='blocked' if failure else 'passed',failure=failure,
            initial=before,after=after,slot_items=items,controlled_selection=selected,
            steps=step,exit_result=result,actual_scene=hex(scene),actual_Behavior_return=behavior,
            pending_scene_id=pending,newfile_template=hex(source),newfile_destination=hex(destination),
            original_template_sha256=shared.base.sha(template_before),
            destination_before_sha256=shared.base.sha(dest_before),
            destination_after_sha256=shared.base.sha(dest_after),
            original_template_copied=dest_after==template_before,
            original_functions_executed=dict(sorted(self.entries.items())),preflight=audit,
            state_transitions=transitions,events=self.events,protected_canaries=self.protected_canaries(False),
            no_exit_result_pending_scene_or_StartFile_forced=True,
            stage_not_constructed_or_initialized=True,
            scope='Original file-selection/fade/Behavior/StartFile components; Stage scene request only. '
                'Raw FAT bytes supplied by declared observer. No complete frame, live process, GPU or physical proof.')

    def scene_state(self):
        """Bounded observation that survives original title-context destruction."""
        view=self.view;names=self.p.names
        scene=view.u32(names['data_0209f5c0'])
        out=dict(scene=hex(scene),pending_scene=view.u16(names['data_02092664']),
            already_spawned=view.read(names['data_02092660'],1)[0],
            current_fader=hex(view.u32(names['data_0209f5bc'])),
            graph_callback=hex(view.u32(names['data_0209d4a8'])),
            title_context=hex(view.u32(names['data_ov007_0210342c'])))
        if scene:
            try:
                record=view.read(scene,0x50)
                out.update(vptr=hex(struct.unpack_from('<I',record)[0]),
                    actor_id=struct.unpack_from('<H',record,0xc)[0],
                    alive_state=record[0xe],should_be_killed=record[0xf],
                    lifecycle_state=record[0x10],pause_flags=record[0x13])
            except ValueError:
                out['scene_outside_captured_spans']=True
        fader=view.u32(names['data_0209f5bc'])
        if fader:
            try:
                out['fader']={k:hex(view.u32(fader+offset)) for k,offset in
                    (('vptr',0),('interpolation',4),('speed',8))}
            except ValueError:out['fader_outside_captured_spans']=True
        return out

    def run_scene_lifecycle(self,elf,steps=90):
        """Run the real scene/fader/list components after the real Stage request.

        This is the ordering of NativeBoot's original phase2..5 calls. Power,
        IRQ/VBlank and scanout presentation are outside this CPU component
        experiment. Input sampler records are the already-released clone
        records; no scene/global/list flag is manually written.
        """
        order=('func_02019390','func_0203bb60','func_0203bc7c','func_02023498',
            '_ZN8dScene_c16SpawnIfNecessaryEv','func_02044120','func_02019404')
        audit=preflight_callers(self.p,elf,order)
        hook=self.p.cpu.hook_add(UC_HOOK_CODE,self.observe)
        before=self.scene_state();failure=None;transitions=[];prior=None;results=[]
        entries_before=self.entries.copy()
        assert before['pending_scene']==3 and before['already_spawned']==1
        # The captured title still owns the scene until its real fader and
        # cleanup finish. Verify the original guard without clearing it.
        span_before=[shared.base.sha(bytes(self.p.cpu.mem_read(s['address'],s['size'])))
            for s in self.snapshot.spans]
        guard=self.call_component('_ZN8dScene_c16SpawnIfNecessaryEv',[])
        assert guard==0 and span_before==[shared.base.sha(bytes(self.p.cpu.mem_read(s['address'],s['size'])))
            for s in self.snapshot.spans]
        step=0;component=None
        try:
            for step in range(1,steps+1):
                for component in order:
                    value=self.call_component(component,[])
                    if component=='_ZN8dScene_c16SpawnIfNecessaryEv':
                        results.append(dict(step=step,return_value=hex(value)))
                state=self.scene_state()
                signature=(state['scene'],state['pending_scene'],state['already_spawned'],
                    state.get('should_be_killed'),state['graph_callback'],state['title_context'])
                if signature!=prior:
                    transitions.append(dict(step=step,**state));prior=signature
                if self.entries['_ZN5Stage13InitResourcesEv']:
                    break
            else:raise RuntimeError('Original scene lifecycle exceeded controlled component bound')
        except Exception as error:
            failure=dict(type=type(error).__name__,message=str(error),step=step,
                component=component,pc=hex(self.p.cpu.reg_read(UC_ARM_REG_PC)),
                faults=self.p.faults,last_entries=list(self.last_entries),component_oracle=True,
                integrated_fault_not_claimed=True)
        after=self.scene_state();self.p.cpu.hook_del(hook)
        canaries=[]
        for address,size,label in [(s['address'],s['size'],'linear heap') for s in self.snapshot.spans
                if s['name']=='linear-heap']+[(self.p.names['data_02082214'],0x4000,'original trig table')]:
            canaries.append(dict(region=label,address=hex(address),bytes=size,
                unchanged=bytes(self.p.cpu.mem_read(address,size))==self.snapshot.read(address,size)))
        return dict(status='component_boundary' if failure else 'passed',failure=failure,
            before=before,after=after,steps=step,actual_component_order=list(order),
            original_already_spawned_guard_return=guard,guard_all_captured_spans_unchanged=True,
            no_scene_id_pending_or_spawn_flag_forced=True,
            original_Stage_constructor_executed=bool(self.entries['dScStage_c_classInit']),
            original_Stage_InitResources_executed=bool(self.entries['_ZN5Stage13InitResourcesEv']),
            state_transitions=transitions,spawn_results=results,
            original_functions_executed=dict(sorted(self.entries.items())),
            lifecycle_phase_entry_deltas=dict(sorted((self.entries-entries_before).items())),
            function_counts_are_cumulative_over_prior_title_and_file_phases=True,
            tls_restoration=self.tls_restoration,
            raw_FAT_provider_events=self.fs.events if self.fs else [],
            file_boundaries=self.file_observations,events=self.events,preflight=audit,
            protected_canaries=canaries,
            scope='Original scene-fader/cleanup/manager/spawn components over copied real RAM. '
                'The actual Stage request comes from original empty-slot selection and Behavior. '
                'Raw FAT provider and final geometry/audio observations remain declared. '
                'No power/IRQ/VBlank/full frame, live process, Azahar or physical hardware proof.')

    def run_route(self,steps=1000,entry='B'):
        initial_ready=bool(self.before.get('touch_to_start_ready'))
        checkpoints=[];transitions=[];ready_step=0 if initial_ready else None
        start_step=None;menu_step=None;file_step=None
        menu_touch_step=None;menu_touch_item=None;file_ready_step=None
        start_key=2 if entry=='B' else 1 if entry=='keyboard_A' else 0
        start_touch=None;target_reached=False;keyboard_trace=[]
        prior=None;failure=None;canaries=self.protected_canaries(True) if initial_ready else []
        try:
            for step in range(1,steps+1):
                # First one-frame touch releases the preserved original wait.
                touch=(128,96) if step==1 and not initial_ready else None
                key=0
                if ready_step is not None and start_step is None and entry=='B':
                    key=start_key;start_step=step
                elif ready_step is not None and entry=='keyboard_A':
                    if start_step is None:
                        key=1;start_step=step
                    elif step==start_step+2:
                        key=1;menu_touch_step=step;menu_touch_item=self.menu_item(0)
                elif ready_step is not None and start_step is None and entry=='adventure':
                    item=self.menu_item(0)
                    box=item['box']
                    if item['selectable'] and max(0,box[0])<=min(255,box[2]) and max(0,box[1])<=min(191,box[3]):
                        start_step=step;menu_touch_step=step;menu_touch_item=item
                        start_touch=item['controlled_touch'];touch=start_touch
                self.clocks(touch,key)
                state=self.state();title=state['title']
                if entry=='keyboard_A' and start_step is not None and step<=start_step+3:
                    logical=self.view.u32(self.view.u32(self.p.names['data_ov007_02103360']))
                    keyboard_trace.append(dict(step=step,key=key,
                        logical_focus=self.view.s16(logical),pending_focus=self.view.s16(logical+2),
                        touch=title['touch'],keys=title['keys'],input_lock=title['input_lock']))
                if step==start_step:
                    checkpoints.append(dict(step=step,reason='fresh controlled title entry',
                        key=key,touch=touch,**state))
                if step==menu_touch_step:
                    checkpoints.append(dict(step=step,reason='fresh controlled menu item touch',
                        selected_item=menu_touch_item,**state))
                if step % 50 == 0:
                    print(json.dumps(dict(progress_step=step,main=title['main']['current'],
                        sub=title['sub']['current'],subcounter=title['sub']['ticks'],
                        frame=title.get('intro_animation',{}).get('frame'),lock=title['input_lock'])),flush=True)
                pair=(title['main']['current'],title['sub']['current'],title['input_lock'])
                if pair!=prior:
                    transitions.append(dict(step=step,counter=title['sub']['ticks'],states=pair,
                        pending=title['main']['pending'],subpending=title['sub']['pending']))
                    checkpoints.append(dict(step=step,reason='state/lock transition',**state));prior=pair
                if title.get('touch_to_start_ready') and ready_step is None:
                    ready_step=step;canaries=self.protected_canaries(True)
                    checkpoints.append(dict(step=step,reason='title ready before fresh controlled entry',**state))
                if title['main']['current'] in (2,3) and menu_step is None:
                    menu_step=step;checkpoints.append(dict(step=step,reason='original B star-drawing mode',**state))
                if entry=='B' and menu_step is not None and not title['input_lock']:
                    # B explicitly chooses drawing mode. Adventure belongs to
                    # title state0/sub0; waiting in drawing mode is expected.
                    target_reached=True;break
                if title['main']['current']==5 and file_step is None:
                    file_step=step;checkpoints.append(dict(step=step,reason='normal file-selection state',**state))
                if file_step is not None and title['sub']['current']==0 and title['sub']['ticks']>=2 and not title['input_lock'] and state['effect']['fields']['clock_state']==3:
                    file_ready_step=step;target_reached=True
                    checkpoints.append(dict(step=step,reason='file-selection settled and real fade complete',**state));break
        except Exception as error:
            failure=dict(type=type(error).__name__,message=str(error),step=step,
                pc=hex(self.p.cpu.reg_read(UC_ARM_REG_PC)),faults=self.p.faults,
                last_entries=list(self.last_entries))
            if self.p.cpu.reg_read(UC_ARM_REG_PC)==self.p.names['__syscall_getreent']+8:
                failure.update(classification='Host CPU oracle lacks captured ARM11 TLS register; '
                    'not an integrated native game fault',function='__syscall_getreent',
                    preceding_instruction='mrc p15,0,r3,c13,c0,3 (TPIDRURW)',
                    fault_instruction='ldr r1,[r3]',tls_value=hex(self.p.cpu.reg_read(shared.REGS[3])),
                    no_IO_failure_or_game_pointer_bug_proven=True)
        if not failure and not target_reached:
            failure=dict(type='OracleStepLimit',message='Chosen normal entry did not settle within controlled component budget',
                step=step,pc=hex(self.p.cpu.reg_read(UC_ARM_REG_PC)),faults=self.p.faults,
                last_entries=list(self.last_entries),not_integrated_game_fault=True)
        after=self.state()
        self.p.cpu.hook_del(self.hook)
        if not failure:
            assert ready_step is not None and start_step is not None
            assert self.entries['RequestScreenFade'] and self.entries['func_ov007_020bf690']
            assert initial_ready or self.entries['func_ov007_020b16f0']
            assert not self.p.faults
            canaries+=self.protected_canaries(False)
        return dict(status='blocked' if failure else 'passed',failure=failure,
            initial=self.before,after=after,steps=step,ready_step=ready_step,
            controlled_start_step=start_step,controlled_start_key=start_key,
            controlled_entry=entry,controlled_start_touch=start_touch,
            initial_real_snapshot_was_ready=initial_ready,
            controlled_keyboard_entry_trace=keyboard_trace,
            visible_menu_step=menu_step,file_selection_step=file_step,
            controlled_menu_touch_step=menu_touch_step,controlled_menu_touch_item=menu_touch_item,
            file_selection_settled_step=file_ready_step,
            state_transitions=transitions,checkpoints=checkpoints,events=self.events,
            original_functions_executed=dict(sorted(self.entries.items())),preflight=self.preflight,
            file_boundary_observations=self.file_observations,target_cpp_layout=self.cpp_layout,
            reference_pack_sha256=shared.base.sha(self.cart.raw),
            optional_raw_FAT_provider=dict(enabled=bool(self.fs),
                functions=sorted(self.fs.NAMES) if self.fs else [],
                events=self.fs.events if self.fs else [],
                controlled_provider_tests=self.fs_checks,
                no_original_game_allocator_decompressor_state_or_callback_replaced=True,
                no_NARC_mount_or_missing_asset_invented=True,
                no_libc_TLS_FILE_or_list_node_fabricated=True),
            observed_service_seams=self.SEAMS,arithmetic_seams=[],protected_canaries=canaries,
            actual_component_order=['original b7090 input/state/widgets','original b7040 render clocks',
                'original adapted b6eb4 fade/display commit'],
            no_pending_state_scene_or_StartFile_forced=True,
            scope='Original component chain over captured RAM; final geometry packets and audio observed. '
                'No complete engine/IRQ frame, rendered scanout, live input or physical hardware proof.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot',type=Path,required=True)
    parser.add_argument('--index',type=Path)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--full-route',action='store_true')
    parser.add_argument('--rom-fs',action='store_true',help='Optional verified raw FAT I/O boundary; original game caller/heap/parser remain real')
    parser.add_argument('--entry',choices=('B','adventure','keyboard_A'),default='B',
        help='Fresh B enters original star-drawing mode; Adventure touch selects the live title item0')
    parser.add_argument('--empty-file',action='store_true',help='Continue settled clone with fresh normal slot14 touch and original Behavior/StartFile')
    parser.add_argument('--stage-after-empty',action='store_true',help='Continue original fader/lists/spawn components after the real empty-file Stage request')
    args = parser.parse_args()
    snap = Snapshot(args.snapshot,args.index)
    elf_hash = shared.base.sha(args.elf.read_bytes())
    snapshot_hash = shared.base.sha(snap.raw)
    if args.full_route:
        runner=FullRoute(snap,args.elf,args.rom_fs)
        route=runner.run_route(entry=args.entry)
        newgame=runner.run_empty_file(args.elf) if args.empty_file and route['status']=='passed' else None
        lifecycle=runner.run_scene_lifecycle(args.elf) if args.stage_after_empty and newgame and newgame['status']=='passed' else None
        report=dict(status=route['status'],snapshot=snap.summary(),elf=str(args.elf.resolve()),
            elf_sha256=elf_hash,route=route,new_game=newgame,scene_lifecycle=lifecycle,
            source_sha256={str(p.relative_to(ROOT)):shared.base.sha(p.read_bytes()) for p in
                (Path(__file__).resolve(),ROOT/'tools/decode_native_title_ram.py')})
        if newgame:report['status']=newgame['status']
        if lifecycle:report['status']=lifecycle['status']
        assert snapshot_hash==shared.base.sha(args.snapshot.read_bytes())
        assert elf_hash==shared.base.sha(args.elf.read_bytes())
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(dict(status=report['status'],ready_step=route['ready_step'],
            menu_step=route['visible_menu_step'],failure=route['failure'],report=str(args.report))))
        return
    cases = []
    for name,touch,key,steps in [('no_touch',None,0,100),('fresh_A',None,1,40),
        ('fresh_B',None,2,40),('touch_left_outside',(95,96),0,40),
        ('touch_lower_outside',(128,129),0,40),('star_touch_inside',(128,96),0,400)]:
        replay = Replay(snap,args.elf)
        result = replay.run(name,touch,key,steps)
        assert result['before']['sub']['current'] == 11
        assert result['before']['no_save_predicate']['aebac'] == 1
        assert result['before']['intro_animation']['pause'] == 1
        if name == 'star_touch_inside':
            expected_detach = ((result['initial_counter']+1+19)//20)*20
            assert result['callback_detach'] == expected_detach,result
            assert result['completion_counter'] == expected_detach+304,result
            assert result['after']['sub']['pending'] == 10
            assert result['after']['intro_animation']['frame'] == 151
            assert result['after']['star_touch_counter'] == result['initial_counter']+1
            assert [e['arguments'][0] for e in result['events'] if e['service']=='RequestScreenFade'] == [3,3]
        else:
            assert result['callback_detach'] is None and result['completion_counter'] is None
            assert result['after']['intro_animation']['pause'] == 1
            assert result['after']['intro_animation']['frame'] == 0
            assert result['after']['intro_wrapper']['callback'] == result['before']['intro_wrapper']['callback']
            assert result['after']['star_touch_counter'] == 0
        cases.append(result)
    assert snapshot_hash == shared.base.sha(args.snapshot.read_bytes())
    assert elf_hash == shared.base.sha(args.elf.read_bytes())
    report = dict(status='passed',snapshot=snap.summary(),elf=str(args.elf.resolve()),
        elf_sha256=elf_hash,cases=cases,
        actual_block_order=['original c92d0(sub state)','controlled clone inputs',
            'original b0a20 title gate','original be9ac(wrapper,ctxf4,ctx30+44) model driver/callback'],
        observed_service_seams=IntroFixture.SEAMS,arithmetic_seams=IntroFixture.MATH,
        no_init_or_reset_of_restored_objects=True,
        scratch_stack=dict(address=hex(shared.STACK),bytes=0x20000,disjoint_from_capture=True),
        conclusion='Captured title is the original new-cart wait: no delivered star touch, callback active, '
            'BCA paused. A controlled clone touch in the original rectangle clears the callback at the '
            'next twenty-tick boundary and advances opening BCA to original subpending10.',
        scope='Host ARM CPU replay of original intro gate/model clock over copied real RAM. '
            'Seams observe rendering/audio/fade requests; no complete frame, fade completion, live input '
            'delivery, UI, Azahar launch, physical performance or menu-playability proof.',
        source_sha256={str(path.relative_to(ROOT)):shared.base.sha(path.read_bytes()) for path in
            (Path(__file__).resolve(),ROOT/'tools/decode_native_title_ram.py',
             ROOT/'tools/test_native_title_start_arm.py',ROOT/'tools/test_native_title_fade_arm.py')})
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='passed',cases=len(cases),snapshot_tick=snap.metadata.get('tick'),
        sub_counter=cases[0]['initial_counter'],controlled_touch_detach=cases[-1]['callback_detach'],
        controlled_touch_pending10=cases[-1]['completion_counter'],report=str(args.report))))


if __name__ == '__main__': main()
