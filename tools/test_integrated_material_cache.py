#!/usr/bin/env python3
"""Replay the actual pinned material-setup block into a GPU-register recorder.

Compare every draw boundary with/without the cache, using captured ARM32
batches plus changed filters, missing textures, shadows and pass boundaries.
This is register equivalence, not a raster-image or physical FPS claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
from elftools.elf.elffile import ELFFile
from inspect_integrated_vm import VM
from prepare_integrated_draw_material import material_block

ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('attempt',type=Path,nargs='?');args=p.parse_args()
 cases=[];inputs={};closed={}
 if args.attempt is not None:
  attempt=args.attempt.resolve();closed=json.loads((attempt/'result.json').read_text())
  assert closed['status']=='acquisition_closed' and closed['first_emulator_fault'] is None
  elf=attempt/'sm64ds-integrated.elf';assert sha(elf)==closed['ELF_sha256']
  with elf.open('rb') as f:symbols={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
  for path in sorted((attempt/'user/sdmc/3ds/sm64ds').glob('*.vm')):
   assert sha(path)==closed['files'][path.relative_to(attempt).as_posix()]['sha256'];inputs[path.name]=sha(path)
   vm=VM(path);start,end,cap=struct.unpack('<3I',vm.read(symbols['_ZL12mesh_batches'],12))
   a,z,c=struct.unpack('<3I',vm.read(symbols['_ZL16g_batch_textures'],12))
   assert start<=end<=cap and (end-start)%40==0;count=(end-start)//40;assert (z-a)//4==count
   texture_ids={}
   for i in range(count):
    tex,tw,th,wrap,first,n,dynamic,attr,version=struct.unpack('<IiiIiiB3xIQ',vm.read(start+i*40,40));gpu=vm.u32(a+i*4)
    if gpu not in texture_ids:texture_ids[gpu]=len(texture_ids)+1
    cases.append([int(bool(tex)),texture_ids[gpu] if gpu else 0,wrap,attr])
 else:
  # Fresh-checkout reference workload: deterministic material transitions,
  # repeats, missing textures and all sampler-wrap flags, not captured batches.
  for i in range(256):
   row=[int(i%5!=0),1+i%8,i%16,0x001f00c0|((i%4)<<4)|((i%2)<<15)]
   cases.extend([row,row])
  cases.extend([[1,0,3,0x001f00c0],[0,0,0,0x001f0040]])
 if any(row[1]>=512 for row in cases):raise ValueError('Material fixture exceeds texture recorder capacity')
 assert cases
 source_path=ROOT/'third_party/sm64ds-3ds/port/platform/3ds/viewer_3ds.cpp'
 _,_,block=material_block(source_path.read_text())
 # The recorder supplies the same atomic setting value per batch.
 block=block.replace('enhance::live().tex_filter.load(std::memory_order_relaxed)','policy')
 block=block.replace('const unsigned policy=policy;','const unsigned selected_policy=policy;').replace('if(policy)touched=true;','if(selected_policy)touched=true;').replace('policy==2','selected_policy==2').replace('policy==1','selected_policy==1')
 prefix=r'''
#include "draw_material_cache.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
using GPU_TEXTURE_FILTER_PARAM=int;
enum {GPU_CULL_NONE,GPU_CULL_BACK_CCW,GPU_CULL_FRONT_CCW,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_MODULATE,GPU_REPLACE,GPU_LINEAR,GPU_NEAREST,C3D_Both,C3D_RGB,C3D_Alpha};
struct C3D_Tex {int id=0,s=0,t=0,min=GPU_NEAREST,mag=GPU_NEAREST;};
struct GpuTex {C3D_Tex tex;bool has_zero_alpha=false;};
struct V3dsBatch {bool tex;uint32_t wrap,polygon_attr;};
struct C3D_TexEnv {std::array<int,6> src{};std::array<int,2> fn{};uint32_t color=0;};
C3D_TexEnv environment;int cull=0;uint32_t fog=0;C3D_Tex* bound=nullptr;unsigned setups=0;
std::vector<GpuTex> textures(512);std::vector<GpuTex*> g_batch_textures;
void C3D_CullFace(int p){cull=p;}
C3D_TexEnv* C3D_GetTexEnv(int n){assert(n==0);return &environment;}
void C3D_TexEnvInit(C3D_TexEnv* e){*e={};}
void C3D_TexEnvSrc(C3D_TexEnv* e,int channels,int a,int b,int c){for(int k=0;k<2;++k)if(channels==C3D_Both||channels==(k?C3D_Alpha:C3D_RGB)){e->src[k*3]=a;e->src[k*3+1]=b;e->src[k*3+2]=c;}}
void C3D_TexEnvFunc(C3D_TexEnv* e,int channels,int f){for(int k=0;k<2;++k)if(channels==C3D_Both||channels==(k?C3D_Alpha:C3D_RGB))e->fn[k]=f;}
void C3D_TexEnvColor(C3D_TexEnv* e,uint32_t c){e->color=c;}
void C3D_TexSetWrap(C3D_Tex* t,int s,int v){t->s=s;t->t=v;}
void C3D_TexSetFilter(C3D_Tex* t,int a,int b){t->min=a;t->mag=b;}
void C3D_TexBind(int n,C3D_Tex* t){assert(n==0);bound=t;}
int wrap_mode(int a,int b){return bool(a)+2*bool(b);}
void fog_stage(uint32_t a){fog=a;}
struct Case{unsigned textured,texture,wrap,attr;};
std::vector<std::array<uint32_t,17>> run(const std::vector<Case>& cases,bool optimized,bool native,bool frame_plan,bool changing_filter){
 environment={};cull=0;fog=0;bound=nullptr;setups=0;
 for(unsigned i=0;i<textures.size();++i){textures[i]={};textures[i].tex.id=i;textures[i].has_zero_alpha=i%2;}
 std::vector<V3dsBatch> batches;g_batch_textures.clear();
 for(auto c:cases){batches.push_back({c.textured!=0,c.wrap,c.attr});g_batch_textures.push_back(c.texture?&textures[c.texture]:nullptr);}
 const auto* b=batches.data();draw_material::Cache material_cache;
 std::vector<std::array<uint32_t,17>> snapshots;
 for(unsigned i=0;i<cases.size();++i){
  const unsigned faces=b[i].polygon_attr&0xc0;if(!faces)continue;
  const auto* material_texture=b[i].tex?g_batch_textures[i]:nullptr;
  if(b[i].tex&&!material_texture){material_cache.invalidate();continue;}
  const unsigned policy=changing_filter?(i/3)%3:0;
  draw_material::Key key{reinterpret_cast<std::uintptr_t>(material_texture),b[i].polygon_attr,b[i].wrap,policy,b[i].tex};
  const bool safe=native&&!frame_plan&&((b[i].polygon_attr>>4)&3)!=3;
  if(!optimized||material_cache.setup(key,safe)){
   ++setups;
'''
 suffix=r'''
  }
  if(!((b[i].polygon_attr>>16)&31))continue;
  std::array<uint32_t,17> s{};s[0]=cull;s[1]=fog;s[2]=environment.color;
  for(unsigned k=0;k<6;++k)s[3+k]=environment.src[k];
  for(unsigned k=0;k<2;++k)s[9+k]=environment.fn[k];
  if(bound){s[11]=bound->id;s[12]=bound->s;s[13]=bound->t;s[14]=bound->min;s[15]=bound->mag;}
  s[16]=b[i].polygon_attr;snapshots.push_back(s);
  if(frame_plan||((b[i].polygon_attr>>4)&3)==3){environment={};environment.color=0x87654321;cull=GPU_CULL_NONE;fog=0;}
 }
 return snapshots;
}
'''
 arrays='std::vector<Case> captured={' + ','.join('{'+','.join(str(n)+'u' for n in row)+'}' for row in cases)+'};\n'
 maincpp=r'''
int main(){
 unsigned original_setups=0,cached_setups=0,comparisons=0;
 for(bool native:{false,true})for(bool plan:{false,true})for(bool filters:{false,true}){
  auto a=run(captured,false,native,plan,filters);unsigned before=setups;
  auto b=run(captured,true,native,plan,filters);assert(a==b);++comparisons;
  if(native&&!plan&&!filters){original_setups=before;cached_setups=setups;}
 }
 // Mutate each part of the key, with rejected/missing resources and shadow
 // helpers between repetitions. Compare the actual original setup block.
 uint32_t rng=0x73594ab1;
 std::vector<Case> stress;
 for(unsigned i=0;i<12000;++i){rng=rng*1664525+1013904223;Case c{unsigned((rng>>7)&1),unsigned((rng>>9)%8),unsigned((rng>>13)&15),0x001f00c0u|((rng>>17)&0x3f000030)};stress.push_back(c);stress.push_back(c);if(i%11==0)stress.push_back({1,0,3,0x001f00c0});}
 for(bool plan:{false,true})for(bool filters:{false,true})assert(run(stress,false,true,plan,filters)==run(stress,true,true,plan,filters));
 assert(cached_setups<original_setups);
 std::printf("register_trace_equivalent combinations=%u original_setups=%u cached_setups=%u\n",comparisons+4,original_setups,cached_setups);
}
'''
 out=ROOT/'build/integrated-native'/('material-cache-'+str(time.time_ns()));out.mkdir()
 cpp=out/'register-replay.cpp';cpp.write_text(prefix+block+suffix+arrays+maincpp)
 exe=out/'register-replay'
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
   '-I'+str(ROOT/'source/integrated_native'),str(cpp),'-o',str(exe)],check=True,capture_output=True,text=True)
 result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
 report=dict(status='actual_original_material_register_trace_equivalent',ELF_sha256=closed.get('ELF_sha256'),captured_batches=len(cases) if args.attempt else 0,
   input_batches=len(cases),fixture_kind='captured' if args.attempt else 'authored_synthetic',
   captured_VM_sha256=inputs,ASan=True,UBSan=True,output=result.stdout,
   sources={str(q.relative_to(ROOT)):sha(q) for q in [source_path,ROOT/'source/integrated_native/draw_material_cache.hpp',ROOT/'tools/prepare_integrated_draw_material.py',Path(__file__)]},
   limitation='GPU register recorder at draw boundaries. Does not establish raster pixel equivalence, measured time improvement or physical60 FPS.')
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 (ROOT/'build/integrated-native/material-cache-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(result.stdout,end='');print(out/'report.json')


if __name__=='__main__':main()
