#!/bin/zsh
# Run one level of the SELECT+START test tour for game-pN in Azahar.
#
#   run_tour_level.sh TAG N LEVEL_INDEX STEPS "VAR=1 VAR2=2" [sd-switch.txt ...]
#
# STEPS limits the entrance situations (0 = the full script). The quoted
# variables go to env.txt for the first boot (.3dsx only; a CIA ignores it).
# Each extra argument creates an empty switch file in sdmc:/3ds/sm64ds/, for
# example sin-recortes.txt. MODEL=new selects the New 3DS profile and
# PLAYABLE_CIA=path installs that CIA instead of the .3dsx.
# Results: build/native-renderer/playable-r1/sessions/run-TAG-sd/prueba/01.
# verify114.py waits for any other Azahar instance and never closes it.
set -u
ROOT=${0:A:h:h:h:h}
cd "$ROOT"
TAG=${1:?usage: run_tour_level.sh TAG N LEVEL_INDEX STEPS [VARS] [switch files]}
N=${2:?}; LEVEL=${3:?}; STEPS=${4:?}; VARS=${5:-}
shift $(( $# < 5 ? $# : 5 ))
CARRY=build/native-renderer/playable-r1/sessions/run-$TAG-sd
rm -rf $CARRY; mkdir -p $CARRY/prueba/01
printf 'SM64DS_PRUEBA_V2\ncarpeta=1\nfase=pendiente\nmodo=recorrido\nindice=%s\nnivel=-1\narea=-1\npersonaje=0\nposicion=0\nx=0\ny=0\nz=0\nangulo=0\ncompletos=0\nincompletos=0\ncolgados=0\nfps_decimas=0\nfps_niveles=0\np95_peor_decimas=0\n' "$LEVEL" > $CARRY/prueba/estado.txt
for switch_file in "$@"; do print -n "1" > "$CARRY/$switch_file"; done
VERIFY_FIRST_BOOT_VARS="SM64DS_PRUEBA_ZONAS=1 $VARS" python3 build/native-renderer/playable-r1/verify114.py \
  tour ${MODEL:-old} "$LEVEL" 1 "$STEPS" run-$TAG "$N" 2>&1 | grep -v "live: reports"
print "RUN DONE $TAG"
