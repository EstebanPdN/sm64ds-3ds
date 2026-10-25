#!/usr/bin/env python3
"""Compare replacement UI PPU layers against exact masked captured registers."""
from pathlib import Path
import hashlib,json,shutil,subprocess,time
from prepare_integrated_render60 import affine_alpha_source
from prepare_integrated_native_ui import ppu_source
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/integrated-native'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def source_set():
 return {str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),ROOT/'tools/prepare_integrated_native_ui.py',ROOT/'source/integrated_native/options/native_lower_state.hpp',ROOT/'tests/options_menu/native_ui_ppu_replay.cpp',ROOT/'tests/integrated_ppu_replay.cpp']}
def expected():return ppu_source(affine_alpha_source((ROOT/'third_party/sm64ds-3ds/port/ntr/ppu.cpp').read_text()))
def verify():
 r=json.loads((OUT/'native-ui-ppu-gate-latest.json').read_text())
 assert r['status']=='native_ui_PPU_pixel_equivalent' and r['sources']==source_set()
 assert r['derived_ppu_sha256']==hashlib.sha256(expected().encode()).hexdigest()
 assert len(r['fixtures'])>=2 and all(x['baseline_equal'] and x['mask_toggles_equal'] for x in r['fixtures'])
 for x in r['fixtures']:
  for name,digest in x['fixture_files'].items():assert sha(Path(x['fixture'])/name)==digest
 return r
def check():
 try:return verify()
 except (OSError,AssertionError):pass
 port=ROOT/'third_party/sm64ds-3ds/port';directory=OUT/('native-ui-ppu-'+str(time.time_ns()));directory.mkdir()
 headers=directory/'platform/3ds';headers.mkdir(parents=True);shutil.copyfile(ROOT/'source/integrated_native/options/native_lower_state.hpp',headers/'native_lower_state.hpp')
 raw=affine_alpha_source((port/'ntr/ppu.cpp').read_text());base=directory/'baseline.cpp';base.write_text(raw)
 changed=directory/'native.cpp';changed.write_text(ppu_source(raw))
 compiler=shutil.which('clang++');assert compiler
 def run(args,log):
  r=subprocess.run(list(map(str,args)),capture_output=True,text=True);log.write_text(r.stdout+r.stderr);r.check_returncode()
 exes={}
 for mode,source in [('baseline',base),('native',changed)]:
  for test in ['full','toggle']:
   exe=directory/(mode+'-'+test);exes[mode,test]=exe
   main=ROOT/('tests/integrated_ppu_replay.cpp' if test=='full' else 'tests/options_menu/native_ui_ppu_replay.cpp')
   run([compiler,'-std=c++17','-O2','-g','-D__3DS__','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(port/'ntr/include'),'-I'+str(ROOT/'source/integrated_native'),'-I'+str(directory),main,source,port/'ntr/vram_banks.cpp',port/'ntr/bitmap.cpp','-o',exe],directory/(mode+'-'+test+'-build.log'))
 # Fixtures retain explicit provenance: historical captures or authored input.
 old=json.loads((OUT/'affine-alpha-gate-latest.json').read_text());fixtures=sorted(Path(old['directory']).glob('fixture-*'));assert len(fixtures)>=2
 results=[]
 for i,fixture in enumerate(fixtures):
  row={'fixture':str(fixture),'fixture_files':{p.name:sha(p) for p in fixture.glob('region-*.bin')}}
  for test in ['full','toggle']:
   outputs=[]
   for mode in ['baseline','native']:
    out=directory/f'{i}-{mode}-{test}.pixels';args=[exes[mode,test],fixture,out]
    if test=='toggle':args.append('1' if mode=='native' else '0')
    run(args,directory/f'{i}-{mode}-{test}-run.log');outputs.append(out)
   assert outputs[0].read_bytes()==outputs[1].read_bytes(),(i,test)
   row['baseline_equal' if test=='full' else 'mask_toggles_equal']=True
   row[test+'_sha256']=sha(outputs[0])
  results.append(row)
 report={'status':'native_ui_PPU_pixel_equivalent','sources':source_set(),'derived_ppu_sha256':sha(changed),'directory':str(directory),'fixtures':results,'fixture_kind':old.get('fixture_kind','captured'),'ASan':True,'UBSan':True,'limits':'Host PPU pixel and cache-toggle differential; fixture provenance recorded separately. No emulator, GPU sampling or hardware performance acceptance.'}
 (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n');(OUT/'native-ui-ppu-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
 return verify()
if __name__=='__main__':print(json.dumps(check(),indent=2))
