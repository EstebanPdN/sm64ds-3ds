#!/usr/bin/env python3
"""Inventory original live DS collider sources for a future movement adapter.

Uses a saved capture only; an incomplete prefix is opt-in, never accepted as a
whole-RAM/performance result. Output contains metadata, not ROM/mesh assets.
"""
from pathlib import Path
import argparse,hashlib,json,struct
from elftools.elf.elffile import ELFFile
from read_mapped_capture import MappedCapture
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def inspect(snapshot,elf,allow_incomplete=False):
 vm=MappedCapture(snapshot,allow_incomplete=allow_incomplete)
 with elf.open('rb') as f:
  binary=ELFFile(f);symbols={s.name:s['st_value'] for s in binary.get_section_by_name('.symtab').iter_symbols()}
  text=binary.get_section_by_name('.text');text_address=text['sh_addr'];text_bytes=text.data()
 anchors=json.loads(snapshot.with_suffix('.anchors.json').read_text())['anchors']
 bias=anchors['data_0209f394']-symbols['data_0209f394']
 for name in ['sm64ds_render60_source_epoch','data_0208751c']:
  if anchors[name]!=symbols[name]+bias:raise ValueError('ELF does not match capture anchors')
 if vm.read(text_address+bias,len(text_bytes))!=text_bytes:raise ValueError('Captured executable text differs from ELF')
 names={v+bias+8:k for k,v in symbols.items() if k.startswith('_ZTV')}
 colliders=[]
 for index,ptr in enumerate(struct.unpack('<24I',vm.read(symbols['data_020a0c80']+bias,96))):
  if not ptr:continue
  vtable=vm.u32(ptr);kind=names.get(vtable,'unknown');item={'slot':index,'vtable':kind,'owner_present':bool(vm.u32(ptr+4))}
  if kind not in ('_ZTV7dBgW_Kc','_ZTV10dBgW_KcMbg','_ZTV14dBgW_KcMbgSclY'):
   item['status']='unknown collider layout; not interpreted';colliders.append(item);continue
  canonical=vm.read(ptr+0x14,1)[0]
  if canonical!=index:
   if canonical>=24 or vm.u32(symbols['data_020a0c80']+bias+canonical*4)!=ptr:raise ValueError('Collider registry slot mismatch')
   item.update(status='duplicate alias of validated canonical collider',canonical_slot=canonical)
   colliders.append(item);continue
  kcl=vm.u32(ptr+0x20);clps=vm.u32(ptr+0x24)
  pos,norm,tris,octree=struct.unpack('<4I',vm.read(kcl,16))
  if not pos<norm<tris<octree or (octree-tris)%16:raise ValueError('Invalid relocated KCL arrays')
  count=(octree-tris)//16
  if not 1<count<=65536:raise ValueError('Unbounded KCL triangle count')
  # KCL tri pointers precede the first real prism by16 bytes. Those
  # leading bytes can still contain final normals, not a zero dummy record.
  # A raw block may pad positions/normals; indices are bounded by the stored
  # array spans, while triangles retain their original attribute indices.
  raw=vm.read(tris,count*16);attributes=set();negative=0
  for i in range(1,count):
   length,vi,ni,e1,e2,e3,attribute=struct.unpack_from('<i6H',raw,i*16)
   if vi*12+12>norm-pos or max(ni,e1,e2,e3)*6+6>tris+16-norm:raise ValueError('KCL index outside captured array')
   attributes.add(attribute);negative+=length<=0
  clps_header=vm.read(clps,8) if clps else bytes(8)
  stride,entries=struct.unpack_from('<2H',clps_header,4)
  item.update(status='captured original KCL arrays validated',triangles=count-1,positions_span=norm-pos,normals_span=tris+16-norm,clps_stride=stride,clps_entries=entries,attributes_used=len(attributes),maximum_attribute=max(attributes),nonpositive_prisms=negative,moving=kind!='_ZTV7dBgW_Kc')
  if stride==8:
   vm.read(clps+8,entries*8)
   item['attributes_within_CLPS']=max(attributes)<entries
  colliders.append(item)
 return {'status':'saved_original_collider_inventory','capture':vm.header,'VM_sha256':sha(snapshot),'ELF_sha256':sha(elf),'ELF_anchor_bias':bias,'executable_text_byte_exact':True,'colliders':colliders,'known_triangles':sum(c.get('triangles',0) for c in colliders),'source_sha256':{Path(__file__).name:sha(Path(__file__)), 'read_mapped_capture.py':sha(Path(__file__).with_name('read_mapped_capture.py'))},'limits':'Inventory only. No mesh export, runtime import, physics parity, stable frame rate or whole-RAM acceptance; incomplete prefixes authorize only bytes actually saved.'}
def export_private_prisms(snapshot,elf,report,output):
 if not output.resolve().is_relative_to((ROOT/'build').resolve()):raise ValueError('Prism fixtures must stay in private build output')
 if any(c.get('status')!='captured original KCL arrays validated' for c in report['colliders']):raise ValueError('Unknown collider cannot be silently omitted')
 vm=MappedCapture(snapshot,allow_incomplete=not report['capture']['complete'])
 with elf.open('rb') as f:symbols={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
 table=struct.unpack('<24I',vm.read(symbols['data_020a0c80']+report['ELF_anchor_bias'],96));records=[]
 for collider in report['colliders']:
  ptr=table[collider['slot']];pos,norm,tris,octree=struct.unpack('<4I',vm.read(vm.u32(ptr+32),16))
  for i in range(1,(octree-tris)//16):
   length,vi,ni,e1,e2,e3,attribute=struct.unpack('<i6H',vm.read(tris+i*16,16));origin=struct.unpack('<3i',vm.read(pos+vi*12,12));normals=[]
   for n in [ni,e1,e2,e3]:normals.extend(struct.unpack('<3h',vm.read(norm+n*6,6)))
   records.append(struct.pack('<i3i12hH2x',length,*origin,*normals,attribute))
 assert len(records)==report['known_triangles']
 output.parent.mkdir(parents=True,exist_ok=True)
 with output.open('xb') as f:f.write(struct.pack('<2I',0x314c434d,len(records))+b''.join(records))
 return {'path':str(output.resolve()),'sha256':sha(output),'records':len(records),'scope':'Private local prism fixture; moving collider vertices remain local, not world transformed.'}

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('snapshot',type=Path);p.add_argument('--elf',required=True,type=Path);p.add_argument('--allow-incomplete',action='store_true');p.add_argument('--output',required=True,type=Path);p.add_argument('--private-prisms',type=Path);a=p.parse_args()
 r=inspect(a.snapshot,a.elf,a.allow_incomplete)
 if a.output.exists():raise FileExistsError(a.output)
 if a.private_prisms:r['private_prism_fixture']=export_private_prisms(a.snapshot,a.elf,r,a.private_prisms)
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'status':r['status'],'colliders':len(r['colliders']),'triangles':r['known_triangles'],'capture_complete':r['capture']['complete']}))
