#!/usr/bin/env python3
"""Bounded DSMT fade and upper title map commit ROM/ARM11 comparison.

Runs preserved original ROM and supplied immutable production ELF instructions
with explicit fixture records. Original DS display RAM is a register observer;
ARM11 runs real native_video providers with DS addresses unmapped. No complete
game frame, Azahar/UI launch, physical test, or cartridge-byte export occurs.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'build/game-data/python')]
import test_native_audio_deferred_arm as shared
import test_native_menu_constructors as constructors
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC,UC_ARM_REG_LR,UC_ARM_REG_SP,UC_ARM_REG_CPSR,UC_ARM_REG_C13_C0_3

WORK=shared.WORK
FADE,OBJ,SUB,WRAPPER,INNER,CTX,STATE=[WORK+i*0x1000 for i in range(1,8)]
CALLBACK='func_ov007_020b16f0'
SOURCES=['src/RequestScreenFade.c','src/func_ov007_020bf790.c',
 'src/func_ov007_020bf690.c','src/func_ov007_020c8498.c',
 'src/func_ov007_020c8b34.c','src/func_ov007_020c8688.c',
 'src/func_ov007_020c86a8.c','src/func_ov007_020c86c4.c',
 'src/func_ov007_020c8970.c','src/func_ov007_020c897c.c',
 'src/func_ov007_020c8bc8.cpp','src/func_020551f0.c',
 'src/func_ov007_020c8ac0.c','src/func_ov007_020b16f0.c',
 'src/func_ov007_020c1d78.c','src/func_ov007_020bca40.c',
 'src/func_ov007_020b6d40.c','src/func_ov007_020b7364.c',
 'src/func_ov007_020c03b0.c','src/func_ov007_020c076c.c']



class Snapshot:
    """Strict read-only SM64RAM1 reader; logical views index physical bytes."""
    def __init__(self,binary_path,index_path=None):
        self.path=Path(binary_path)
        self.raw=self.path.read_bytes()
        if len(self.raw)<32 or self.raw[:8]!=b'SM64RAM1':raise ValueError('Invalid SM64RAM1 header')
        version,payload,span_count,view_count,total,reserved=struct.unpack_from('<6I',self.raw,8)
        if version!=1 or reserved or not 1<=span_count<=5 or view_count>272:
            raise ValueError('Unsupported SM64RAM1 descriptor header')
        descriptor_end=32+16*span_count+28*view_count
        if payload!=descriptor_end or total!=len(self.raw):raise ValueError('Incomplete SM64RAM1 payload')
        self.spans=[];self.views=[]
        cursor=payload
        for index in range(span_count):
            address,size,offset,identity=struct.unpack_from('<4I',self.raw,32+index*16)
            if not address or not size or address+size>0xffffffff or identity!=index or offset!=cursor:
                raise ValueError('Invalid physical span descriptor')
            if size>len(self.raw)-offset:raise ValueError('Truncated physical span')
            if any(address<x['address']+x['size'] and x['address']<address+size for x in self.spans):
                raise ValueError('Overlapping physical spans')
            self.spans.append(dict(id=index,address=address,size=size,file_offset=offset,name='span-'+str(index)))
            cursor+=size
        if cursor!=total:raise ValueError('Trailing or missing physical payload')
        for index in range(view_count):
            address,size,offset,parent,module,original,flags=struct.unpack_from('<7I',self.raw,32+16*span_count+index*28)
            if parent>=span_count or not address or not size or address+size>0xffffffff:
                raise ValueError('Invalid logical view descriptor')
            owner=self.spans[parent]
            if not self.contains(owner,address,size) or offset!=owner['file_offset']+address-owner['address']:
                raise ValueError('Uncovered logical view')
            self.views.append(dict(id=index,address=address,size=size,file_offset=offset,span=parent,
                                   module=module,original_address=original,flags=flags,name='view-'+str(index)))
        if index_path is None:
            candidate=self.path.with_suffix('.json')
            if candidate.is_file():index_path=candidate
        self.index_path=Path(index_path) if index_path is not None else None
        self.metadata={}
        if self.index_path:
            metadata=json.loads(self.index_path.read_text())
            if (metadata.get('format')!='SM64RAM1' or metadata.get('version')!=1 or
                metadata.get('byte_order')!='little' or metadata.get('io_status')!='complete' or
                metadata.get('binary_bytes')!=total):raise ValueError('Incomplete or incompatible snapshot index')
            for key,actual,fields in [('spans',self.spans,('id','address','size','file_offset')),
                                     ('views',self.views,('id','address','size','file_offset','span','module','original_address','flags'))]:
                expected=metadata.get(key)
                if not isinstance(expected,list) or len(expected)!=len(actual):raise ValueError('Index descriptor count mismatch')
                for left,right in zip(expected,actual):
                    if any(left.get(f)!=right[f] for f in fields):raise ValueError('Binary/index descriptor mismatch')
                    if not isinstance(left.get('name'),str):raise ValueError('Unnamed index descriptor')
                    right['name']=left['name']
            self.metadata=metadata

    @staticmethod
    def contains(span,address,size):
        return address>=span['address'] and 0<=size<=span['size'] and address-span['address']<=span['size']-size

    def read(self,address,size):
        if size<0 or address<0 or address+size>0x100000000:raise ValueError('Invalid requested span')
        owners=[s for s in self.spans if self.contains(s,address,size)]
        if len(owners)!=1:raise ValueError('Read is outside a complete physical span')
        span=owners[0];offset=span['file_offset']+address-span['address']
        return self.raw[offset:offset+size]

    def u32(self,address):return struct.unpack('<I',self.read(address,4))[0]
    def s32(self,address):return struct.unpack('<i',self.read(address,4))[0]
    def u16(self,address):return struct.unpack('<H',self.read(address,2))[0]
    def s16(self,address):return struct.unpack('<h',self.read(address,2))[0]

    def video_view(self,original_address):
        candidates=[v for v in self.views if v['module']==0xffffffff and v['original_address']==original_address]
        if len(candidates)!=1:raise ValueError('Missing or ambiguous native video view')
        return candidates[0]

    def summary(self):
        return dict(path=str(self.path),sha256=shared.base.sha(self.raw),bytes=len(self.raw),
                    metadata={k:v for k,v in self.metadata.items() if k not in ('spans','views')},
                    spans=[dict(s,sha256=shared.base.sha(self.read(s['address'],s['size']))) for s in self.spans],
                    view_count=len(self.views))

    def install(self,cpu):
        """Restore native identities over ELF RW; map only absent RAM pages.

        Logical views are never separately mapped or relocated. Scratch stack
        must be disjoint from these restored bytes and selected by the caller.
        """
        for span in self.spans:
            begin=span['address']&~0xfff;end=(span['address']+span['size']+0xfff)&~0xfff
            if begin<0x08000000 and end>0x04000000:raise ValueError('Native snapshot overlaps original DS bus addresses')
            existing=sorted((a,b+1) for a,b,_ in cpu.mem_regions())
            cursor=begin
            for a,b in existing:
                if b<=cursor or a>=end:continue
                if a>cursor:cpu.mem_map(cursor,a-cursor)
                cursor=max(cursor,b)
                if cursor>=end:break
            if cursor<end:cpu.mem_map(cursor,end-cursor)
            cpu.mem_write(span['address'],self.read(span['address'],span['size']))

    def restore_tls(self,cpu):
        """Restore TPIDRURW only from an explicitly captured ThreadVars view.

        RAM restoration alone never guesses this register from heap bytes. Old
        snapshots without TLS metadata leave the caller's CP15 register intact.
        A captured pointer's residency is checked; this supplies no OS services
        and makes no claim that the complete newlib reentrancy graph is covered.
        """
        available=self.metadata.get('thread_tls_available',False)
        if available is not True:
            if available is not False:raise ValueError('Invalid TLS availability metadata')
            return dict(restored=False,reason='TLS was not declared available in the capture')
        address=self.metadata.get('thread_tls_address')
        if type(address) is not int or not 0<address<=0xffffffff-512:
            raise ValueError('Invalid captured TLS address')
        views=[v for v in self.views if v['name']=='calling-thread-tls']
        if len(views)!=1 or any(views[0][key]!=value for key,value in
                [('address',address),('size',512),('module',0xffffffff),
                 ('original_address',0),('flags',0)]):
            raise ValueError('Missing or incompatible captured TLS view')
        self.read(address,512)
        if self.u32(address)!=0x21545624:raise ValueError('Captured TLS has no libctru ThreadVars magic')
        reent=self.u32(address+8)
        if not reent:raise ValueError('Captured TLS has a null reentrancy pointer')
        self.read(reent,4)
        cpu.reg_write(UC_ARM_REG_C13_C0_3,address)
        return dict(restored=True,register='TPIDRURW',address=hex(address),
                    reentrancy_pointer=hex(reent),reentrancy_pointer_resident=True,
                    complete_OS_services_not_claimed=True)


def snapshot_reader_selftest():
    """Synthetic format fixture, extent rejection and index cross-check."""
    spans=[dict(id=0,name='elf-rw',address=0x08000000,size=512,file_offset=120),
           dict(id=1,name='app-heap',address=0x08100000,size=1024,file_offset=632)]
    views=[dict(id=0,name='original-section',address=0x08000040,size=32,file_offset=184,span=0,module=7,original_address=0x02104bd8,flags=1),
           dict(id=1,name='native-video',address=0x08100020,size=64,file_offset=664,span=1,module=0xffffffff,original_address=0x04000000,flags=0)]
    raw=bytearray(b'SM64RAM1'+struct.pack('<6I',1,120,2,2,1656,0))
    for span in spans:raw+=struct.pack('<4I',*(span[k] for k in ('address','size','file_offset','id')))
    for view in views:raw+=struct.pack('<7I',*(view[k] for k in ('address','size','file_offset','span','module','original_address','flags')))
    raw+=bytes((i*17+3)&255 for i in range(1536))
    index=dict(format='SM64RAM1',version=1,byte_order='little',io_status='complete',binary_bytes=1656,spans=spans,views=views)
    with tempfile.TemporaryDirectory(prefix='sm64ds-snapshot-reader-') as directory:
        path=Path(directory)/'synthetic.ram.bin';metadata=path.with_suffix('.json')
        path.write_bytes(raw);metadata.write_text(json.dumps(index))
        snap=Snapshot(path)
        assert snap.read(0x08000040,32)==raw[184:216]
        assert snap.video_view(0x04000000)['address']==0x08100020
        assert snap.u32(0x08000000)==struct.unpack_from('<I',raw,120)[0]
        cpu=shared.Uc(shared.UC_ARCH_ARM,shared.UC_MODE_ARM)
        cpu.mem_map(0x08000000,4096)  # Existing ELF page is deliberately reused.
        snap.install(cpu)
        assert bytes(cpu.mem_read(0x08000000,512))==raw[120:632]
        assert bytes(cpu.mem_read(0x08100000,1024))==raw[632:]
        assert len(list(cpu.mem_regions()))==2
        failures=0
        for address,size in [(0x07ffffff,1),(0x080001ff,2),(0x08000200,1),(0xffffffff,2)]:
            try:snap.read(address,size)
            except ValueError:failures+=1
        assert failures==4
        corruptions=[raw[:-1],raw+b'x',raw[:],raw[:],raw[:]]
        struct.pack_into('<I',corruptions[2],8,2)
        struct.pack_into('<I',corruptions[3],32+16,0x08000100)
        struct.pack_into('<I',corruptions[4],32+32+8,185)
        for data in corruptions:
            path.write_bytes(data)
            try:Snapshot(path)
            except ValueError:continue
            raise AssertionError('Reader accepted malformed snapshot')
        path.write_bytes(raw);index['spans'][0]['size']=511;metadata.write_text(json.dumps(index))
        try:Snapshot(path)
        except ValueError:pass
        else:raise AssertionError('Reader accepted mismatched JSON descriptor')
    return dict(status='passed',synthetic_fixture=True,bad_reads=4,bad_formats=5,index_mismatch_rejected=True,physical_pages_restored=True,existing_ELF_page_reused=True)


def signed(bits):return bits-(1<<32) if bits&(1<<31) else bits


class FadeProbe(shared.Probe):
    def __init__(self,label,**kwargs):
        super().__init__(label,**kwargs)
        self.native='elf' in kwargs
        if not self.native:self.cpu.mem_map(0x04000000,0x2000)
        self.events=[]
        self.cpu.hook_add(UC_HOOK_CODE,self.observe)

    def observe(self,cpu,address,size,_):
        if address==self.names['func_ov007_020bdeb0']:
            self.events.append(('sound_request',self.cpu.reg_read(shared.REGS[0])))
            cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        elif address==self.names[CALLBACK]:self.events.append(('completion_callback',))
        elif address==self.names['func_ov007_020c897c']:self.events.append(('disable_blend',))
        elif address==self.names['func_ov007_020c86c4']:self.events.append(('initialize_effect',))

    def fixture(self):
        self.events=[];self.faults=[]
        self.cpu.mem_write(WORK,bytes([0xa5])*0x10000)
        for pointer,size in [(FADE,0x14),(OBJ,0x34),(SUB,0x98),
                             (WRAPPER,8),(INNER,0x30),(CTX,0x188),(STATE,0x10)]:
            self.cpu.mem_write(pointer,bytes(size))
        self.words(self.names['data_ov007_02104bd8'],FADE)
        self.words(FADE,OBJ,SUB)
        self.words(SUB,WRAPPER)
        self.words(WRAPPER+4,INNER)
        self.words(self.names['data_ov007_0210342c'],CTX)
        self.words(CTX+8,STATE)
        self.cpu.mem_write(STATE+2,struct.pack('<h',-1))
        self.words(self.names['data_ov007_02104bec'],1)
        self.call('func_ov007_020c8ac0',[OBJ])
        if self.native:
            # The full owned VRAM zero-fill needs a larger explicit bound than
            # shared's small audio-leaf helper. Keep this fixture setup separate.
            self.cpu.reg_write(UC_ARM_REG_CPSR,0x10)
            self.cpu.reg_write(UC_ARM_REG_SP,shared.STACK+0xf000)
            self.cpu.reg_write(UC_ARM_REG_LR,shared.STOP)
            self.cpu.emu_start(self.names['_ZN6sm64ds18native_video_resetEv'],shared.STOP,count=1000000)
            assert self.cpu.reg_read(UC_ARM_REG_PC)==shared.STOP
            assert self.cpu.reg_read(UC_ARM_REG_SP)==shared.STACK+0xf000
            self.regbase=self.call('_ZN6sm64ds20native_video_pointerEmj',[0x04000000,0x2000])
        else:self.regbase=0x04000000
        self.cpu.mem_write(self.regbase,bytes(0x2000))
        for offset in (0x50,0x1050):
            self.cpu.mem_write(self.regbase+offset,struct.pack('<3H',0xff,0,16))
        self.events=[]

    def snapshot(self):
        fields=list(struct.unpack('<13i',self.cpu.mem_read(OBJ,0x34)))
        fields[9]=1 if fields[9]==self.names[CALLBACK] else fields[9]
        request=list(struct.unpack('<5I',self.cpu.mem_read(FADE,0x14)))
        request[:2]=[1,2]
        request[3]=1 if request[3]==self.names[CALLBACK] else request[3]
        return dict(effect=fields,request=request,
          main=list(struct.unpack('<3H',self.cpu.mem_read(self.regbase+0x50,6))),
          sub=list(struct.unpack('<3H',self.cpu.mem_read(self.regbase+0x1050,6))),
          geometry=[self.u32(SUB+x) for x in (0x40,0x48,0x94)],
          geometry_color=[self.u32(INNER+x) for x in (0x28,0x2c)],
          pending=struct.unpack('<h',self.cpu.mem_read(STATE+2,2))[0],
          input_lock=self.u32(self.names['data_ov007_02104bec']),events=list(self.events))


def fade_case(probe,state,flags,ticks=90,callback=True):
    probe.fixture()
    probe.call('RequestScreenFade',[state,flags,probe.names[CALLBACK] if callback else 0])
    requested=probe.snapshot()
    assert requested['request'][2:]==[flags,int(callback),state]
    snapshots=[]
    for tick in range(1,ticks+1):
        probe.events=[]
        probe.call('func_ov007_020bf690',[])
        snapshots.append(probe.snapshot())
    completions=[i+1 for i,s in enumerate(snapshots) if ('completion_callback',) in s['events']]
    expected={1:69,2:69,3:69,4:17,5:69,6:2,7:2,8:17,9:11}[state]
    assert completions==([expected] if callback else []),(probe.label,state,flags,completions)
    assert snapshots[expected-2]['input_lock']==1
    if callback:
        assert snapshots[expected-1]['input_lock']==0
        assert snapshots[expected-1]['pending']==1
        assert snapshots[expected-1]['effect'][9]==0
    else:
        assert snapshots[-1]['input_lock']==1 and snapshots[-1]['pending']==-1
    return dict(request=requested,ticks=snapshots,completion_tick=expected)


def idle_case(probe):
    probe.fixture()
    before=probe.snapshot()
    for _ in range(100):probe.call('func_ov007_020bf690',[])
    after=probe.snapshot()
    assert before==after
    return dict(ticks=100,unchanged=True,sub=after['sub'],effect_state=after['effect'][2])


def stopped_clock_case(probe):
    probe.fixture()
    probe.call('RequestScreenFade',[5,3,probe.names[CALLBACK]])
    probe.call('func_ov007_020bf690',[])
    probe.words(OBJ+8,0)
    before=probe.snapshot()
    probe.events=[]
    for _ in range(100):probe.call('func_ov007_020bf690',[])
    after=probe.snapshot()
    assert after['effect']==before['effect'] and after['input_lock']==1
    assert after['pending']==-1 and not after['events']
    return dict(ticks=100,stopped_state=0,completion_callback_calls=0,input_lock=1)


def intro_fade_sequence(probe):
    """Original requests from b0a20 → b10dc; holds are explicit fixtures.

    206 hold dispatches model the observed checkpoint's elapsed range only;
    they do not claim to reproduce its BCA/frame or human input state.
    """
    probe.fixture()
    observations=[]
    def request(state,flags,callback=0):
        probe.call('RequestScreenFade',[state,flags,callback])
    def ticks(count,label):
        first=None
        for index in range(count):
            probe.events=[]
            probe.call('func_ov007_020bf690',[])
            if index==0:first=probe.snapshot()
        last=probe.snapshot()
        observations.append(dict(stage=label,dispatches=count,first=first,last=last))
        return last
    request(4,6)
    stage=ticks(17,'b0a20 start: main fade opens')
    assert stage['main'][0]==0
    request(7,24)
    stage=ticks(206,'b0a20 timer30: upper BG black hold')
    assert stage['main']==[0xbf,0,0] and stage['sub']==[0xef,0,16]
    assert stage['effect'][2]==3 and stage['effect'][9]==0
    assert stage['input_lock']==1 and stage['pending']==-1
    request(3,10)
    stage=ticks(69,'b0a20 animation total-31: upper BG fade opens')
    assert stage['sub'][0]==0 and stage['effect'][2]==0
    assert stage['input_lock']==1 and stage['pending']==-1
    request(5,3,probe.names[CALLBACK])
    stage=ticks(69,'b10dc timer60: completion callback unlocks title')
    assert stage['main']==stage['sub']==[0xbf,0,0]
    assert stage['pending']==1 and stage['input_lock']==0
    assert ('completion_callback',) in stage['events']
    return dict(stages=observations,
       matches_actual_video180_main_blend=[0xbf,0,0],
       matches_actual_video180_sub_blend=[0xef,0,16],
       BCA_duration_fixture_claimed=False,human_input_reproduced=False)


def queue_case(elf):
    """Actual constructors → active flags → c03b0 → real SDK tilemap copies."""
    h=constructors.Harness(elf)
    h.original.cpu.mem_map(0x04000000,0x2000)
    h.original.cpu.mem_map(0x06200000,0x20000)
    rows=[]
    def words(p,at,*vals):p.cpu.mem_write(at,struct.pack('<'+'I'*len(vals),*(v&0xffffffff for v in vals)))
    def node(p,at):
        v=struct.unpack('<14I',p.cpu.mem_read(at,0x38))
        return dict(type=signed(v[0]),priority=v[1],active=v[2],dirty=v[3],source=hex(v[4]),
          width=v[7]&65535,height=v[7]>>16,next=hex(v[12]),prev=hex(v[13]),
          source_sha256=shared.base.sha(bytes(p.cpu.mem_read(v[4],(v[7]&65535)*(v[7]>>16)*2))) if v[4] else None)
    for p in (h.original,h.native):
        p.reset();p.cpu.mem_write(constructors.CONTEXT,bytes(0x188))
        p.seams[p.names['_ZN4CP1527FlushAndInvalidateDataCacheEjj']]=lambda:None
        words(p,p.names['data_02099fd0'],0xffffffff)
        p.call('func_ov007_020b6d40',[])
        graph=p.u32(constructors.CONTEXT+0x74);maps=p.u32(graph+0x34)
        nodes=[p.u32(maps+4*i) for i in range(8)]
        before=[node(p,x) for x in nodes]
        p.call('func_ov007_020bca40',[0])
        # Explicit BG-B fragment. Keep real sub mask31 and node chain; exclude
        # the separate lower/main map graph by setting its mask0 for this test.
        words(p,p.u32(graph+8)+4,0)
        active=[node(p,x) for x in nodes]
        if p is h.native:
            p.call('_ZN6sm64ds18native_video_resetEv',[])
            regbase=p.call('_ZN6sm64ds20native_video_pointerEmj',[0x04000000,0x2000])['return']
            vram=p.call('_ZN6sm64ds20native_video_pointerEmj',[0x06800000,0xa4000])['return']
            subbase=vram+0x40000
        else:regbase,subbase=0x04000000,0x06200000
        words(p,regbase+0x1000,0x10000)
        p.cpu.mem_write(regbase+0x1008,struct.pack('<4H',0x284,0x314,0x84,0x184))
        p.cpu.mem_write(regbase+0x242,b'\x84')
        uploads=[]
        lookup={p.names[name]:name for name in ['func_02056554','func_02056494','func_020563d4','func_02056314',
                    'func_020564f4','func_02056434','func_02056374','func_020562b4']}
        def observe(cpu,at,size,_,p=p,lookup=lookup):
            if at in lookup:uploads.append(dict(function=lookup[at],source=hex(p.reg(0)),offset=p.reg(1),bytes=p.reg(2),
                                    sha256=shared.base.sha(bytes(cpu.mem_read(p.reg(0),p.reg(2))))))
        handle=p.cpu.hook_add(UC_HOOK_CODE,observe)
        p.call('func_ov007_020b7364',[graph]);p.cpu.hook_del(handle)
        after=[node(p,x) for x in nodes]
        actual=[bytes(p.cpu.mem_read(subbase+off,0x800)) for off in (0x800,0)]
        for index,data in zip((2,3),actual):assert data==bytes(p.cpu.mem_read(p.u32(nodes[index]+0x10),0x800))
        assert after[2]['dirty']==after[3]['dirty']==0
        assert after[2]['active']==after[3]['active']==1
        assert [u['function'] for u in uploads]==['func_020562b4','func_02056374','func_02056434']
        assert all(u['bytes']==0x800 for u in uploads[:2])
        rows.append(dict(label=p.label,before=before,active=active,after=after,uploads=uploads,
          map_sha256=[shared.base.sha(d) for d in actual],sub_backgrounds=list(struct.unpack('<HH',p.cpu.mem_read(regbase+0x100c,4)))))
    assert rows[0]['map_sha256']==rows[1]['map_sha256']
    assert rows[0]['sub_backgrounds']==rows[1]['sub_backgrounds']==[0x87,0x184]
    return dict(status='passed',implementations=rows,main_mask0_fixture=True,
       cache_flush_observed_as_no_op=True,real_sub_mask=31,real_SDK_copy_path=True)



def inspect_snapshot(snapshot,names,cpu=None):
    """Decode only records fully resident in captured physical ranges."""
    def read(address,size):
        baseline=snapshot.read(address,size)
        return bytes(cpu.mem_read(address,size)) if cpu is not None else baseline
    def u32(address):return struct.unpack('<I',read(address,4))[0]
    def record(address,size):
        if not address:return dict(address=0,available=False,reason='null')
        try:return dict(address=address,available=True,words=list(struct.unpack('<'+str(size//4)+'I',read(address,size))))
        except ValueError as error:return dict(address=address,available=False,reason=str(error))
    def global_value(name):
        if name not in names:return None
        try:
            if name=='data_0209d4f0':return read(names[name],1)[0]
            return u32(names[name])
        except ValueError:return None
    output=dict(global_slots={name:global_value(name) for name in
        ('data_ov007_02104bd8','data_ov007_0210342c','data_ov007_02103430','data_ov007_02104bec',
         'data_0209d4a8','data_0209d514','data_0208ee44','data_0209d4f0')})
    fade=record(output['global_slots']['data_ov007_02104bd8'],0x14)
    output['fade_root']=fade
    if fade['available']:
        output['effect']=record(fade['words'][0],0x34)
        output['geometry']=record(fade['words'][1],0x98)
        if output['effect']['available']:
            output['effect']['fields']=dict(zip(
                ('type','engine_mask','clock_state','completion_policy','curve','interpolation','step',
                 'invert','pending','callback','target_layers','start','end'),
                [signed(value) for value in output['effect']['words']]))
    context=record(output['global_slots']['data_ov007_0210342c'],0x188)
    output['context']=context
    if context['available']:
        words=context['words']
        output['state_records']=[record(words[x//4],16) for x in (4,8,12)]
        output['widgets']=[]
        for index in range(9):
            wrapper=record(words[(0xa4+index*4)//4],8)
            widget=dict(index=index,wrapper=wrapper)
            if wrapper['available']:
                widget['state']=record(wrapper['words'][0],0x2c)
                widget['sprite']=record(wrapper['words'][1],0x44)
            output['widgets'].append(widget)
        graph=record(words[0x74//4],0x48);output['map_graph']=graph
        if graph['available']:
            output['map_queues']=[record(graph['words'][x//4],16) for x in (8,12)]
            output['sprite_queues']=[record(graph['words'][x//4],12) for x in (0,4)]
            oam_view=snapshot.video_view(0x07000000)
            output['queued_oam_matches_native']=[]
            for index,offset in enumerate((0x10,0x14)):
                source=graph['words'][offset//4]
                try:matches=read(source,0x400)==read(oam_view['address']+index*0x400,0x400)
                except ValueError:matches=False
                output['queued_oam_matches_native'].append(matches)
            table=record(graph['words'][0x34//4],32)
            output['map_objects']=[]
            if table['available']:
                for address in table['words']:
                    entry=record(address,0x38)
                    if entry['available']:
                        fields=entry['words'];extent=(fields[7]&65535)*(fields[7]>>16)*2
                        try:
                            if not 0<extent<=0x20000:raise ValueError('Map extent outside bounded audit')
                            if fields[4]:entry['source_sha256']=shared.base.sha(read(fields[4],extent))
                            else:entry['source_null']=True
                        except ValueError as error:entry['source_error']=str(error)
                    output['map_objects'].append(entry)
            known={node['address']:node for node in output['map_objects']}
            output['map_queue_chains']=[]
            for queue in output['map_queues']:
                chain=[];issue=None;previous=0
                if queue['available']:
                    cursor=queue['words'][2]
                    while cursor:
                        if cursor not in known or cursor in chain:
                            issue='unknown or repeated node';break
                        node=known[cursor]
                        if not node['available']:
                            issue='unavailable node';break
                        if node['words'][13]!=previous:
                            issue='previous link mismatch';break
                        chain.append(cursor);previous=cursor;cursor=node['words'][12]
                    if not issue and previous!=queue['words'][3]:issue='tail mismatch'
                else:issue='unavailable queue'
                output['map_queue_chains'].append(dict(nodes=chain,valid=issue is None,issue=issue))
    view=snapshot.video_view(0x04000000)
    regs=read(view['address'],0x2000)
    output['display']={engine:dict(control=struct.unpack_from('<I',regs,offset)[0],
        bg=list(struct.unpack_from('<4H',regs,offset+8)),blend=list(struct.unpack_from('<3H',regs,offset+0x50)))
        for engine,offset in (('main',0),('sub',0x1000))}
    stack_probe=snapshot.metadata.get('stack_probe_address')
    if stack_probe:
        output['stack_probe']=dict(address=stack_probe,
            metadata_stack_available=snapshot.metadata.get('stack_available'),
            resident_in_physical_span=any(snapshot.contains(span,stack_probe,1) for span in snapshot.spans),
            probe_is_original_fault_PC=False)
    return output


def captured_replay(snapshot,elf,ticks):
    """One component's real RAM clock; does not advance BCA/scene/IRQ loops."""
    if not 0<=ticks<=1000:raise ValueError('Replay tick bound must be 0..1000')
    probe=FadeProbe('Captured ARM11 fade component',elf=elf)
    snapshot.install(probe.cpu)
    # Never reset native_video or restore synthetic fixture state after RAM.
    probe.regbase=snapshot.video_view(0x04000000)['address']
    # The captured ctrulib linear heap occupies 0x30000000..0x32000000.
    # Keep oracle registers and scratch stack outside every captured span.
    stack,stop=0x34000000,0x34010000
    if any(stack<=end and begin<stop+4096 for begin,end,_ in probe.cpu.mem_regions()):
        raise ValueError('Captured RAM overlaps reserved oracle stack')
    probe.cpu.mem_map(stack,0x20000)
    before=inspect_snapshot(snapshot,probe.names,probe.cpu)
    outputs=[]
    for index in range(ticks):
        probe.events=[]
        probe.cpu.mem_write(stack,bytes([0x6d])*0x10000)
        probe.cpu.reg_write(UC_ARM_REG_CPSR,0x10)
        for reg in shared.REGS:probe.cpu.reg_write(reg,0)
        for n,reg in enumerate(shared.base.SAVED):probe.cpu.reg_write(reg,0x12340000+n)
        probe.cpu.reg_write(UC_ARM_REG_SP,stack+0xf000)
        probe.cpu.reg_write(UC_ARM_REG_LR,stop)
        probe.cpu.emu_start(probe.names['func_ov007_020bf690'],stop,count=200000)
        if probe.cpu.reg_read(UC_ARM_REG_PC)!=stop:raise RuntimeError('Captured fade replay exceeded instruction bound')
        assert probe.cpu.reg_read(UC_ARM_REG_SP)==stack+0xf000
        assert bytes(probe.cpu.mem_read(stack,32))==bytes([0x6d])*32
        assert bytes(probe.cpu.mem_read(stack+0xf000,64))==bytes([0x6d])*64
        assert all(probe.cpu.reg_read(reg)==0x12340000+n for n,reg in enumerate(shared.base.SAVED))
        state=inspect_snapshot(snapshot,probe.names,probe.cpu)
        outputs.append(dict(dispatch=index+1,effect=state.get('effect'),fade_root=state['fade_root'],
                            display=state['display'],input_lock=state['global_slots']['data_ov007_02104bec'],
                            states=state.get('state_records'),events=list(probe.events)))
    assert not probe.faults
    unchanged_hold=(before.get('effect',{}).get('available') and
        before['effect']['words'][2]==3 and before['fade_root']['words'][4]==0 and
        before['fade_root']['words'][2]&1==0)
    span_checks=[]
    for span in snapshot.spans:
        expected=snapshot.read(span['address'],span['size'])
        actual=bytes(probe.cpu.mem_read(span['address'],span['size']))
        unchanged=actual==expected
        if unchanged_hold:assert unchanged,(span['name'],'completed hold mutated captured memory')
        span_checks.append(dict(name=span['name'],bytes=span['size'],all_bytes_unchanged=unchanged))
    return dict(before=before,component_dispatches=outputs,
                completed_hold_invariant_checked=bool(unchanged_hold),captured_span_canaries=span_checks,
                other_game_clocks_advanced=False,scene_update_advanced=False,
                VBlank_handler_executed=False,cartridge_loader_executed=False,
                captured_registers_reset=False,synthetic_fade_request_inserted=False,
                scratch_stack=stack,sound_requests_observed_without_playback=True)


