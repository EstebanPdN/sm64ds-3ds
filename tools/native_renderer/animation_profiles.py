"""Explicit reviewed model/animation associations, checked against local ROM.

Only source-reviewed associations enter this list. Matching names is not an
association heuristic. The caller verifies the supported ROM SHA1 first.
"""
import hashlib
import json
from pathlib import Path
import struct
try:
    from .audit_models import ROOT, SourceError,span,u32
    from .texture_sequence import string
except ImportError:
    from audit_models import ROOT, SourceError,span,u32
    from texture_sequence import string

DEFAULT=Path(__file__).with_name('animation_profiles.json')


def member(rom,path):
    import ndspy.lz10
    identity=rom.filenames.idOf(path)
    if identity is None:raise SourceError('Explicit animation/model path absent from ROM')
    data=rom.files[identity]
    if data.startswith(b'LZ77'):
        if len(data)<8 or data[4]!=0x10 or not 0<int.from_bytes(data[5:8],'little')<=16*1024*1024:
            raise SourceError('Compressed animation/model bound')
        data=ndspy.lz10.decompress(data[4:])
    if len(data)>16*1024*1024:raise SourceError('Animation/model size bound')
    return identity,data


def load(rom,path=DEFAULT):
    import ndspy.codeCompression
    path=Path(path);config=json.loads(path.read_text())
    if config.get('version')!=1 or not isinstance(config.get('models'),list):raise SourceError('Animation profile schema')
    rows=[r for r in struct.iter_unpack('<8I',rom.arm9OverlayTable) if r[0]==0]
    if len(rows)!=1:raise SourceError('Expected one original handle-table overlay')
    overlay=rows[0]
    if overlay[6]>=len(rom.files):raise SourceError('Handle-table FAT identity')
    data=rom.files[overlay[6]]
    if overlay[7]&0x01000000:data=ndspy.codeCompression.decompress(data)
    if len(data)!=overlay[2]:raise SourceError('Handle-table overlay length')
    table=0x020bd4b8-overlay[1];span(data,table,0x80a,4)
    def verify(handle,expected):
        if not isinstance(handle,int) or not 0<=handle<0x80a:raise SourceError('Profile handle out of range')
        offset=u32(data,table+handle*4)-overlay[1]
        if string(data,offset,0).decode('ascii')!=expected:raise SourceError('Profile handle path differs from ROM')
    result={};vendor=(ROOT/'third_party/sm64ds-3ds/sm64ds-decomp').resolve()
    for entry in config['models']:
        name=entry['model'];verify(entry['model_handle'],name)
        if name in result or not name.endswith('.bmd') or not entry['animations'] or not entry['evidence']:
            raise SourceError('Duplicate/empty model animation profile')
        for evidence in entry['evidence']:
            source=(vendor/evidence['source']).resolve()
            if not source.is_relative_to(vendor) or hashlib.sha256(source.read_bytes()).hexdigest()!=evidence['sha256']:
                raise SourceError('Animation association source evidence changed')
        sequences=[];ids=[];seen=set()
        for animation in entry['animations']:
            file=animation['path'];verify(animation['handle'],file)
            if file in seen or not file.endswith('.btp'):raise SourceError('Duplicate/invalid animation path')
            identity,source=member(rom,file);sequences.append((file,source));ids.append(identity);seen.add(file)
        model_id,_=member(rom,name)
        result[name]=dict(sequences=tuple(sequences),proof=dict(model_file_id=model_id,animation_file_ids=ids,
            profile_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),evidence=entry['evidence'],
            original_handle_paths_verified=True))
    return result
