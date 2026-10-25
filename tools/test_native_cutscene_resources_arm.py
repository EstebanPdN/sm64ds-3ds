#!/usr/bin/env python3
"""Verify the production ending-cutscene setup against genuine resource bytes.

This is the param-0x2e staff-roll family, adjacent to the param-0x2f intro
fault. The complete production setup and real SDK/native video/LZ16 helpers
execute with DS bus windows unmapped. File delivery, allocation and language
configuration are declared boundaries. All owned video, object, allocation
and guard bytes are checked; resource payloads stay private in memory.
"""
import argparse
import json
from pathlib import Path
import struct

from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR
import test_native_cutscene_video_arm as controls
import test_native_file_video_arm as files

ROOT, shared, base = controls.ROOT, controls.shared, controls.base
ARENA, SIZE = 0x14000000, 0x100000
HANDLES=[0x267,0x268,0x24f,0x250,0x254,0x271,0x26f,0x26d,0x26b,
         0x251,0x252,0x253,0x255,0x256,0x257,0x22c]
FUNCTION='func_ov002_020f5fe4'


def resources(cart):
    _,indices=cart.handles();result={}
    for handle in HANDLES:
        path=cart.entries[indices[handle]][4];raw=cart.file(path)
        item=dict(path=path,raw_bytes=len(raw),raw_sha256=base.sha(raw),data=raw)
        if raw[0]==0x10: item['decoded'],item['LZ16_audit']=files.lz_audit(raw)
        result[handle]=item
    return result


