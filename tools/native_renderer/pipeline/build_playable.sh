#!/bin/zsh
# Build one new numbered playable candidate; never replace an existing output.
set -euo pipefail
N=${1:?Usage: build_playable.sh BUILD_NUMBER}
[[ "$N" == <-> ]] || { print -u2 "Invalid build number"; exit 1; }
ROOT="${0:A:h:h:h:h}"
cd "$ROOT"
[[ ! -e "build/native-renderer/playable-r1/game-p$N" ]] || { print -u2 "Candidate already exists"; exit 1; }
python3 build/native-renderer/game-runtime-20261026-r1/refresh_native_overlay.py | tail -1
SRC=build/native-renderer/game-runtime-20261026-r1/source
python3 - "$N" "$SRC" <<'PY'
from pathlib import Path
import re,sys
version=Path('source/integrated_native/options/build_version.hpp')
text=version.read_text()
old=int(re.search(r'^#define SM64DS_BUILD_NUMBER (\d+)$',text,re.M).group(1))
number=int(sys.argv[1])
if number<=old:raise SystemExit(f'Build number must exceed reserved build {old}')
text=re.sub(r'^#define SM64DS_BUILD_NUMBER \d+$',f'#define SM64DS_BUILD_NUMBER {number}',text,flags=re.M)
version.write_text(text)
(Path(sys.argv[2])/'port/platform/3ds/build_version.hpp').write_text(text)
PY
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
cd $SRC
cmake --build build3ds/cmake --target smoke_player_3dsx -j8 > /tmp/sm64-p$N.log 2>&1 || { grep -E "error" /tmp/sm64-p$N.log | head; exit 1; }
python3 build3ds/build_game.py --output ../../playable-r1/game-p$N > ../../playable-r1/game-p$N.log 2>&1 || { grep -E "error|undefined" ../../playable-r1/game-p$N.log | head; exit 1; }
echo "built game-p$N (BUILD $N)"
