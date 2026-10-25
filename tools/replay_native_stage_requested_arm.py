#!/usr/bin/env python3
"""Bounded offline replay of the exact live pre-Spawn checkpoint.

No pending scene, spawn result, actor or collision is manufactured. The first
case keeps production FAT I/O; an optional separate clone observes raw FAT
using verified private pack bytes. Every SVC is an explicit OS boundary.
"""
import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

import test_native_title_ram_arm as route
from unicorn import UC_HOOK_CODE, UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_C13_C0_3, UC_ARM_REG_CPSR,
    UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP, UC_ARM_REG_R4, UC_ARM_REG_R5)

shared = route.shared
ROOT = shared.ROOT
ATTEMPT = ROOT/'build/native-runtime-azahar/attempt-1791624434443135000'
OUT = ROOT/'build/native-integration-check/stage-requested-443444'
ELF_HASH = 'cd3799f6752140172893ec61b384777078c614b21d9e726b20cff8538e4f3f29'
ORDER = ('_ZN8dScene_c16SpawnIfNecessaryEv', 'func_02044120', 'func_02019404')


class Boundary(Exception):
    pass


def elf_functions(elf):
    rows = subprocess.check_output([shared.TOOL+'nm', '-S', '--defined-only', str(elf)], text=True)
    functions = {}
    for line in rows.splitlines():
        columns = line.split()
        if len(columns)==3 and columns[1] in 'TtWw':
            columns=[columns[0],'0',columns[1],columns[2]]
        if len(columns) == 4 and columns[2] in 'TtWw':
            address, size = int(columns[0], 16), int(columns[1], 16)
            functions.setdefault(address, []).append((columns[3], size))
    return functions


def preflight(elf, functions):
    text = subprocess.check_output([shared.TOOL+'objdump', '-d', str(elf)], text=True)
    direct = {}; owner = None
    for line in text.splitlines():
        header = re.match(r'^([0-9a-fA-F]+) <[^>]+>:', line)
        if header:
            owner = int(header[1],16); direct.setdefault(owner, [])
        elif owner is not None:
            branch = re.search(r'\b(?:blx?|b(?:\.w)?)\s+([0-9a-fA-F]+)\s*<', line)
            if branch:
                direct[owner].append(int(branch[1],16))
    names = shared.base.symbols(elf)
    queue = deque(names[n] for n in ORDER); seen = set(); result = []
    while queue:
        address = queue.popleft()
        if address in seen or address not in functions:
            continue
        seen.add(address)
        targets = sorted(set(t for t in direct.get(address,[]) if t in functions and t != address))
        result.append(dict(address=hex(address), aliases=[n for n,_ in functions[address]],
            size=functions[address][0][1],
            direct_calls=[dict(address=hex(t), names=[n for n,_ in functions[t]]) for t in targets]))
        # These are fail boundaries, not successful paths through debug I/O.
        if not any(n in ('sm64ds_native_missing', '_ZN6sm64ds20native_platform_failEPKc')
                   for n,_ in functions[address]):
            queue.extend(targets)
    return dict(roots=list(ORDER), direct_functions=len(result), functions=result,
        evidence='Exact acquisition ELF; no current manifest or current generated body used.',
        indirect_callbacks_require_runtime_observation=True)


