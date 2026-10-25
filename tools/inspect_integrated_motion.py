#!/usr/bin/env python3
"""Read the actual production motion owner in complete, closed process captures."""
import argparse,json,hashlib,struct,math
from pathlib import Path
from elftools.elf.elffile import ELFFile
from inspect_integrated_vm import VM
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('attempt',type=Path);a=p.parse_args();root=a.attempt.resolve();closed=json.loads((root/'result.json').read_text());assert closed['status']=='acquisition_closed'
 elf=root/'sm64ds-integrated.elf';assert sha(elf)==closed['ELF_sha256']
 with elf.open('rb') as f:
  e=ELFFile(f);symbols={s.name:(s['st_value'],s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols()};t=e.get_section_by_name('.text');address=t['sh_addr'];code=t.data()
 def sym(name):return symbols['_ZN13native_motion12_GLOBAL__N_1L'+str(len(name))+name+'E'][0]
 rows=[]
 for capture in sorted((root/'user/sdmc/3ds/sm64ds').glob('*.vm')):
  assert sha(capture)==closed['files'][capture.relative_to(root).as_posix()]['sha256'];vm=VM(capture);assert vm.read(address,len(code))==code
  def byte(name):return vm.read(sym(name),1)[0]
  mode=byte('mode');owned=byte('owned');faulted=byte('faulted');initialized=byte('initialized');assert mode<3 and owned<2 and faulted<2 and initialized<2
  steps=vm.u32(sym('steps'));mario=struct.unpack('<i',vm.read(sym('mario'),4))[0]
  row=dict(snapshot=capture.name,VM_sha256=sha(capture),mode=['Original','Hybrid','Nintendo 64'][mode],owns_position=bool(owned),native_fault_latched=bool(faulted),initialized=bool(initialized),solver=mario,steps=steps,ds_held_jump=bool(byte('ds_jump')))
  if owned:
   assert mode and initialized and not faulted and mario>=0 and steps>0
   position=list(struct.unpack('<3f',vm.read(sym('state'),12)));assert all(math.isfinite(x) for x in position);row['native_position']=position;row['action']=hex(vm.u32(sym('state')+36))
  rows.append(row)
 assert rows
 report=dict(status='production_motion_owner_captured',ELF_sha256=sha(elf),snapshots=rows,observed_native_modes=sorted({x['mode'] for x in rows if x['owns_position']}),limits='Captured ownership and finite staged state do not prove every DS interaction, level or physical performance. Baseline Original captures stay distinct from later manual captures.')
 target=root/'motion-analysis.json';assert not target.exists();target.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
