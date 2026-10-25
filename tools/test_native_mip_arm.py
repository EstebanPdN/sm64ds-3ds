#!/usr/bin/env python3
"""Compare MIPS construction, local data views and matrices against ASMP0.

Uses the immutable production ELF's genuine cartridge-data publication and ROM
instruction bodies. Fixtures declare bounded allocation/failure, spawn inputs,
borrowed original rabbit BCA bytes and an identity bone matrix. No resource I/O,
old RAM import, invented module activation, game/GPU frames or console FPS.
All 1140 constructed bytes are compared; five terminal dead vptrs are separately
verified and the other 1120 destroyed bytes are compared. Intrusive-list/global
effects are compared after each call, including release of seven empty handles.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

import ndspy.code
from test_native_menu_callbacks import Machine,Cartridge,verify_publication,TOOL
from test_native_audio_deferred_arm import Probe,WORK
from test_native_overlay_static_init_arm import original_names
from test_native_shadow_models_arm import call,put,words
from test_native_mushroom_arm import Observer,OBJECT
from analyze_native_acquisition import elf_bytes
from native_local_externs import adapt_mip_data
from unicorn.arm_const import UC_ARM_REG_PC

ROOT = Path(__file__).resolve().parents[1]
PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
SIZE, MODULE = 0x474,85
MODULES = (0xffffffff,2,MODULE)
FACTORY = 'daMip_c_classInit'
VTABLE = '_ZTV7daMip_c'
BCAS = [('_ZN7daMip_c13StateRestInitEv','data_ov085_021305b0','data/enemy/mip/mip_wait.bca'),
        ('_ZN7daMip_c15StateCaughtInitEv','data_ov085_021305b8','data/enemy/mip/mip_hold.bca'),
        ('_ZN7daMip_c17StateReleasedInitEv','data_ov085_021305c0','data/enemy/mip/mip_run.bca')]
IDENTITY = [4096,0,0,0,4096,0,0,0,4096,0,0,0]

def sha(raw): return hashlib.sha256(raw).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--pack',type=Path,default=ROOT/'build/europe.pak')
    parser.add_argument('--report',type=Path,required=True)
    args = parser.parse_args()
    assert subprocess.check_output(['git','-C',ROOT/'upstream','rev-parse','HEAD'],text=True).strip() == PIN
    source = ROOT/'upstream/src/actors/daMip_c.cpp'
    _,views = adapt_mip_data(source.read_text()); assert len(views) == 63
    entry = next(e for e in json.loads((ROOT/'build/native-core/report.json').read_text())['entries'] if e['source'] == 'src/actors/daMip_c.cpp')
    assert entry['compiled'] and not entry['diagnostics'] and entry['sha256_original'] == sha(source.read_bytes())
    listing = subprocess.check_output([TOOL+'nm','-g',ROOT/entry['object']],text=True)
    assert 'sm64ds_mip_' not in listing
    declared = json.loads((ROOT/'upstream/config/tu_manifest.d/ov085/daMip_c.json').read_text())['functions']
    definitions = {line.split()[-1] for line in listing.splitlines() if len(line.split()) == 3}
    assert len(declared) == 32 and {r['symbol'] for r in declared} <= definitions
    cart = Cartridge(args.pack)
    native = Machine(args.elf,cart); native.label = 'immutable production ARM11'; verify_publication(native)
    original = Probe('ASMP0 ROM MIPS',cart=cart)
    original.names = {**original_names(2),**original_names(MODULE)}
    match = re.search(r'    \.bss\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)',(ROOT/'upstream/config/arm9/delinks.txt').read_text())
    begin,end = [int(v,16) for v in match.groups()]
    original.cpu.mem_write(begin,bytes(end-begin))
    address,_,image = cart.image(0xffffffff)
    for section in ndspy.code.MainCodeFile(image,address).sections:
        if section.ramAddress < 0x02000000 and section.data:
            base = section.ramAddress & ~0xfff
            limit = (section.ramAddress+len(section.data)+0xfff)&~0xfff
            original.cpu.mem_map(base,limit-base)
            original.cpu.mem_write(section.ramAddress,bytes(section.data))
    # dEnemyBase's genuine constructor is in common ov002. MIPS ov085 does
    # not replace that resident code; the title-oriented Probe's default ov007
    # must be replaced by both real dependencies for this actor oracle.
    for module in (2,MODULE):
        address,bss,image = cart.image(module)
        original.cpu.mem_write(address,image);original.cpu.mem_write(address+len(image),bytes(bss))
    raw = args.elf.read_bytes()
    start,end = [native.names[k] for k in ('sm64ds_runtime_sections','sm64ds_runtime_sections_end')]
    table = elf_bytes(raw,start,end-start)
    sections = [struct.unpack_from('<5I',table,i) for i in range(0,len(table),20)]
    assert all(r['symbol'] in native.names for r in declared)
    for at in range(native.names['sm64ds_runtime_choices'],native.names['sm64ds_runtime_choices_end'],20):
        section,offset,expected,first,count = words(native.cpu,at,5)
        for n in range(count):
            module,choice = words(native.cpu,native.names['sm64ds_runtime_candidates']+(first+n)*8,2)
            if module == MODULE: put(native.cpu,sections[section][0]+offset,choice)
    saved = [[(target if is_native else src,bytes(p.cpu.mem_read(target if is_native else src,size)))
        for target,module,src,size,zero in sections if module in MODULES]
        for p,is_native in ((original,False),(native,True))]
    vtables = {}
    for name,address in original.names.items():
        if name.startswith('_ZTV') and name in native.names:
            destination = native.names[name]
            if words(native.cpu,destination,2) == [0,native.names.get('_ZTI'+name[4:])]: destination += 8
            vtables[address] = destination
    for table,derived,displacement,prefix in [
        ('VTable_dBgPi_dBgCh_SphCrrThunk','12dBgCh_SphCrr',16,16),
        ('VTable_dM3dGSph_dBgCh_SphCrrThunk','12dBgCh_SphCrr',56,32),
        ('VTable_dBgPi_dBgCh_LinThunk','9dBgCh_Lin',16,16),
        ('VTable_Animation_ModelAnimThunk','9ModelAnim',80,36)]:
        destination = native.names['_ZTV'+derived]+prefix
        assert words(native.cpu,destination,4) == [(-displacement)&0xffffffff,native.names['_ZTI'+derived],
            native.names[f'_ZThn{displacement}_N{derived}D1Ev'],native.names[f'_ZThn{displacement}_N{derived}D0Ev']]
        vtables[original.names[table]] = destination+8
    assert vtables[original.names[VTABLE]] == native.names[VTABLE]+8
    function_targets = {a:native.names[n] for n,a in original.names.items() if n in native.names and (n.startswith('_ZN') or n.startswith('func_'))}
    assert [function_targets[x] for x in words(original.cpu,original.names[VTABLE],31)] == words(native.cpu,native.names[VTABLE]+8,31)
    def normal(value):
        if value in vtables:return vtables[value]
        for target,module,src,size,zero in sections:
            if module in MODULES and src <= value < src+size: return target+value-src
        return value
    def payload(p,native_flag,dead=False):
        data = bytearray(p.cpu.mem_read(OBJECT,SIZE))
        if not native_flag:
            for offset in range(0,SIZE,4):struct.pack_into('<I',data,offset,normal(struct.unpack_from('<I',data,offset)[0]))
        if dead:
            for offset,base,derived in [(0x144,'_ZTV5dBgCh',native.names['_ZTV10dBgCh_Actr']+8),
                (0x164,'_ZTV5dBgCh',native.names['_ZTV12dBgCh_SphCrr']+8),
                (0x19c,'_ZTV8dM3dGSph',native.names['_ZTV12dBgCh_SphCrr']+40),
                (0x278,'_ZTV5dBgCh',native.names['_ZTV9dBgCh_Lin']+8),
                (0x350,'_ZTV9Animation',native.names['_ZTV9ModelAnim']+44)]:
                assert struct.unpack_from('<I',data,offset)[0] == (derived if native_flag else vtables[original.names[base]])
                struct.pack_into('<I',data,offset,0)
        return bytes(data)
    # Find the real restored profile rather than assigning a guessed actor ID.
    candidates = []
    count = (native.names['ACTOR_DEBUG_NAMES']-native.names['ACTOR_SPAWN_TABLE'])//4
    for actor in range(count):
        profile = words(native.cpu,native.names['ACTOR_SPAWN_TABLE']+actor*4,1)[0]
        if profile and words(native.cpu,profile,1)[0] == native.names[FACTORY]: candidates.append(actor)
    assert len(candidates) == 1; actor = candidates[0]
    observers = [Observer(original,False,sections,SIZE,MODULES),Observer(native,True,sections,SIZE,MODULES)]
    def globals_after(obs):
        values = {}
        for module,address in sorted({(m,a & ~3) for m,a in obs.dirty}):
            location = address
            if obs.native:
                location = next(t+address-s for t,m,s,z,b in sections if m == module and s <= address < s+z)
            value = words(obs.p.cpu,location,1)[0]
            values[(module,address)] = value if obs.native else normal(value)
        return values
    cases = []
    specs = [(0,'construct',False),(0xa5,'construct',False),(0xff,'construct',False),(0xa5,'construct',True)]
    specs += [(0xa5,item[0],False) for item in BCAS]
    specs += [(0,'matrix',False),(0xff,'matrix',False),(0xff,'cleanup-empty',False)]
    for fill,operation,fail in specs:
        outcomes = []
        for i,obs in enumerate(observers):
            p,cpu = obs.p,obs.p.cpu
            for location,data in saved[i]:cpu.mem_write(location,data)
            obs.reset(fill,fail)
            put(cpu,p.names['data_020a4bb8'],p.names['ACTOR_SPAWN_TABLE']);put(cpu,p.names['data_020a0eac'],0)
            put(cpu,p.names['data_02099e70'],0x51)
            put(cpu,WORK+0x2000,0x12000,0xfffde000,0x34000)
            cpu.mem_write(WORK+0x2020,struct.pack('<3h',123,-456,789))
            put(cpu,p.names['data_0209b460'],WORK+0x2000);put(cpu,p.names['data_0209b45c'],WORK+0x2020)
            call(p,'func_02043180',[actor,0,0x71,1])
            try: returned = obs.run(FACTORY)
            except Exception as error:
                raise AssertionError((p.label,FACTORY,'pc',hex(cpu.reg_read(UC_ARM_REG_PC)),
                    'faults',getattr(p,'faults',[]),'actor_id',actor)) from error
            assert returned == (0 if fail else OBJECT)
            stages = [payload(p,i == 1)]
            global_stages = [globals_after(obs)]
            if fail:
                assert stages[0] == bytes([fill])*SIZE and not obs.entries and not obs.dirty
            else:
                if operation not in ('construct','matrix','cleanup-empty'):
                    _,resource,path = next(item for item in BCAS if item[0] == operation)
                    bca = cart.file(path);assert len(bca) <= 0x3000
                    cpu.mem_write(WORK+0x6000,bca);put(cpu,p.names[resource]+4,WORK+0x6000)
                    assert obs.run(operation,[OBJECT]) == 1;stages.append(payload(p,i == 1));global_stages.append(globals_after(obs))
                    assert obs.run(operation,[OBJECT]) == 1;stages.append(payload(p,i == 1));global_stages.append(globals_after(obs))
                elif operation == 'matrix':
                    put(cpu,WORK+0x3000,*IDENTITY);put(cpu,OBJECT+0x314,WORK+0x3000)
                    put(cpu,OBJECT+0xb0,words(cpu,OBJECT+0xb0,1)[0]&~0x10)
                    if not fill: cpu.mem_write(OBJECT+0x8c,bytes(6))
                    obs.run('_ZN7daMip_c21UpdateMatrixAndShadowEv',[OBJECT]);stages.append(payload(p,i == 1))
                    assert words(cpu,p.names['data_0209cef4'],1) == [OBJECT+0x368]
                    stages.append(bytes(cpu.mem_read(p.names['data_020a0e68'],48)))
                    global_stages.append(globals_after(obs))
                elif operation == 'cleanup-empty':
                    assert obs.run('_ZN7daMip_c16CleanupResourcesEv',[OBJECT]) == 1
                    stages.append(payload(p,i == 1));global_stages.append(globals_after(obs))
                dtor = '_ZN7daMip_cD0Ev' if fill == 0xa5 else '_ZN7daMip_cD1Ev'
                obs.run(dtor,[OBJECT]);stages.append(payload(p,i == 1,True))
                global_stages.append(globals_after(obs))
                assert obs.frees == ([OBJECT] if fill == 0xa5 else [])
            obs.canaries()
            outcomes.append((stages,global_stages))
        assert outcomes[0] == outcomes[1], (fill,operation,fail,'full actor/matrix mismatch',
            [[hex(j) for j,(a,b) in enumerate(zip(x,y)) if a!=b][:20] for x,y in zip(outcomes[0][0],outcomes[1][0])])
        cases.append({'poison_byte':fill,'operation':operation,'allocation_failed':fail,
            'constructed_bytes_compared':SIZE,'destroyed_payload_bytes_compared':0 if fail else SIZE-20,
            'terminal_dead_vptrs_separately_validated':not fail,'animation_fast_path_executed':operation not in ('construct','matrix','cleanup-empty'),
            'actual_matrix_and_shadow_update':operation == 'matrix','empty_resource_cleanup':operation == 'cleanup-empty',
            'globals_and_intrusive_lists_equal_after_each_call':True,
            'payload_sha256':[sha(x) for x in outcomes[0][0]],'status':'PASS'})
    report = {'status':'PASS','scope':__doc__,'ELF_sha256':sha(raw),'upstream_pin':PIN,'actor_id':actor,
        'source_sha256':sha(source.read_bytes()),'object_sha256':sha((ROOT/entry['object']).read_bytes()),
        'local_data_views':len(views),'original_function_definitions':len(declared),'body_bridge_inverts_byte_for_byte':True,
        'original_31_slot_vtable_dispatch_equal':True,'cases':cases,'ROM_instructions':observers[0].instructions,
        'native_instructions':observers[1].instructions,'bounded_writes':True,'allocation_canaries':True,
        'callee_saved_and_stack_ABI':True,'old_RAM_imported':False,'production_recompiled_for_gate':False,
        'resource_IO_executed':False,'game_frames_executed':False,'physical_hardware':False}
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':'PASS','cases':len(cases),'actor_bytes':SIZE,'local_data_views':len(views),'report':str(args.report)}))

if __name__ == '__main__':main()