class StageReplay:
    def __init__(self, snapshot, elf, functions, rom_fs=False, pack_read=False):
        self.snapshot = snapshot
        self.p = shared.Probe('exact pre-Spawn RAM clone', elf=elf)
        assert all(not (s['address'] < shared.STACK+0x20000 and
                        shared.STACK < s['address']+s['size']) for s in snapshot.spans)
        snapshot.install(self.p.cpu)
        self.tls_restoration = snapshot.restore_tls(self.p.cpu)
        assert self.tls_restoration['restored']
        self.view = route.CpuView(snapshot,self.p.cpu)
        self.functions = functions
        self.entry_names = {address:[n for n,_ in aliases] for address,aliases in functions.items()}
        self.entries = Counter(); self.last_entries = deque(maxlen=64)
        self.blocks = Counter(); self.last_blocks = deque(maxlen=64)
        self.instructions = 0; self.component = None
        self.events = []; self.file_observations = []
        self.counter = snapshot.metadata['tick']
        self.mount_calls=[];self.registry_writes=[];self.registry_lookups=[]
        self.watched_nodes=set();self.lookup_seen=set();self.current_lookup=None
        self.registry_head=self.p.names['data_020a8048']
        self.cart = shared.base.Cartridge(ROOT/'build/europe.pak')
        self.fs = route.RomFileObserver(self) if rom_fs else None
        self.pack_read=pack_read
        self.pack_binding=None
        if pack_read:
            service=self.view.u32(self.p.names['_ZN6sm64ds12_GLOBAL__N_1L12native_filesE'])
            pack=self.view.u32(service);file=self.view.u32(pack)
            fd=self.view.u16(file+14)
            # Literal from the exact __get_handle body in this acquisition.
            table=self.p.u32(self.p.names['__get_handle']+0x54)
            descriptor=self.view.u32(table+fd*4)
            cookie=descriptor+12
            handle=self.view.u32(cookie)
            assert file==0x08088510 and fd==3 and handle==0x90022
            self.pack_binding=dict(service=hex(service),pack=hex(pack),FILE=hex(file),
                fd=fd,descriptor_table=hex(table),descriptor=hex(descriptor),
                cookie=hex(cookie),handle=hex(handle),
                handle_and_FILE_already_captured=True,handle_not_created=True,
                source_sha256=shared.base.sha(self.cart.raw))
        self.hooks = [self.p.cpu.hook_add(UC_HOOK_CODE,self.observe),
                      self.p.cpu.hook_add(UC_HOOK_BLOCK,self.block),
                      self.p.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write)]

    def symbol(self, pc):
        owners = [(a,rows) for a,rows in self.functions.items()
                  if a <= pc < a+max(size for _,size in rows)]
        if not owners:
            # libctru assembly SVC functions have no ELF size. Decode their
            # containing entry using the next exact text-symbol address.
            starts=sorted(self.functions)
            candidates=[a for i,a in enumerate(starts) if a<=pc and
                (i+1==len(starts) or pc<starts[i+1]) and pc-a<0x1000]
            if candidates:
                address=max(candidates)
                return dict(pc=hex(pc),entry=hex(address),offset=hex(pc-address),
                    names=[n for n,_ in self.functions[address]],bounds_inferred_from_next_text_symbol=True)
            return dict(pc=hex(pc),names=[])
        address, rows = max(owners)
        return dict(pc=hex(pc),entry=hex(address),offset=hex(pc-address),names=[n for n,_ in rows])

    def cstring(self, pointer):
        try:
            return bytes(self.p.cpu.mem_read(pointer,256)).split(b'\0')[0].decode(errors='replace')
        except Exception:
            return '<unreadable diagnostic>'

    def observe(self,cpu,address,size,_):
        self.instructions += 1
        aliases = self.entry_names.get(address, [])
        if aliases:
            for name in aliases:
                self.entries[name] += 1
            args = [cpu.reg_read(r) for r in shared.REGS]
            self.last_entries.append(dict(instruction=self.instructions, component=self.component,
                PC=hex(address), names=aliases, GP=list(map(hex,args)),
                SP=hex(cpu.reg_read(UC_ARM_REG_SP)), LR=hex(cpu.reg_read(UC_ARM_REG_LR))))
            missing = [n for n in aliases if n in ('sm64ds_native_missing',
                '_ZN6sm64ds20native_platform_failEPKc')]
            if missing:
                self.events.append(dict(boundary=missing[0], diagnostic=self.cstring(args[0]),
                    instruction=self.instructions,component=self.component,PC=hex(address),GP=list(map(hex,args))))
                raise Boundary(missing[0])
            for name in aliases:
                if name=='func_0205d568' or name.startswith('_ZN6sm64ds9AssetPack4readE'):
                    route.FullRoute.inspect_file_boundary(self,name,args)
            if 'func_0204ee40' in aliases:
                self.watched_nodes.add(args[0])
                self.mount_calls.append(dict(instruction=self.instructions,record=hex(args[0]),
                    name=self.cstring(args[1]),payload=hex(args[2]),
                    registry_head_before=hex(self.p.u32(self.registry_head)),
                    head_equals_record=self.p.u32(self.registry_head)==args[0]))
            if 'func_0205d23c' in aliases:
                self.lookup_seen=set()
                self.current_lookup=dict(instruction=self.instructions,
                    requested_name=self.cstring(args[0])[:args[1]],length=args[1],nodes=[])
                self.registry_lookups.append(self.current_lookup)
            if self.pack_read and 'FSFILE_Read' in aliases:
                self.read_captured_pack(args)
                return
            if self.fs:
                fs_names = sorted(set(aliases)&self.fs.NAMES)
                if fs_names:
                    value=self.fs.invoke(fs_names[0],args)
                    cpu.reg_write(shared.REGS[0],value & 0xffffffff)
                    cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
                    return
        if address==self.p.names['func_0205d23c']+0x18:
            node=cpu.reg_read(UC_ARM_REG_R4);key=cpu.reg_read(UC_ARM_REG_R5)
            if node:
                self.current_lookup['key']=hex(key)
                item=dict(node=hex(node),key=hex(self.p.u32(node)),next=hex(self.p.u32(node+4)),
                    instruction=self.instructions)
                self.current_lookup['nodes'].append(item)
                if node in self.lookup_seen:
                    self.current_lookup['repeated_node']=hex(node)
                    raise Boundary('archive registry lookup repeated a node with unmatched key')
                self.lookup_seen.add(node)
        opcode = bytes(cpu.mem_read(address,size))
        arm_svc = size == 4 and not (cpu.reg_read(UC_ARM_REG_CPSR)&0x20) and (struct.unpack('<I',opcode)[0] & 0x0f000000) == 0x0f000000
        thumb_svc = size == 2 and (struct.unpack('<H',opcode)[0] & 0xff00) == 0xdf00
        if arm_svc or thumb_svc:
            immediate = int.from_bytes(opcode,'little') & (0xffffff if arm_svc else 0xff)
            self.events.append(dict(boundary='SVC',number=hex(immediate),
                instruction=self.instructions,component=self.component,**self.symbol(address),
                GP=[hex(cpu.reg_read(r)) for r in shared.REGS],
                LR=hex(cpu.reg_read(UC_ARM_REG_LR)),SP=hex(cpu.reg_read(UC_ARM_REG_SP))))
            raise Boundary('real OS SVC '+hex(immediate))

    def readable_destination(self,address,size):
        return any(route.Snapshot.contains(s,address,size) for s in self.snapshot.spans) or (
            shared.STACK<=address and address+size<=shared.STACK+0x20000)

    def read_captured_pack(self,args):
        cpu=self.p.cpu;handle,read_count,lo,hi=args
        sp=cpu.reg_read(UC_ARM_REG_SP)
        destination,count=self.p.u32(sp),self.p.u32(sp+4)
        expected=int(self.pack_binding['handle'],16)
        if handle!=expected:
            raise Boundary('FSFILE_Read requested an uncaptured/unbound handle')
        offset=(hi<<32)|lo
        assert offset<=len(self.cart.raw),'pack read offset beyond immutable file'
        amount=min(count,len(self.cart.raw)-offset)
        assert self.readable_destination(destination,amount),'pack read destination not captured/scratch'
        assert not read_count or self.readable_destination(read_count,4),'pack read count pointer'
        payload=self.cart.raw[offset:offset+amount]
        if amount:cpu.mem_write(destination,payload)
        if read_count:self.p.words(read_count,amount)
        self.events.append(dict(boundary='declared FSFILE_Read backend',
            handle=hex(handle),offset=offset,requested=count,bytes_read=amount,
            destination=hex(destination),count_destination=hex(read_count),
            payload_sha256=shared.base.sha(payload),result=0,
            original_private_pack_bytes=True,handle_from_captured_binding=True,
            instruction=self.instructions,component=self.component))
        cpu.reg_write(shared.REGS[0],0)
        cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))

    def block(self,cpu,address,size,_):
        self.blocks[address] += 1
        self.last_blocks.append(dict(pc=hex(address),bytes=size))

    def write(self,cpu,access,address,size,value,_):
        owner=next((n for n in self.watched_nodes if n<=address<n+12),None)
        if owner is None and not(self.registry_head<=address<self.registry_head+16):
            return
        self.registry_writes.append(dict(instruction=self.instructions,
            **self.symbol(cpu.reg_read(UC_ARM_REG_PC)),address=hex(address),bytes=size,
            before=hex(int.from_bytes(cpu.mem_read(address,size),'little')),
            value=hex(value & ((1<<(size*8))-1)),node=hex(owner) if owner is not None else 'head/tail'))

    def scene_state(self):
        output=route.FullRoute.scene_state(self)
        output['dispatch_phase']=self.view.read(self.p.names['data_0209d50c'],1)[0]
        return output

    def call(self,name,budget):
        cpu=self.p.cpu; stack=shared.STACK; sp=stack+0xf000
        self.component=name
        cpu.reg_write(UC_ARM_REG_CPSR,0x10)
        cpu.mem_write(stack,bytes([0x6d])*0x10000)
        for reg in shared.REGS:
            cpu.reg_write(reg,0)
        cpu.reg_write(UC_ARM_REG_SP,sp);cpu.reg_write(UC_ARM_REG_LR,shared.STOP)
        for i,reg in enumerate(shared.base.SAVED):cpu.reg_write(reg,0x12340000+i)
        incoming=bytes(cpu.mem_read(sp,64));initial_instructions=self.instructions
        error=None
        try:
            cpu.emu_start(self.p.names[name],shared.STOP,count=budget)
            if cpu.reg_read(UC_ARM_REG_PC)!=shared.STOP:
                raise Boundary('bounded instruction limit; no normal return')
            assert cpu.reg_read(UC_ARM_REG_SP)==sp,'callee SP'
            assert all(cpu.reg_read(reg)==0x12340000+i for i,reg in enumerate(shared.base.SAVED)),'callee-saved ABI'
            assert bytes(cpu.mem_read(sp,64))==incoming,'caller stack canary'
            assert bytes(cpu.mem_read(stack,32))==bytes([0x6d])*32,'lower stack canary'
        except Exception as caught:
            error=dict(type=type(caught).__name__,message=str(caught),
                **self.symbol(cpu.reg_read(UC_ARM_REG_PC)))
        return dict(component=name,instructions=self.instructions-initial_instructions,
            result=hex(cpu.reg_read(shared.REGS[0])) if not error else None,
            normal_return=error is None, failure=error,
            tls_register=hex(cpu.reg_read(UC_ARM_REG_C13_C0_3)),
            state_after=self.scene_state())

    def run(self,budget):
        before=self.scene_state()
        assert before['pending_scene']==3 and before['already_spawned']==0
        assert before['dispatch_phase']==3 and before['title_context']=='0x0'
        components=[]
        for phase,name in enumerate(ORDER,3):
            if phase!=3:
                # These are the literal diagnostic stores of NativeBoot's
                # known dispatcher, not pending/spawn/object state forcing.
                self.p.cpu.mem_write(self.p.names['data_0209d50c'],bytes([phase]))
            result=self.call(name,budget);components.append(result)
            print(json.dumps(dict(component=name,instructions=result['instructions'],
                normal_return=result['normal_return'],failure=result['failure'])),flush=True)
            if not result['normal_return']:
                break
        after=self.scene_state()
        top=[dict(count=count,**self.symbol(pc)) for pc,count in self.blocks.most_common(24)]
        span_changes=[]
        for span in self.snapshot.spans:
            original=self.snapshot.read(span['address'],span['size'])
            current=bytes(self.p.cpu.mem_read(span['address'],span['size']))
            span_changes.append(dict(name=span['name'],address=hex(span['address']),bytes=span['size'],
                before_sha256=shared.base.sha(original),after_sha256=shared.base.sha(current),
                unchanged=original==current))
        return dict(status='normal_component_returns' if components[-1]['normal_return'] else 'component_boundary',
            before=before,after=after,components=components,instructions=self.instructions,
            executed_function_counts=dict(sorted(self.entries.items())),last_entries=list(self.last_entries),
            top_basic_blocks=top,last_basic_blocks=list(self.last_blocks),events=self.events,
            memory_faults=self.p.faults,tls_restoration=self.tls_restoration,
            final_TLS_register=hex(self.p.cpu.reg_read(UC_ARM_REG_C13_C0_3)),
            span_changes=span_changes,raw_FAT_observer=bool(self.fs),
            raw_FAT_events=self.fs.events if self.fs else [],
            captured_pack_read_backend=self.pack_binding,
            file_boundary_observations=self.file_observations,
            original_mount_calls=self.mount_calls,archive_registry_writes=self.registry_writes,
            original_archive_lookup_traversals=self.registry_lookups,
            final_CPU=dict(R0_R3=[hex(self.p.cpu.reg_read(r)) for r in shared.REGS],
                R4=hex(self.p.cpu.reg_read(UC_ARM_REG_R4)),R5=hex(self.p.cpu.reg_read(UC_ARM_REG_R5)),
                SP=hex(self.p.cpu.reg_read(UC_ARM_REG_SP)),LR=hex(self.p.cpu.reg_read(UC_ARM_REG_LR)),
                CPSR=hex(self.p.cpu.reg_read(UC_ARM_REG_CPSR)),PC=hex(self.p.cpu.reg_read(UC_ARM_REG_PC))),
            no_scene_pending_spawn_result_actor_or_collision_forced=True,
            live_stall_PC_captured=False,live_stall_cause_proven=False,
            scope='Copied exact acquisition RAM/ELF and captured TLS; synthetic disjoint component-call stack. '
                'No live CPU GP/SP/CPSR resume, services, frame completion or physical evidence.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom-fs',action='store_true')
    parser.add_argument('--pack-read',action='store_true',help='Declared FSFILE_Read backend bound only to the already captured pack FILE/handle')
    parser.add_argument('--budget',type=int,default=2000000)
    parser.add_argument('--label',help='Distinct private report name; existing evidence is never overwritten')
    args=parser.parse_args()
    assert 1<=args.budget<=64000000
    elf=ATTEMPT/'sm64ds-native.elf';binary=ATTEMPT/'ram-stage-requested.ram.bin'
    index=ATTEMPT/'ram-stage-requested.ram.json'
    elf_hash=shared.base.sha(elf.read_bytes());assert elf_hash==ELF_HASH
    snapshot=route.Snapshot(binary,index)
    before_hashes={str(p):shared.base.sha(p.read_bytes()) for p in (elf,binary,index)}
    snapshot_summary=snapshot.summary()
    acquisition=json.loads((ROOT/'build/native-integration-check/acquisition-443444.json').read_text())
    assert acquisition['status']=='passed' and acquisition['elf_sha256']==elf_hash
    functions=elf_functions(elf);audit=preflight(elf,functions)
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'preflight.json').write_text(json.dumps(audit,indent=2)+'\n')
    assert not(args.rom_fs and args.pack_read),'Choose one explicitly distinct file backend'
    variant='captured-pack-read' if args.pack_read else ('raw-FAT' if args.rom_fs else 'production-FAT')
    report=dict(variant=variant,snapshot=snapshot_summary,elf=str(elf),elf_sha256=elf_hash,
        input_sha256=before_hashes,preflight_file='preflight.json',budget_per_component=args.budget,
        production_sources_not_recompiled=True,current_manifest_not_used=True,
        binary_and_snapshot_from_same_acquisition=True)
    runner=StageReplay(snapshot,elf,functions,args.rom_fs,args.pack_read)
    report['replay']=runner.run(args.budget)
    report['source_sha256']={str(p.relative_to(ROOT)):shared.base.sha(p.read_bytes()) for p in
        (Path(__file__),ROOT/'tools/test_native_title_fade_arm.py',
         ROOT/'tools/test_native_title_ram_arm.py',ROOT/'tools/test_native_audio_deferred_arm.py')}
    assert all(shared.base.sha(Path(p).read_bytes())==h for p,h in before_hashes.items())
    destination=OUT/((args.label or variant)+'-replay.json')
    assert not destination.exists(),'Choose a distinct --label to preserve prior evidence'
    frozen=OUT/(destination.stem+'-tools');frozen.mkdir()
    for name in report['source_sha256']:
        source=ROOT/name;(frozen/source.name).write_bytes(source.read_bytes())
    destination.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status=report['replay']['status'],variant=variant,report=str(destination))))


if __name__=='__main__':
    main()
