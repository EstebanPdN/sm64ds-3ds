#!/usr/bin/env python3
"""Compare actual original/adapted mesh_bind_batch uniform streams on host."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from prepare_integrated_draw_material import prepare as material_prepare
from prepare_integrated_mesh_uniforms import prepare

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / 'third_party/sm64ds-3ds/port/platform/3ds'


def function(text):
    return text[text.index('static void mesh_bind_batch('):text.index('static bool mesh_prepare(')]


def check():
    out = ROOT / 'build/integrated-native/mesh-uniform-gate'
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp) / 'port/platform/3ds'
        base.mkdir(parents=True)
        for name in ('viewer_3ds.cpp', 'mesh_backend.inc'):
            shutil.copyfile(VENDOR / name, base / name)
        material_prepare(tmp)
        prepare(tmp)
        adapted = function((base / 'mesh_backend.inc').read_text())
        viewer = (base / 'viewer_3ds.cpp').read_text()
        assert 'UnifiedDrawScope(){mesh_uniform_cache.invalidate();' in viewer
        assert 'highlight_batch(i)){\n                    material_cache.invalidate();' in viewer
    original = function((VENDOR / 'mesh_backend.inc').read_text()).replace(
        'mesh_bind_batch(', 'reference_bind_batch(', 1)
    prefix = r'''
#include "mesh_uniform_cache.hpp"
#include <array>
#include <vector>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
struct Matrix {int32_t fixed[16]{};};
struct Run {unsigned matrix_count=0;std::array<unsigned,16> matrices{};};
struct Topology {std::vector<Run> runs;};
struct Frame {Topology* topology;std::vector<Matrix> matrices;std::vector<int> batch_env,envs;};
struct Source {std::vector<Matrix> data;const auto& matrices()const{return data;}} source;
struct Batch {int tw=0,th=0;};
struct Stats {uint64_t uniform_vectors=0;} mesh_stats;
Topology mesh_plan,topology;Frame storage{&topology,{}, {}, {}};
Frame* mesh_frame=nullptr;Source* mesh_source=&source;
std::vector<Batch> mesh_batches;
mesh_uniform::Cache mesh_uniform_cache;
bool unified=true,unified_draw_scope=true,enabled=true;
unsigned unified_palette_run=UINT_MAX,unified_palette_hits=0;
int mesh_attributes=0,mesh_env_loaded=-2,environment=-2;
constexpr int GPU_VERTEX_SHADER=0,mesh_u_objects=5,mesh_u_texdims=69;
std::array<std::array<float,4>,96> regs{};
uint64_t calls=0;
bool unified_shader_enabled(){return unified;}
bool mesh_uniform_reuse(){return enabled&&unified&&unified_draw_scope;}
void shader_bind(bool){} // Same program; mode and eye fields are disjoint.
void C3D_SetAttrInfo(int*){}
void C3D_FVUnifSet(int,int reg,float x,float y,float z,float w){assert(reg>=5&&reg<=69);regs[reg]={x,y,z,w};++calls;}
void mesh_upload_env(int e){environment=e;}
'''
    main = r'''
struct Snapshot {std::array<std::array<float,4>,96> data;int env;};
int main(){
 uint32_t random=123;
 auto next=[&](){random=random*1664525u+1013904223u;return random;};
 storage.matrices.resize(48);source.data.resize(48);
 for(auto& m:storage.matrices)for(auto& x:m.fixed)x=int(next()%0x40000000)-0x20000000;
 storage.matrices[1]=storage.matrices[0]; // distinct IDs, identical values
 topology.runs.resize(80);mesh_batches.resize(80);storage.batch_env.resize(80);storage.envs={17,23,47};
 for(unsigned i=0;i<80;++i){
  auto& r=topology.runs[i];r.matrix_count=1+(i%16);
  for(unsigned s=0;s<r.matrix_count;++s)r.matrices[s]=(s+i/16)%48;
  mesh_batches[i]={i%19==0?0:64,i%23==0?0:32};storage.batch_env[i]=int(i/13)%3;
 }
 // Limits, signed values and equal contents at distinct matrix IDs.
 storage.matrices[0].fixed[0]=INT32_MIN;storage.matrices[0].fixed[15]=INT32_MAX;
 storage.matrices[1]=storage.matrices[0];
 mesh_plan=topology;source.data=storage.matrices;
 uint64_t baseline=0,optimized=0,comparisons=0;
 for(int frame=0;frame<60;++frame){
  // Same owner address and IDs, changing pose across scopes.
  storage.matrices[frame%48].fixed[frame%16]^=12345;source.data=storage.matrices;
  for(bool owned:{false,true})for(bool one:{false,true})for(bool scope:{false,true})for(bool on:{false,true}){
   mesh_frame=owned?&storage:nullptr;unified=one;unified_draw_scope=scope;enabled=on;
   std::vector<Snapshot> expected;
   std::vector<unsigned> sequence;
   for(unsigned i=0;i<80;++i){sequence.push_back(i);sequence.push_back(i);}
   for(unsigned i=0;i<300;++i)sequence.push_back(next()%80);
   uint64_t count[2]{};
   for(unsigned pass=0;pass<2;++pass){
    regs={};calls=0;mesh_stats={};unified_palette_run=UINT_MAX;mesh_env_loaded=-2;environment=-2;
    mesh_uniform_cache.invalidate();
    for(unsigned j=0;j<sequence.size();++j){
     const auto i=sequence[j];
     if(pass==0)reference_bind_batch(i);else mesh_bind_batch(i);
     if(pass==0)expected.push_back({regs,environment});
     else {
      // Only live slots are consumed by the current run. Unused registers
      // retain the same history too, so compare the entire register file.
      assert(std::memcmp(regs.data(),expected[j].data.data(),sizeof(regs))==0);
      assert(environment==expected[j].env);++comparisons;
     }
    }
    assert(mesh_stats.uniform_vectors==calls);count[pass]=calls;
   }
   if(one&&scope&&on){baseline+=count[0];optimized+=count[1];assert(count[1]<count[0]);}
   else assert(count[0]==count[1]);
  }
 }
 assert(sizeof(mesh_uniform::Cache)<=1080);
 std::printf("uniform_trace_equivalent comparisons=%llu baseline_vectors=%llu optimized_vectors=%llu cache_bytes=%zu\n",
  (unsigned long long)comparisons,(unsigned long long)baseline,(unsigned long long)optimized,sizeof(mesh_uniform::Cache));
}
'''
    cpp = out / 'replay.cpp'
    cpp.write_text(prefix + original + adapted + main)
    exe = out / 'replay'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'source/integrated_native'), str(cpp), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    report = {'status': 'actual_mesh_bind_uniform_trace_equivalent', 'ASan': True, 'UBSan': True,
              'output': result.stdout, 'scope': 'Synthetic host replay of actual source functions; no physical timing or pixel claim.'}
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(result.stdout, end='')
    return report


if __name__ == '__main__':
    check()
