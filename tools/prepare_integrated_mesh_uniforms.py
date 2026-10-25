"""Adapt NP49's redundant uniform suppression to the existing DS palette ABI.

Generate into the integrated overlay; never modify the pinned vendor tree.
"""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text, old, new):
    assert text.count(old) == 1, 'Uniform integration anchor changed: ' + old[:80]
    return text.replace(old, new, 1)


def prepare(output):
    output = Path(output).resolve()
    if output.is_relative_to((ROOT / 'third_party').resolve()):
        raise ValueError('Pinned vendor tree is immutable')
    base = output / 'port/platform/3ds'
    viewer = base / 'viewer_3ds.cpp'
    text = viewer.read_text()
    text = replace_once(text, 'struct UnifiedDrawScope {', '''static mesh_uniform::Cache mesh_uniform_cache;
static bool mesh_uniform_reuse_enabled(){
 static const bool enabled=[](){const char* s=std::getenv("PLAYER_MESH_UNIFORM_REUSE");return !(s&&std::strcmp(s,"0")==0);}();
 return enabled;
}
static bool mesh_uniform_reuse(){return mesh_uniform_reuse_enabled()&&unified_shader_enabled()&&unified_draw_scope;}
struct UnifiedDrawScope {''')
    text = replace_once(text, 'UnifiedDrawScope(){unified_draw_scope=true;',
                        'UnifiedDrawScope(){mesh_uniform_cache.invalidate();unified_draw_scope=true;')
    text = replace_once(text, 'extern "C" void native_mesh_print_stats(){', '''extern "C" void native_mesh_print_stats(){
 std::printf("Mesh uniform reuse matrices=%llu hits=%llu dimensions=%llu hits=%llu bytes=%u enabled=%d\\n",
  (unsigned long long)mesh_uniform_cache.matrix_requests,(unsigned long long)mesh_uniform_cache.matrix_hits,
  (unsigned long long)mesh_uniform_cache.dimension_requests,(unsigned long long)mesh_uniform_cache.dimension_hits,
  unsigned(sizeof(mesh_uniform_cache)),mesh_uniform_reuse_enabled());''')
    # This pass changes TexEnv0 after material setup. A matching following
    # material must restore it, even though its texture/polygon key is equal.
    text = replace_once(text, 'if(gpugeo_draw && !partial && gpugeo::highlight_batch(i)){',
                        'if(gpugeo_draw && !partial && gpugeo::highlight_batch(i)){\n                    material_cache.invalidate();')
    viewer.write_text('#include "mesh_uniform_cache.hpp"\n' + text)
    mesh = base / 'mesh_backend.inc'
    text = mesh.read_text()
    text = replace_once(text,
        ' for(unsigned slot=0;slot<run.matrix_count;++slot){const auto& matrix=',
        ' const bool reuse_uniforms=mesh_uniform_reuse();\n if(!reuse_uniforms)mesh_uniform_cache.invalidate();\n for(unsigned slot=0;slot<run.matrix_count;++slot){const auto& matrix=')
    text = replace_once(text,
        '  for(unsigned j=0;j<4;++j)C3D_FVUnifSet(GPU_VERTEX_SHADER,mesh_u_objects+slot*4+j,',
        '  if(reuse_uniforms&&!mesh_uniform_cache.matrix(slot,matrix.fixed))continue;\n  mesh_stats.uniform_vectors+=4;\n  for(unsigned j=0;j<4;++j)C3D_FVUnifSet(GPU_VERTEX_SHADER,mesh_u_objects+slot*4+j,')
    text = replace_once(text,
        ' C3D_FVUnifSet(GPU_VERTEX_SHADER,mesh_u_texdims,b.tw?1.f/b.tw:0.f,b.th?1.f/b.th:0.f,0.f,0.f);\n mesh_stats.uniform_vectors+=run.matrix_count*4+1;',
        ''' if(!reuse_uniforms||mesh_uniform_cache.dimensions(b.tw,b.th)){
  C3D_FVUnifSet(GPU_VERTEX_SHADER,mesh_u_texdims,b.tw?1.f/b.tw:0.f,b.th?1.f/b.th:0.f,0.f,0.f);
  ++mesh_stats.uniform_vectors;
 }''')
    mesh.write_text(text)
    shutil.copyfile(ROOT / 'source/integrated_native/mesh_uniform_cache.hpp', base / 'mesh_uniform_cache.hpp')
    return ['port/platform/3ds/' + name for name in
            ('viewer_3ds.cpp', 'mesh_backend.inc', 'mesh_uniform_cache.hpp')]
