#!/bin/zsh
# Rebuild an undelivered candidate game-pN under the same BUILD number.
# build_playable.sh refuses an existing output; this removes game-pN first and
# lowers the reserved counter by one so the number can be reused. Never use it
# for a number whose CIA was already handed out.
set -eu
ROOT=${0:A:h:h:h:h}
cd "$ROOT"
N=${1:?usage: rebuild_candidate.sh N}
[[ "$N" == <-> ]] || { print -u2 "Invalid build number"; exit 1; }
rm -rf build/native-renderer/playable-r1/game-p$N build/native-renderer/playable-r1/game-p$N.log
sed -i '' "s/^#define SM64DS_BUILD_NUMBER .*/#define SM64DS_BUILD_NUMBER $((N-1))/" source/integrated_native/options/build_version.hpp
python3 tools/native_renderer/pipeline/rehash_overlay.py >/dev/null
zsh build/native-renderer/playable-r1/build_playable.sh $N
