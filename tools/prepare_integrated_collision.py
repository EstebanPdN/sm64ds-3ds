#!/usr/bin/env python3
"""Bounded raycast arithmetic adaptation in the generated port copy only."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
RELATIVE='port/unmatched/MeshCollider_DetectClsn_RaycastLine.cpp'
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Raycast source anchor changed: '+old[:70])
    return text.replace(old,new)
def derive(text):
    text=once(text,'#include "dBgW_Kc.h"','#include "dBgW_Kc.h"\n#include "raycast_arithmetic.hpp"\n#include <climits>')
    text=once(text,'s32 dotS, dotE, denom, t, dot, dist;','s64 dotS, dotE, denom, dot;\n                    s32 t=0, dist;')
    for variable,delta in [('dotS','d0'),('dotE','d1')]:
        old=f'{variable} = fnrm->x * {delta}.x + fnrm->y * {delta}.y + fnrm->z * {delta}.z;'
        new=f'{variable} = (s64)fnrm->x * {delta}.x + (s64)fnrm->y * {delta}.y + (s64)fnrm->z * {delta}.z;'
        text=once(text,old,new)
    text=once(text,'if (func_020397dc(denom))','if (denom <= 8)')
    text=once(text,'t = _ZN4cstd4fdivEii(dotS >> 4, denom) << 4;',
              'const bool wide=dotS>INT_MAX || dotE<INT_MIN || dotS-dotE>INT_MAX || safe_raycast::long_segment(s,e);\n                    if(!wide)t = _ZN4cstd4fdivEii((s32)(dotS >> 4), (s32)denom) << 4;')
    for axis in 'xyz':
        text=once(text,f'scaled.{axis} = (s32)(((s64)delta.{axis} * t) >> 16);',f'scaled.{axis} = safe_raycast::interpolate(delta.{axis},t,wide,dotS,dotE);')
    edge='enrm->x * rel.x + enrm->y * rel.y + enrm->z * rel.z'
    assert text.count(edge)==3
    text=text.replace(edge,'(s64)enrm->x * rel.x + (s64)enrm->y * rel.y + (s64)enrm->z * rel.z')
    text=once(text,'(*(s32 *)prism) + 0x20000','(s64)(*(s32 *)prism) + 0x20000')
    return text
def prepare(output):
    path=output/RELATIVE;path.write_text(derive(path.read_text()))
    header=output/'port/unmatched/raycast_arithmetic.hpp'
    header.write_bytes((ROOT/'source/integrated_native/movement/raycast_arithmetic.hpp').read_bytes())
    return [RELATIVE,'port/unmatched/raycast_arithmetic.hpp']
