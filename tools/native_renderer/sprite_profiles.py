"""Reviewed menu texture/palette associations; original files stay local.

b697c constructs eleven descriptors through b2308, which pairs texture i with
palette i. The listed regional containers are verified individually against
the ROM handle table. This proves resources/schema, not language-flow visuals.
"""
import hashlib
import json
from pathlib import Path
import struct
try:
    from .audit_models import ROOT,SourceError,span,u32
    from .animation_profiles import member
    from .texture_sequence import string
except ImportError:
    from audit_models import ROOT,SourceError,span,u32
    from animation_profiles import member
    from texture_sequence import string

DEFAULT=Path(__file__).with_name('sprite_profiles.json')


def load(rom,path=DEFAULT):
    path=Path(path);config=json.loads(path.read_text())
    if config.get('version')!=1 or not isinstance(config.get('models'),list):raise SourceError('Sprite profile schema')
    overlays=rom.loadArm9Overlays([0]);overlay=overlays[0];table=0x020bd4b8-overlay.ramAddress
    span(overlay.data,table,0x80a,4)
    result={};vendor=(ROOT/'third_party/sm64ds-3ds/sm64ds-decomp').resolve()
    for row in config['models']:
        name,handle=row['model'],row['handle']
        if not isinstance(handle,int) or not 0<=handle<0x80a or name in result:raise SourceError('Sprite profile identity')
        offset=u32(overlay.data,table+handle*4)-overlay.ramAddress
        if string(overlay.data,offset,0).decode('ascii')!=name:raise SourceError('Sprite handle differs from supported ROM')
        entries=row.get('paired_entries')
        if entries not in (2,11) or not row.get('evidence'):raise SourceError('Unreviewed sprite descriptor range')
        for proof in row['evidence']:
            source=(vendor/proof['source']).resolve()
            if not source.is_relative_to(vendor) or hashlib.sha256(source.read_bytes()).hexdigest()!=proof['sha256']:
                raise SourceError('Sprite association source evidence changed')
        identity,data=member(rom,name);textures=u32(data,20);palettes=u32(data,28)
        if textures!=entries or palettes!=entries:raise SourceError('Paired menu texture/palette roster differs')
        images=[]
        for i in range(entries):
            images.append((i,i))
        result[name]=dict(images=tuple(images),proof=dict(file_id=identity,source_sha256=hashlib.sha256(data).hexdigest(),
            handle=handle,handle_path_verified=True,profile_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),evidence=row['evidence'],
            visual_language_flow_accepted=False))
    return result
