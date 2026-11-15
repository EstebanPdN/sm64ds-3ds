"""Hoist per-frame environment lookups out of the GX GPU geometry build.

gpugeo::build() runs once per presented GPU frame and read PLAYER_GPU_SHADOWS
and SM64DS_FORCE_3D through getenv() every time (wrapped: lock + hash + strcmp).
The integrated runtime only sets environment variables during boot (game_entry,
renderer_defaults, enhance_settings export_env); neither of these two names is
ever set, changed or removed at runtime, so a first-use cache returns exactly
the values every later per-frame lookup would have returned.

The index/run/batch vectors filled by build() are already persistent (static or
caller-owned globals) and std::vector::clear() keeps capacity, so they do not
reallocate in steady state; they are intentionally left unchanged.

Generate into the integrated overlay; never modify the pinned vendor tree.
"""
from pathlib import Path

from prepare_integrated_mesh_uniforms import replace_once

ROOT = Path(__file__).resolve().parents[1]

SHADOWS_OLD = '    if(std::getenv("PLAYER_GPU_SHADOWS"))return false; // shadow-plan passes rebind the CPU layout'
SHADOWS_NEW = ('    // Boot-time switch; never changed at runtime (prepare_integrated_gpu_geometry.py).\n'
               '    static const bool gpu_shadows=std::getenv("PLAYER_GPU_SHADOWS")!=nullptr;\n'
               '    if(gpu_shadows)return false; // shadow-plan passes rebind the CPU layout')
FORCE_OLD = '    if(const char* forced=std::getenv("SM64DS_FORCE_3D"))slider=float(std::atof(forced));'
FORCE_NEW = ('    static const char* const forced_3d=std::getenv("SM64DS_FORCE_3D");\n'
             '    static const float forced_3d_value=forced_3d?float(std::atof(forced_3d)):0.f;\n'
             '    if(forced_3d)slider=forced_3d_value;')


def patch(text):
    text = replace_once(text, SHADOWS_OLD, SHADOWS_NEW)
    text = replace_once(text, FORCE_OLD, FORCE_NEW)
    return text


def prepare(output):
    output = Path(output).resolve()
    if output.is_relative_to((ROOT / 'third_party').resolve()):
        raise ValueError('Pinned vendor tree is immutable')
    path = output / 'port/platform/3ds/gx_gpu_backend.inc'
    path.write_text(patch(path.read_text()))
    return ['port/platform/3ds/gx_gpu_backend.inc']
