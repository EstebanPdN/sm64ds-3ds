#!/usr/bin/env python3
"""Host projection/batch regression using real decoder and preserved scene GX.

No emulator, GPU or physical-console run. Compare target clip conversion with
independent DS clip arithmetic; the former final-matrix behavior must fail for
the six triangles identified by the per-vertex read-only audit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import audit_native_gx_stream

ROOT=Path(__file__).resolve().parents[1]
SOURCES=('tests/test_game_projection.cpp','source/vendor/ntr/gx.cpp','source/vendor/ntr/texture.cpp',
         'source/core/game_textures.cpp','source/core/game_geometry.cpp')
HEADERS=('include/ntr/gx.h','include/sm64ds/game_projection.hpp','include/sm64ds/game_renderer.hpp')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot-dir',type=Path,required=True)
    parser.add_argument('--prefix',default='scene-1-first')
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-runtime/game-projection-test.json')
    args=parser.parse_args()
    files={kind:args.snapshot_dir/(args.prefix+'-'+kind+extension)
           for kind,extension in (('gx','.bin'),('textures','.bin'),('palettes','.bin'),('geometry','.json'))}
    evidence=(*SOURCES,*HEADERS,'source/ctr/game_renderer.cpp')
    source_bytes={name:(ROOT/name).read_bytes() for name in evidence}
    with tempfile.TemporaryDirectory(prefix='sm64ds-game-projection-') as directory:
        temporary=Path(directory)
        sources=[]
        for name in SOURCES:
            path=temporary/Path(name).name;path.write_bytes(source_bytes[name]);sources.append(path)
        for name in HEADERS:
            path=temporary/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(source_bytes[name])
        metadata=temporary/'metadata.bin'
        metadata.write_bytes(audit_native_gx_stream.encode_metadata(json.loads(files['geometry'].read_text())))
        executable=temporary/'test-game-projection'
        flags=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(temporary/'include'),'-I'+str(ROOT/'include'),
               '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
        subprocess.run(['c++',*flags,*map(str,sources),'-o',str(executable)],check=True,cwd=ROOT)
        run=subprocess.run([str(executable),str(files['gx']),str(metadata),str(files['textures']),str(files['palettes'])],
                           check=True,capture_output=True,text=True)
        assert not run.stderr,run.stderr
        report=json.loads(run.stdout)
        assert report['status']=='passed' and report['snapshot_triangles']==78
        report.update({'scope':'host helper and real GX/texture replay; target clip arithmetic, no GPU execution',
            'source_sha256':{name:hashlib.sha256(data).hexdigest() for name,data in source_bytes.items()},
            'snapshot':{kind:{'path':str(path.resolve()),'bytes':path.stat().st_size,
                'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for kind,path in files.items()},
            'compiler_flags':flags,'sanitizers':['address','undefined'],'Azahar_run':False,
            'physical_hardware':False,'gameplay_verified':False,'target_performance_verified':False})
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))


if __name__=='__main__':main()
