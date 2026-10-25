#!/usr/bin/env python3
"""Read-only PM/LCD/sleep frontier inventory; no native or ROM execution."""
import hashlib,json,re,subprocess
from pathlib import Path
import audit_native_stage_closure as stage

ROOT,ELF,TOOL=stage.ROOT,stage.ELF,stage.TOOL
OUT=ROOT/'build/native-stage-plan/power'
NAMES=('func_0205f650','func_0205f66c','func_0205f68c','func_0205f77c',
 'func_0205fde8','func_0205fc44','func_0205fcfc','func_0205fa78',
 'func_0205fab4','func_0205fbd0','func_0205f8b0','func_0205ff20',
 'func_0205ff08','func_0205feac','func_0205f958','func_0205f994',
 'func_0205f9ac','func_0205f9e8','func_02019ac4','func_02019a58',
 'func_02019ed0','func_020199a4','func_02019dfc','func_02019e60',
 'func_02030790','func_02058c84','func_0206a4a0','func_0205ba3c',
 'func_0205ba64','func_0205b858','func_0205bad8','IPCSend',
 '_ZN3IRQ7SetIRQsEj','_ZN3IRQ11DisableIRQsEj','_ZN3IRQ7DisableEv',
 '_ZN3IRQ7RestoreEj','_ZN4CP1516WaitForInterruptEv','func_02059d8c',
 '_ZN5Stage8BehaviorEv','_ZN6sm64ds30native_platform_boot_indicatorEv',
 '_ZN6sm64ds21native_platform_ticksEv')

def sha(raw):return hashlib.sha256(raw).hexdigest()

def main():
 assert sha(ELF.read_bytes())==stage.HASH
 OUT.mkdir(parents=True,exist_ok=True)
 disasm=subprocess.check_output([TOOL+'objdump','-d',str(ELF)],text=True)
 bodies={};name=None
 for line in disasm.splitlines():
  m=re.match(r'^([0-9a-f]+) <(.+)>:$',line)
  if m:name=m[2];bodies[name]=dict(address='0x'+m[1],lines=[])
  elif name and re.match(r'^\s+[0-9a-f]+:',line):bodies[name]['lines'].append(line)
 records={name:bodies.get(name) for name in NAMES}
 sources={}
 for p in (ROOT/'upstream/src').rglob('*'):
  if p.suffix in ('.c','.cpp','.S') and p.stem in NAMES:
   sources[str(p.relative_to(ROOT))]=sha(p.read_bytes())
 for name in ['source/game_port/native_platform.cpp','source/game_port/native_platform_abi.cpp',
  'source/game_port/native_boot.cpp','source/game_port/native_boot_services.cpp',
  'source/game_port/native_save.cpp','source/ctr/native_main.cpp','include/sm64ds/native_platform.hpp',
  'tools/audit_native_power_contract.py']:
  sources[name]=sha((ROOT/name).read_bytes())
 sources['/opt/devkitpro/libctru/include/3ds/services/apt.h']=sha(Path('/opt/devkitpro/libctru/include/3ds/services/apt.h').read_bytes())
 (OUT/'frozen-frontier-disassembly.txt').write_text('\n'.join(
  '\n'+record['address']+' <'+name+'>:\n'+'\n'.join(record['lines'])
  for name,record in records.items() if record)+'\n')
 for name,h in sources.items():
  p=Path(name) if name.startswith('/') else ROOT/name
  dst=OUT/'frozen-sources'/name.lstrip('/');dst.parent.mkdir(parents=True,exist_ok=True)
  raw=p.read_bytes();assert sha(raw)==h;dst.write_bytes(raw)
 report=dict(scope=__doc__,ELF=str(ELF.relative_to(ROOT)),ELF_sha256=stage.HASH,
  frozen_functions=records,current_source_sha256=sources,
  shared_words={'0x027ffc3c':dict(meaning='ARM7-maintained shared VBlank count, u32',
   consumers=['func_0205fde8 initializes data_020a8110','func_0205f68c records/counts LCD-off interval'],
   native_candidate='low32(native_platform_stats().vblanks); must document presentation-edge clock and blocking-loop limits'),
   '0x027ffc40':dict(meaning='Boot indication; this family tests only ==2 for download-play',
    native_provider='native_platform_boot_indicator(), currently 0; this is native cartridge-policy input, not a DS firmware word'),
   '0x027fffa8':dict(meaning='ARM7-shared extra-key/lid status; bit15 closed in func_02019ac4',
    native_provider='Normal NativeBoot phase0xb uses power_tick/aptMainLoop instead of dereferencing this DS word')},
  current_boundaries={'func_0205ba3c(channel,1)':'explicit sm64ds_native_missing; no ARM7 readiness fabricated',
   'IPCSend':'explicit sm64ds_native_missing; no PXI ack fabricated',
   'func_0205f8e0':'returns original nonzero error2 for unavailable DS firmware query',
   'IRQ::SetIRQs':'raw original IE/IME body in frozen ELF; unlike the adapted EnableIRQs/DisableIRQs',
   'CP15::WaitForInterrupt':'native split-loop wait_vblank adapter, cannot suspend an arbitrary synchronous PM caller',
   'APT':'NativeBoot power_tick and outer native_main aptMainLoop handle real lifecycle at frame boundaries; no aptHook registered'},
  external_sources=[
   'https://twlsdk.randommeaninglesscharacters.com/docs/nitro/NitroSDK/os/irq/OS_GetVBlankCount.html',
   'https://raw.githubusercontent.com/devkitPro/libctru/master/libctru/source/services/apt.c'],
  executions=0,production_changed=False,UI_launched=False,
  limits='Frozen ELF classification and separately hashed current sources are different evidence sets. This audit does not execute sleep, I/O, ARM7 or hardware.')
 (OUT/'inventory.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(dict(status='complete',functions_present=sum(v is not None for v in records.values()),
  source_count=len(sources),ELF_sha256=stage.HASH,report=str(OUT/'inventory.json'))))

if __name__=='__main__':main()
