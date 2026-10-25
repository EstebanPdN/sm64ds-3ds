"""Import actual SM64 3DS-port function bodies with auditable ABI adaptations."""
from pathlib import Path
import re,hashlib,json,subprocess,os
ROOT=Path(__file__).resolve().parents[2]
# Explicit paths make clean checkouts independent of the owner's other ports.
# Retain the existing local reference as a compatibility fallback only.
_LEGACY_PORT=ROOT.parent/'MARIO KART 64/References/github/sm64-port-3ds'
_DEFAULT_PORT=ROOT/'build/dependencies/sm64-port-3ds'
PORT=Path(os.environ.get('SM64DS_MOTION_SOURCE',str(
 _DEFAULT_PORT if _DEFAULT_PORT.exists() or not _LEGACY_PORT.exists() else _LEGACY_PORT))).expanduser().resolve()
COMMIT='3f3c01eae67d6e9f3af89bc8073e37a5d153e00f'
FILES=['mario.c','mario_step.c','mario_actions_moving.c','mario_actions_airborne.c','mario_actions_stationary.c','mario_actions_automatic.c','mario_actions_object.c','mario_actions_submerged.c']
# These functions refer to the owning engine's data layout, startup, geometry
# query interface, camera or unported object/level systems. Preserve the explicit
# host-engine adapters rather than pretending their APIs are interchangeable.
BOUNDARY={
 'init_mario','init_mario_from_save_file','update_mario_geometry_inputs',
 'update_mario_info_for_cam','mario_get_terrain_sound_addend','debug_update_mario_cap',
 'execute_mario_action','set_mario_animation','set_mario_anim_with_accel','mario_update_walking_pitch','act_hold_jump','act_hold_walking','act_hold_idle','add_tree_leaf_particles','play_climbing_sounds','act_climbing_pole','act_holding_pole','act_water_shell_swimming','act_water_punch',
}
def functions(text):
 # Replace comments/strings with spaces while retaining source offsets.
 clean=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),text,flags=re.S)
 pattern=r'^(?:static\s+)?(?:UNUSED\s+)?(?:[A-Za-z_]\w*\s+)+(?:\*\s*)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{'
 result={}
 for m in re.finditer(pattern,clean,re.M):
  depth=1;i=m.end()
  while depth:
   depth+=(clean[i]=='{')-(clean[i]=='}');i+=1
  result[m[1]]=(m.start(),i,text[m.start():i])
 return result

def import_port(lib,output):
 # Reject drift in the actual owning source instead of labeling arbitrary
 # local edits with the pinned commit in the provenance receipt.
 actual=subprocess.check_output(['git','-C',str(PORT),'rev-parse','HEAD'],text=True).strip()
 if actual!=COMMIT:raise RuntimeError(f'3DS source pin changed: {actual}')
 for name in [*('src/game/'+f for f in FILES),'src/game/game_init.c','src/pc/controller/controller_3ds.c']:
  original=subprocess.check_output(['git','-C',str(PORT),'show',COMMIT+':'+name])
  if (PORT/name).read_bytes()!=original:raise RuntimeError('Modified 3DS reference source: '+name)
 rows=[];kept=[]
 for filename in FILES:
  source=PORT/'src/game'/filename;raw=source.read_text();orig=functions(raw)
  target=lib/'src/decomp/game'/filename;old=target.read_text();current=functions(old)
  changes=[]
  for name,(start,end,body) in current.items():
   if name not in orig or name in BOUNDARY:
    kept.append({'file':filename,'function':name,'reason':'engine interface or library-specific function'});continue
   original=orig[name][2]
   adapted=original.replace('INPUT_UNKNOWN_A41F','0xA41F').replace('func_803205E8','stop_sound').replace('struct Surface','struct SM64SurfaceCollisionData').replace('.unk38','.animInfo').replace('struct GraphNodeObject_sub','struct AnimInfo').replace('->unk08','->loopEnd').replace('struct WallCollisionData','struct SM64WallCollisionData').replace('INT_STATUS_MARIO_UNK4','INT_STATUS_HIT_BY_SHOCKWAVE')
   adapted=adapted.replace('m->area->terrainType','m->curTerrain').replace('gCurrAreaIndex','m->area->index').replace('gCurrAreaIndex','m->area->index').replace('sFloorAlignMatrix[m->unk00]','g_state->msFloorAlignMatrix')
   adapted=adapted.replace('find_water_level(nextPos[0], nextPos[2])','m->waterLevel')
   # The private reference source uses integer return from a decompilation-era
   # void helper in one expression; keep its exact source where it compiles.
   changes.append((start,end,adapted))
   rows.append({'source':str(source),'file':filename,'function':name,'line':raw.count('\n',0,orig[name][0])+1,'original_sha256':hashlib.sha256(original.encode()).hexdigest(),'adapted_sha256':hashlib.sha256(adapted.encode()).hexdigest()})
  for start,end,body in sorted(changes,reverse=True):old=old[:start]+body+old[end:]
  target.write_text(old)
 receipt={'repository':'https://github.com/mkst/sm64-port','commit':COMMIT,'imported_function_count':len(rows),'functions':rows,'engine_boundaries':kept,'status':'pinned source import; runtime validation is recorded separately'}
 (output/'port-source.json').write_text(json.dumps(receipt,indent=2)+'\n')
 return receipt
