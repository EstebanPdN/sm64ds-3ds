"""Authored native GPU lighting cases with a wide-integer colour oracle."""
import argparse
import json
from pathlib import Path
from test_lighting_shader import cases, reference


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('header',type=Path)
    args=parser.parse_args()
    records=cases(128)
    # Add ordinary non-saturating diffuse/ambient/emission combinations to
    # the full-width, signed-wrap and shininess-table stress cases.
    for i in range(32):
        c=records[i]
        c['matrix']=[4096,0,0,0,4096,0,0,0,4096]
        c['normal']=[0,0,511]
        c['material']=[0x421,0x842,0 if i%2 else 0x421,i%4]
        c['lights']=[([0,0,511],[8+slot*4,4+slot*3,2+slot*2],256) for slot in range(4)]
        c['table']=bool(i%3==0)
        c['shininess']=[j*2 for j in range(128)]
    def array(values):return '{'+','.join(map(str,values))+'}'
    rows=[]
    for c in records:
        c['expected']=reference(c)
        lights=','.join('NativeLight{'+array(d)+','+array(rgb)+','+str(reciprocal)+'}' for d,rgb,reciprocal in c['lights'])
        state='LightSet{std::array<NativeLight,4>{'+lights+'},'+array(c['shininess'])+',{},0,15,true,false}'
        material=array([*c['material'],c['mask'],str(c['table']).lower()])
        rows.append('{'+array(c['normal'])+','+array(c['matrix'])+','+material+','+state+','+array(c['expected'])+'}')
    args.header.write_text('#pragma once\n#include "lighting_uniforms.hpp"\nnamespace native_lab_cases {\nusing namespace sm64ds::render;\n'
        'struct LightingCase {std::array<std::int16_t,3> normal;LightingState::Basis basis;LightingMaterial material;LightSet lights;std::array<std::uint8_t,3> expected;};\n'
        'inline constexpr LightingCase lighting_cases[]={\n'+',\n'.join(rows)+'\n};\n}\n')
    args.header.with_suffix('.json').write_text(json.dumps(dict(scope='Authored native signed-integer lighting cases',cases=records),indent=2)+'\n')


if __name__=='__main__':main()
