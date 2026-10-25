#!/usr/bin/env python3
"""Compare complete 1-Up/Mega Mushroom construction and destruction with ASMP0.

The immutable native ELF publishes the actual cartridge data. The ROM runs its
original ARM9/ov002 instructions with the declared, zeroed ARM9/overlay BSS.
Only bounded heap allocation/deallocation and separate spawn/stack inputs are
fixtures. Both full 920-byte constructed actors, intrusive lists, virtual tables
and failure returns are checked. After complete destruction, four compiler-dead
vptr words are checked separately; all other 904 bytes are compared. No old RAM,
file I/O, resource initialization, game frame,
GPU output or physical-console performance is claimed. ROM bytes stay in RAM.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from test_native_menu_callbacks import Machine, Cartridge, verify_publication, TOOL
from test_native_audio_deferred_arm import Probe, WORK, STACK
from test_native_overlay_static_init_arm import original_names
from test_native_shadow_models_arm import call, put, words
from analyze_native_acquisition import elf_bytes
from native_local_externs import adapt_da1up
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_PC, UC_ARM_REG_LR

ROOT = Path(__file__).resolve().parents[1]
PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
OBJECT = WORK+0x4020
SIZE = 0x398
FACTORIES = [('da1up_c_classInit_ONEUPKINOKO', 0x114),
             ('da1up_c_classInit_SCALEUP_KINOKO', 0x115)]
DTORS = ['_ZN7da1up_cD1Ev', '_ZN7da1up_cD0Ev']

def sha(raw): return hashlib.sha256(raw).hexdigest()

class Observer:
    def __init__(self, probe, native, sections, actor_bytes=SIZE, modules=(0xffffffff,2)):
        self.p, self.native, self.sections = probe, native, sections
        self.actor_bytes = actor_bytes
        self.modules = modules
        self.active = False
        self.instructions = 0
        probe.cpu.hook_add(UC_HOOK_CODE, self.code)
        probe.cpu.hook_add(UC_HOOK_MEM_WRITE, self.write)

    def reset(self, fill, fail):
        self.fill, self.fail = fill, fail
        self.allocations, self.frees, self.dirty, self.entries = [], [], set(), set()
        self.p.cpu.mem_write(WORK, bytes([0x6b])*0x10000)
        self.p.cpu.mem_write(OBJECT, bytes([fill])*self.actor_bytes)

    def code(self, cpu, address, size, _):
        if not self.active: return
        self.instructions += 1
        names = self.p.names
        assert address not in (names.get('sm64ds_native_missing'), names['Crash']), ('fatal boundary', self.p.label, hex(address))
        for name in ('_ZN12dEnemyBase_cC2Ev','_ZN8dActor_cC2Ev',
                     '_ZN7fBase_cC2Ev','_ZN5ModelC1Ev','_ZN11ShadowModelC1Ev',
                     '_ZN7dCcAc_cC1Ev','_ZN10dBgCh_ActrC1Ev'):
            if address == names.get(name): self.entries.add(name)
        if address == names['_ZN6Memory8AllocateEjiP4Heap']:
            requested = cpu.reg_read(UC_ARM_REG_R0)
            assert requested == self.actor_bytes
            assert cpu.reg_read(UC_ARM_REG_R1) == 0xfffffffc
            assert cpu.reg_read(UC_ARM_REG_R2) == 0
            assert not self.allocations
            result = 0 if self.fail else OBJECT
            self.allocations.append((requested, result))
            cpu.reg_write(UC_ARM_REG_R0, result)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif address == names['_ZN6Memory10DeallocateEPvP4Heap']:
            assert cpu.reg_read(UC_ARM_REG_R1) == 0
            self.frees.append(cpu.reg_read(UC_ARM_REG_R0))
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def write(self, cpu, access, address, size, value, _):
        if not self.active: return
        if OBJECT <= address and address+size <= OBJECT+self.actor_bytes: return
        if STACK+0xd000 <= address and address+size <= STACK+0xf000: return
        for target, module, source, length, zero in self.sections:
            start = target if self.native else source
            if module in self.modules and start <= address and address+size <= start+length:
                for offset in range(address-start, address-start+size):
                    self.dirty.add((module, source+offset))
                return
        raise AssertionError(('write outside actor/stack/original globals', self.p.label, hex(address), size))

    def run(self, name, arguments=()):
        self.active = True
        try: return call(self.p, name, arguments)
        finally: self.active = False

    def canaries(self):
        cpu = self.p.cpu
        assert bytes(cpu.mem_read(OBJECT-32,32)) == bytes([0x6b])*32
        assert bytes(cpu.mem_read(OBJECT+self.actor_bytes,32)) == bytes([0x6b])*32

def bridge_audit():
    source = ROOT/'upstream/src/actors/da1up_c.cpp'
    _, views = adapt_da1up(source.read_text())
    assert len(views) == 91
    report = json.loads((ROOT/'build/native-core/report.json').read_text())
    entry = next(e for e in report['entries'] if e['source'] == 'src/actors/da1up_c.cpp')
    assert entry['compiled'] and entry['sha256_original'] == sha(source.read_bytes())
    listing = subprocess.check_output([TOOL+'nm', '-g', ROOT/entry['object']], text=True)
    assert 'sm64ds_da1up_local_' not in listing
    manifest = json.loads((ROOT/'upstream/config/tu_manifest.d/ov002/da1up_c.json').read_text())
    definitions = {line.split()[-1] for line in listing.splitlines() if len(line.split()) == 3}
    symbols = [row['symbol'] for row in manifest['functions']]
    assert len(symbols) == 38 and set(symbols) <= definitions
    assert 'retain-original-null-check-before-mushroom-construction' in entry['adaptations']
    return {'local_declaration_views':len(views),'original_function_definitions':len(symbols),
        'body_bridge_inverts_byte_for_byte':True,'assembler_aliases_keep_original_symbols':True,
        'source_sha256':sha(source.read_bytes()), 'object_sha256':sha((ROOT/entry['object']).read_bytes())}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    assert subprocess.check_output(['git','-C',ROOT/'upstream','rev-parse','HEAD'],text=True).strip() == PIN
    bridge = bridge_audit()
    cart = Cartridge(args.pack)
    native = Machine(args.elf, cart); native.label = 'immutable production ARM11'
    verify_publication(native)
    original = Probe('original ASMP0 ROM',cart=cart); original.names = original_names(2)
    match = re.search(r'    \.bss\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)',
        (ROOT/'upstream/config/arm9/delinks.txt').read_text())
    begin,end = [int(s,16) for s in match.groups()]
    original.cpu.mem_write(begin,bytes(end-begin))
    address,bss,image = cart.image(2)
    original.cpu.mem_write(address,image); original.cpu.mem_write(address+len(image),bytes(bss))
    raw = args.elf.read_bytes()
    start,end = [native.names[k] for k in ('sm64ds_runtime_sections','sm64ds_runtime_sections_end')]
    table = elf_bytes(raw,start,end-start)
    sections = [struct.unpack_from('<5I',table,i) for i in range(0,len(table),20)]
    # Select precisely ov002's metadata destinations; initialized/loaded flags
    # and constructors are not invented by the fixture.
    for at in range(native.names['sm64ds_runtime_choices'],native.names['sm64ds_runtime_choices_end'],20):
        section,offset,expected,first,count = words(native.cpu,at,5)
        for n in range(count):
            module,choice = words(native.cpu,native.names['sm64ds_runtime_candidates']+(first+n)*8,2)
            if module == 2: put(native.cpu,sections[section][0]+offset,choice)
    saved = []
    for p,is_native in ((original,False),(native,True)):
        saved.append([(target if is_native else source,bytes(p.cpu.mem_read(target if is_native else source,size)))
            for target,module,source,size,zero in sections if module in (0xffffffff,2)])
    vtables = {}
    for name,value in original.names.items():
        if name.startswith('_ZTV') and name in native.names:
            destination = native.names[name]
            typeinfo = native.names.get('_ZTI'+name[4:])
            if typeinfo and words(native.cpu,destination,2) == [0,typeinfo]: destination += 8
            vtables[value] = destination
    assert vtables[original.names['_ZTV7da1up_c']] == native.names['_ZTV7da1up_c']+8
    declared = json.loads((ROOT/'upstream/config/tu_manifest.d/ov002/da1up_c.json').read_text())
    assert all(row['symbol'] in native.names for row in declared['functions'])
    function_targets = {address:native.names[name] for name,address in original.names.items()
        if name in native.names and (name.startswith('_ZN') or name.startswith('func_'))}
    original_slots = words(original.cpu,original.names['_ZTV7da1up_c'],31)
    assert [function_targets[x] for x in original_slots] == words(native.cpu,native.names['_ZTV7da1up_c']+8,31)
    # CW names the secondary tables separately; GCC places them in the full
    # derived vtable. Verify each offset-to-top, RTTI and destructor pair before
    # translating these three genuine subobject address points.
    for original_table, derived, displacement, prefix in [
            ('VTable_dBgPi_dBgCh_SphCrrThunk','12dBgCh_SphCrr',16,16),
            ('VTable_dM3dGSph_dBgCh_SphCrrThunk','12dBgCh_SphCrr',56,32),
            ('VTable_dBgPi_dBgCh_LinThunk','9dBgCh_Lin',16,16)]:
        destination = native.names['_ZTV'+derived]+prefix
        assert words(native.cpu,destination,4) == [(-displacement)&0xffffffff,
            native.names['_ZTI'+derived],native.names[f'_ZThn{displacement}_N{derived}D1Ev'],
            native.names[f'_ZThn{displacement}_N{derived}D0Ev']]
        vtables[original.names[original_table]] = destination+8
    def normal(value):
        if value in vtables: return vtables[value]
        for target,module,source,size,zero in sections:
            if module in (0xffffffff,2) and source <= value < source+size: return target+value-source
        return value
    def payload(p,is_native):
        result = bytearray(p.cpu.mem_read(OBJECT,SIZE))
        if not is_native:
            for at in range(0,SIZE,4): struct.pack_into('<I',result,at,normal(struct.unpack_from('<I',result,at)[0]))
        return bytes(result)
    def destroyed_payload(p,is_native):
        result = bytearray(payload(p,is_native))
        # These empty base destructors have no observable action. GCC omits
        # their last vptr stores after lifetime ends; MWCC retained them. Check
        # precisely the four terminal values, then compare the other 904 bytes.
        for offset,original_vtable,native_vtable in [
                (0x144,'_ZTV5dBgCh',native.names['_ZTV10dBgCh_Actr']+8),
                (0x164,'_ZTV5dBgCh',native.names['_ZTV12dBgCh_SphCrr']+8),
                (0x19c,'_ZTV8dM3dGSph',native.names['_ZTV12dBgCh_SphCrr']+40),
                (0x278,'_ZTV5dBgCh',native.names['_ZTV9dBgCh_Lin']+8)]:
            expected = native_vtable if is_native else vtables[original.names[original_vtable]]
            assert struct.unpack_from('<I',result,offset)[0] == expected, ('unexpected terminal vptr',p.label,hex(offset))
            struct.pack_into('<I',result,offset,0)
        return bytes(result)
    observers = [Observer(original,False,sections),Observer(native,True,sections)]
    def global_words(obs):
        result = {}
        for module,address in sorted({(m,a & ~3) for m,a in obs.dirty}):
            location = address
            if obs.native:
                location = next(target+address-source for target,m,source,size,zero in sections
                    if m == module and source <= address < source+size)
            value = words(obs.p.cpu,location,1)[0]
            result[(module,address)] = value if obs.native else normal(value)
        return result
    cases = []
    for factory,actor in FACTORIES:
        for fill,mode,fail,dtor in [(0,0,False,DTORS[0]),(0xa5,0,False,DTORS[1]),
                (0xff,2,False,DTORS[0]),(0xa5,0,True,None)]:
            results = []
            for i,obs in enumerate(observers):
                p,cpu = obs.p,obs.p.cpu
                for address,data in saved[i]: cpu.mem_write(address,data)
                obs.reset(fill,fail)
                put(cpu,p.names['data_020a4bb8'],p.names['ACTOR_SPAWN_TABLE'])
                put(cpu,p.names['data_020a0eac'],0)
                put(cpu,p.names['data_02099e70'],0x51)
                put(cpu,WORK+0x2000,0x12000,0xfffde000,0x34000)
                cpu.mem_write(WORK+0x2020,struct.pack('<3h',123,-456,789))
                put(cpu,p.names['data_0209b460'],WORK+0x2000)
                put(cpu,p.names['data_0209b45c'],WORK+0x2020)
                cpu.mem_write(p.names['data_0209f2d8'],bytes([mode]))
                call(p,'func_02043180',[actor,0,0x71,1])
                result = obs.run(factory)
                assert result == (0 if fail else OBJECT)
                constructed = payload(p,i == 1)
                constructor_globals = global_words(obs)
                if fail:
                    assert constructed == bytes([fill])*SIZE and not obs.entries and not obs.dirty
                else:
                    assert words(cpu,OBJECT+4,2) == [0x51,0x71]
                    assert bytes(cpu.mem_read(OBJECT+12,2)) == struct.pack('<H',actor)
                    assert words(cpu,p.names['data_02099e70'],1) == [0x52]
                    obs.run(dtor,[OBJECT])
                obs.canaries()
                assert obs.frees == ([OBJECT] if dtor == DTORS[1] else [])
                results.append((constructed,payload(p,i == 1) if fail else destroyed_payload(p,i == 1),
                    set(obs.dirty),constructor_globals,global_words(obs)))
            for phase in (0,1):
                assert results[0][phase] == results[1][phase], (factory,fill,mode,fail,'actor byte mismatch',phase,
                    [(hex(i),hex(a),hex(b)) for i,(a,b) in enumerate(zip(results[0][phase],results[1][phase])) if a!=b][:30])
            dirty = set().union(results[0][2],results[1][2])
            slots = sorted({(module,address & ~3) for module,address in dirty})
            for phase in (3,4):
                assert results[0][phase] == results[1][phase], (factory,'global/list mismatch',phase,results[0][phase],results[1][phase])
            cases.append({'factory':factory,'actor_id':actor,'poison_byte':fill,'game_mode_fixture':mode,
                'allocation_failed':fail,'null_return_without_construction':fail,'destructor':dtor,
                'complete_constructed_actor_equal':True,'destroyed_payload_equal':True,
                'constructed_bytes_compared':SIZE,'destroyed_payload_bytes_compared':SIZE if fail else SIZE-16,
                'terminal_dead_vptrs_separately_validated':not fail,'constructor_sha256':sha(results[0][0]),
                'modified_global_words_compared':len(slots),'status':'PASS'})
    report = {'status':'PASS','scope':__doc__,'ELF_sha256':sha(raw),'upstream_pin':PIN,
        'compiler_bridge':bridge,'cases':cases,'ROM_instructions':observers[0].instructions,
        'native_instructions':observers[1].instructions,'bounded_writes':True,
        'allocation_canaries':True,'callee_saved_and_stack_ABI':True,
        'original_31_slot_mushroom_vtable_dispatch_equal':True,
        'original_vptr_to_verified_native_address_point':True,'old_RAM_imported':False,
        'production_recompiled_for_gate':False,'resource_initialization_executed':False,
        'game_frames_executed':False,'physical_hardware':False}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':'PASS','cases':len(cases),'complete_actor_bytes':SIZE,'report':str(args.report)}))

if __name__ == '__main__': main()
