"""PRIVATE offline census, using native validators and immutable asset banks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from component_fixture import make_fixture


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('banks',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--contracts',type=Path,required=True)
    parser.add_argument('--compiler',default='/usr/bin/c++')
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    raw=args.capture.read_bytes()
    if len(raw)>16*1024*1024:raise ValueError('Oversized component capture')
    capture=json.loads(raw)
    if capture['format']!='SM64_NATIVE_COMPONENT_SCENE_5':raise ValueError('Complete version5 capture required')
    header=make_fixture(capture)
    work=args.output.resolve().parent/(args.output.stem+'-private-census')
    work.mkdir(parents=True,exist_ok=True)
    fixture=work/'component_fixture.h';fixture.write_text(header)
    binary=work/'census'
    flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all'] if args.sanitize else []
    subprocess.run([args.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',*flags,
        '-I',str(root/'source/native_renderer'),'-DSM64DS_COMPONENT_FIXTURE="'+str(fixture)+'"',
        str(root/'tools/native_renderer/component_batch_report.cpp'),str(args.contracts.resolve()),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary),str(args.banks.resolve())],text=True,capture_output=True,check=True)
    report=json.loads(result.stdout)
    report['capture_sha256']=hashlib.sha256(raw).hexdigest()
    bank_ids=sorted({o[0] for o in capture['occurrences'] if o[1]==0})
    report['bank_sha256']={str(i):hashlib.sha256((args.banks/f'{i:05d}.nmd').read_bytes()).hexdigest() for i in bank_ids}
    report['limitations']=['World geometry only; compositor and image-layer draws excluded.',
        'Adjacent draws only; order, material and texture identity preserved.',
        'Palette16 and stream costs are not implemented or timed.',
        'Capacity, UV-domain, frame ownership and shader precision still require runtime validation.']
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