def captured_video_commit(snapshot,elf):
    """Execute actual b72a0 OAM/map commit once over an independent RAM clone."""
    probe=FadeProbe('Captured ARM11 title video commit',elf=elf)
    assert all(not (span['address']<shared.STACK+0x20000 and
                   shared.STACK<span['address']+span['size']) for span in snapshot.spans)
    snapshot.install(probe.cpu)
    before=inspect_snapshot(snapshot,probe.names,probe.cpu)
    probe.call('func_ov007_020b72a0',[])
    after=inspect_snapshot(snapshot,probe.names,probe.cpu)
    checked=[]
    for span in snapshot.spans:
        expected=snapshot.read(span['address'],span['size'])
        actual=bytes(probe.cpu.mem_read(span['address'],span['size']))
        assert actual==expected,(span['name'],'title commit changed captured bytes')
        checked.append(dict(name=span['name'],bytes=span['size'],all_bytes_unchanged=True))
    assert before==after and not probe.events and not probe.faults
    return dict(status='passed',original_entry='func_ov007_020b72a0',dispatches=1,
        actual_OAM_and_map_services_executed=True,service_success_stubs_used=False,
        captured_span_canaries=checked,state_unchanged=True,
        scratch_stack=shared.STACK,normal_return_preserves_registers_and_stack=True,
        pipeline=['b72a0','b7384 OAM copies','b7364','c03b0 selects active nodes','c076c map commit'],
        azahar_executed=False,live_process_mutated=False,GPU_presentation_executed=False)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf',type=Path)
    ap.add_argument('--pack',type=Path,default=ROOT/'build/europe.pak')
    ap.add_argument('--report',type=Path,default=ROOT/'build/native-title-video/fade-oracle.json')
    ap.add_argument('--ram',type=Path)
    ap.add_argument('--ram-index',type=Path)
    ap.add_argument('--ram-replay-ticks',type=int,default=0)
    ap.add_argument('--reader-selftest',action='store_true')
    args=ap.parse_args()
    if args.reader_selftest:
        print(json.dumps(snapshot_reader_selftest(),indent=2));return
    if args.ram:
        if args.elf is None:raise ValueError('Actual RAM analysis requires the exact --elf from its acquisition')
        snapshot=Snapshot(args.ram,args.ram_index)
        elf_hash=shared.base.sha(args.elf.read_bytes())
        result=dict(status='passed',scope='Actual snapshot read and bounded ARM11 fade component replay',
          snapshot=snapshot.summary(),elf=str(args.elf),elf_sha256=elf_hash,
          replay=captured_replay(snapshot,args.elf,args.ram_replay_ticks),
          title_video_commit=captured_video_commit(snapshot,args.elf),
          native_DS_IO_mapped=False,azahar_executed=False,UI_executed=False,physical_hardware=False,complete_frame=False)
        assert shared.base.sha(args.elf.read_bytes())==elf_hash
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(dict(status='passed',snapshot_tick=snapshot.metadata.get('tick'),replay_dispatches=args.ram_replay_ticks,report=str(args.report)),indent=2));return
    if args.elf is None:args.elf=ROOT/'build/native-runtime-azahar/attempt-1791612346875167000/sm64ds-native.elf'
    assert subprocess.check_output(['git','-C',ROOT/'upstream','rev-parse','HEAD'],text=True).strip()==shared.PIN
    elf_hash=shared.base.sha(args.elf.read_bytes())
    cart=shared.base.Cartridge(args.pack)
    rom,native=FadeProbe('ASMP0 ROM',cart=cart),FadeProbe('production ARM11',elf=args.elf)
    idle=[idle_case(p) for p in (rom,native)];assert idle[0]==idle[1]
    stopped=[stopped_clock_case(p) for p in (rom,native)];assert stopped[0]==stopped[1]
    cases=[]
    for state in range(1,10):
        for flags in (0,2,3,4,6,8,10,16,18,24,26):
            left,right=fade_case(rom,state,flags),fade_case(native,state,flags)
            assert left==right,(state,flags,'ROM/ARM11 different')
            states=left['ticks'];events=[dict(tick=i+1,events=s['events']) for i,s in enumerate(states) if s['events']]
            cases.append(dict(state=state,flags=flags,completion_tick=left['completion_tick'],
              events=events,first=states[0],at_complete=states[left['completion_tick']-1],last=states[-1],
              trace_sha256=shared.base.sha(json.dumps(states,sort_keys=True).encode())))
    no_callback=[]
    for state,flags in ((3,10),(4,6),(5,3),(6,2),(7,24)):
        left,right=fade_case(rom,state,flags,callback=False),fade_case(native,state,flags,callback=False)
        assert left==right
        no_callback.append(dict(state=state,flags=flags,completion_tick=left['completion_tick'],last=left['ticks'][-1]))
    sequences=[intro_fade_sequence(p) for p in (rom,native)]
    assert sequences[0]==sequences[1]
    queue=queue_case(args.elf)
    assert shared.base.sha(args.elf.read_bytes())==elf_hash
    report=dict(status='passed',scope='Bounded host original ROM/production ARM11 fade clock, callback, register, queue/copy equivalence',
      elf=str(args.elf),elf_sha256=elf_hash,cases=cases,no_callback=no_callback,idle=idle[0],stopped_clock=stopped[0],intro_fade_sequence=sequences[0],map_commit=queue,
      source_hashes={name:shared.base.sha((ROOT/'upstream'/name).read_bytes()) for name in SOURCES},
      native_DS_IO_mapped=False,seams=['sound request observed without playback','fixture records, not game-produced RAM',
          'map commit controlled constructor allocation and cache flush'],
      azahar_executed=False,UI_executed=False,physical_hardware=False,complete_frame=False,
      game_generated_fade_state_claimed=False,cartridge_bytes_exported=False)
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='passed',cases=len(cases),no_callback=len(no_callback),
       fade_completion_ticks={state:next(c['completion_tick'] for c in cases if c['state']==state) for state in range(1,10)},
       map_commit=queue['status'],report=str(args.report)),indent=2))

if __name__=='__main__':main()
