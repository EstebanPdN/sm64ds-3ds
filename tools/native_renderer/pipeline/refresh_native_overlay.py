from pathlib import Path
import hashlib,json
root=Path.cwd();s=root/'build/native-renderer/game-runtime-20261026-r1/source';receipt=s/'native-renderer-overlay.json';r=json.loads(receipt.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for n,h in r['files'].items():assert sha(s/n)==h,n
changed=[]
for d in ['source/native_renderer','source/integrated_native/renderer']:
 for p in (root/d).iterdir():
  if p.suffix not in ('.cpp','.hpp','.h','.pica','.S'):continue
  dest=s/'port/platform/3ds/native_renderer'/p.name
  if not dest.is_file() or sha(p)!=sha(dest):dest.write_bytes(p.read_bytes());changed.append(p.name)
  r['files'][str(dest.relative_to(s))]=sha(dest)
for source,name in [('source/core/fixed_math.cpp','fixed_math.cpp'),('include/sm64ds/fixed_math.hpp','sm64ds/fixed_math.hpp')]:
 p=root/source;dest=s/'port/platform/3ds/native_renderer'/name
 if sha(p)!=sha(dest):dest.write_bytes(p.read_bytes());changed.append(name)
 r['files'][str(dest.relative_to(s))]=sha(dest)
p=root/'source/integrated_native/native_diagnostics.hpp';dest=s/'port/platform/3ds/native_diagnostics.hpp'
if sha(p)!=sha(dest):dest.write_bytes(p.read_bytes());changed.append(p.name)
r['files'][str(dest.relative_to(s))]=sha(dest)
receipt.write_text(json.dumps(r,indent=2)+'\n');print('Verified overlay; refreshed',changed)