def run(p, rom, resource, language):
    p.call('_ZN6sm64ds18native_video_resetEv',[],instruction_limit=1000000)
    p.cpu.mem_write(p.names['data_020a6088'],bytes(26))
    p.words(p.names['data_020a60a4'],0)
    for name,mask in [('_ZN2GX12SetBankForBGEt',8),('_ZN2GX13SetBankForOBJEt',0x40),
        ('_ZN2GX15SetBankForSubBGEt',4),('_ZN2GX16SetBankForSubOBJEt',0x100)]:
        p.call(name,[mask])
    spans={label:(p.call('_ZN6sm64ds20native_video_pointerEmj',[address,size]),size)
        for label,address,size in [('registers',0x04000000,0x2000),
            ('palette',0x05000000,0x800),('vram',0x06800000,0xa4000),('oam',0x07000000,0x800)]}
    for label,(pointer,size) in spans.items():
        if label!='registers':p.cpu.mem_write(pointer,b'\xa5'*size)
    before={label:bytes(p.cpu.mem_read(pointer,size)) for label,(pointer,size) in spans.items()}
    raw=bytearray(b'\xa5'*0x10000)
    raw[controls.SELF-controls.WORK:controls.SELF-controls.WORK+0x800]=b'\xc7'*0x800
    p.cpu.mem_write(controls.WORK,bytes(raw))
    p.cpu.mem_write(p.names['data_0209d454'],b'\x1f')
    allocations={};events=[];next_pointer=ARENA+64
    names=['LoadFile','Deallocate','_ZN6Memory13operator_new2Ej','GetOwnerLanguage',
           'DecompressLZ16','sm64ds_decompress_lz16_video']
    observed={p.names[n]:n for n in names}
    calls={n:0 for n in names}
    def allocate(cpu,data,kind,handle=None):
        nonlocal next_pointer
        pointer=(next_pointer+31)&~31;next_pointer=pointer+len(data)+64
        assert next_pointer<ARENA+SIZE
        cpu.mem_write(pointer-32,b'\x6d'*32);cpu.mem_write(pointer,data)
        cpu.mem_write(pointer+len(data),b'\x5a'*32)
        allocations[pointer]=(len(data),kind)
        events.append(dict(operation=kind,handle=hex(handle) if handle is not None else None,
            pointer=hex(pointer),bytes=len(data),sha256=base.sha(data)))
        cpu.reg_write(shared.REGS[0],pointer)
    def code(cpu,address,size,_):
        name=observed.get(address)
        if name is None:return
        calls[name]+=1
        if name=='LoadFile':
            handle=cpu.reg_read(shared.REGS[0]);assert handle in resource,hex(handle)
            allocate(cpu,resource[handle]['data'],'LoadFile',handle)
        elif name=='_ZN6Memory13operator_new2Ej':
            count=cpu.reg_read(shared.REGS[0]);assert count==0x4000
            allocate(cpu,b'\xcd'*count,'operator_new2')
        elif name=='Deallocate':
            pointer=cpu.reg_read(shared.REGS[0]);count,kind=allocations.pop(pointer)
            assert kind=='LoadFile'
            assert bytes(cpu.mem_read(pointer-32,32))==b'\x6d'*32
            assert bytes(cpu.mem_read(pointer+count,32))==b'\x5a'*32
            events.append(dict(operation='Deallocate',pointer=hex(pointer),bytes=count))
        elif name=='GetOwnerLanguage':cpu.reg_write(shared.REGS[0],language)
        else:return # Both real LZ16 functions execute; only count their entries.
        cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
    hook=p.cpu.hook_add(UC_HOOK_CODE,code)
    p.call(FUNCTION,[controls.SELF],instruction_limit=5000000)
    p.cpu.hook_del(hook)
    assert not any(b<=0x04000000<=e for b,e,_ in p.cpu.mem_regions())
    assert p.call('_ZN6sm64ds15native_video_okEv',[])==1
    assert p.cpu.mem_read(p.names['data_0209d454'],1)==b'\x10'
    assert p.u32(p.names['data_020a60a4'])==0
    assert p.cpu.mem_read(p.names['data_020a6088']+22,2)==b'\x80\0'
    expected={label:bytearray(data) for label,data in before.items()}
    def patch(label,offset,data):expected[label][offset:offset+len(data)]=data
    lang_handle={2:0x26b,3:0x26d,4:0x26f,5:0x271}.get(language,0x251)
    for offset,handle in [(0x94000,0x267),(0x40000,0x24f),(0x50000,lang_handle),(0xa0000,0x256)]:
        patch('vram',offset,resource[handle]['decoded'])
    for offset,handle in [(0x44000,0x254),(0x46000,0x253),(0x45000,0x255)]:
        patch('vram',offset,resource[handle]['data'])
    for offset in [0x9c000,0x9e000]:patch('vram',offset,resource[0x252]['data'])
    for offset,handle,count in [(0x200,0x268,0x200),(0,0x250,2),(0x400,0x250,0x200),(0x600,0x257,0x200)]:
        patch('palette',offset,resource[handle]['data'][:count])
    for offset,value in [(0xe,0x1100),(0x1008,0xe08),(0x100a,0x802),(0x100c,0xa92),(0x100e,0xc92)]:
        struct.pack_into('<H',expected['registers'],offset,value)
    for offset in range(0x1010,0x1020,2):struct.pack_into('<H',expected['registers'],offset,0)
    struct.pack_into('<I',expected['registers'],0x1000,0x40000000)
    expected['registers'][0x248]=0x82
    actual={label:bytes(p.cpu.mem_read(pointer,size)) for label,(pointer,size) in spans.items()}
    for label in actual:
        assert actual[label]==bytes(expected[label]),(language,label,
            [(hex(i),a,b) for i,(a,b) in enumerate(zip(actual[label],expected[label])) if a!=b][:20])
    assert len(allocations)==1
    pointer=p.u32(controls.SELF+0x510);assert allocations[pointer]==(0x4000,'operator_new2')
    assert bytes(p.cpu.mem_read(pointer,0x4000))==resource[0x22c]['decoded']
    assert bytes(p.cpu.mem_read(pointer-32,32))==b'\x6d'*32
    assert bytes(p.cpu.mem_read(pointer+0x4000,32))==b'\x5a'*32
    # Compare the nested state initializer with the actual ROM, not a copy of
    # the C implementation. Pointer/ordinary-buffer setup is checked above.
    struct.pack_into('<I',raw,controls.SELF-controls.WORK+0x510,pointer)
    rom.cpu.mem_write(controls.WORK,bytes(raw))
    rom.call('func_ov002_020f5ad4',[controls.SELF])
    assert bytes(p.cpu.mem_read(controls.WORK,0x10000))==bytes(rom.cpu.mem_read(controls.WORK,0x10000))
    assert calls['DecompressLZ16']==5 and calls['sm64ds_decompress_lz16_video']==5
    expected_order=[0x267,0x268,0x24f,0x250,0x254,lang_handle,0x252,0x253,0x255,0x256,0x257,0x22c]
    assert [int(e['handle'],16) for e in events if e['operation']=='LoadFile']==expected_order
    assert calls['LoadFile']==calls['Deallocate']==12
    return dict(language=language,language_handle=hex(lang_handle),
        video_sha256={n:base.sha(v) for n,v in actual.items()},
        full_video_bytes_compared=sum(size for _,size in spans.values()),
        full_object_and_fixture_compared_with_ROM_initializer=True,
        ordinary_LZ16_allocation_bytes_compared=0x4000,all_allocation_guards_intact=True,
        calls=calls,events=events,ABI_preserved=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,default=ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-integration-check/cutscene-resources-arm-gate.json')
    args=parser.parse_args();assert not args.report.exists()
    cart=base.Cartridge(ROOT/'build/europe.pak');resource=resources(cart)
    p=shared.Probe('production ARM11',elf=args.elf);p.cpu.mem_map(ARENA,SIZE)
    rom=controls.original_machine(cart)
    results=[run(p,rom,resource,language) for language in range(6)]
    report=dict(status='passed',ELF_sha256=base.sha(args.elf.read_bytes()),upstream_commit=shared.PIN,
        function=FUNCTION,cutscene_parameter='0x2e ending/staff roll',cases=results,
        resources={hex(h):{k:v for k,v in item.items() if k not in ['data','decoded']} for h,item in resource.items()},
        declared_boundaries=['LoadFile delivers genuine pack bytes','Deallocate/Memory allocator','owner language configuration'],
        native_DS_MMIO_unmapped=True,
        limitations=['not a full original hardware SDK comparison','not integrated ending completion','no physical hardware/FPS'])
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='passed',cases=len(results),report=str(args.report))))


if __name__=='__main__':main()
