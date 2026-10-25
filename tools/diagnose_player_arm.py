#!/usr/bin/env python3
"""Execute the rebuilt Player constructor against relocated ASMP0 data.

DIAGNOSTIC ONLY: missing functions stop execution at named traps. They never
return success or emulate a service. This executable cannot be packaged as a
3DS application. ROM data is read locally, kept non-executable, and all declared
data pointers are relocated. The original DS code is never mapped executable.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import zlib
import native_operators
import native_data_layout

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/player-diagnostic'
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
ROOTS=['_ZN6PlayerC1Ev','__sinit_02075154','__sinit_02074d90','sm64ds_restore_native_data']
PLAYER_INIT='_ZN6Player13InitResourcesEv'
PLAYER_STATIC_INIT='__sinit_ov002_021019d0'
HEAP_INIT='_ZN6sm64ds34initialize_game_root_heap_in_placeEPvj'
PLAYER_FACTORY='daPly_c_classInit'
FLAGS=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft']


def run(*args):
    subprocess.run([str(a) for a in args],cwd=ROOT,check=True)


def metadata():
    symbols={};addresses={};regions=[];relocs=[]
    for index,module in enumerate(('arm9','ov002','ov000','ov001','ov009')):
        folder=ROOT/'upstream/config/arm9'
        if module!='arm9': folder=folder/'overlays'/module
        for line in (folder/'symbols.txt').read_text().splitlines():
            m=re.match(r'(\S+) kind:(\S+) addr:0x([0-9a-f]+)',line)
            if m:
                record={'name':m[1],'kind':m[2],'module':module,'address':int(m[3],16)}
                symbols[m[1]]=record
                addresses.setdefault((module,record['address']),[]).append(record)
        # Whole declared storage sections avoid guessing a symbol's extent from
        # its next neighbour. No original .text or .init instructions are loaded.
        for line in (folder/'delinks.txt').read_text().splitlines():
            if line and not line.startswith(' '): break
            m=re.match(r'    (\.\S+)\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+) kind:(\w+)',line)
            if m and m[1] in ('.rodata','.data','.bss'):
                start,end=int(m[2],16),int(m[3],16)
                if end>start:
                    regions.append({'module':module,'section':m[1],'start':start,'end':end})
        for line in (folder/'relocs.txt').read_text().splitlines():
            m=re.match(r'from:0x([0-9a-f]+) kind:(\S+) to:0x([0-9a-f]+) module:(\S+)',line)
            if m:
                destination='arm9' if m[4]=='main' else m[4]
                overlay=re.fullmatch(r'overlay\((\d+)\)',m[4])
                if overlay: destination='ov'+overlay[1].zfill(3)
                relocs.append((module,int(m[1],16),m[2],destination,int(m[3],16)))
    # This arm9 spelling denotes the overlay-1 cylinder-shadow template, not
    # bytes after the arm9 BSS. Both aliases are named in the pinned metadata.
    alias=symbols['data_ov001_020ad560']
    symbols['data_020ad560']={**alias,'name':'data_020ad560'}
    addresses[('arm9',alias['address'])]=[symbols['data_020ad560']]
    # Keep metadata for unmounted modules so an unreachable branch can link to
    # an explicitly unmapped data address. It must still fault on first use;
    # this does not allocate zero-filled substitutes for missing game data.
    for file in (ROOT/'upstream/config/arm9').rglob('symbols.txt'):
        module=file.parent.name
        for line in file.read_text().splitlines():
            m=re.match(r'(\S+) kind:(\S+) addr:0x([0-9a-f]+)',line)
            if m and m[1] not in symbols:
                symbols[m[1]]={'name':m[1],'kind':m[2],'module':module,'address':int(m[3],16)}
    return symbols,addresses,regions,relocs


def storage(regions,module,address):
    for region in regions:
        if region['module']==module and region['start']<=address<region['end']:
            return region['native']+address-region['start']
    return None


def elf_symbols(path):
    result={}
    for line in subprocess.check_output([TOOL+'nm','-g','--defined-only',str(path)],text=True).splitlines():
        p=line.split()
        if len(p)==3: result[p[2]]=int(p[0],16)
    return result


def build(symbols,regions):
    (OUT/'native_data.S').write_text(native_data_layout.assembly(regions))
    run(TOOL+'gcc',*FLAGS,'-c',OUT/'native_data.S','-o',OUT/'native_data.o')
    extras=['build/3ds/source/ctr/CMakeFiles/sm64ds_game_math.dir/game_math.o',
        'build/3ds/CMakeFiles/sm64ds_math.dir/source/core/fixed_math.o',
        'build/3ds/source/ctr/CMakeFiles/sm64ds_game_primitives.dir/game_primitives.o',
        'build/3ds/source/ctr/CMakeFiles/sm64ds_game_primitives.dir/game_memory_primitives.o',
        'build/native-collision/sphere.o','build/native-collision/line.o',
        'build/native-collision/bridge.o',str(OUT/'native_data.o'),
        'build/3ds/CMakeFiles/sm64ds_native_data.dir/source/core/native_data.o']
    replacements=json.loads((ROOT/'build/native-collision/symbols.json').read_text())['replaced_sources']
    if 'src/NormalizeVec3.c' not in replacements: replacements.append('src/NormalizeVec3.c')
    if 'src/func_02049018.c' not in replacements: replacements.append('src/func_02049018.c')
    extras+=['build/3ds/CMakeFiles/sm64ds_game_geometry.dir/source/core/game_geometry.o']
    if PLAYER_INIT in ROOTS:
        run(TOOL+'g++',*FLAGS,'-Os','-ffunction-sections','-fdata-sections','-Wa,--noexecstack',
            '-Iinclude','-c','tests/game_geometry_arm.cpp','-o',OUT/'geometry_wrappers.o')
        extras+=['build/3ds/source/ctr/CMakeFiles/sm64ds_game_geometry_bridge.dir/game_geometry.o',
            'build/3ds/CMakeFiles/sm64ds_game_files.dir/source/ctr/game_file_bridge.o',
            'build/native-heap/bridge.o','build/native-collision/wrappers.o',str(OUT/'geometry_wrappers.o')]
        replacements+=['src/_ZN13SharedFilePtr4LoadEv.cpp',
            'src/_ZN2GX13SetBankForTexEt.cpp','src/_ZN2GX17SetBankForTexPlttEt.cpp',
            'src/_ZN2GX12BeginLoadTexEv.cpp','src/_ZN2GX10EndLoadTexEv.cpp',
            'src/_ZN2GX7LoadTexEPKvjj.cpp','src/_ZN2GX16BeginLoadTexPlttEv.cpp',
            'src/_ZN2GX14EndLoadTexPlttEv.cpp','src/_ZN2GX11LoadTexPlttEPKvjj.cpp',
            'src/func_02053e34.c','src/func_02053e1c.c']
    if '_ZN6Player6RenderEv' in ROOTS:
        extras+=['build/3ds/CMakeFiles/sm64ds_game_draw.dir/source/core/game_draw.o',
                 'build/3ds/source/ctr/CMakeFiles/sm64ds_game_draw_bridge.dir/game_draw.o']
        replacements+=['src/Geometry_MatrixMultiply3x3.c','src/func_020553a4.c',
                       'src/func_02055388.c','src/func_02055998.c','src/func_01ffde98.c']
    command=[sys.executable,'tools/native_core_inventory.py','--roots-only',
        '--output-directory',OUT,'--link-frontier']
    for name in ROOTS: command+=['--root-symbol',name]
    for index in range(len(regions)):
        command+=['--root-symbol',native_data_layout.section_name(index)]
    for obj in extras: command+=['--extra-object',obj]
    for source in replacements: command+=['--replace-source',source]
    run(*command)
    frontier=json.loads((OUT/'symbols.json').read_text())['playable']
    if frontier['strong_conflicts']: raise ValueError('Ambiguous native function ownership')
    # Imported data belongs to the executable's BSS, at linker-selected native
    # addresses. Unmounted data still uses diagnostic-only unmapped addresses.
    definitions=[];unmounted_globals=[]
    for name in frontier['unresolved']:
        record=symbols.get(name)
        if record and not record['kind'].startswith('function'):
            expression=native_data_layout.expression(regions,record['module'],record['address'])
            if expression is None:
                address=0x35000000+len(unmounted_globals)*4096
                unmounted_globals.append({'symbol':name,'module':record['module'],
                    'original_address':record['address'],'unmapped_native_address':address})
                expression=hex(address)
            definitions.append(f'--defsym={name}={expression}')
        elif re.fullmatch(r'overlay_\d+',name):
            definitions.append(f'--defsym={name}={int(name[8:])}')
        elif name.startswith('data_'):
            raise ValueError(f'Missing original data metadata: {name}')
    # Keep scalar game allocation distinct from the C++ library allocator.
    objects=shlex.split((OUT/'frontier-objects.rsp').read_text())
    isolated=OUT/'objects';isolated.mkdir(exist_ok=True)
    copied=[]
    for obj in objects:
        dest=isolated/(hashlib.sha256(obj.encode()).hexdigest()[:16]+'.o')
        native_operators.copy_isolated(TOOL,obj,dest,ROOT)
        copied.append(str(dest))
    native_operators.verify_isolated(TOOL,copied,ROOT)
    rsp=OUT/'diagnostic-objects.rsp'
    rsp.write_text('\n'.join('"'+o+'"' for o in copied)+'\n')
    linker=OUT/'data-symbols.rsp';linker.write_text('\n'.join(definitions)+'\n')
    traps=set()
    for attempt in range(5):
        assembly=['.syntax unified','.arm','.section .text.diagnostic_traps,"ax",%progbits']
        for name in sorted(traps):
            if not re.fullmatch(r'[\w.$]+',name): raise ValueError('Unsafe symbol '+name)
            assembly += [f'.global {name}',f'.type {name},%function',f'{name}:','udf #0']
        assembly+=['.section .note.GNU-stack,"",%progbits']
        (OUT/'traps.S').write_text('\n'.join(assembly)+'\n')
        run(TOOL+'gcc',*FLAGS,'-c',OUT/'traps.S','-o',OUT/'traps.o')
        link=['--gc-sections','--no-demangle','-z','noexecstack','--fatal-warnings',
              '-e',ROOTS[0],'-Ttext=0x100000','-Map='+str(OUT/'diagnostic.map'),'@'+str(linker)]
        for name in ROOTS[1:]: link+=['-u',name]
        for index in range(len(regions)): link+=['-u',native_data_layout.section_name(index)]
        command=[TOOL+'g++',*FLAGS,'-nostartfiles','-Wl,'+','.join(link),
                 '@'+str(rsp),str(OUT/'traps.o'),'-o',str(OUT/'diagnostic.elf')]
        result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
        (OUT/'link.log').write_text(result.stdout+result.stderr)
        if result.returncode==0: break
        missing=set(re.findall(r"undefined reference to [`']([^']+)'",result.stderr))
        if not missing or missing<=traps: raise RuntimeError(result.stderr[-10000:])
        if any(n.startswith(('data_','_ZTV','_ZTI','_ZTS')) for n in missing):
            raise ValueError('Missing data cannot become a function trap: '+str(missing))
        traps|=missing
    else: raise RuntimeError('Diagnostic link did not converge')
    linked=elf_symbols(OUT/'diagnostic.elf')
    missing_roots=[name for name in ROOTS if name not in linked or name in traps]
    if missing_roots: raise ValueError('Requested original entry points did not link: '+str(missing_roots))
    result={'scope':'Diagnostic only; every missing function is a non-returning named trap',
        'roots':ROOTS,'trap_symbols':sorted(traps),'data_definitions':definitions,
        'isolated_game_operators':True,'symbol_isolation':native_operators.SYMBOLS,
        'data_storage':'Linker-owned BSS; ROM data loaded by native transaction',
        'unmounted_globals':unmounted_globals,
        'elf_sha256':hashlib.sha256((OUT/'diagnostic.elf').read_bytes()).hexdigest(),
        'upstream_commit':json.loads((ROOT/'build/native-core/report.json').read_text())['upstream_commit']}
    if '_ZN6Player8BehaviorEv' in ROOTS:
        result['walk_provider']={'source':'upstream/port/unmatched/func_ov002_020d3b9c.c',
            'byte_matched':False,'sha256':hashlib.sha256((ROOT/'upstream/port/unmatched/func_ov002_020d3b9c.c').read_bytes()).hexdigest()}
    (OUT/'build.json').write_text(json.dumps(result,indent=2)+'\n')


def execute(rom_path,symbols,addresses,regions,relocs,ticks=0,isolated_input=False,draw=False):
    from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_PROT_READ,UC_PROT_WRITE,UC_PROT_EXEC,UC_HOOK_CODE,UC_HOOK_MEM_INVALID,UC_HOOK_MEM_WRITE
    from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
        UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,
        UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_CPSR)
    sys.path.insert(0,str(ROOT/'build/game-data/python'))
    import ndspy.rom,ndspy.codeCompression,ndspy.narc,ndspy.lz10
    raw=rom_path.read_bytes()
    if zlib.crc32(raw)!=0x29715dec: raise ValueError('Expected verified ASMP0 ROM, CRC32 29715dec')
    rom=ndspy.rom.NintendoDSRom(raw)
    if rom.idCode!=b'ASMP' or rom.version!=0: raise ValueError('Unsupported ROM')
    images={'arm9':(rom.arm9RamAddress,ndspy.codeCompression.decompress(rom.arm9))}
    images.update({f'ov{i:03d}':(v.ramAddress,bytes(v.data)) for i,v in rom.loadArm9Overlays().items()})
    native=elf_symbols(OUT/'diagnostic.elf')
    for index,region in enumerate(regions):
        region['native']=native[native_data_layout.section_name(index)]
    build_info=json.loads((OUT/'build.json').read_text())
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    # Parse the ELF32 PT_LOAD segments, including actual linker BSS. Code and
    # reconstructed data have disjoint ranges and original DS IO remains unmapped.
    elf=(OUT/'diagnostic.elf').read_bytes()
    if hashlib.sha256(elf).hexdigest()!=build_info['elf_sha256']:
        raise ValueError('Diagnostic ELF changed after its manifest was written')
    phoff=struct.unpack_from('<I',elf,28)[0]
    phsize,phnum=struct.unpack_from('<HH',elf,42)
    pages={}
    segments=[]
    for i in range(phnum):
        typ,off,va,pa,filesz,memsz,flags,align=struct.unpack_from('<8I',elf,phoff+i*phsize)
        if typ!=1 or not memsz: continue
        prot=(UC_PROT_READ if flags&4 else 0)|(UC_PROT_WRITE if flags&2 else 0)|(UC_PROT_EXEC if flags&1 else 0)
        for page in range(va&~4095,(va+memsz+4095)&~4095,4096): pages[page]=pages.get(page,0)|prot
        segments.append((va,off,filesz))
    for page,prot in pages.items(): cpu.mem_map(page,4096,prot)
    for va,off,size in segments: cpu.mem_write(va,elf[off:off+size])
    # Host I/O stages only declared data slices. The actual ARM11 transaction
    # performs all writes/relocations into the executable's owned storage.
    staging=0x70000000
    cpu.mem_map(staging,0x100000,UC_PROT_READ|UC_PROT_WRITE)
    staging_cursor=staging
    section_descriptors=[];expected_sections=[]
    for region in regions:
        start=region['native'];size=region['end']-region['start']
        assert all(page in pages and not pages[page]&UC_PROT_EXEC
                   for page in range(start&~4095,(start+size+4095)&~4095,4096))
        source=0;zero_fill=region['section']=='.bss'
        expected=bytearray(size)
        if not zero_fill:
            base,data=images[region['module']];off=region['start']-base
            if off<0 or off+size>len(data): raise ValueError('Original data section outside ROM image')
            expected[:]=data[off:off+size]
            source=staging_cursor;cpu.mem_write(source,bytes(expected))
            staging_cursor=(staging_cursor+size+31)&~31
        section_descriptors.append((start,source,size,int(zero_fill)))
        expected_sections.append(expected)
        cpu.mem_write(start,bytes([0xa5])*size)
    # Missing indirect methods receive their own UDF address, with identifying
    # metadata; the test halts if any of these methods is actually called.
    trap_base=0x20000000;cpu.mem_map(trap_base,0x10000,UC_PROT_READ|UC_PROT_EXEC)
    traps={native[n]:n for n in build_info['trap_symbols'] if n in native}
    indirect={};poison={}
    def resolve(module,address):
        overlay_set=re.fullmatch(r'overlays\(([^)]+)\)',module)
        if overlay_set:
            options=[f'ov{int(n):03d}' for n in overlay_set[1].split(',')]
            mounted=[name for name in options if storage(regions,name,address) is not None]
            if len(mounted)==1: module=mounted[0]
        records=addresses.get((module,address),[])
        funcs=[r for r in records if r['kind'].startswith('function')]
        if funcs:
            for record in funcs:
                name=native_operators.SYMBOLS.get(record['name'],record['name'])
                if name in native: return native[name]
            key=(module,address)
            if key not in indirect:
                dest=trap_base+4*len(indirect)
                if dest>=trap_base+0x10000: raise ValueError('Too many indirect traps')
                indirect[key]=dest;traps[dest]=funcs[0]['name'];cpu.mem_write(dest,bytes.fromhex('f000f0e7'))
            return indirect[key]
        # A native object definition takes precedence over an imported data
        # alias (e.g. math state). Preserve GCC-emitted vtable address points.
        for record in records:
            if record['name'] in native and not record['name'].startswith('_ZTV'):
                return native[record['name']]
        if module=='arm9' and address==0x020ad560:
            return storage(regions,'ov001',address)
        dest=storage(regions,module,address)
        if dest is not None: return dest
        # Fail on first access to a pointer whose source module/type is still
        # unsupported; do not manufacture initialized storage for it.
        key=(module,address)
        if key not in poison: poison[key]=0x30000000+len(poison)*4096
        return poison[key]
    patches=[]
    for module,origin,kind,target_module,target in relocs:
        match=native_data_layout.owner(regions,module,origin)
        if match is None: continue
        index,region=match;offset=origin-region['start']
        if kind!='load': raise ValueError('Unexpected relocation in original data')
        if struct.unpack_from('<I',expected_sections[index],offset)[0]!=target:
            raise ValueError(f'Relocation source word mismatch: {module}:{origin:x}')
        replacement=resolve(target_module,target)
        patches.append((index,offset,target,replacement))
        struct.pack_into('<I',expected_sections[index],offset,replacement)
    patches.sort()
    relocated=len(patches)
    descriptor_address=staging_cursor
    descriptor_bytes=b''.join(struct.pack('<4I',*s) for s in section_descriptors)
    cpu.mem_write(descriptor_address,descriptor_bytes)
    fixup_address=descriptor_address+len(descriptor_bytes)
    fixup_bytes=b''.join(struct.pack('<4I',*p) for p in patches)
    if fixup_address+len(fixup_bytes)>staging+0x100000: raise ValueError('Data staging buffer exceeded')
    cpu.mem_write(fixup_address,fixup_bytes)
    stack=0x500000;stop=0x600000;arena=0x700000
    cpu.mem_map(stack,0x10000,3);cpu.mem_map(stop,4096,5);cpu.mem_map(arena,0x10000,3)
    heap_arena=0x40000000;file_arena=0x50000000
    cpu.mem_map(heap_arena,8*1024*1024,3);cpu.mem_map(file_arena,1024*1024,3)
    file_requests=[]
    expected_geometry=[bytearray(512*1024),bytearray(96*1024)]
    geometry_transfers=[]
    if 'sm64ds_game_file_span' in native:
        # Only platform I/O is host-supplied. The compiled bridge calls the
        # original allocator, copies bytes and updates the real SharedFilePtr.
        cpu.mem_write(native['sm64ds_game_file_span'],bytes.fromhex('1eff2fe1')) # bx lr
    archive=ndspy.narc.NARC(rom.getFileByName('ARCHIVE/arc0.narc'))
    trace=[];visited=set();trap_hits=[]
    by_address={value:key for key,value in native.items()}
    def hook(uc,address,size,user):
        if address in by_address:
            name=by_address[address];visited.add(name)
            trace.append(name)
            if len(trace)>64: del trace[0]
        uploads=('_ZN2GX7LoadTexEPKvjj','_ZN2GX11LoadTexPlttEPKvjj')
        for palette,name in enumerate(uploads):
            if address!=native.get(name): continue
            source=uc.reg_read(UC_ARM_REG_R0)
            offset=uc.reg_read(UC_ARM_REG_R1)
            length=uc.reg_read(UC_ARM_REG_R2)
            if offset+length>len(expected_geometry[palette]):
                raise ValueError('Original model upload exceeds native geometry storage')
            payload=bytes(uc.mem_read(source,length)) if length else b''
            expected_geometry[palette][offset:offset+length]=payload
            geometry_transfers.append({'palette':bool(palette),'offset':offset,
                'bytes':length,'sha256':hashlib.sha256(payload).hexdigest()})
        if address==native.get('sm64ds_game_file_span'):
            file_id=uc.reg_read(UC_ARM_REG_R0)&0xffff
            if file_id<0x8000:
                if file_id>=len(rom.files): raise ValueError('FAT ID out of range')
                data=bytes(rom.files[file_id]);path=rom.filenames.filenameOf(file_id)
                if path is None: raise RuntimeError(f'Game requested unnamed FAT entry {file_id}; check original SharedFilePtr initialization')
            elif file_id<0x80c6:
                data=bytes(archive.files[file_id-0x8000]);path=archive.filenames.filenameOf(file_id-0x8000)
            else: raise RuntimeError(f'Unsupported archive request {file_id:04x}')
            if data.startswith(b'LZ77'): data=bytes(ndspy.lz10.decompress(data[4:]))
            if not data or len(data)>1024*1024: raise ValueError('File outside diagnostic I/O buffer limit')
            uc.mem_write(file_arena,data)
            uc.mem_write(uc.reg_read(UC_ARM_REG_R1),struct.pack('<I',file_arena))
            uc.mem_write(uc.reg_read(UC_ARM_REG_R2),struct.pack('<I',len(data)))
            uc.reg_write(UC_ARM_REG_R0,1)
            file_requests.append({'id':file_id,'path':path,'bytes':len(data),
                                  'sha256':hashlib.sha256(data).hexdigest()})
            return
        if address in traps:
            trap_hits.append(traps[address])
            raise RuntimeError('Unimplemented function reached: '+traps[address])
    cpu.hook_add(UC_HOOK_CODE,hook)
    invalid_accesses=[]
    def invalid_memory(uc,access,address,size,value,user):
        invalid_accesses.append({'access':access,'address':address,'size':size,
                                'pc':uc.reg_read(UC_ARM_REG_PC)})
        return False
    cpu.hook_add(UC_HOOK_MEM_INVALID,invalid_memory)
    watched_player=0;position_writes=[]
    def record_position(uc,access,address,size,value,user):
        if watched_player and watched_player+0x5c<=address<watched_player+0x68 and len(position_writes)<128:
            position_writes.append({'offset':address-watched_player,'value':value&0xffffffff,
                'pc':uc.reg_read(UC_ARM_REG_PC),'recent_functions':trace[-8:]})
    cpu.hook_add(UC_HOOK_MEM_WRITE,record_position)
    saved=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,
        UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
    calls=[]
    def call(name,*args):
        cpu.reg_write(UC_ARM_REG_CPSR,0x10)
        for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args): cpu.reg_write(reg,value&0xffffffff)
        cpu.reg_write(UC_ARM_REG_SP,stack+0xf000);cpu.reg_write(UC_ARM_REG_LR,stop)
        for i,reg in enumerate(saved): cpu.reg_write(reg,0x12340000+i)
        calls.append(name)
        cpu.emu_start(native[name],stop,count=10000000 if name=='sm64ds_restore_native_data' else 2000000)
        assert cpu.reg_read(UC_ARM_REG_PC)==stop,'Instruction budget exceeded'
        assert cpu.reg_read(UC_ARM_REG_SP)==stack+0xf000
        assert all(cpu.reg_read(reg)==0x12340000+i for i,reg in enumerate(saved))
        return cpu.reg_read(UC_ARM_REG_R0)
    def data_address(name):
        record=symbols[name]
        return native.get(name,storage(regions,record['module'],record['address']))
    def write(name,value,fmt='<I'): cpu.mem_write(data_address(name),struct.pack(fmt,value))
    report={'scope':'ARM11 Player constructor diagnostic in Unicorn, ASMP0 only; no frame rate or playability proof',
        'elf_sha256':hashlib.sha256(elf).hexdigest(),'rom_sha256':hashlib.sha256(raw).hexdigest(),
        'relocated_data_pointers':relocated,'nonreturning_direct_traps':len(build_info['trap_symbols']),
        'nonreturning_indirect_traps':len(indirect),'unmapped_data_references':len(poison),
        'original_ds_code_executable':False,'ds_io_mapped':False}
    try:
        # Rejection at the last fixup must preserve ALL destination sections.
        last_expected=fixup_address+len(fixup_bytes)-8
        cpu.mem_write(last_expected,struct.pack('<I',patches[-1][2]^1))
        assert call('sm64ds_restore_native_data',descriptor_address,len(regions),fixup_address,relocated)==5
        assert all(bytes(cpu.mem_read(r['native'],r['end']-r['start']))==bytes([0xa5])*(r['end']-r['start']) for r in regions)
        cpu.mem_write(last_expected,struct.pack('<I',patches[-1][2]))
        assert call('sm64ds_restore_native_data',descriptor_address,len(regions),fixup_address,relocated)==0
        assert all(bytes(cpu.mem_read(r['native'],len(expected)))==expected for r,expected in zip(regions,expected_sections))
        report['native_data_transaction']={'sections':len(regions),'bytes':sum(len(s) for s in expected_sections),
            'fixups':relocated,'owned_by_linker':True,'late_rejection_preserves_all_sections':True,
            'all_bytes_match_independent_relocation':True,'loader_functions':sorted(visited)}
        visited.clear();trace.clear()
        for name in ('__sinit_02075154','__sinit_02074d90'): call(name)
        # Same actor-table binding as func_0201a054; per-spawn context is the
        # caller's responsibility, exactly as in the original Spawn routine.
        write('data_020a4bb8',data_address('ACTOR_SPAWN_TABLE'))
        write('data_020a4b54',0xbf,'<H')
        profile=struct.unpack('<I',cpu.mem_read(data_address('ACTOR_SPAWN_TABLE')+0xbf*4,4))[0]
        assert profile==data_address('g_profile_PLAYER'),'Player spawn profile is not bound to its original overlay data'
        write('data_020a4b60',0x12345678)
        write('data_0209b44c',0,'<B')
        pos=arena+0x4000;rot=pos+16
        position=(100*4096,254*4096,-100*4096);rotation=(0,16384,0)
        cpu.mem_write(pos,struct.pack('<3i',*position));cpu.mem_write(rot,struct.pack('<3h',*rotation))
        write('data_0209b460',pos);write('data_0209b45c',rot)
        # Pattern-fill catches constructors that simply appear to work because
        # the test supplied a zero-filled Player object.
        cpu.mem_write(arena,bytes([0xa5])*0x800)
        before_id=struct.unpack('<I',cpu.mem_read(data_address('data_02099e70'),4))[0]
        result=call(ROOTS[0],arena)
        assert result==arena
        assert struct.unpack('<3i',cpu.mem_read(arena+0x5c,12))==position
        assert struct.unpack('<3h',cpu.mem_read(arena+0x8c,6))==rotation
        assert struct.unpack('<3h',cpu.mem_read(arena+0x92,6))==rotation
        assert struct.unpack('<I',cpu.mem_read(arena+4,4))[0]==before_id
        assert struct.unpack('<I',cpu.mem_read(arena+8,4))[0]==0x12345678
        assert bytes(cpu.mem_read(arena+0x768,0x98))==bytes([0xa5])*0x98
        expected_matrix=bytes(cpu.mem_read(data_address('IDENTITY_MATRIX4X3'),48))
        for offset in (0xf0,0x174):
            assert bytes(cpu.mem_read(arena+offset+0x1c,48))==expected_matrix
        report.update(status='constructor_passed',position_fixed12=position,
            actor_profile_id=0xbf,
            actor_unique_id=before_id,callee_saved_registers_preserved=True,
            canary_preserved=True,completed_calls=list(calls),traps_reached=0)
        if PLAYER_STATIC_INIT in ROOTS:
            assert call(HEAP_INIT,heap_arena,8*1024*1024)==1
            write('data_020a0eac',heap_arena)
            # These are the exact texture/palette bank masks selected by
            # Stage::SetVramBanks; other 2D banks are outside this diagnostic.
            call('_ZN2GX13SetBankForTexEt',3)
            call('_ZN2GX17SetBankForTexPlttEt',0x30)
            call('InitialiseVramGlobals')
            # The table is built from the verified source ROM's logical handle
            # catalog. This is the same mapping func_ov000_020aa420 constructs
            # through NitroFS name lookups; no US numeric offsets are inferred.
            import importlib.util
            spec=importlib.util.spec_from_file_location('diagnostic_catalog',ROOT/'upstream/tools/asset_catalog.py')
            catalog=importlib.util.module_from_spec(spec);sys.modules[spec.name]=catalog;spec.loader.exec_module(catalog)
            _,handles=catalog.catalogs_from_rom(rom_path)
            assert len(handles)==0x80a and [h.handle for h in handles]==list(range(0x80a))
            table=arena+0x5000
            cpu.mem_write(table,struct.pack('<2058H',*[h.file_id for h in handles]))
            write('data_0209d3b8',table)
            report['phase']='original_player_static_initialization'
            initialized=[]
            for name in ROOTS:
                if name.startswith('__sinit_ov002_'):
                    call(name);initialized.append(name)
            report['overlay2_initializers_completed']=initialized
            report['static_initialization_passed']=True
            # Register the real castle collider through the original world
            # registry, so Player's own DetectClsn dispatch reaches the terrain.
            castle_dir=OUT/'castle';castle_dir.mkdir(exist_ok=True)
            run(ROOT/'build/sanitize/test_collision_data',ROOT/'build/europe.pak',castle_dir)
            kcl=(castle_dir/'castle.kcl').read_bytes();clps=(castle_dir/'castle.clps').read_bytes()
            collision_data=0x60000000;cpu.mem_map(collision_data,0x30000,3)
            cpu.mem_write(collision_data,kcl);cpu.mem_write(collision_data+0x20000,clps)
            collider=arena+0x7000
            assert call('native_collision_init',collider,collision_data,collision_data+0x20000)==1
            assert call('_ZN4dBgW6EnableEP8dActor_c',collider,0)==1
            lower=call('_ZNK7dBgW_Kc16GetOctreeOriginYEv',collider)
            upper=call('_ZNK7dBgW_Kc13GetUnkOctreeYEv',collider)
            call('func_0202a850',lower,upper)
            report['castle_collision_registered']=True
            report['castle_kcl_sha256']=hashlib.sha256(kcl).hexdigest()
            # The resource phase needs the normal single-player spawn context;
            # the constructor test above deliberately used a nontrivial param.
            for name in ('__sinit_02075154','__sinit_02074d90'): call(name)
            resource_position=tuple(int(v)*4096 for v in
                (castle_dir/'entrances.csv').read_text().splitlines()[0].split(','))
            cpu.mem_write(pos,struct.pack('<3i',*resource_position))
            write('data_020a4b60',3|(3<<3))
            player=call(PLAYER_FACTORY)
            assert heap_arena<player<heap_arena+8*1024*1024
            report['factory_player_address']=player
            write('data_0209f2f8',1,'<b')
            # Stage selects this descriptor from its original sublevel table.
            # It supplies the actual start-camera flag consumed by Player.
            level_pointer=struct.unpack('<I',cpu.mem_read(data_address('data_02092208')+4,4))[0]
            assert storage(regions,'ov009',0x02112bdc)==level_pointer
            write('data_0209f340',level_pointer)
            if ticks and not isolated_input:
                # Explicit single-player diagnostic session context, normally
                # supplied by the title/Stage startup. This does not execute
                # or claim verification of that startup/menu sequence.
                session_flags=struct.unpack('<I',cpu.mem_read(data_address('data_0209caa0')+8,4))[0]
                cpu.mem_write(data_address('data_0209caa0')+8,struct.pack('<I',session_flags|0x80))
                write('data_0209f21c',1,'<B')
            report['phase']='original_player_resource_initialization'
            report['init_resources_result']=call(PLAYER_INIT,player)
            report['player_position_after_init']=struct.unpack('<3i',cpu.mem_read(player+0x5c,12))
            assert report['init_resources_result']==1
            assert report['player_position_after_init']==resource_position,'Player did not initialize on the original castle ground'
            report['status']='resources_passed'
            if ticks:
                # Stage normally owns these 12-byte area records and camera.
                # One visible area + fixed translated camera is fixture data;
                # the actual Clipper constructor and actor gates still execute.
                area=arena+0x6000
                cpu.mem_write(area,struct.pack('<IB7x',0,1))
                write('data_0209f314',area)
                cpu.mem_write(data_address('data_0209b3ec'),expected_matrix)
                camera_translation=(-resource_position[0]//8,-resource_position[1]//8,
                                    -resource_position[2]//8-150*4096)
                cpu.mem_write(data_address('data_0209b3ec')+36,struct.pack('<3i',*camera_translation))
                call('_ZN7ClipperC1Ev',data_address('data_0209f43c'))
                write('data_020a4b54',0x14c,'<H')
                camera=call('dCamera_c_classInit')
                assert heap_arena<camera<heap_arena+8*1024*1024
                write('data_0209f318',camera)
                write('data_020a4b54',0xbf,'<H')
                report['camera_fixture']='Original Camera factory + Clipper constructor; fixed view, camera resource initialization and behavior not executed'
                if isolated_input:
                    # Independent behavior fixture, NOT a normal session boot.
                    # The regular --ticks path still tests (and currently stops
                    # inside) character audio startup. Do not mask that failure.
                    session_flags=struct.unpack('<I',cpu.mem_read(data_address('data_0209caa0')+8,4))[0]
                    cpu.mem_write(data_address('data_0209caa0')+8,struct.pack('<I',session_flags|0x80))
                    write('data_0209f21c',1,'<B')
                    report['startup_fixture']='Isolated Player: input enabled AFTER resources; character audio and normal Stage boot not initialized or verified'
                report['phase']='original_player_behavior'
                report['tick_samples']=[]
                if draw: report['draw_samples']=[]
                report['input_fixture']='Original CheckCameraInput + CheckInput: 60 idle, 60 DS right, then released; analog integration not tested here'
                for tick in range(ticks):
                    watched_player=player if tick==0 else 0
                    held=0x10 if 60<=tick<120 else 0
                    cpu.mem_write(data_address('data_020a0e58'),struct.pack('<HH',held,held if tick==60 else 0))
                    call('_ZN5Stage16CheckCameraInputEv')
                    call('_ZN5Stage10CheckInputEv',0)
                    assert call('_ZN8dActor_c14BeforeBehaviorEv',player)==1,'Original actor lifecycle rejected behavior'
                    result=call('_ZN6Player8BehaviorEv',player)
                    assert result==1
                    call('_ZN8dActor_c13AfterBehaviorEj',player,2)
                    report['tick_samples'].append({'tick':tick,'position':struct.unpack('<3i',cpu.mem_read(player+0x5c,12)),
                        'state':struct.unpack('<I',cpu.mem_read(player+0x370,4))[0],
                        'speed_fixed12':struct.unpack('<5i',cpu.mem_read(player+0x98,20)),
                        'control':struct.unpack('<8h',cpu.mem_read(data_address('data_0209f498'),16))})
                    sample=report['tick_samples'][-1]
                    assert sample['control'][3]==(0x10 if tick==60 else 0),'Pressed edge persisted across frames'
                    if tick<60: assert sample['position']==resource_position,'Idle Player moved from the original entrance'
                    if draw:
                        call('sm64ds_draw_reset')
                        assert call('_ZN6Player6RenderEv',player)==1
                        assert call('sm64ds_draw_ok')==1,'Original draw exceeded native command buffer bounds'
                        word_count=call('sm64ds_draw_count')
                        word_address=call('sm64ds_draw_entries')
                        stream=bytes(cpu.mem_read(word_address,word_count*8))
                        report['draw_samples'].append({'tick':tick,'words':word_count,
                            'sha256':hashlib.sha256(stream).hexdigest()})
                        if tick in (0,59,60,90,119,179):
                            frame=OUT/f'frame-{tick:03d}';frame.mkdir(exist_ok=True)
                            (frame/'commands.bin').write_bytes(stream)
                            for palette,kind in enumerate(('textures','palettes')):
                                pointer=call('native_geometry_storage',palette)
                                (frame/(kind+'.bin')).write_bytes(cpu.mem_read(pointer,len(expected_geometry[palette])))
                if ticks>=120:
                    after_walk=report['tick_samples'][119]['position']
                    assert after_walk[0]>resource_position[0]+4096,'Right input did not move Player along the map'
                    assert all(s['position'][1:]==resource_position[1:] for s in report['tick_samples'][:120]),'Player left the flat entrance floor unexpectedly'
                    report['movement_verification']={'idle_ticks_stationary':60,'right_ticks':60,
                        'start_fixed12':resource_position,'after_walk_fixed12':after_walk,
                        'horizontal_displacement':(after_walk[0]-resource_position[0])/4096,
                        'castle_ground_height_maintained':True,'button_edges_correct':True}
                if ticks>=180:
                    settled=report['tick_samples'][179]['position']
                    assert all(s['position']==settled for s in report['tick_samples'][170:180]),'Player did not stop after releasing input'
                    report['movement_verification']['stopped_after_release']=True
                    report['movement_verification']['settled_fixed12']=settled
                report['status']='behavior_completed'
                if draw:
                    assert all(s['words']>0 for s in report['draw_samples']),'Player produced an empty render'
                    report['status']='draw_completed'
    except Exception as error:
        report.update(status='stopped',error=str(error),calls=calls,
            pc=cpu.reg_read(UC_ARM_REG_PC),lr=cpu.reg_read(UC_ARM_REG_LR),recent_functions=list(trace))
    report['visited_function_count']=len(visited)
    report['invalid_accesses']=invalid_accesses
    report['traps_reached']=len(trap_hits)
    report['trap_hits']=trap_hits
    report['platform_file_io']='Host supplies verified ROM bytes; native bridge and original heap execute on ARM11'
    report['file_requests']=file_requests
    report['first_tick_position_writes']=position_writes
    if PLAYER_INIT in ROOTS:
        call('native_geometry_stats',arena+0x4800)
        keys=('texture_capacity','palette_capacity','texture_uploads','palette_uploads',
              'texture_bytes','palette_bytes','texture_generation','palette_generation',
              'highlight_shading','texture_transfer_active','palette_transfer_active')
        report['geometry']=dict(zip(keys,struct.unpack('<11I',cpu.mem_read(arena+0x4800,44))))
        report['geometry_transfers']=geometry_transfers
        verified_geometry=[]
        for palette,expected in enumerate(expected_geometry):
            address=call('native_geometry_storage',palette)
            actual=bytes(cpu.mem_read(address,len(expected)))
            verified_geometry.append({'palette':bool(palette),'matches_original_uploads':actual==expected,
                'sha256':hashlib.sha256(actual).hexdigest()})
            if actual!=expected:
                report.update(status='stopped',error='Native geometry storage differs from original model uploads')
        report['geometry_storage_verification']=verified_geometry
    report['visited_functions']=sorted(visited)
    report['unsupported_data']=[{'module':m,'original':a,'poison_address':v} for (m,a),v in poison.items()]
    (OUT/'execution.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('visited_functions','unsupported_data','geometry_transfers','file_requests')},indent=2))
    return 0 if report['status'] in ('constructor_passed','resources_passed','behavior_completed','draw_completed') else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,default=ROOT/'build/roms/europe.nds')
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--resources',action='store_true',help='Trace original static and resource initialization beyond the constructor')
    parser.add_argument('--ticks',type=int,default=0,help='Continue through this many original Player::Behavior calls (diagnostic session setup)')
    parser.add_argument('--isolated-input',action='store_true',help='Separate behavior fixture: enable input after resources; does not verify normal audio/session boot')
    parser.add_argument('--draw',action='store_true',help='Execute original Player render into the native command buffer; does not run PICA')
    args=parser.parse_args()
    if args.ticks<0 or args.ticks>3600: parser.error('--ticks must be 0..3600')
    if args.isolated_input and not args.ticks: parser.error('--isolated-input requires --ticks')
    if args.draw and not args.ticks: parser.error('--draw requires --ticks')
    if args.ticks: args.resources=True
    global OUT
    if args.resources:
        OUT=ROOT/('build/player-motion-diagnostic' if args.ticks else 'build/player-resources-diagnostic')
        if args.isolated_input: OUT=ROOT/'build/player-motion-isolated-diagnostic'
        if args.draw: OUT=ROOT/'build/player-render-diagnostic'
        # Run the original overlay's complete static-initializer sequence.
        # Shared models such as number.bmd are owned by other TUs in ov002;
        # running only the Player TU leaves real shared handles uninitialized.
        config=(ROOT/'upstream/config/arm9/overlays/ov002/symbols.txt').read_text()
        initializers=re.findall(r'^(__sinit_ov002_\w+) kind:function',config,re.M)
        ROOTS.extend(initializers+[PLAYER_INIT,HEAP_INIT,PLAYER_FACTORY,
            '_ZN6Player18St_LevelEnter_InitEv',
            'native_collision_init','_ZN4dBgW6EnableEP8dActor_c',
            '_ZNK7dBgW_Kc16GetOctreeOriginYEv','_ZNK7dBgW_Kc13GetUnkOctreeYEv','func_0202a850',
            '_ZN2GX13SetBankForTexEt','_ZN2GX17SetBankForTexPlttEt','InitialiseVramGlobals',
            'native_geometry_stats','native_geometry_storage'])
        if args.ticks:
            ROOTS.extend(['_ZN6Player8BehaviorEv','_ZN5Stage10CheckInputEv','_ZN5Stage16CheckCameraInputEv',
                '_ZN8dActor_c14BeforeBehaviorEv','_ZN8dActor_c13AfterBehaviorEj',
                '_ZN7ClipperC1Ev','dCamera_c_classInit',
                '_ZN6Player18St_LevelEnter_MainEv','_ZN6Player21St_LevelEnter_CleanupEv',
                'func_ov002_020c71e0','func_ov002_020d3b9c',
                '_ZN6Player12St_Wait_InitEv','_ZN6Player12St_Wait_MainEv',
                '_ZN6Player15St_Wait_CleanupEv',
                '_ZN6Player12St_Walk_InitEv','_ZN6Player12St_Walk_MainEv'])
        if args.draw:
            ROOTS.extend(['_ZN6Player6RenderEv','sm64ds_draw_reset','sm64ds_draw_entries',
                          'sm64ds_draw_count','sm64ds_draw_ok'])
    OUT.mkdir(parents=True,exist_ok=True)
    symbols,addresses,regions,relocs=metadata()
    if not args.skip_build: build(symbols,regions)
    return execute(args.rom,symbols,addresses,regions,relocs,args.ticks,args.isolated_input,args.draw)


if __name__=='__main__': sys.exit(main())
