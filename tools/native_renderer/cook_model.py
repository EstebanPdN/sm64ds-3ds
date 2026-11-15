#!/usr/bin/env python3
"""Prepare original rigid model resources on the host, never per frame.

The first native contract covers colour geometry with ordinary UVs and native
RGBA8 images, ordinary normal-generated UVs and native lit-normal associations.
Mixed-bone triangles, position-generated UVs and special passes remain explicit
admission failures.
UV-mode-1 resources require explicit native material bindings at draw time.
This is resource conversion, not acceptance of original actor behaviour.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from dataclasses import dataclass

try:
    from .audit_models import ROOT, SourceError, audit_bmd, span, u32, PARAMETERS
    from .bank import Mesh, Image, Material, Part, build, tiled_rgba8
    from .texture_sequence import prepare as prepare_sequences
    from .textures import decode_texture
except ImportError:
    from audit_models import ROOT, SourceError, audit_bmd, span, u32, PARAMETERS
    from bank import Mesh, Image, Material, Part, build, tiled_rgba8
    from texture_sequence import prepare as prepare_sequences
    from textures import decode_texture


class NeedsNativeFeature(SourceError):
    pass


# PLAYABLE MODE (temporary, explicitly recorded per model): instead of
# refusing, approximate mixed-bone triangles by rebinding vertices into the
# triangle's first bone with REST-POSE matrices (joints may tear/stretch),
# blend partially transparent model textures, and use the first vertex's
# colour source for mixed-source triangles. Strict cooking stays the default.
import os
APPROXIMATE=os.environ.get('SM64DS_COOK_APPROXIMATE')=='1'
EDGE_STATS=None  # diagnostics: {'mixed':[], 'rigid':[]}


ANGLE_UNIT=float(os.environ.get('SM64DS_COOK_ANGLE_UNIT','65536'))


def rest_pose(data,bones):
    """Bind-pose world matrices with the game's own convention (func_02045074,
    func_02048234): row vectors, local = S * Rx * Ry * Rz with translation row,
    world = local * parent; angles are u16 with 0x10000 = 2*pi."""
    import math
    table=u32(data,8);world=[None]*bones
    def local(b):
        n=table+b*64
        sx,sy,sz=(struct.unpack_from('<i',data,n+o)[0]/4096 for o in (16,20,24))
        ax,ay,az=(struct.unpack_from('<H',data,n+o)[0]*2*math.pi/ANGLE_UNIT for o in (28,30,32))
        tx,ty,tz=(struct.unpack_from('<i',data,n+o)[0]/4096 for o in (36,40,44))
        a0,a1,b0,b1,c0,c1=math.sin(ax),math.cos(ax),math.sin(ay),math.cos(ay),math.sin(az),math.cos(az)
        p00,p11,p01,p10=a0*c0,a1*c1,a0*c1,a1*c0
        r=[[b1*c1,b1*c0,-b0],[b0*p01-p10,p11+b0*p00,b1*a0],[p00+b0*p11,b0*p10-p01,b1*a1]]
        r=[[v*s for v in row] for row,s in zip(r,(sx,sy,sz))]
        return r,[tx,ty,tz]
    def compose(l,p):
        (lr,lt),(pr,pt)=l,p
        r=[[sum(lr[i][k]*pr[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
        tt=[sum(lt[k]*pr[k][j] for k in range(3))+pt[j] for j in range(3)]
        return r,tt
    def resolve(b,depth=0):
        if world[b] is None:
            parent=b+struct.unpack_from('<h',data,table+b*64+8)[0]
            m=local(b)
            if parent!=b and 0<=parent<bones and depth<256:m=compose(m,resolve(parent,depth+1))
            world[b]=m
        return world[b]
    for b in range(bones):resolve(b)
    return world


def rebind(position,source,target,poses):
    """Raw 4.12 vertex from source-bone space into target-bone space (bind pose)."""
    (ar,at),(br,bt)=poses[source],poses[target]
    v=[c/4096 for c in position]
    w=[sum(v[k]*ar[k][j] for k in range(3))+at[j] for j in range(3)]
    d=[w[j]-bt[j] for j in range(3)]
    m=br;det=(m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))
    if abs(det)<1e-9:return position
    inv=[[(m[(j+1)%3][(i+1)%3]*m[(j+2)%3][(i+2)%3]-m[(j+1)%3][(i+2)%3]*m[(j+2)%3][(i+1)%3])/det for j in range(3)] for i in range(3)]
    # row vector: d = local * m  =>  local = d * inv(m)
    local=[sum(d[k]*inv[k][j] for k in range(3)) for j in range(3)]
    return tuple(max(-32768,min(32767,int(round(c*4096)))) for c in local)


def signed(value,bits):
    value &= (1<<bits)-1
    return value-(1<<bits) if value&(1<<(bits-1)) else value


def texture_rgba(data,texture,palette,zero_transparent=None,retain_zero_rgb=False):
    count,table=u32(data,20),u32(data,24)
    if not 0<=texture<count:
        raise SourceError('Texture identity out of range')
    rec=table+texture*20
    address,size,flags=u32(data,rec+4),u32(data,rec+8),u32(data,rec+16)
    w,h=8<<((flags>>20)&7),8<<((flags>>23)&7)
    fmt=(flags>>26)&7
    if fmt not in (1,2,3,4,5,6,7):
        raise NeedsNativeFeature(f'Texture format {fmt}')
    if zero_transparent is not None:
        if not isinstance(zero_transparent,bool) or fmt not in (2,3,4):
            raise SourceError('Colour-zero override requires an indexed image')
    else:zero_transparent=bool(flags&(1<<29))
    source=span(data,address,size)
    raw=b''
    if fmt!=7:
        if not 0<=palette<u32(data,28):
            raise SourceError('Palette identity out of range')
        p=u32(data,32)+palette*16
        raw=span(data,u32(data,p+4),u32(data,p+8))
    descriptor=span(data,address+size,size//2) if fmt==5 else None
    return w,h,decode_texture(source,raw,w,h,fmt,zero_transparent=zero_transparent,
                             retain_zero_rgb=retain_zero_rgb,descriptor=descriptor)



@dataclass
class NormalUvState:
    # None denotes the live material offset installed at group entry. Explicit
    # TEXCOORD persists across its display-list parts; no runtime decoding.
    raw_uv: tuple | None = None
    diffuse_override: int | None = None


def geometry(stream,bone_ids,width,height,scale,diffuse,emission,light_mask,textured=True,normal_uv=False,normal_state=None,poses=None,notes=None,skin=None):
    at=0;mode=None;pending=[];position=None;uv=None if textured else (0,0);binding=None
    colour=diffuse&32767 if diffuse&(1<<15) else None
    role='diffuse' if colour is not None else None
    vertices=[];triangles=[]
    raw_uv=normal_state.raw_uv if normal_state is not None else None
    normal=None;normal_coords=(0,0);material_offset=1
    diffuse_override=normal_state.diffuse_override if normal_state is not None else None
    has_normals=normal_uv or bool(light_mask)
    direction_binding=(bone_ids[0] if bone_ids else None) if normal_uv else next((b for b in reversed(bone_ids) if b is not None),None)
    def emit_triangle(ids):
        bones={vertices[i][3] for i in ids};mixed=len(bones)!=1
        if len(bones)!=1 and APPROXIMATE and skin is not None:
            # Per-vertex CPU skinning (exact): keep each vertex in its own bone.
            role=vertices[ids[0]][4]
            skin.append((role,[(vertices[i][0],vertices[i][1],vertices[i][2],vertices[i][3],vertices[i][5],vertices[i][6]) for i in ids]))
            if notes is not None:notes.add('per-vertex CPU skinning of mixed-bone triangles')
            # Degenerate placeholder keeps a part with this material/role for state.
            if not any(r==role for _,_,r in triangles):triangles.append(((ids[0],ids[0],ids[0]),vertices[ids[0]][3],role))
            return
        if len(bones)!=1:
            if not APPROXIMATE or poses is None:raise NeedsNativeFeature('Triangle requires a GPU bone palette')
            target=vertices[ids[0]][3];fixed=[]
            for i in ids:
                v=vertices[i]
                if v[3]!=target:
                    vertices.append((rebind(v[0],v[3],target,poses),*v[1:3],target,*v[4:]));i=len(vertices)-1
                fixed.append(i)
            ids=fixed;bones={target}
            if notes is not None:notes.add('rest-pose rebind of mixed-bone triangles')
        roles={vertices[i][4] for i in ids}
        if len(roles)!=1:
            if APPROXIMATE:
                role=vertices[ids[0]][4];fixed=[]
                for i in ids:
                    v=vertices[i]
                    if v[4]!=role:vertices.append((*v[:4],role,*v[5:]));i=len(vertices)-1
                    fixed.append(i)
                ids=fixed;roles={role}
                if notes is not None:notes.add('first-vertex colour source for mixed triangles')
            elif all(isinstance(r,tuple) and r[0]=='lighting' for r in roles) and len({r[2] for r in roles})>1:
                raise NeedsNativeFeature('Triangle requires a GPU normal basis palette')
            else:raise NeedsNativeFeature('Triangle mixes dynamic and literal colour sources')
        if EDGE_STATS is not None:
            import math
            pts=[vertices[i][0] for i in ids];edge=max(math.dist(pts[k],pts[(k+1)%3]) for k in range(3))
            EDGE_STATS['mixed' if mixed else 'rigid'].append(edge)
        triangles.append((tuple(ids),next(iter(bones)),next(iter(roles))))
    while at<len(stream):
        opcodes=bytes(span(stream,at,4));at+=4
        for op in opcodes:
            if op not in PARAMETERS:
                raise SourceError('Unknown geometry opcode')
            params=span(stream,at,PARAMETERS[op],4);at+=len(params)
            p=struct.unpack_from('<I',params)[0] if params else 0
            if op==0:continue
            if op==0x14:
                slot=p&31
                if slot>=len(bone_ids) or bone_ids[slot] is None:
                    raise NeedsNativeFeature('Unresolved inherited matrix slot')
                binding=bone_ids[slot]
                direction_binding=binding
            elif op==0x20:colour=p&32767;role='literal'
            elif op==0x21:
                if light_mask:
                    if direction_binding is None:raise NeedsNativeFeature('Normal inherits an unresolved palette basis')
                    colour=32767;role=('lighting',diffuse_override,direction_binding)
                else:colour=(emission>>16)&32767;role='emission'
                if has_normals:
                    normal=tuple(signed(p>>shift,10) for shift in (0,10,20))
                if normal_uv:
                    normal_coords=(0,0) if raw_uv is None else raw_uv
                    material_offset=int(raw_uv is None)
            elif op==0x22:
                uv=(signed(p,16),signed(p>>16,16));raw_uv=uv
                if normal_uv:normal_coords=raw_uv;material_offset=2
            elif op==0x30:
                diffuse=p;diffuse_override=p
                if p&(1<<15):colour=p&32767;role='literal'
            elif op==0x40:
                if mode is not None:raise SourceError('Nested primitive')
                mode=p&3;pending=[]
            elif op==0x41:
                if (mode is None or (mode==0 and len(pending)%3) or (mode==1 and len(pending)%4) or
                    (mode==2 and len(pending)<3) or (mode==3 and (len(pending)<4 or len(pending)%2))):
                    raise SourceError('Incomplete primitive')
                mode=None;pending=[]
            elif 0x23<=op<=0x28:
                if mode is None or binding is None or (normal is None and material_offset!=2 if normal_uv else uv is None) or colour is None:
                    raise NeedsNativeFeature('Vertex inherits unresolved entry state')
                if op==0x23:
                    p1=struct.unpack_from('<I',params,4)[0]
                    position=(signed(p,16),signed(p>>16,16),signed(p1,16))
                elif op==0x24:position=tuple(signed(p>>shift,10)*64 for shift in (0,10,20))
                else:
                    if position is None:raise NeedsNativeFeature('Relative vertex at part entry')
                    x,y,z=position
                    if op==0x25:position=(signed(p,16),signed(p>>16,16),z)
                    elif op==0x26:position=(signed(p,16),y,signed(p>>16,16))
                    elif op==0x27:position=(x,signed(p,16),signed(p>>16,16))
                    else:position=tuple(signed(v+signed(p>>shift,10),16) for v,shift in zip(position,(0,10,20)))
                vertices.append((position,normal_coords if normal_uv else uv,colour,binding,role,normal,material_offset));pending.append(len(vertices)-1);n=len(pending)
                if mode==0 and n%3==0:emit_triangle(pending[-3:])
                elif mode==1 and n%4==0:
                    a,b,c,d=pending[-4:];emit_triangle((a,b,c));emit_triangle((a,c,d))
                elif mode==2 and n>=3:
                    a,b,c=pending[-3:];emit_triangle((b,a,c) if n%2==0 else (a,b,c))
                elif mode==3 and n>=4 and n%2==0:
                    a,b,c,d=pending[-4:];emit_triangle((a,b,d));emit_triangle((a,d,c))
            else:raise NeedsNativeFeature(f'Geometry state 0x{op:02x}')
    if mode is not None or not triangles:raise SourceError('Unclosed or empty geometry')
    has_normals=normal_uv or any(isinstance(t[2],tuple) and t[2][0]=='lighting' for t in triangles)
    native=[];indices=[];cache={};runs=[]
    for triangle,bone,role in triangles:
        if not runs or runs[-1][:2]!=[bone,role]:runs.append([bone,role,len(indices),0])
        for source_index in triangle:
            pos,coords,c,b,r,n,inherit=vertices[source_index]
            # Dynamic material colours stay in native uniforms. Only literal
            # source colours are immutable geometry. Keep provenance distinct
            # even when their current RGB values happen to be identical.
            if r!='literal':c=32767
            key=(pos,coords,c,b,r,n if has_normals else None,inherit if normal_uv else None)
            if key not in cache:
                rgb=tuple((((c>>shift)&31)<<3)|(((c>>shift)&31)>>2) for shift in (0,5,10))
                value=struct.pack('<5f4B',*(v*scale/4096 for v in pos),coords[0]/(16*width),coords[1]/(16*height),*rgb,255)
                if has_normals:value+=struct.pack('<4h',*(n if n is not None else (0,0,0)),inherit if normal_uv else 0)
                cache[key]=len(native);native.append(value)
            indices.append(cache[key]);runs[-1][3]+=1
    if len(native)>65536 or len(indices)>65535:raise NeedsNativeFeature('Mesh partition required')
    if normal_state is not None:normal_state.raw_uv=raw_uv;normal_state.diffuse_override=diffuse_override
    if skin is not None:
        skin[:]=[(role,[(tuple(v*scale/4096 for v in pos),(coords[0]/(16*width),coords[1]/(16*height)),c if role=='literal' else 32767,b,n or (0,0,0),inherit if normal_uv else 0) for pos,coords,c,b,n,inherit in verts]) for role,verts in skin]
    return Mesh(b''.join(native),struct.pack('<'+'H'*len(indices),*indices),has_normals),runs


def cook(data,name,sequences=(),sprite_images=()):
    audit=audit_bmd(data,name)
    if not 1<=audit['bones']<=256 or audit['scale_shift']>15:
        raise NeedsNativeFeature('Model pose/scale contract')
    meshes=[];images=[];materials=[];parts=[];image_cache={};material_cache={};mesh_cache={}
    image_sources=[];bindings=[];native_material_sources=[];native_part_sources=[]
    animation_pairs,animation_sources=prepare_sequences(data,sequences) if sequences else ([],[])
    notes=set();poses=rest_pose(data,audit['bones']) if APPROXIMATE else None;translucent_images=set()
    skin_groups=[]
    def prepare_image(pair,sprite=False):
        key=(*pair,sprite)
        if key not in image_cache:
            if len(images)>=128:raise NeedsNativeFeature('Native image identity budget')
            texture,palette=pair
            override=None
            if sprite:
                if not 0<=texture<audit['textures']:raise SourceError('Sprite texture outside source')
                flags=u32(data,u32(data,24)+texture*20+16)
                if (flags>>26)&7 in (2,3,4):override=True
            w,h,rgba=texture_rgba(data,texture,palette,override,sprite)
            if not sprite and any(0<a<255 for a in rgba[3::4]):
                if not APPROXIMATE:raise NeedsNativeFeature('Mixed texture alpha/depth pass contract')
                notes.add('translucent texture drawn in alpha pass');translucent_images.add(len(images))
            image_cache[key]=len(images);images.append(Image(w,h,tiled_rgba8(rgba,w,h)))
            image_sources.append(dict(texture=texture,palette=palette,width=w,height=h,rgba=rgba))
            if sprite:image_sources[-1]['sprite_only']=True
            if override:image_sources[-1]['indexed_zero_mask']=True
        return image_cache[key]
    bone_table=u32(data,8);group_table=u32(data,16);matrix_table=u32(data,44)
    for bone in range(audit['bones']):
        node=bone_table+bone*64
        count=struct.unpack('<H',span(data,node+48,2))[0]
        material_ids=span(data,u32(data,node+52),count)
        group_ids=span(data,u32(data,node+56),count)
        for material_id,group_id in zip(material_ids,group_ids):
            if material_id>=audit['materials'] or group_id>=audit['groups']:
                raise SourceError('Model draw references missing material/group')
            m=u32(data,40)+material_id*48
            texture,palette,texparam,polygon,diffuse,emission=(u32(data,m+i) for i in (4,8,32,36,40,44))
            texmode=(texparam>>30)&3
            if polygon&0x80000000:raise NeedsNativeFeature('Live material visibility required')
            if polygon&(1<<15):raise NeedsNativeFeature('Native fog required')
            if ((polygon>>4)&3)!=0:raise NeedsNativeFeature('Special polygon mode required')
            if texture!=0xffffffff:
                if texmode>2:raise NeedsNativeFeature('Native generated UV mode '+str(texmode)+' required')
                if texmode==2 and struct.unpack('<H',span(data,node+60,2))[0]&1:
                    raise NeedsNativeFeature('Billboard normal UV inherits a texture matrix')
            normal_mode=texture!=0xffffffff and texmode==2
            alpha=(polygon>>16)&31
            if not alpha:raise NeedsNativeFeature('Wireframe polygon')
            image_id=None;w=h=1
            if texture!=0xffffffff:
                pair=(texture,palette)
                image_id=prepare_image(pair);w,h=images[image_id].width,images[image_id].height
            front,back=bool(polygon&128),bool(polygon&64)
            if not front and not back:raise NeedsNativeFeature('No enabled polygon face')
            n,p=u32(data,group_table+group_id*8),u32(data,group_table+group_id*8+4)
            normal_state=NormalUvState()
            for part in range(n):
                key=(group_id,part,material_id)
                if key not in mesh_cache:
                    record=p+part*16
                    slots,slot_ids,size,source=struct.unpack('<4I',span(data,record,16))
                    bone_ids=[]
                    for slot_id in span(data,slot_ids,slots):
                        target=None if slot_id==255 else struct.unpack('<H',span(data,matrix_table+slot_id*2,2))[0]
                        if target is not None and target>=audit['bones']:raise SourceError('Missing model bone')
                        bone_ids.append(target)
                    skin=[] if APPROXIMATE else None
                    mesh,runs=geometry(span(data,source,size),bone_ids,w,h,1<<audit['scale_shift'],diffuse,emission,polygon&15,texture!=0xffffffff,normal_mode,normal_state,poses,notes,skin)
                    normal_bone=next((b for b in reversed(bone_ids) if b is not None),65535) if normal_mode else 65535
                    if normal_mode and normal_bone==65535:raise NeedsNativeFeature('Normal UV inherits an unresolved palette basis')
                    mesh_cache[key]=(len(meshes),runs,normal_bone,normal_state.raw_uv,normal_state.diffuse_override,skin or []);meshes.append(mesh)
                mesh_id,runs,normal_bone,normal_state.raw_uv,normal_state.diffuse_override,skin=mesh_cache[key]
                run_parts={}
                for matrix,role,first,number in runs:
                    lit=isinstance(role,tuple) and role[0]=='lighting'
                    lighting_bone=role[2] if lit else 65535
                    override=role[1] if lit else None
                    colour_role='lighting' if lit else role
                    material_key=(material_id,colour_role,override)
                    if material_key not in material_cache:
                        colour=diffuse&32767 if role=='diffuse' else (emission>>16)&32767 if role=='emission' else 32767
                        rgb=[(((colour>>s)&31)<<3)|(((colour>>s)&31)>>2) for s in (0,5,10)]
                        tint=(rgb[0]<<24)|(rgb[1]<<16)|(rgb[2]<<8)|(alpha*255//31)
                        material_cache[material_key]=len(materials)
                        native_material_sources.append(dict(source_material=material_id,colour_source=colour_role,
                            width=w,height=h,uv_transform=texmode==1,normal_uv=normal_mode))
                        if lit:native_material_sources[-1].update(diffuse_override=override is not None,diffuse_ambient=override or 0)
                        materials.append(Material(texture=image_id,blend=int(alpha<31 or image_id in translucent_images),cull=0 if front and back else 1 if front else 2,
                            depth_write=alpha==31 or bool(polygon&(1<<11)),tint=tint,
                            wrap_s=(2 if texparam&(1<<18) else 1) if texparam&(1<<16) else 0,
                            wrap_t=(2 if texparam&(1<<19) else 1) if texparam&(1<<17) else 0))
                    run_parts.setdefault(role,len(parts))
                    parts.append(Part(mesh_id,material_cache[material_key],matrix,first,number))
                    native_part_sources.append(dict(bone=matrix,material=material_cache[material_key],
                        billboard=bool(struct.unpack('<H',span(data,node+60,2))[0]&1),
                        normal_bone=normal_bone))
                    if lit:native_part_sources[-1]['lighting_bone']=lighting_bone
                for role,verts in skin:
                    lit=isinstance(role,tuple) and role[0]=='lighting'
                    out=[]
                    for pos,coords,c,b,n,inherit in verts:
                        rgb=tuple((((c>>s)&31)<<3)|(((c>>s)&31)>>2) for s in (0,5,10))
                        out.append([*pos,*coords,b,*rgb,*n,inherit])
                    # One group (one draw) per part: merge all cross-bone triangles of it.
                    part=run_parts[role];existing=next((g for g in skin_groups if g['part']==part),None)
                    if existing:existing['vertices'].extend(out)
                    else:skin_groups.append(dict(part=part,vertices=out))
            bindings.append(dict(bone=bone,material=material_id,group=group_id,texgen=texmode,
                                 billboard=bool(struct.unpack('<H',span(data,node+60,2))[0]&1)))
    if not parts:raise NeedsNativeFeature('Texture-only or empty model')
    for pair in animation_pairs:prepare_image(pair)
    if APPROXIMATE:
        # Texture animations (BTP) swap texture/palette pairs at runtime. Prepare
        # name-matched and same-index pairs so live selections find an image.
        def label(o):
            raw=data[o:data.index(b'\0',o)].decode('latin-1').lower()
            return ''.join(c for c in raw.replace('_pl','').replace('_16','').replace('16','') if c.isalnum())
        nt,tt,npal,pt=u32(data,20),u32(data,24),u32(data,28),u32(data,32)
        try:
            tex=[label(u32(data,tt+i*20)) for i in range(nt)];pal=[label(u32(data,pt+i*16)) for i in range(npal)]
        except (ValueError,IndexError,struct.error):tex,pal=[],[]
        def norm(s):return ''.join(c for c in s if not c.isdigit()),''.join(c for c in s if c.isdigit())
        for i,name in enumerate(tex):
            if not (u32(data,tt+i*20+16)>>26)&7 in (1,2,3,4,5,6):continue
            for j,pname in enumerate(pal):
                a,b=norm(name),norm(pname)
                if pname==name or (a[1]==b[1] and (a[0] in b[0] or b[0] in a[0])) or (i==j and nt==npal) or npal<=2:
                    if len(images)>=127:break
                    try:prepare_image((i,j))
                    except (NeedsNativeFeature,SourceError,ValueError,struct.error):pass
    for texture,palette in sprite_images:prepare_image((texture,palette),True)
    # Original instances require live bindings, even if today's defaults happen
    # to be identity. Admission of a resource never implies actor equivalence.
    bank=build(meshes,images,materials,parts,audit['bones'],hashlib.sha256(data).digest(),True)
    report=dict(source=name,source_sha256=hashlib.sha256(data).hexdigest(),native_bytes=len(bank),
        meshes=len(meshes),images=len(images),materials=len(materials),parts=len(parts),matrices=audit['bones'],
        native_material_bindings_required=True,source_bindings=bindings,native_material_sources=native_material_sources,
        native_part_sources=native_part_sources,source_material_count=audit['materials'],source_bone_count=audit['bones'],
        source_texture_count=audit['textures'],prepared_image_pairs=[{k:v for k,v in s.items() if k!='rgba'} for s in image_sources],
        prepared_animations=animation_sources,
        resource_pipeline_ready=True,game_behaviour_ready=False,approximations=sorted(notes),skin=skin_groups,
        pending=['Complete original-scene producer coverage','Unprofiled animation bindings','Whole-game visual and performance acceptance'])
    return bank,report,image_sources


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--model',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--animation',action='append',default=[],help='Explicit original BTP path; repeat for shared palette-state closure')
    args=parser.parse_args()
    if not args.output.resolve().is_relative_to((ROOT/'build').resolve()):parser.error('Private resources stay under ignored build/')
    import ndspy.rom,ndspy.lz10
    rom_data=args.rom.read_bytes()
    if hashlib.sha1(rom_data).hexdigest()!='1367529f2cb23e76ef295cb1727333ae8f0a6cd7':parser.error('Expected supported EU revision 0')
    rom=ndspy.rom.NintendoDSRom(rom_data);file_id=rom.filenames.idOf(args.model)
    if file_id is None:parser.error('Model path absent from ROM')
    packed=rom.files[file_id]
    if packed.startswith(b'LZ77'):
        if len(packed)<8 or packed[4]!=0x10 or not 0<int.from_bytes(packed[5:8],'little')<=16*1024*1024:
            parser.error('Invalid or oversized compressed model')
        data=ndspy.lz10.decompress(packed[4:])
    else:data=packed
    try:
        try:from .animation_profiles import member
        except ImportError:from animation_profiles import member
        sequences=[(name,member(rom,name)[1]) for name in args.animation]
        bank,report,images=cook(data,args.model,sequences)
    except (SourceError,ValueError,struct.error) as error:parser.exit(1,str(error)+'\n')
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(bank)
    report.update(file_id=file_id,native_sha256=hashlib.sha256(bank).hexdigest())
    for i,image in enumerate(images):
        path=args.output.with_suffix(f'.image-{i}.rgba');path.write_bytes(image['rgba'])
        report['prepared_image_pairs'][i].update(reference_file=path.name,reference_sha256=hashlib.sha256(image['rgba']).hexdigest())
    args.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('file_id','meshes','images','parts','native_bytes','resource_pipeline_ready','game_behaviour_ready')},indent=2))


if __name__=='__main__':raise SystemExit(main())
