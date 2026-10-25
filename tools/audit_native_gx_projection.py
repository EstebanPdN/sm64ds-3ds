#!/usr/bin/env python3
"""Audit real per-vertex projection versus the renderer's final matrix on host.

The snapshot and real texture resolver feed a frozen production GX decoder.
Read-only taps in a temporary copy record matrices/viewport at each vertex;
every emitted vertex/material byte must equal an intact decoder replay. No
production source, emulator, GPU, scene loop or physical-console test is used.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import audit_native_gx_stream

ROOT=Path(__file__).resolve().parents[1]
SOURCES=('tools/native_gx_projection_probe.cpp','source/vendor/ntr/gx.cpp',
         'source/vendor/ntr/texture.cpp','source/core/game_textures.cpp','source/core/game_geometry.cpp')
HEADERS=('include/ntr/gx.h','include/sm64ds/game_projection.hpp','include/sm64ds/game_renderer.hpp')
EVIDENCE_SOURCES=(*SOURCES,*HEADERS,'source/ctr/game_renderer.cpp')


def clip(vertex,matrix):
    x,y,z=vertex[7:10]
    return [x*matrix[c]+y*matrix[c+4]+z*matrix[c+8]+matrix[c+12] for c in range(4)]


def ndc(value):
    return [value[c]/value[3] if abs(value[3])>1e-6 else 0.0 for c in range(3)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot-dir',type=Path,required=True)
    parser.add_argument('--prefix',default='scene-1-first')
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-runtime/gx-projection-audit.json')
    args=parser.parse_args()
    files={kind:args.snapshot_dir/(args.prefix+'-'+kind+extension)
           for kind,extension in (('gx','.bin'),('textures','.bin'),('palettes','.bin'),('geometry','.json'))}
    source_bytes={name:(ROOT/name).read_bytes() for name in EVIDENCE_SOURCES}
    raw=source_bytes['source/vendor/ntr/gx.cpp'].decode()
    vertex_site='    out.color = g.color;\n    return out;'
    triangle_site=('    std::memcpy(t.projection, g.proj.m, sizeof t.projection);\n'
                   '    t.native_projection = true;\n    g.tris.push_back(t);')
    assert raw.count(vertex_site)==raw.count(triangle_site)==1,'Reviewed projection/triangle tap site changed'
    tapped=raw.replace(vertex_site,'    out.color = g.color;\n'+
        '    ::sm64ds_projection_vertex_tap(out,g.proj.m,g.vp_x,g.vp_y,g.vp_w,g.vp_h,g.strip.size());\n    return out;')
    tapped=tapped.replace(triangle_site,'    std::memcpy(t.projection, g.proj.m, sizeof t.projection);\n'+
        '    t.native_projection = true;\n'+
        '    ::sm64ds_projection_triangle_tap(a,b,c,g.strip.data(),g.strip.size());\n    g.tris.push_back(t);')
    preamble=('#include "ntr/gx.h"\nvoid sm64ds_projection_vertex_tap(const ntr::GxVertex&,const float*,int,int,int,int,std::size_t);\n'+
        'void sm64ds_projection_triangle_tap(const ntr::GxVertex&,const ntr::GxVertex&,const ntr::GxVertex&,const ntr::GxVertex*,std::size_t);\n')
    with tempfile.TemporaryDirectory(prefix='sm64ds-projection-audit-') as directory:
        temp=Path(directory)
        frozen=[]
        for name in SOURCES:
            data=source_bytes[name]
            path=temp/Path(name).name;path.write_bytes(data);frozen.append(path)
        for name in HEADERS:
            path=temp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(source_bytes[name])
        body=temp/'gx-tapped.cpp';body.write_text(preamble+tapped)
        metadata=temp/'metadata.bin';metadata.write_bytes(audit_native_gx_stream.encode_metadata(json.loads(files['geometry'].read_text())))
        results={};outputs={}
        for mode in ('intact','tap'):
            sources=[body if mode=='tap' and p.name=='gx.cpp' else p for p in frozen]
            executable=temp/mode
            flags=['-std=c++20','-O2','-Wall','-Wextra','-Werror','-I'+str(temp/'include'),'-I'+str(ROOT/'include'),
                   '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
            subprocess.run(['c++',*flags,*map(str,sources),'-o',str(executable)],check=True,cwd=ROOT)
            output=temp/(mode+'-geometry.bin');trace=temp/(mode+'-tap.bin')
            run=subprocess.run([str(executable),str(files['gx']),str(metadata),str(files['textures']),
                                str(files['palettes']),str(output),str(trace)],check=True,capture_output=True,text=True)
            assert not run.stderr,run.stderr
            results[mode]=json.loads(run.stdout);outputs[mode]=output.read_bytes()
            if mode=='tap':trace_bytes=trace.read_bytes()
        assert outputs['intact']==outputs['tap'],'Read-only taps changed decoded geometry/material bytes'
        final=list(struct.unpack_from('<16f',trace_bytes))
        nvertices,ntriangles=struct.unpack_from('<2I',trace_bytes,64)
        assert ntriangles==results['intact']['triangles'] and len(trace_bytes)==72+nvertices*124+ntriangles*12
        vertices=[]
        for i in range(nvertices):
            base=72+i*124
            vertex=list(struct.unpack_from('<6fI3f',trace_bytes,base))
            matrix=list(struct.unpack_from('<16f',trace_bytes,base+40))
            viewport=list(struct.unpack_from('<4i',trace_bytes,base+104))
            word=struct.unpack_from('<I',trace_bytes,base+120)[0]
            vertices.append({'vertex':vertex,'matrix':matrix,'viewport':viewport,'word':word})
        triangles=list(struct.iter_unpack('<3I',trace_bytes[72+nvertices*124:]))
        # Explicit field serialization avoids host/ARM ABI padding. Validate
        # the new per-triangle contract against matrices observed at vertices,
        # independently of the state that emit_tri() copied into the triangle.
        geometry_bytes=outputs['intact']
        assert len(geometry_bytes)==4+ntriangles*195,'Triangle evidence record changed'
        for index,triangle in enumerate(triangles):
            base=4+index*195
            stored_matrix=list(struct.unpack_from('<16f',geometry_bytes,base+130))
            assert geometry_bytes[base+194]==1,('Missing native projection',index)
            assert all(stored_matrix==vertices[vertex]['matrix'] for vertex in triangle),(
                'Triangle projection differs from its vertex projection',index,triangle)
        used=sorted(set(index for triangle in triangles for index in triangle))
        matrices=[];groups=[]
        for entry in vertices:
            if entry['matrix'] not in matrices:matrices.append(entry['matrix'])
            entry['matrix_id']=matrices.index(entry['matrix'])
            group=(entry['matrix_id'],tuple(entry['viewport']))
            if group not in groups:groups.append(group)
            entry['group_id']=groups.index(group)
        mismatches=[];max_clip=max_ndc=max_screen=0.0;projection_counts=Counter();triangle_counts=Counter()
        for index in used:
            entry=vertices[index];correct=clip(entry['vertex'],entry['matrix']);wrong=clip(entry['vertex'],final)
            correct_ndc,wrong_ndc=ndc(correct),ndc(wrong)
            delta_clip=max(abs(a-b) for a,b in zip(correct,wrong))
            delta_ndc=max(abs(a-b) for a,b in zip(correct_ndc,wrong_ndc))
            delta_screen=max(abs(correct_ndc[0]-wrong_ndc[0])*128,abs(correct_ndc[1]-wrong_ndc[1])*96)
            max_clip=max(max_clip,delta_clip);max_ndc=max(max_ndc,delta_ndc);max_screen=max(max_screen,delta_screen)
            projection_counts[entry['matrix_id']]+=1
            if delta_clip>1e-5:
                mismatches.append({'vertex':index,'word':entry['word'],'matrix_id':entry['matrix_id'],
                    'view_position':entry['vertex'][7:10],'viewport':entry['viewport'],
                    'correct_clip':correct,'final_matrix_clip':wrong,'correct_ndc':correct_ndc,
                    'final_matrix_ndc':wrong_ndc,'max_source_pixel_delta':delta_screen})
        mixed=0;wrong_triangles=0;mismatch_ids={entry['vertex'] for entry in mismatches}
        for triangle in triangles:
            ids=tuple(vertices[i]['matrix_id'] for i in triangle)
            if len(set(ids))!=1:mixed+=1
            else:triangle_counts[ids[0]]+=1
            if any(i in mismatch_ids for i in triangle):wrong_triangles+=1
        # Recompute the decoder's stored screen coordinates to verify the taps
        # captured the actual matrix/viewport used at each vertex.
        max_reconstruction=0.0
        for index in used:
            entry=vertices[index];value=clip(entry['vertex'],entry['matrix']);normal=ndc(value)
            x,y,w,h=entry['viewport']
            reconstructed=[(normal[0]+1)*.5*w+x,(1-(normal[1]+1)*.5)*h+y,(normal[2]+1)*.5,value[3]]
            max_reconstruction=max(max_reconstruction,max(abs(a-b) for a,b in zip(reconstructed,entry['vertex'][:4])))
        assert max_reconstruction<1e-4,('Observed projection does not reproduce decoder coordinates',max_reconstruction)
        report={'status':'audit completed','scope':'host replay of preserved scene1 GX, no GPU/scene loop',
            'snapshot':{kind:{'path':str(path.resolve()),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for kind,path in files.items()},
            'frozen_source_sha256':{name:hashlib.sha256(data).hexdigest() for name,data in source_bytes.items()},
            'decoder':results,'tap_geometry_matches_intact_byte_for_byte':True,
            'native_projection_triangles_checked':ntriangles,'projection_fields_match_vertex_taps':True,
            'triangle_bytes_sha256':hashlib.sha256(outputs['intact']).hexdigest(),
            'projection_matrices':matrices,'final_projection_matrix':final,
            'vertices':nvertices,'used_vertices':len(used),'triangles':ntriangles,
            'used_vertices_by_projection':dict(projection_counts),'triangles_by_projection':dict(triangle_counts),
            'mixed_projection_triangles':mixed,'distinct_used_viewports':sorted(set(tuple(vertices[i]['viewport']) for i in used)),
            'vertices_differing_under_final_projection':len(mismatches),'triangles_differing_under_final_projection':wrong_triangles,
            'max_clip_component_delta':max_clip,'max_ndc_component_delta':max_ndc,
            'max_original_full_viewport_pixel_delta':max_screen,
            'max_reconstructed_decoder_coordinate_delta':max_reconstruction,
            'examples':sorted(mismatches,key=lambda e:e['max_source_pixel_delta'],reverse=True)[:8],
            'sanitizers':['address','undefined'],'Azahar_run':False,'physical_hardware':False,
            'gameplay_verified':False,'target_performance_verified':False,
            'renderer_source_sha256':hashlib.sha256((ROOT/'source/ctr/game_renderer.cpp').read_bytes()).hexdigest()}
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:report[k] for k in ('status','triangles','used_vertices_by_projection','triangles_by_projection',
            'vertices_differing_under_final_projection','triangles_differing_under_final_projection',
            'max_original_full_viewport_pixel_delta','max_reconstructed_decoder_coordinate_delta','examples')},indent=2))


if __name__=='__main__':main()
