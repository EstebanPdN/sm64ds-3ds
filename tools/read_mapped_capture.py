#!/usr/bin/env python3
"""Bounded reads from a complete VM capture or its explicitly incomplete prefix.

An incomplete capture never establishes whole-process coverage or allocator
consistency. Readable bytes must exist in the file; virtual mappings alone do
not authorize a read. No process is launched or modified.
"""
from pathlib import Path
import struct
HEADER=struct.Struct('<8sIIIIQQIIIIII');REGION=struct.Struct('<6I')
# Version 2 adds plain device memory (VRAM, DSP windows) with reason 4.
DEVICE_RANGES=((0x1F000000,0x1F600000),(0x1FF00000,0x1FF80000))
class MappedCapture:
 def __init__(self,path,*,allow_incomplete=False):
  self.path=Path(path);self.raw=self.path.read_bytes();self.regions=[]
  if len(self.raw)<HEADER.size:raise ValueError('Missing VM header')
  magic,version,size,word,region_size,begin,end,scene,epoch,count,complete,scan_end,devices=HEADER.unpack_from(self.raw)
  if magic!=b'SM64VM1\0' or version not in (1,2) or (size,word,region_size)!=(64,4,24):raise ValueError('Unknown VM format')
  if complete not in (0,1) or count>512 or not 0<scan_end<=0x40000000:raise ValueError('Invalid VM bounds')
  if not complete and not allow_incomplete:raise ValueError('Incomplete VM requires explicit prefix analysis')
  if complete and end<begin:raise ValueError('Invalid capture clock')
  self.complete=bool(complete);self.header={'scene':scene,'epoch':epoch,'complete':self.complete,'scan_end':scan_end,'non_atomic_devices':bool(devices)}
  offset=size;address=0;self.truncation=None
  for _ in range(512):
   if self.complete and len(self.regions)==count:break
   if offset==len(self.raw):break
   if len(self.raw)-offset<region_size:self.truncation={'kind':'region_header','file_offset':offset};break
   base,length,perm,state,payload,reason=REGION.unpack_from(self.raw,offset);offset+=region_size
   if base!=address or not length or base+length>scan_end:raise ValueError('Invalid or noncontiguous VM region')
   device=version>=2 and reason==4 and payload==length and perm&1 and state&255==2 and any(lo<=base and base+length<=hi for lo,hi in DEVICE_RANGES)
   valid=device or (payload==length and reason==0 and perm&1 and state&255 not in (0,1,2)) or (payload==0 and reason in (1,2,3))
   if not valid:raise ValueError('Invalid VM payload classification')
   captured=min(payload,len(self.raw)-offset)
   self.regions.append({'address':base,'size':length,'perm':perm,'state':state,'payload_bytes':payload,'captured_bytes':captured,'file_offset':offset})
   if captured<payload:self.truncation={'kind':'region_payload','address':base,'declared':payload,'captured':captured};break
   offset+=payload;address=base+length
  if self.complete and (self.truncation or len(self.regions)!=count or address!=scan_end or offset!=len(self.raw)):
   raise ValueError('Complete VM has truncated/trailing payload or coverage gap')
  if len(self.regions)==512 and offset<len(self.raw):raise ValueError('Too many VM regions')
 def read(self,address,size):
  if not 0<=address<=0xffffffff or not 0<=size<=0x10000000 or address+size>0x100000000:raise ValueError('Invalid virtual read')
  output=[]
  while size:
   r=next((r for r in self.regions if r['address']<=address<r['address']+r['size']),None)
   if not r:raise ValueError('Address not described in captured prefix')
   inside=address-r['address'];n=min(size,r['size']-inside)
   if inside+n>r['captured_bytes']:raise ValueError('Read reaches missing/nonreadable VM bytes')
   off=r['file_offset']+inside;output.append(self.raw[off:off+n]);address+=n;size-=n
  return b''.join(output)
 def u32(self,address):return struct.unpack('<I',self.read(address,4))[0]
