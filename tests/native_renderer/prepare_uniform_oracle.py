"""Compile the pinned upstream implementation as an independent host oracle."""
import hashlib
from pathlib import Path
import re
import sys

kind=sys.argv[2] if len(sys.argv)==3 else 'uniforms'
hashes={'uniforms':'d679e1ff7dcad9c9175cbe0860add73a9d947aac9472025e843f017e67e81830',
        'drawElements':'dd5cb551ec7dbc88d3ceb7ad88301f0198476225549ec2bb511fb61a24b4bcbe',
        # BUILD134 register deltas (tests/native_renderer/pica_state.cpp)
        'effect':'c32e5972aed2d7b99c542dae571f085e9dc046b7a0c3d2f12e01b64333a0ab65',
        'buffers':'3d16ca5c551f2ab913b6790b99a515ac1636182ba83e66aeeb1d123d3d5966dc'}
assert kind in hashes
source=Path(__file__).resolve().parents[2]/'third_party/citro3d-uniforms-1.7.1'/f'{kind}.c'
data=source.read_bytes()
assert hashlib.sha256(data).hexdigest()==hashes[kind]
symbols=('C3D_FVUnif','C3D_IVUnif','C3D_BoolUnifs','C3D_FVUnifDirty',
         'C3D_IVUnifDirty','C3D_BoolUnifsDirty','C3D_UpdateUniforms',
         'C3Di_DirtyUniforms','C3Di_LoadShaderUniforms','C3Di_ClearShaderUniforms','C3D_DrawElements',
         'C3D_DepthMap','C3D_CullFace','C3D_StencilTest','C3D_StencilOp','C3D_BlendingColor','C3D_EarlyDepthTest',
         'C3D_DepthTest','C3D_AlphaTest','C3D_AlphaBlend','C3D_ColorLogicOp','C3D_FragOpMode','C3D_FragOpShadow','C3Di_EffectBind',
         'BufInfo_Init','BufInfo_Add','C3D_GetBufInfo','C3D_SetBufInfo','C3Di_BufInfoBind')
text=data.decode().replace('#include "internal.h"','').replace('#include <c3d/uniforms.h>','')
text=re.sub(r'\b('+'|'.join(symbols)+r')\b',lambda m:'oracle_'+m[0],text)
Path(sys.argv[1]).write_text('#include "internal.h"\n'+text)
