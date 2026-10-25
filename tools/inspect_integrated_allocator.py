#!/usr/bin/env python3
"""Read the captured ARM32 allocators without running or changing the game.

The layouts below are checked against instructions/literal inputs of the exact
captured newlib/libctru functions. Reject changed ABIs or inconsistent lists.
Committed malloc arena free bytes are not total free console RAM.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile
from inspect_integrated_vm import VM, find_dumps


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def observe(vm,s):
    update=s['__malloc_update_mallinfo'];bins=s['__malloc_av_'];info=s['__malloc_current_mallinfo']
    # ARM32 newlib function: top at bins+8,128 bin heads8bytes apart,
    # chunk size+4, backward link+12, computed stats at mallinfo+28/32/36.
    assert vm.u32(update)==0xe59fc074 and vm.u32(update+0x7c)==bins and vm.u32(update+0x80)==info
    assert vm.u32(update+0x34)==0xe5932004 and vm.u32(update+0x38)==0xe593300c
    top=vm.u32(bins+8);top_size=vm.u32(top+4)&~3
    free=top_size;largest=top_size;nodes=set()
    for head in range(bins+8,bins+1024,8):
        node=vm.u32(head+12)
        while node!=head:
            assert node%8==0 and node not in nodes and len(nodes)<100000
            nodes.add(node);size=vm.u32(node+4)&~3
            assert size>=16 and size%8==0
            fd,bk=struct.unpack('<2I',vm.read(node+8,8))
            assert vm.u32(fd+12)==node and vm.u32(bk+8)==node,dict(
                check='free_chunk_reciprocal_links',node=hex(node),fd=hex(fd),bk=hex(bk),
                fd_back=hex(vm.u32(fd+12)),bk_forward=hex(vm.u32(bk+8)))
            free+=size;largest=max(largest,size);node=bk
    arena=vm.u32(info);assert 0<=free<=arena
    def pool(address):
        node=vm.u32(address);previous=0;total=0;largest=0;blocks=[];seen=set();end=0
        while node:
            assert node not in seen and len(seen)<100000;seen.add(node)
            prev,nxt,start,size=struct.unpack('<4I',vm.read(node,16))
            assert prev==previous and size and start>=end and start+size<=0x40000000
            blocks.append({'address':hex(start),'bytes':size});total+=size;largest=max(largest,size)
            previous=node;end=start+size;node=nxt
        assert vm.u32(address+4)==previous
        return dict(free_bytes=total,largest_free_block=largest,free_blocks=len(blocks))
    linear=s['linearSpaceFree'];getter=s['_ZN7MemPool12GetFreeSpaceEv'];vram=s['vramSpaceFree']
    assert vm.u32(linear)==0xe59f0000 and vm.u32(getter)==0xe5903000
    assert vm.u32(getter+0x10)==0xe593200c and vm.u32(getter+0x14)==0xe5933004
    assert vm.u32(vram)==0xe92d4010 and vm.u32(vram+4)==0xe59f0014
    linear_pool=pool(vm.u32(linear+8));vram_pools=[pool(vm.u32(vram+0x20)),pool(vm.u32(vram+0x24))]
    return dict(epoch=vm.header['epoch'],begin_tick=vm.header['begin_tick'],
        malloc=dict(configured_heap_bytes=vm.u32(s['__ctru_heap_size']),committed_arena_bytes=arena,
            allocated_chunk_bytes=arena-free,free_in_committed_arena=free,largest_free_chunk=largest,
            top_chunk_bytes=top_size,free_bin_chunks=len(nodes),reciprocal_links_checked=True),
        linear=linear_pool,vram=dict(free_bytes=sum(p['free_bytes'] for p in vram_pools),pools=vram_pools))


def analyze(attempt):
    closed=json.loads((attempt/'result.json').read_text());elf=attempt/'sm64ds-integrated.elf'
    assert sha(elf)==closed['ELF_sha256']
    with elf.open('rb') as f:
        s={x.name:x['st_value'] for x in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    snapshots=[];rejected=[]
    for p in find_dumps(attempt/'user/sdmc/3ds/sm64ds'):
        assert sha(p)==closed['files'][p.relative_to(attempt).as_posix()]['sha256']
        v=VM(p)
        try:item=observe(v,s)
        except AssertionError as error:
            rejected.append(dict(snapshot=p.name,VM_sha256=sha(p),scene=v.header['scene'],
                epoch=v.header['epoch'],reason=repr(error),
                interpretation='Inconsistent captured allocator list; allocation totals are withheld. Capture is not atomic across background threads; this alone does not prove runtime heap corruption.'))
            continue
        item.update(snapshot=p.name,VM_sha256=sha(p));snapshots.append(item)
    assert snapshots or rejected
    return dict(status='read_only_captured_allocator_lists_validated' if not rejected else 'partial_allocator_capture_validation',ELF_sha256=closed['ELF_sha256'],
        inspector_sha256=sha(Path(__file__)),snapshots=sorted(snapshots,key=lambda r:r['begin_tick']),
        rejected_snapshots=rejected,
        limits='Captured process allocator state includes diagnostic overhead. Committed arena is not total available RAM. These samples alone cannot prove absence of leaks, fragmentation safety, or physical performance.',
        game_executed_by_observer=False,RAM_modified=False,physical_hardware_proof=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('attempt',type=Path);args=p.parse_args()
    result=analyze(args.attempt);out=args.attempt/'allocator-analysis.json';assert not out.exists()
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
