"""Bounded host-only BTP preparation; no runtime graphics/VRAM dependency.

The original Prepare resolves names to logical BMD indices. Update chooses the
LAST key whose start is <= frame, retaining the current palette if no palette
records exist. This module preserves those rules, including unsorted/duplicate
starts, and prepares a conservative closure across explicitly associated files.
"""
from dataclasses import dataclass
import hashlib
import struct
try:
    from .audit_models import SourceError, span, u32
except ImportError:
    from audit_models import SourceError, span, u32

NO_TEXTURE=0xffffffff


def string(data,offset,minimum):
    if not minimum<=offset<len(data):raise SourceError('Animation name pointer outside source')
    end=data.find(b'\0',offset,min(len(data),offset+1024))
    if end<0 or end==offset:raise SourceError('Empty or unterminated animation name')
    return bytes(data[offset:end])


def names(data,count_at,table_at,stride):
    count,table=u32(data,count_at),u32(data,table_at)
    if count>65536:raise SourceError('Unbounded model names')
    span(data,table,count,stride);result={}
    for i in range(count):
        # Original strcmp lookup returns the first equal name.
        result.setdefault(string(data,u32(data,table+i*stride),56),i)
    return result


@dataclass(frozen=True)
class Key:
    start:int
    texture:int
    palette:int|None


@dataclass(frozen=True)
class Track:
    material:int
    keys:tuple[Key,...]

    def select(self,frame):
        for key in reversed(self.keys):
            if key.start<=frame:return key
        raise SourceError('Frame precedes every animation key')

    def reachable(self,frames):
        # A suffix with an earlier/equal start hides an earlier source key.
        # No O(frames*keys) enumeration and no assumption of sorted starts.
        ceiling=frames
        for key in reversed(self.keys):
            if key.start<ceiling:
                yield key
                ceiling=key.start


@dataclass(frozen=True)
class Sequence:
    name:str
    sha256:str
    frames:int
    tracks:tuple[Track,...]

    def select(self,frame):
        if not 0<=frame<self.frames:raise SourceError('Animation frame outside source range')
        return tuple((track.material,track.select(frame)) for track in self.tracks)


def bind(model,source,name='authored'):
    """Resolve original names, validate every referenced range and selector."""
    if len(source)>16*1024*1024:raise SourceError('Animation exceeds source size bound')
    span(source,0,36)
    frames,nt,tex,np,pal,starts,tex_keys,pal_keys,ns,tracks=struct.unpack_from('<HHIH2xIIIIH2xI',source)
    if not frames or ns>512:raise SourceError('Animation frame/track count outside native contract')
    lookup_tex=names(model,20,24,20) if nt else {}
    lookup_pal=names(model,28,32,16) if np else {}
    lookup_mat=names(model,36,40,48) if ns else {}
    def table(offset,count,stride):
        if count and offset<36:raise SourceError('Animation table points into header')
        return span(source,offset,count,stride)
    def resolve(offset,count,lookup):
        table(offset,count,8);out=[]
        for i in range(count):
            key=string(source,u32(source,offset+i*8+4),36)
            if key not in lookup:raise SourceError('Animation references absent model name')
            out.append(lookup[key])
        return out
    textures=resolve(tex,nt,lookup_tex);palettes=resolve(pal,np,lookup_pal)
    table(tracks,ns,12);result=[]
    for i in range(ns):
        record=tracks+i*12;material_name=string(source,u32(source,record+4),36)
        if material_name not in lookup_mat:raise SourceError('Animation references absent material')
        count,offset=struct.unpack('<HH',span(source,record+8,4))
        if not count or count+offset>65536:raise SourceError('Animation key range outside native contract')
        for address in (starts,tex_keys,*((pal_keys,) if np else ())):table(address,offset+count,2)
        keys=[]
        for k in range(offset,offset+count):
            first=struct.unpack('<H',span(source,starts+k*2,2))[0]
            ti=struct.unpack('<H',span(source,tex_keys+k*2,2))[0]
            pi=struct.unpack('<H',span(source,pal_keys+k*2,2))[0] if np else None
            if ti>=nt or (pi is not None and pi>=np):raise SourceError('Animation selector outside record table')
            keys.append(Key(first,textures[ti],palettes[pi] if pi is not None else None))
        result.append(Track(lookup_mat[material_name],tuple(keys)))
    return Sequence(name,hashlib.sha256(source).hexdigest(),frames,tuple(result))


def prepare(model,sequences):
    """Return identity pairs + provenance for explicit (name,bytes) sequences.

    Palette-free tracks can inherit any palette reachable through another
    supplied sequence on the same material. Include that closure; never infer
    identity from texture bytes, addresses or a coincidentally similar path.
    """
    bound=[bind(model,data,name) for name,data in sequences]
    material_table=u32(model,40);palette_states={}
    for sequence in bound:
        for track in sequence.tracks:
            states=palette_states.setdefault(track.material,{u32(model,material_table+track.material*48+8)})
            states.update(key.palette for key in track.reachable(sequence.frames) if key.palette is not None)
    pairs=set();reports=[]
    for sequence in bound:
        tracks=[]
        for track in sequence.tracks:
            used=list(track.reachable(sequence.frames));tracks.append(dict(material=track.material,keys=len(track.keys),reachable_keys=len(used)))
            for key in used:
                for palette in palette_states[track.material] if key.palette is None else (key.palette,):
                    pairs.add((key.texture,palette))
                    if len(pairs)>128:raise SourceError('Animation image identity budget exceeds native bank')
        reports.append(dict(source=sequence.name,source_sha256=sequence.sha256,frames=sequence.frames,tracks=tracks))
    return sorted(pairs),reports
