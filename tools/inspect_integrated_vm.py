#!/usr/bin/env python3
"""Read a mapped native process capture and its bounded timing ring.

This format records virtual addresses returned by svcQueryMemory. It is not
a raw physical FCRAM dump and does not guess a Boost/CST archive offset.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import statistics
import struct
import re

HEADER=struct.Struct('<8sIIIIQQIIIIII')
REGION=struct.Struct('<6I')
TRACE_HEADER=struct.Struct('<8sIIIIQ')
TRACE_RECORD=struct.Struct('<QQ4I32I')
TRACE_RECORD_V2=struct.Struct('<QQ4IQ48I')
LCD_HEADER=struct.Struct('<8s4IQ4I4I')
LCD_RECORD=struct.Struct('<12I4I')
# Version 2 captures plain device memory: both VRAM banks and DSP windows.
DEVICE_RANGES=((0x1F000000,0x1F600000),(0x1FF00000,0x1FF80000))
REGION_DEVICE_PAYLOAD=4
LCD_REGISTER_BLOCKS=(0x400000,0x400400,0x400500,0x400C00)
LCD_FORMATS={0:('RGBA8',4),1:('BGR8',3),2:('RGB565',2),3:('RGB5A1',2),4:('RGBA4',2)}
SLOTS=['MainWaitIdle','MainPrepareB','MainCompiledFrame','MainPreparePpu','MainPresentTotal',
       'WorkerMesh','WorkerClassify','WorkerMeshPrepare','WorkerCore','MainObjPrepare',
       'MainSynchronous','WorkerTextures','WorkerDraw','WorkerCompose','WorkerFrameBegin',
       'MainPpuA','MainHeldCheck','ClassifyInput','ClassifyScan','ClassifyPartition','GameFsRead',
       'GameFsLoad','GameFsOpen','ClassifyCross','ClassifyGroups','ClassifyTriangles',
       'ClassifyBulk','WorkerComposeObjects','WorkerComposeViews','WorkerComposePanels',
       'WorkerComposeTransfer','WorkerComposeObjectUpdate',
       'CompositeUpload','CompositeMaskPreparation','CompositeMaskTransfer','CompositeDraw',
       'CompositeObjectUpload','GameInputAndSceneSetup','MainAudioMaintenance','CadenceWait',
       'NativeMotionBridge','FrameBeginSync','FrameBeginAsync','FrameEndFlush',
       'FrameEndAsync','AudioPump','GameRenderCallbacks','GameActorCallbacks']

def trace_context(value):
    if value&0xffff0000!=0x534d0000:return None
    result=dict(pause=bool(value&1),dialogue=bool(value&2),cutscene=bool(value&4))
    if value&0x8000:
        yaw=(value>>3)&255
        result.update(camera_yaw_bin=yaw,camera_sector=yaw//32,
                      camera_yaw_degrees=yaw*360/256,
                      camera_height_relation={0:'equal',1:'above',2:'below'}.get((value>>11)&3,'unknown'))
    return result

def initialized_script_bank(original,bank,initializer):
    """Evaluate only the pinned constructor's integer/byte-store subset.

    These packed bytecode pointer operands were zero in the ROM; the original
    initializer writes native address bytes, including unaligned operands.
    They must be checked as real writes, not masked as arbitrary differences.
    """
    start=0x0208751c;expected=bytearray(original);variables={};writes={}
    for field in [*range(start+4,0x020876d4,8),*range(0x020876e4,0x020876fc,4)]:
        target=struct.unpack_from('<I',original,field-start)[0];assert start<=target<0x0208a0e0
        struct.pack_into('<I',expected,field-start,bank+target-start)
    source=initializer.read_text();body=source.split('void __sinit_02073e6c(void)',1)[1].split('{',1)[1]
    body=body.split('    *(void **)&data_0209b294[0]',1)[0]
    def evaluate(node):
        if isinstance(node,ast.Constant) and isinstance(node.value,int):return node.value
        if isinstance(node,ast.Name):
            if re.fullmatch(r'data_[0-9a-f]{8}',node.id):
                address=int(node.id[5:],16);assert start<=address<0x0208a0e0
                return bank+address-start
            return variables[node.id]
        if isinstance(node,ast.BinOp) and isinstance(node.op,ast.RShift):return evaluate(node.left)>>evaluate(node.right)
        raise AssertionError(('Unsupported initializer expression',ast.dump(node)))
    for line in body.splitlines():
        line=line.strip()
        if not line or line.startswith('int '):continue
        match=re.fullmatch(r'(\w+)(?:\[(0x[0-9a-f]+|[0-9]+)\])?\s*=\s*(.+);',line);assert match,line
        name,index,expr=match.groups();narrow='(u8)' in expr
        value=evaluate(ast.parse(re.sub(r'\((?:u8|unsigned int)\)','',expr).strip(),mode='eval').body)
        if narrow:value&=255
        if index is None:variables[name]=value;continue
        address=int(name[5:],16)+int(index,0);assert start<=address<0x0208a0e0
        expected[address-start]=value&255;writes[address]=value&255
    return expected,dict(byte_stores=len(writes),source_sha256=hashlib.sha256(initializer.read_bytes()).hexdigest())

def dump_files(vm_path):
    """Companion files of one dump: folder layout (memory.vm) or legacy stem."""
    vm_path=Path(vm_path)
    if vm_path.name=='memory.vm':
        d=vm_path.parent
        return dict(trace=d/'trace.bin',anchors=d/'anchors.json',render=d/'render.bin',upper=d/'upper.bin',
                    lower=d/'lower.bin',lcd=d/'lcd.bin',info=d/'info.txt')
    return {k:vm_path.with_suffix(s) for k,s in [('trace','.trace'),('anchors','.anchors.json'),('render','.render'),
            ('upper','.upper'),('lower','.lower'),('lcd','.lcd')]}

def find_dumps(game_directory):
    """Full dumps in sdmc:/3ds/sm64ds: legacy native-*.vm and dumps/NNN-dump-*/memory.vm."""
    game_directory=Path(game_directory)
    return sorted([*game_directory.glob('native-*.vm'),*game_directory.glob('dumps/*-dump-*/memory.vm')])

class VM:
    def __init__(self,path):
        self.path=Path(path);self.raw=self.path.read_bytes();self.regions=[]
        values=HEADER.unpack_from(self.raw)
        magic,version,size,word,region_size,begin,end,scene,epoch,count,complete,scan_end,devices=values
        assert magic==b'SM64VM1\0' and version in (1,2) and (size,word,region_size)==(64,4,24)
        assert complete==1 and count<=512 and end>=begin
        self.header=dict(scene=scene,epoch=epoch,begin_tick=begin,end_tick=end,
                         complete=True,scan_end=scan_end,non_atomic_devices=bool(devices),version=version)
        offset=size;address=0
        for _ in range(count):
            base,length,perm,state,payload,reason=REGION.unpack_from(self.raw,offset);offset+=region_size
            assert base==address and length and base+length<=scan_end
            device=version>=2 and reason==REGION_DEVICE_PAYLOAD and payload==length and perm&1 and state&255==2 and \
                any(lo<=base and base+length<=hi for lo,hi in DEVICE_RANGES)
            assert device or (payload==length and reason==0 and perm&1 and state&255 not in (0,1,2)) or (payload==0 and reason in (1,2))
            assert offset+payload<=len(self.raw)
            self.regions.append(dict(address=base,size=length,perm=perm,state=state,payload_bytes=payload,
                                     reason=reason,file_offset=offset))
            address+=length;offset+=payload
        assert address==scan_end and offset==len(self.raw),'Incomplete address coverage or trailing data'
    def read(self,address,size):
        assert 0<=address<=0xffffffff and 0<=size<=256*1024*1024
        chunks=[]
        while size:
            region=next((r for r in self.regions if r['address']<=address<r['address']+r['size']),None)
            assert region and region['payload_bytes'],('Address is not captured readable memory',hex(address))
            count=min(size,region['address']+region['size']-address)
            offset=region['file_offset']+address-region['address'];chunks.append(self.raw[offset:offset+count])
            address+=count;size-=count
        return b''.join(chunks)
    def u32(self,address):return struct.unpack('<I',self.read(address,4))[0]
    def vram(self):
        """Both 3MiB VRAM banks (VA0x1F000000), or None for version1 dumps."""
        try:return self.read(0x1F000000,0x600000)
        except AssertionError:return None

class LCD:
    """Physical LCD scanout captured before any dump painting (.lcd)."""
    def __init__(self,path):
        self.path=Path(path);raw=self.path.read_bytes()
        assert len(raw)>=64+3*64+4*256,'Missing LCD header, records or registers'
        magic,version,header_bytes,record_bytes,count,tick,scene,epoch,blocks,block_bytes,*_=LCD_HEADER.unpack_from(raw)
        assert magic==b'SM64LCD1' and (version,header_bytes,record_bytes,count)==(1,64,64,3) and (blocks,block_bytes)==(4,256)
        self.header=dict(tick=tick,scene=scene,epoch=epoch)
        offset=header_bytes;self.records=[]
        for i in range(count):
            fields=LCD_RECORD.unpack_from(raw,offset);offset+=record_bytes
            names=('screen','eye','valid','physical','virtual_address','format','select','stride','rows','columns','payload_bytes','status')
            self.records.append(dict(zip(names,fields[:12])))
        words=struct.unpack_from('<%dI'%(blocks*block_bytes//4),raw,offset);offset+=blocks*block_bytes
        self.registers={hex(base+i*4):words[b*64+i] for b,base in enumerate(LCD_REGISTER_BLOCKS) for i in range(64)}
        for index,r in enumerate(self.records):
            assert (r['screen'],r['eye'])==((0,0),(0,1),(1,0))[index],'Unexpected LCD screen/eye order'
            assert r['valid'] in (0,1),'Invalid LCD validity flag'
            r['payload_offset']=offset if r['valid'] else None
            if r['valid']:
                assert r['status']==0 and r['format']&7 in LCD_FORMATS,'Invalid LCD status or format'
                bpp=LCD_FORMATS[r['format']&7][1]
                assert 0<r['rows']<=240 and 0<r['columns']<=800,'Invalid LCD dimensions'
                assert r['rows']*bpp<=r['stride']<=4096 and not r['stride']&3,'Invalid LCD stride'
                assert r['payload_bytes']==r['stride']*r['columns'],'Invalid LCD payload size'
                assert r['physical'] and r['virtual_address'] and not r['virtual_address']&3,'Invalid LCD address'
                offset+=r['payload_bytes']
            else:
                assert r['payload_bytes']==0,'Invalid LCD record carries payload'
        assert offset==len(raw),'Truncated or trailing LCD payload'
        self.raw=raw
    def name(self,r):return 'bottom' if r['screen'] else 'top-'+('right' if r['eye'] else 'left')
    def rgb(self,r):
        """Rows of (R,G,B) in screen orientation: x left->right, y top->bottom."""
        assert r['valid']
        kind,bpp=LCD_FORMATS[r['format']&7];data=memoryview(self.raw)[r['payload_offset']:r['payload_offset']+r['payload_bytes']]
        rows,columns,stride=r['rows'],r['columns'],r['stride']
        image=[[None]*columns for _ in range(rows)]
        for x in range(columns):
            column=data[x*stride:x*stride+rows*bpp]
            for y in range(rows):
                p=(rows-1-y)*bpp
                if kind=='RGBA8':c=(column[p+3],column[p+2],column[p+1])
                elif kind=='BGR8':c=(column[p+2],column[p+1],column[p])
                else:
                    v=column[p]|column[p+1]<<8
                    if kind=='RGB565':c=((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31)
                    elif kind=='RGB5A1':c=((v>>11)*255//31,((v>>6)&31)*255//31,((v>>1)&31)*255//31)
                    else:c=((v>>12)*17,((v>>8)&15)*17,((v>>4)&15)*17)
                image[y][x]=c
        return image
    def export(self,directory):
        """Write one PNG per valid screen plus registers/records JSON."""
        from PIL import Image
        directory=Path(directory);directory.mkdir(parents=True,exist_ok=True);written=[]
        for r in self.records:
            if not r['valid']:continue
            pixels=self.rgb(r);image=Image.new('RGB',(r['columns'],r['rows']))
            image.putdata([c for row in pixels for c in row])
            target=directory/(self.path.stem+'.lcd-'+self.name(r)+'.png');image.save(target);written.append(str(target))
        meta=dict(header=self.header,records=[{k:v for k,v in r.items() if k!='payload_offset'} for r in self.records],
                  registers=self.registers,images=written,
                  scope='Display-controller scanout read through GSP after the worker drained; not an emulator window capture')
        (directory/(self.path.stem+'.lcd.json')).write_text(json.dumps(meta,indent=2)+'\n')
        return meta

def capture_media_report(vm,path,metadata):
    """Require the requested screen/VRAM evidence for v2; keep v1 readable."""
    if vm.header['version']<2:
        return dict(available=False,reason='Legacy v1 has no LCD/VRAM contract')
    vram=vm.vram()
    assert vram is not None and len(vram)==0x600000,'Incomplete VRAM: both 3 MiB banks are required'
    lcd=LCD(Path(path).with_suffix('.lcd'))
    assert metadata.get('physical_lcd') is True,'Capture did not report successful LCD acquisition'
    assert lcd.header['scene']==vm.header['scene'] and lcd.header['epoch']==vm.header['epoch'],'LCD/VM context mismatch'
    assert vm.header['begin_tick']<=lcd.header['tick']<=vm.header['end_tick'],'LCD is outside VM capture interval'
    assert lcd.records[0]['valid'] and lcd.records[2]['valid'],'Missing top-left or bottom scanout'
    for record in lcd.records:
        if record['valid']:
            # Establish memory coverage, not pixel equality: dump progress
            # deliberately replaces the bottom scanout after .lcd is saved.
            vm.read(record['virtual_address'],record['payload_bytes'])
    return dict(available=True,LCD_sha256=hashlib.sha256(lcd.raw).hexdigest(),
        screens=[lcd.name(r) for r in lcd.records if r['valid']],
        vram_bytes=len(vram),VRAM_sha256=hashlib.sha256(vram).hexdigest(),
        device_payload_bytes=sum(r['payload_bytes'] for r in vm.regions if r['reason']==REGION_DEVICE_PAYLOAD),
        console=metadata.get('console'),GPU_OS_DSP_atomic=False,physical_console_validation=False)

def trace_report(path):
    raw=path.read_bytes();magic,version,size,count,overwritten,frequency=TRACE_HEADER.unpack_from(raw)
    assert (magic,version,size) in [(b'SM64PR1\0',1,160),(b'SM64PR2\0',2,232)] and count<=4096 and frequency==268111856
    assert len(raw)==32+count*size
    rows=[]
    for i in range(count):
        context=0
        if version==1:tick,frame,epoch,scene,held,overflow,*deltas=TRACE_RECORD.unpack_from(raw,32+i*size)
        else:tick,frame,epoch,scene,held,context,overflow,*deltas=TRACE_RECORD_V2.unpack_from(raw,32+i*size)
        rows.append(dict(tick=tick,frame=frame,epoch=epoch,scene=scene,held=held,overflow=overflow,
                         context=trace_context(context),
                         intervals_ms={name:value*1000/frequency for name,value in zip(SLOTS,deltas)}))
    groups={}
    # The first row contains counters accumulated before the first retained job.
    for row in rows[1:]:
        if row['overflow']:continue
        groups.setdefault(str(row['scene']),[]).append(row)
    summaries={}
    for scene,items in groups.items():
        stats={}
        for name in items[0]['intervals_ms']:
            values=sorted(row['intervals_ms'][name] for row in items)
            if not values or not values[-1]:continue
            stats[name]=dict(mean_ms=statistics.mean(values),p95_ms=values[(len(values)-1)*95//100],max_ms=values[-1])
        summaries[scene]=dict(samples=len(items),intervals=stats)
    return dict(status='bounded_job_handoff_trace',record_count=count,overwritten=overwritten,scenes=summaries,
                scope_notes={'WorkerMesh':'Entire worker presentation call, including FrameBegin wait, drawing and composition; not geometry conversion alone.',
                             'WorkerCore':'Includes gpu_frame and its nested WorkerFrameBegin wait; not CPU-only geometry work.'},
                limits='Counters have overlapping scopes and cannot be added. Timestamps are result handoffs, not LCD presentation. Capture write time follows this ring and is excluded.',rows=rows)

def inspect(path,elf,rom=None):
    vm=VM(path);meta_path=dump_files(path)['anchors'];meta=json.loads(meta_path.read_text())
    assert meta['epoch']==vm.header['epoch'] and meta['scene']==vm.header['scene'] and meta['worker_drained']
    anchors=meta['anchors']
    result=dict(status='mapped_native_process_capture_validated',header=vm.header,
                VM_sha256=hashlib.sha256(vm.raw).hexdigest(),ELF_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                anchors=anchors,regions=vm.regions,captured_bytes=sum(r['payload_bytes'] for r in vm.regions),
                physical_FCRAM_dump=False,RAM_modified=False,GPU_OS_DSP_atomic=False,
                capture_media=capture_media_report(vm,path,meta))
    if 'data_0208ee44' in anchors:
        result['data_0208ee44']=dict(runtime_address=hex(anchors['data_0208ee44']),u32=vm.u32(anchors['data_0208ee44']))
    if rom:
        source=rom.read_bytes();start,end,base=0x0208751c,0x0208a0e0,0x02004000
        original=source[start-base:end-base];bank=anchors['data_0208751c'];actual=vm.read(bank,end-start)
        initializer=rom.parent.parent/'src/__sinit_02073e6c.c'
        expected,runtime_writes=initialized_script_bank(original,bank,initializer)
        fields=[*range(start+4,0x020876d4,8),*range(0x020876e4,0x020876fc,4)]
        for field in fields:
            target=struct.unpack_from('<I',original,field-start)[0];assert start<=target<end
            pointer=struct.unpack_from('<I',actual,field-start)[0]
            assert pointer==bank+target-start,('Runtime script pointer',hex(field),hex(pointer))
        assert actual==expected,'Mapped runtime script bytes differ from ROM plus original native initializer'
        sites=[0x02089ba4,0x02089bd0,0x02089c4c,0x02089cb0,0x02089cf0,0x02089d78]
        assert all(actual[address-start:address-start+4]==original[address-start:address-start+4] for address in sites)
        result['live_script_bank']=dict(bytes_compared=end-start,typed_pointers=len(fields),
            original_SHA256=hashlib.sha256(original).hexdigest(),runtime_bytewise_initializer=runtime_writes,
            six_previously_corrupted_words_intact=True)
    if 'data_0209f394' in anchors:
        players=[]
        for slot in range(4):
            address=vm.u32(anchors['data_0209f394']+slot*4)
            if not address:continue
            actor=vm.read(address,0x768)
            players.append(dict(slot=slot,address=hex(address),uid=struct.unpack_from('<I',actor,4)[0],
                                position_fixed=list(struct.unpack_from('<3i',actor,0x5c)),
                                object_sha256=hashlib.sha256(actor).hexdigest()))
        result['players']=players
    trace=dump_files(path)['trace']
    if trace.exists():result['profile']=trace_report(trace)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('snapshot',type=Path)
    parser.add_argument('--elf',type=Path,required=True);parser.add_argument('--arm9',type=Path)
    parser.add_argument('--export',type=Path,help='Write LCD PNGs, LCD/GPU register JSON and vram.bin here')
    args=parser.parse_args();result=inspect(args.snapshot,args.elf,args.arm9)
    if args.export:
        args.export.mkdir(parents=True,exist_ok=True)
        lcd=dump_files(args.snapshot)['lcd']
        if lcd.exists():result['physical_lcd']=LCD(lcd).export(args.export)
        vram=VM(args.snapshot).vram()
        if vram is not None:
            target=args.export/(args.snapshot.stem+'.vram.bin');target.write_bytes(vram)
            result['vram']=dict(path=str(target),bytes=len(vram),sha256=hashlib.sha256(vram).hexdigest())
    target=args.snapshot.with_suffix('.analysis.json');target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('regions','profile')},indent=2))
    if 'profile' in result:print(json.dumps(result['profile']['scenes'],indent=2))
    print(target)

if __name__=='__main__':main()
