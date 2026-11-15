#!/bin/zsh
# Cross-compile single objects of the native-renderer checkout after refreshing
# it from the repository sources. Prints compiler diagnostics and OK/FAILED.
#
#   tools/native_renderer/pipeline/compile_objects.sh platform/3ds/native_renderer/scene_renderer ntr/ppu
#
# Each argument is an object path relative to the checkout's port/ tree,
# without extension. game_main has no single-object rule; build_playable.sh
# compiles it. Exit status: 0 when every object compiled.
set -u
ROOT=${0:A:h:h:h:h}
cd "$ROOT"
python3 tools/native_renderer/pipeline/rehash_overlay.py >/dev/null
python3 build/native-renderer/game-runtime-20261026-r1/refresh_native_overlay.py | tail -1
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
cd build/native-renderer/game-runtime-20261026-r1/source/build3ds/cmake
rc=0
for object in "$@"; do
  makefile=$(grep -l "/$object\.o:" CMakeFiles/*.dir/build.make | head -1)
  if [[ -z "$makefile" ]]; then print "NO RULE $object"; rc=1; continue; fi
  make -f "$makefile" "${makefile:h}/$object.o" 2>&1 | grep -E "error|warning|Error" | head -40
  if [[ ${pipestatus[1]} -eq 0 ]]; then print "OK $object"; else print "FAILED $object"; rc=1; fi
done
exit $rc
