#!/usr/bin/env python3
"""Run an isolated native-resource preview; never copy the personal profile."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'third_party/sm64ds-3ds/build3ds'))
import run_isolated


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/native-renderer/arm/sm64ds-native-asset-lab.3dsx')
    parser.add_argument('--asset',type=Path,required=True)
    parser.add_argument('--instance',type=Path,required=True)
    parser.add_argument('--app',type=Path,required=True)
    parser.add_argument('--3ds-model',dest='model',choices=['old','new'],required=True)
    parser.add_argument('--timeout',type=float,default=90)
    parser.add_argument('--prepare-only',action='store_true')
    args=parser.parse_args()
    if not 0<args.timeout<=300:parser.error('Timeout must be within (0,300] seconds')
    asset=args.asset.resolve(strict=True)
    if not 128<=asset.stat().st_size<=8*1024*1024:parser.error('Preview asset size outside bound')
    if asset.read_bytes()[:8]!=b'SM64NMD2':parser.error('Expected a prepared version2 native bank')
    try:
        plan=run_isolated.prepare(args.binary,args.instance,args.app,[],args.model)
        destination=Path(plan['sd_directory'])/'model.nmd'
        shutil.copy2(asset,destination)
        plan['native_asset']={'sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),
            'bytes':destination.stat().st_size,'source_path':str(asset),'scope':'prepared resource preview only'}
        (args.instance/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
        if args.prepare_only:
            print('PREPARED:',plan['instance']);return 0
        result=run_isolated.execute(plan,args.timeout)
        print('RESULT:',result['result'],'\nINSTANCE:',plan['instance'])
        return 0 if result['result']=='PASS' else 1
    except (ValueError,RuntimeError,OSError) as error:parser.exit(2,str(error)+'\n')


if __name__=='__main__':raise SystemExit(main())
