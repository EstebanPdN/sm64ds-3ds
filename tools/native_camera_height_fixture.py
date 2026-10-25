"""Generate an isolated exact original/adapted camera height-block ARM fixture.

Range fields are explicit block inputs, not emulated collision returns. Real
Lin/Gnd whole-body coverage belongs to test_native_camera_world's castle cases.
"""
import hashlib,json,subprocess
from pathlib import Path
import native_camera_body
ROOT=native_camera_body.ROOT

def build(out,flags):
    out=Path(out)
    original=(ROOT/'upstream/src/func_02009e70.cpp').read_text()
    assert hashlib.sha256(original.encode()).hexdigest()==native_camera_body.BODY_SHA
    candidate=native_camera_body.generate_text()
    start='    r7h = sb;\n    if (sb < r6) r7h = r6;'
    candidate_start='    if (manual_active) {\n        const s32 manual_floor = '
    end='    *(s16 *)(self + 0x17c) = Vec3_HorzAngle'
    blocks={'golden':original[original.index(start):original.index(end,original.index(start))],
            'candidate':candidate[candidate.index(candidate_start):candidate.index(end,candidate.index(candidate_start))]}
    mode=original[original.index('struct CamMode {'):original.index('\n\nextern s16 Vec3_HorzAngle')]
    head='''#include "types.h"
#include "dBgCh_Gnd.h"
#include "sm64ds/native_camera.hpp"
extern "C" {
'''+mode+'''
extern s32 func_020093d4(void*,s32);
extern void func_0200c9e0(void*,s32*,s32*);
extern s32 Math_Function_0203b14c(s32*,s32,s32,s32,s32);
extern CamMode data_0208715c,data_0208706c,data_02087094,data_020870e4,data_0208710c;
extern signed char data_0209f2f8;
}
#define FXMUL(a,b) ((s32)((((s64)(a))*(b)+0x800)>>12))
struct HeightInput {s32 sb,r6,sp04,sp28,ceiling,initial_floor,probe_y,vec34_y,vec40_y;
 s32 final_goal,final_floor,final_ceiling,manual,height_ok;};
'''
    text=head
    for name,block in blocks.items():
        text+='''extern "C" void native_camera_height_'''+name+'''(char*self,HeightInput*in) {
    s32 sb=in->sb,r6=in->r6,sp04=in->sp04,sp28=in->sp28,r4=in->ceiling,sp10=in->initial_floor;
    u32 r7h=0;s32 sp2c=0,sp30=0,t0=0;CamMode*sp1c=&data_0208715c;
    Vector3 sp34={},sp40={},sp4c={};sp34.y=in->vec34_y;sp40.y=in->vec40_y;sp4c.y=in->probe_y;
    bool manual_active='''+('true' if name=='candidate' else 'false')+''',manual_height_ok=false;
'''+block+'''
    in->final_goal=(s32)r7h;in->final_floor=sp10;in->final_ceiling=r4;
    in->manual=manual_active?1:0;in->height_ok=manual_height_ok?1:0;
}
'''
    source=out/'camera_height_block.cpp';source.write_text(text)
    obj=out/'camera_height_block.o'
    subprocess.run([native_camera_body.TOOL+'g++',*flags,'-std=gnu++17','-I'+str(ROOT/'include'),
                    '-c',str(source),'-o',str(obj)],cwd=ROOT,check=True)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report={'scope':'Exact extracted camera height blocks; block-input ranges are not collision results',
        'original_body_sha256':native_camera_body.BODY_SHA,'source_sha256':sha(source),'object_sha256':sha(obj),
        'original_block_sha256':hashlib.sha256(blocks['golden'].encode()).hexdigest(),
        'candidate_block_sha256':hashlib.sha256(blocks['candidate'].encode()).hexdigest(),
        'tool_sha256':sha(Path(__file__))}
    (out/'camera-height-block.json').write_text(json.dumps(report,indent=2)+'\n')
    return obj
