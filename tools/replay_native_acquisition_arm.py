#!/usr/bin/env python3
"""Continue an exact captured pre-Spawn checkpoint with declared host boundaries.

The pack backend is bound to the captured FILE/handle and immutable bytes.
Optional libc copies are checked against this ELF's ARM instructions first;
game parsing, decompression, allocations, constructors and dispatch run unchanged.
Uses an explicit disjoint call stack, not a live GP/PC resume. No Azahar launch,
replacement actor state, RAM import into a different ELF, frame or hardware proof.
"""
import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path

import replay_native_stage_requested_arm as original
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC

shared = original.shared

def sha(raw): return hashlib.sha256(raw).hexdigest()

def libc_gate(elf):
    """Check byte results, return value, canaries and ABI of real ARM libc."""
    probe = shared.Probe('archived production libc ARM contract',elf=elf)
    source,destination = shared.WORK+0x1000,shared.WORK+0x6000
    cases = []
    for operation in ('memcpy','memmove','memset'):
        for length in (0,1,3,16,63,1024,8192):
            for shift in (0,1):
                payload = bytes((i*37+11)&255 for i in range(length))
                probe.cpu.mem_write(source,payload or b'\0')
                probe.cpu.mem_write(destination-32,b'\xa5'*(length+65))
                second = 0x1ff if operation == 'memset' else source
                value = probe.call(operation,[destination+shift,second,length])
                expected = b'\xa5'*(32+shift)+(b'\xff'*length if operation == 'memset' else payload)+b'\xa5'*(33-shift)
                assert bytes(probe.cpu.mem_read(destination-32,length+65)) == expected
                assert value == destination+shift
                cases.append({'operation':operation,'bytes':length,'unaligned_destination':bool(shift),'status':'PASS'})
    for delta in (-7,7):
        payload = bytes((i*17+29)&255 for i in range(512))
        probe.cpu.mem_write(source,payload)
        start = source+64;target = start+delta;length = 255
        expected = payload[:target-source]+payload[64:64+length]+payload[target-source+length:]
        assert probe.call('memmove',[target,start,length]) == target
        assert bytes(probe.cpu.mem_read(source,512)) == expected
        cases.append({'operation':'memmove','overlap_delta':delta,'bytes':length,'status':'PASS'})
    return {'status':'PASS','cases':cases,'ARM_library_executed':True,
        'byte_results_return_value_canaries_and_ABI':True,'game_executed':False}

class CopyReplay(original.StageReplay):
    def __init__(self,*args,libc_copies=False,**kwargs):
        super().__init__(*args,**kwargs)
        self.libc_copies = libc_copies
        self.copy_counts = Counter();self.copy_bytes = Counter();self.last_copies = deque(maxlen=16)
        self.particle_calls = deque(maxlen=32)
        self.copy_names = {self.p.names[n]:n for n in ('memcpy','memmove','memset')}

    def observe(self,cpu,address,size,userdata):
        super().observe(cpu,address,size,userdata)
        if address in (self.p.names['func_0204a17c'],self.p.names['func_02023178']):
            self.particle_calls.append({'function':self.entry_names[address],
                'instruction':self.instructions,'arguments':[hex(cpu.reg_read(r)) for r in shared.REGS],
                'arena_cursor':hex(self.p.u32(self.p.names['data_0209ee78'])),
                'arena_end':hex(self.p.u32(self.p.names['data_0209ee7c']))})
        if not self.libc_copies or address not in self.copy_names:return
        operation = self.copy_names[address]
        destination,source,count = [cpu.reg_read(r) for r in shared.REGS[:3]]
        if count:
            if count > 64*1024*1024 or not self.readable_destination(destination,count):
                raise original.Boundary('libc copy destination outside captured/scratch residency')
            if operation == 'memset':payload = bytes([source&255])*count
            else:
                if operation == 'memcpy' and source < destination+count and destination < source+count:
                    raise original.Boundary('Overlapping memcpy; no host result manufactured')
                payload = bytes(cpu.mem_read(source,count))
            cpu.mem_write(destination,payload)
        else:payload = b''
        self.copy_counts[operation] += 1;self.copy_bytes[operation] += count
        self.last_copies.append({'operation':operation,'destination':hex(destination),
            'source_or_fill':hex(source),'bytes':count,'payload_sha256':sha(payload),
            'instruction':self.instructions})
        cpu.reg_write(shared.REGS[0],destination)
        cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--budget',type=int,default=64000000)
    parser.add_argument('--libc-copies',action='store_true')
    args = parser.parse_args();assert 1 <= args.budget <= 64000000
    result = json.loads((args.attempt/'result.json').read_text())
    elf = args.attempt/'sm64ds-native.elf';binary = args.attempt/'ram-stage-requested.ram.bin'
    index = binary.with_suffix('.json')
    assert sha(elf.read_bytes()) == result['elf_sha256']
    for path in (binary,index):
        assert sha(path.read_bytes()) == result['ram_snapshot_files'][path.name]['sha256']
    gate = libc_gate(elf) if args.libc_copies else None
    print(json.dumps({'libc_contract':None if gate is None else gate['status'],
        'cases':0 if gate is None else len(gate['cases']),'Azahar_launched':False}),flush=True)
    case = CopyReplay(original.route.Snapshot(binary),elf,original.elf_functions(elf),
        pack_read=True,libc_copies=args.libc_copies)
    report = case.run(args.budget)
    report.update(scope=__doc__,ELF_sha256=result['elf_sha256'],capture_sha256=sha(binary.read_bytes()),
        immutable_acquisition=str(args.attempt),budget_per_component=args.budget,
        libc_contract=gate,libc_host_backend={'enabled':args.libc_copies,'calls':dict(case.copy_counts),
            'bytes':dict(case.copy_bytes),'last_operations':list(case.last_copies)},
        particle_call_observations=list(case.particle_calls),physical_hardware=False,Azahar_launched=False)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'instructions':report['instructions'],
        'terminal_events':[e for e in report['events'] if e['boundary']!='declared FSFILE_Read backend'],
        'final_CPU':report['final_CPU'],'libc_calls':dict(case.copy_counts),'report':str(args.report)}),flush=True)

if __name__ == '__main__':main()
