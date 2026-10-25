"""Avoid redundant vertex descriptor commands inside one owned draw scope."""
from pathlib import Path
from prepare_integrated_mesh_uniforms import replace_once
ROOT=Path(__file__).resolve().parents[1]
TRACKING='''// Only draw_batches owns this cache. All descriptor writers in this TU and
// its GPU backend includes pass through the invalidating accessors below.
static bool mesh_attr_bound=false,mesh_buffers_bound=false;
static uint64_t mesh_attr_reuses=0,mesh_buffer_reuses=0;
static bool mesh_input_reuse(){
 static const bool enabled=[](){const char* s=std::getenv("PLAYER_MESH_INPUT_REUSE");return !(s&&std::strcmp(s,"0")==0);}();
 return enabled && unified_draw_scope;
}
static C3D_BufInfo* mesh_tracked_get_buf_info(){mesh_buffers_bound=false;return C3D_GetBufInfo();}
static void mesh_tracked_set_attr_info(C3D_AttrInfo* p){mesh_attr_bound=false;C3D_SetAttrInfo(p);}
#define C3D_GetBufInfo mesh_tracked_get_buf_info
#define C3D_SetAttrInfo mesh_tracked_set_attr_info
'''
def prepare(output):
 output=Path(output).resolve()
 if output.is_relative_to((ROOT/'third_party').resolve()):raise ValueError('Immutable vendor')
 base=output/'port/platform/3ds';p=base/'viewer_3ds.cpp';s=p.read_text()
 s=replace_once(s,'struct UnifiedDrawScope {',TRACKING+'struct UnifiedDrawScope {')
 s=replace_once(s,' UnifiedDrawScope(){',' UnifiedDrawScope(){mesh_attr_bound=mesh_buffers_bound=false;')
 s=replace_once(s,'~UnifiedDrawScope(){','~UnifiedDrawScope(){mesh_attr_bound=mesh_buffers_bound=false;')
 s=replace_once(s,'extern "C" void native_mesh_print_stats(){','extern "C" void native_mesh_print_stats(){\n std::printf("Mesh input reuse attributes=%llu buffers=%llu\\n",(unsigned long long)mesh_attr_reuses,(unsigned long long)mesh_buffer_reuses);')
 # All buffer mutations in the owned scope must acquire a dirty descriptor;
 # SetBufInfo/GetAttrInfo would bypass the two tracked entrypoints.
 for name in ('viewer_3ds.cpp','gx_gpu_backend.inc','mesh_backend.inc'):
  t=(base/name).read_text();assert 'C3D_SetBufInfo(' not in t and 'C3D_GetAttrInfo(' not in t
 p.write_text(s)
 p=base/'mesh_backend.inc';s=p.read_text()
 s=replace_once(s,'static void mesh_buffers(){auto* b=C3D_GetBufInfo();BufInfo_Init(b);','static void mesh_buffers(){\n if(mesh_input_reuse()&&mesh_buffers_bound){++mesh_buffer_reuses;return;}\n auto* b=C3D_GetBufInfo();BufInfo_Init(b);')
 s=replace_once(s,' BufInfo_Add(b,mesh_dynamic,sizeof(native_mesh::Attribute),2,0x21);',' BufInfo_Add(b,mesh_dynamic,sizeof(native_mesh::Attribute),2,0x21);\n mesh_buffers_bound=mesh_input_reuse();')
 s=replace_once(s,' shader_bind(true);C3D_SetAttrInfo(&mesh_attributes);',' shader_bind(true);\n if(mesh_input_reuse()&&mesh_attr_bound)++mesh_attr_reuses;\n else{C3D_SetAttrInfo(&mesh_attributes);mesh_attr_bound=mesh_input_reuse();}')
 p.write_text(s)
 return ['port/platform/3ds/viewer_3ds.cpp','port/platform/3ds/mesh_backend.inc']
