#!/usr/bin/env python3
"""Replay actual mesh input binding functions across geometry/shadow boundaries."""
import json,os,shutil,subprocess,tempfile
from pathlib import Path
from prepare_integrated_draw_material import prepare as material
from prepare_integrated_mesh_uniforms import prepare as uniforms
from prepare_integrated_vertex_inputs import prepare,TRACKING
ROOT=Path(__file__).resolve().parents[1];VENDOR=ROOT/'third_party/sm64ds-3ds/port/platform/3ds'
def buffers(s):return s[s.index('static void mesh_buffers()'):s.index('static void mesh_bind_batch(')]
def check():
 out=ROOT/'build/integrated-native/vertex-input-gate';out.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory() as tmp:
  b=Path(tmp)/'port/platform/3ds';b.mkdir(parents=True)
  for n in ('viewer_3ds.cpp','mesh_backend.inc','gx_gpu_backend.inc'):shutil.copyfile(VENDOR/n,b/n)
  material(tmp);uniforms(tmp);prepare(tmp)
  patched=(b/'mesh_backend.inc').read_text()
  binding=patched[patched.index(' shader_bind(true);'):patched.index(' if(unified_shader_enabled()',patched.index(' shader_bind(true);'))]
  original=buffers((VENDOR/'mesh_backend.inc').read_text()).replace('mesh_buffers()','reference_buffers()')
  optimized=buffers(patched)
 prefix=r'''
#include <array>
#include <vector>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
namespace native_mesh {struct Position{int16_t xyz[3];};struct Attribute{uint32_t c;float u,v;};}
struct C3D_AttrInfo{int identity;};
struct Entry {const void* ptr;size_t stride;int count;uint64_t permutation;bool operator==(const Entry& b)const{return ptr==b.ptr&&stride==b.stride&&count==b.count&&permutation==b.permutation;}};
struct C3D_BufInfo{std::vector<Entry> entries;};
C3D_BufInfo actual;int attr=-1;uint64_t buffer_calls=0,attr_calls=0;
C3D_AttrInfo mesh_attributes{1},cpu_attributes{2};
native_mesh::Position position[8];native_mesh::Attribute dynamic[8];
auto* mesh_positions=position;auto* mesh_dynamic=dynamic;
bool unified_draw_scope=false;
C3D_BufInfo* C3D_GetBufInfo(){++buffer_calls;return &actual;}
void C3D_SetAttrInfo(C3D_AttrInfo* p){++attr_calls;attr=p->identity;}
void BufInfo_Init(C3D_BufInfo* p){p->entries.clear();}
void BufInfo_Add(C3D_BufInfo* p,const void* v,size_t stride,int count,uint64_t permutation){p->entries.push_back({v,stride,count,permutation});}
void shader_bind(bool){}
'''
 # Original helper is compiled before the accessor macros, preserving the oracle.
 middle='''void reference_bind(){shader_bind(true);C3D_SetAttrInfo(&mesh_attributes);}
struct Snapshot{int attr;std::vector<Entry> buffers;bool operator==(const Snapshot& b)const{return attr==b.attr&&buffers==b.buffers;}};
'''
 main=r'''
std::vector<Snapshot> run(bool optimized){
 std::vector<Snapshot> result;uint32_t rng=123;
 actual={};attr=-1;attr_calls=buffer_calls=0;mesh_attr_bound=mesh_buffers_bound=false;
 for(unsigned frame=0;frame<120;++frame){
  // Storage can change outside a draw scope; never reuse across that boundary.
  mesh_positions=position+(frame%3);mesh_dynamic=dynamic+(frame%4);
  unified_draw_scope=frame%5!=0;mesh_attr_bound=mesh_buffers_bound=false;
  for(unsigned draw=0;draw<2000;++draw){
   rng=rng*1664525+1013904223;
   if(draw%17==0){ // shadow/CPU/other backend replaces both descriptors
    C3D_SetAttrInfo(&cpu_attributes);auto* b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,position+4,24,3,0x210);
    result.push_back({attr,actual.entries});
   }
   if(draw%29==0)C3D_SetAttrInfo(&cpu_attributes); // independent attribute dirtiness
   if(draw%31==0){auto* b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,dynamic+4,24,3,0x210);}
   if(optimized){optimized_bind();mesh_buffers();}else{reference_bind();reference_buffers();}
   result.push_back({attr,actual.entries});
   if(rng%7==0){ // repeated stencil/alpha pass with the same run
    if(optimized){optimized_bind();mesh_buffers();}else{reference_bind();reference_buffers();}
    result.push_back({attr,actual.entries});
   }
  }
  unified_draw_scope=false;mesh_attr_bound=mesh_buffers_bound=false;
 }
 return result;
}
int main(){auto a=run(false);const auto ba=attr_calls,bb=buffer_calls;auto b=run(true);assert(a==b);
 const char* off=std::getenv("PLAYER_MESH_INPUT_REUSE");
 if(off&&std::strcmp(off,"0")==0){assert(attr_calls==ba&&buffer_calls==bb);}else{assert(attr_calls<ba/2&&buffer_calls<bb/2);}
 std::printf("vertex_input_trace_equal draws=%zu attributes=%llu/%llu buffers=%llu/%llu\n",a.size(),(unsigned long long)attr_calls,(unsigned long long)ba,(unsigned long long)buffer_calls,(unsigned long long)bb);
}
'''
 cpp=out/'replay.cpp';cpp.write_text(prefix+original+middle+TRACKING+optimized+'\nvoid optimized_bind(){\n'+binding+'}\n'+main)
 exe=out/'replay';subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(exe)],check=True)
 outputs=[]
 for value in ('1','0'):
  r=subprocess.run([str(exe)],env={**os.environ,'PLAYER_MESH_INPUT_REUSE':value},check=True,text=True,capture_output=True);outputs.append(r.stdout);print(r.stdout,end='')
 result=dict(status='actual_binding_functions_equivalent',ASan=True,UBSan=True,output=outputs,scope='Descriptor values at each draw across scopes, pointer changes, independent mutations, CPU/shadow switches and disabled fallback; not physical performance')
 (out/'report.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':check()
