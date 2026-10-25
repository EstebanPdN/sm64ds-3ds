# Third-party source

- `sm64ds-3ds/`: 768 files from the native backend revision recorded in
  `reference-native.lock.json`. They are tracked byte-for-byte; local
  transformations run on generated copies. Its nested decompilation,
  extraction and build outputs remain ignored.
- `libsm64-motion/`: the intact public libsm64 submodule, pinned by Git at
  `fd11813208272b4271d92bd92feb8f3fdbe61be5`. Initialize with a recursive
  clone or `git submodule update --init --recursive`.

Run `python3 tools/setup_dependencies.py` to prepare additional pinned public
source references. `--check` verifies without mutation. Full motion builds
also need local animation data; the helper never downloads it. See
[Building](../docs/BUILDING.md), [Upstream](../docs/UPSTREAM.md) and
[notices](../docs/legal/THIRD_PARTY_NOTICES.md).
