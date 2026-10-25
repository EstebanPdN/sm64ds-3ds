#!/usr/bin/env python3
"""Compare three recovered overlay initializer seats against ASMP0 instructions.

Executes the frozen production ELF and the original ROM bodies, including real
file-handle construction, destructor registration and empty-handle cleanup.
Only the verified handle catalog and a disjoint component-call stack are input
fixtures. No old RAM, OS I/O, game frames, module manager or hardware is implied.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from test_native_menu_callbacks import Cartridge, symbols
from test_native_audio_deferred_arm import Probe, WORK, STACK, STOP
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT=Path(__file__).resolve().parents[1]
DEFAULT=ROOT/'build/native-integration-check/candidate-overlay-static-init/sm64ds-native.elf'
SAVED=(UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
       UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11)
REGS=(UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)
SENTINEL=WORK+0x2000
CATALOG=WORK+0x3000
ROWS=[
    (23,'__sinit_d_a_obj_fm_battan.cpp',[
        ('data_ov023_02112088','func_02017acc','func_02017ab4',1558,0x0211209c,'battan_model_node'),
        ('data_ov023_02112080','func_02017b4c','SharedFilePtr_Destruct_Clsn',1559,0x02112090,'battan_collision_node')]),
    (44,'__sinit_d_a_obj_kb1_billboard.cpp',[
        ('data_ov044_02111680','func_02017acc','func_02017ab4',1570,0x02111688,'kb1_billboard_node')]),
    (100,'__sinit_d_a_star_gate.cpp',[
        ('data_ov100_02148934','func_02017acc','func_02017ab4',0x4a1,0x0214893c,'star_gate_node')])]
STATES=['_ZN12daStarGate_c13ST_OPEN_CLOSEE','_ZN12daStarGate_c14ST_STAY_CLOSEDE',
        '_ZN12daStarGate_c7ST_WAITE','_ZN12daStarGate_c20ST_TALKING_TO_PLAYERE',
        '_ZN12daStarGate_c12ST_UNLOCKINGE']

def sha(raw):return hashlib.sha256(raw).hexdigest()

def original_names(module):
    names={}
    for file in [ROOT/'upstream/config/arm9/symbols.txt',
                 ROOT/f'upstream/config/arm9/overlays/ov{module:03}/symbols.txt']:
        for line in file.read_text().splitlines():
            m=re.match(r'(\S+) kind:\S+ addr:0x([0-9a-f]+)',line)
            if m:names[m[1]]=int(m[2],16)
    return names

def component_call(probe,name,args=()):
    cpu=probe.cpu
    cpu.reg_write(UC_ARM_REG_CPSR,0x10)
    for reg,value in zip(REGS,(*args,0,0,0)[:3]):cpu.reg_write(reg,value)
    saved=[0x12345600+i for i in range(len(SAVED))]
    for reg,value in zip(SAVED,saved):cpu.reg_write(reg,value)
    cpu.reg_write(UC_ARM_REG_SP,STACK+0xf000);cpu.reg_write(UC_ARM_REG_LR,STOP)
    cpu.emu_start(probe.names[name],STOP,count=20000)
    assert cpu.reg_read(UC_ARM_REG_PC)==STOP,('bounded call did not return',probe.label,name)
    assert cpu.reg_read(UC_ARM_REG_SP)==STACK+0xf000
    assert [cpu.reg_read(r) for r in SAVED]==saved,('callee-saved ABI',probe.label,name)
    assert not probe.faults,probe.faults

def run_case(cart,elf,module,name,handles,fill,head):
    native=Probe('frozen production ARM11',elf=elf)
    original=Probe('ASMP0 ROM',cart=cart)
    original.names=original_names(module)
    address,bss,image=cart.image(module);original.cpu.mem_write(address,image)
    if bss:original.cpu.mem_write(address+len(image),bytes(bss))
    ids=[cart.entries[i][0] for i in cart.handles()[1]]
    assert len(ids)==2058 and all(x<0x8000 for x in ids)
    catalog=struct.pack('<'+'H'*len(ids),*ids)
    native_nodes={suffix:next(value for key,value in native.names.items()
        if key.startswith('_ZN12_GLOBAL__N_1') and suffix in key)
        for *_,suffix in handles}
    rewrites={original.names[key]:value for key,value in native.names.items()
              if key in original.names and not key.startswith('$')}
    for *_,node,suffix in handles:rewrites[node]=native_nodes[suffix]
    all_results=[]
    for probe,is_native in [(original,False),(native,True)]:
        cpu=probe.cpu
        cpu.mem_write(CATALOG,catalog);probe.words(probe.names['data_0209d3b8'],CATALOG)
        sentinel=struct.pack('<3I',0,0x12300004,0x12300008)
        cpu.mem_write(SENTINEL,sentinel);probe.words(probe.names['data_020aa3f0'],head)
        probe.words(probe.names['data_0209d3bc'],0x6b6b6b6b)
        allowed=[(probe.names['data_020aa3f0'],4),(probe.names['data_0209d3bc'],4),
                 (STACK+0xd000,0x2000)]
        for obj,_,_,_,node,suffix in handles:
            node=native_nodes[suffix] if is_native else node
            cpu.mem_write(probe.names[obj],bytes([fill])*8);cpu.mem_write(node,bytes([fill])*12)
            allowed.extend([(probe.names[obj],8),(node,12)])
        if module==100:
            for state in STATES:
                cpu.mem_write(probe.names[state],bytes([fill])*16);allowed.append((probe.names[state],16))
            vector=probe.names['data_ov100_02148948']
            cpu.mem_write(vector,bytes([0x6b])*12)
        calls=[];writes=[];instructions=[0]
        observed={probe.names[key]:key for key in
            ['func_02017acc','func_02017b4c','func_020731dc','func_02017ab4','SharedFilePtr_Destruct_Clsn']}
        def code(c,a,s,u):
            instructions[0]+=1
            if a in observed:calls.append({'name':observed[a],'args':[c.reg_read(r) for r in REGS]})
        def write(c,k,a,s,v,u):
            assert any(start<=a and a+s<=start+size for start,size in allowed),('write outside outputs/stack',probe.label,hex(a),s)
            writes.append((a,s))
        cpu.hook_add(UC_HOOK_CODE,code);cpu.hook_add(UC_HOOK_MEM_WRITE,write)
        component_call(probe,name)
        objects={obj:bytes(cpu.mem_read(probe.names[obj],8)).hex() for obj,*_ in handles}
        nodes={suffix:list(struct.unpack('<3I',cpu.mem_read(native_nodes[suffix] if is_native else node,12)))
               for *_,node,suffix in handles}
        states={state:list(struct.unpack('<4I',cpu.mem_read(probe.names[state],16)))
                for state in STATES} if module==100 else {}
        assert bytes(cpu.mem_read(SENTINEL,12))==sentinel
        assert bytes(cpu.mem_read(CATALOG,len(catalog)))==catalog
        if module==100:assert bytes(cpu.mem_read(vector,12))==bytes([0x6b])*12
        cleanup=[]
        for obj,_,destructor,_,_,_ in reversed(handles):
            before=bytes(cpu.mem_read(probe.names[obj],8))
            component_call(probe,destructor,[probe.names[obj]])
            after=bytes(cpu.mem_read(probe.names[obj],8))
            cleanup.append({'object':obj,'destructor':destructor,'before':before.hex(),'after':after.hex()})
        all_results.append({'objects':objects,'nodes':nodes,'states':states,
            'head':probe.u32(probe.names['data_020aa3f0']),'calls':calls,
            'instructions':instructions[0],'write_count':len(writes),'cleanup':cleanup,
            'last_destroyed_id':probe.u32(probe.names['data_0209d3bc'])})
    original_result,native_result=all_results
    normal=lambda x:rewrites.get(x,x)
    assert original_result['objects']==native_result['objects']
    assert {k:[normal(x) for x in v] for k,v in original_result['nodes'].items()}==native_result['nodes']
    assert {k:[normal(x) if i in (0,2) else x for i,x in enumerate(v)]
        for k,v in original_result['states'].items()}==native_result['states']
    assert normal(original_result['head'])==native_result['head']
    assert original_result['cleanup']==native_result['cleanup']
    assert original_result['last_destroyed_id']==native_result['last_destroyed_id']
    # r2 of constructors and all destructor trailing registers are unspecified.
    init_call_count=2*len(handles)
    for left,right in zip(original_result['calls'][:init_call_count],native_result['calls'][:init_call_count]):
        assert left['name']==right['name']
        count=3 if left['name']=='func_020731dc' else 2
        assert [normal(v) for v in left['args'][:count]]==right['args'][:count]
    assert len(original_result['calls'])==len(native_result['calls'])==3*len(handles)
    return {'module':module,'initializer':name,'poison_byte':fill,'prior_destructor_head':hex(head),
        'status':'PASS','handle_ids':[row[3] for row in handles],
        'ROM_instructions':original_result['instructions'],'native_instructions':native_result['instructions'],
        'cleanup_callbacks':len(handles),'state_pairs':len(states),'ABI_preserved':True,
        'writes_bounded':True,'sentinel_and_catalog_unchanged':True,'POD_vector_unchanged':module==100}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,default=DEFAULT)
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-stage-plan/overlay-static-init-gate.json')
    args=parser.parse_args();elf=args.elf.resolve();raw=elf.read_bytes();elf_sha=sha(raw)
    cart=Cartridge(ROOT/'build/europe.pak');names=symbols(elf)
    p=Probe('ctor table inspection',elf=elf)
    begin,end=[names[key] for key in ['sm64ds_runtime_ctors','sm64ds_runtime_ctors_end']]
    table=[struct.unpack('<2I',p.cpu.mem_read(at,8)) for at in range(begin,end,8)]
    assert len(table)==305
    for module,name,_ in ROWS:assert table.count((module,names[name]))==1
    cases=[run_case(cart,elf,module,name,handles,fill,head)
        for module,name,handles in ROWS for fill in (0,0xa5,0xff) for head in (0,SENTINEL)]
    assert sha(elf.read_bytes())==elf_sha
    report={'status':'PASS','elf':str(elf),'elf_sha256':elf_sha,'cases':cases,
        'case_count':len(cases),'cleanup_callbacks':sum(x['cleanup_callbacks'] for x in cases),
        'instructions':sum(x['ROM_instructions']+x['native_instructions'] for x in cases),
        'source_sha256':sha(Path(__file__).read_bytes()),'production_recompiled_for_gate':False,
        'old_RAM_imported':False,'runtime_ctor_table_count':len(table),
        'scope':'Actual immutable ELF and original ASMP0 instructions; verified 2058-entry FAT-ID catalog, '
        'disjoint component stack and sentinel are declared fixtures. File bodies, state assignments, '
        'registration and empty-resource destructor callbacks execute without seams. No OS I/O, module '
        'activation manager, loaded-resource cleanup, gameplay, live Stage or physical hardware proof.'}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    assert not args.report.exists(),'Preserve prior gate report; select a distinct --report'
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['status','case_count','cleanup_callbacks','instructions','elf_sha256']}))

if __name__=='__main__':main()
