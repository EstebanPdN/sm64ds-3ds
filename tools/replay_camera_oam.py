#!/usr/bin/env python3
"""Execute captured OAM RenderSub in an offline RAM copy for all camera states.

Never launches a game or writes the supplied capture. Incomplete captures use
only fully stored readable regions; they do not establish whole-RAM coverage.
"""
import argparse,hashlib,json,struct,time
from pathlib import Path
from elftools.elf.elffile import ELFFile
from read_mapped_capture import MappedCapture
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_CPSR
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--elf',required=True,type=Path);p.add_argument('--allow-incomplete',action='store_true');a=p.parse_args()
 vm=MappedCapture(a.capture,allow_incomplete=a.allow_incomplete)
 with a.elf.open('rb') as f:
  image=ELFFile(f);symbols={s.name:s['st_value'] for s in image.get_section_by_name('.symtab').iter_symbols()}
  text=image.get_section_by_name('.text');assert vm.read(text['sh_addr'],text['sh_size'])==text.data()
 assert vm.read(symbols['data_0209e660'],1)==b'\x00','Expected captured engine-B staging selection'
 cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for r in vm.regions:
  if r['captured_bytes']!=r['size'] or not r['captured_bytes']:continue
  cpu.mem_map(r['address'],r['size']);cpu.mem_write(r['address'],vm.read(r['address'],r['size']))
 stack=0x0e000000;stop=0x0e020000;cpu.mem_map(stack,0x10000);cpu.mem_map(stop,0x1000)
 regions=struct.unpack('<8I',vm.read(symbols['_ZN3ntr8g_regionE'],32))
 output=ROOT/'build/native-hud'/('camera-states-'+str(time.time_ns()));output.mkdir(parents=True)
 templates=['CAM_BUTTON_L','CAM_BUTTON_R','S_CAM_BUTTON_L','S_CAM_BUTTON_R','CAM_ZOOM_BUTTON','S_CAM_ZOOM_BUTTON']
 results=[]
 for template in templates:
  for pressed in (False,True):
   name=template+('_PRESSED' if pressed else '')
   address=symbols['_ZN3OAM'+str(len(name))+name+'E']
   cpu.mem_write(symbols['data_0209e670'],struct.pack('<I',0));cpu.mem_write(symbols['data_0209e66c'],struct.pack('<I',0))
   cpu.reg_write(UC_ARM_REG_CPSR,0x10);cpu.reg_write(UC_ARM_REG_SP,stack+0x8000);cpu.reg_write(UC_ARM_REG_LR,stop)
   cpu.reg_write(UC_ARM_REG_R0,address);cpu.reg_write(UC_ARM_REG_R1,25);cpu.reg_write(UC_ARM_REG_R2,165);cpu.reg_write(UC_ARM_REG_R3,0xffffffff)
   cpu.mem_write(stack+0x8000,struct.pack('<I',1))
   cpu.emu_start(symbols['_ZN3OAM9RenderSubEP7OamAttriiii'],stop,count=1000000)
   assert cpu.reg_read(UC_ARM_REG_PC)==stop and cpu.reg_read(UC_ARM_REG_SP)==stack+0x8000
   count=struct.unpack('<I',cpu.mem_read(symbols['data_0209e670'],4))[0];assert 0<count<=16
   oam=bytearray(struct.pack('<4H',0x200,0,0,0)*128)
   oam[:count*8]=cpu.mem_read(symbols['data_0209ea74'],count*8)
   directory=output/name;directory.mkdir();(directory/'oam.bin').write_bytes(oam)
   for key,address,size in [('display',regions[4]+0x1000,4),('palette',regions[5]+0x600,512),('vram',regions[6]+0x600000,0x20000)]:
    (directory/(key+'.bin')).write_bytes(vm.read(address,size))
   results.append(dict(name=name,sprites=count,directory=str(directory),OAM_sha256=sha(directory/'oam.bin')))
 report=dict(status='captured_RenderSub_camera_templates_executed',capture_sha256=sha(a.capture),ELF_sha256=sha(a.elf),
  source_sha256=sha(Path(__file__)),complete_capture=vm.complete,variants=results,
  offline_parameter_changes='Fresh OAM staging counts; template pointer, position25/165, palette-1, priority1. Saved VRAM/palette retained.',
  live_game_executed=False,input_capture_modified=False,physical_hardware_proof=False)
 (output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(output)
if __name__=='__main__':main()
