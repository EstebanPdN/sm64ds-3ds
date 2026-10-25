# Building from source

Use the integrated ROM-only target for the current game. The historical
viewer/runtime targets remain separate and are described in the dated
[development record](DEVELOPMENT.md) and [legacy packaging guide](PACKAGING.md).

## 1. Clone and run the portable checks

```sh
git clone --recurse-submodules https://github.com/EstebanPdN/sm64ds-3ds.git
cd sm64ds-3ds
python3 scripts/check-repository.py
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
```

Requirements: Git, Python 3.11+, CMake 3.20+ and a C++17 compiler. No ROM, SDK,
save, firmware or author's cache is needed for this subset. For Clang/GCC,
add `-DSM64DS_ENABLE_SANITIZERS=ON` to instrument the host build.

GitHub-generated ZIP downloads omit the motion-library submodule. A recursive
clone is recommended; an existing clone can run
`git submodule update --init --recursive`.

## 2. Prepare public source dependencies

```sh
python3 tools/setup_dependencies.py
python3 tools/setup_dependencies.py --check
```

This verifies the 768 vendored backend files, initializes the pinned libsm64
submodule, and prepares the fixed SM64DS decompilation and SM64 3DS movement
reference checkouts. It downloads public source only. Existing mismatched
revisions or tracked content edits cause a visible error instead of a reset.
Pins are explained in [Upstream](UPSTREAM.md).

For source/host contract tests alone, use
`python3 tools/setup_dependencies.py --backend-only`. This fetches the public
backend headers without preparing the movement reference or any game data.

The movement reference defaults to `build/dependencies/sm64-port-3ds/`.
An existing local reference can be selected explicitly:

```sh
export SM64DS_MOTION_SOURCE=/path/to/pinned/sm64-port-3ds
export SM64DS_MOTION_ANIMS=/path/to/local/assets/anims
```

The solver consumes **209 complete `anim_*.inc.c` animation definitions**
from the supported SM64 source layout. These are private game-derived build
inputs and are not supplied by this repository or the dependency helper.
Prepare them locally using your own compatible game input and the pinned
movement reference's extraction procedure. The source importer checks the
reference commit and the actual imported function bytes before adaptation.
Do not substitute empty animation tables. The former sibling MK64 reference
is accepted as a compatibility fallback on an existing development machine;
it is not required for a clean checkout.

## 3. Set up the SDK and Python environment

Use your installed devkitPro/devkitARM toolchain with libctru and Citro3D.
`makerom` and `bannertool` are needed for CIA packaging. Keep the installed
versions used for a candidate in its local receipt; do not upgrade the SDK
as part of source cleanup.

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
python3 -m venv build/python
build/python/bin/python -m pip install \
  ndspy==4.2.0 capstone==5.0.9 pyelftools==0.33 Pillow==12.3.0 numpy
```

Adjust the SDK path to your installation. Tools honor explicit SDK locations
and fail on invalid ones. The ARM compiler and SDK utilities are resolved by
`tools/devkitpro_tools.py`. Existing code uses POSIX facilities such as
`fcntl`; run the complete build on a supported macOS/Linux host. The desktop
Packer has its own Windows/macOS/Linux launchers and dependencies.

## 4. Supply private inputs and build

The integrated game accepts European ASMP revision 0:
16,777,216 bytes, SHA-1 `1367529f2cb23e76ef295cb1727333ae8f0a6cd7`.
Store your ROM outside tracked source, for example `build/roms/europe.nds`.

```sh
build/python/bin/python tools/build_integrated_native.py \
  --python build/python/bin/python \
  --rom /private/path/europe.nds --render60 --rom-only --jobs 4
```

This runs gates, prepares local catalogs/resources, compiles the backend,
checks ELF/3DSX correspondence and freezes a candidate under
`build/integrated-native/candidate-<hash>/`. `latest.json` identifies it;
`BUILD.json` records source evidence, hashes and validation. The builder
does not launch an emulator or publish anything. Rendering gates use
authored synthetic fixtures when the historical private captures are absent.

The build reserves an increasing number in
`source/integrated_native/options/build_version.hpp` before freezing inputs.
Failed or interrupted reservations leave gaps. Commit an intentional counter
change with the candidate's source; never change a closed candidate in place.

## 5. Package and inspect locally

Substitute the actual closed candidate path below:

```sh
build/python/bin/python tools/package_integrated_cia.py \
  --candidate build/integrated-native/candidate-<hash> \
  --output dist/sm64ds-local
```

Packaging verifies the candidate and CIA against its ELF and retains the
integrated Title ID `000400000F536400`. The output contains the executable,
instructions and notices; private ROMs, companion payloads, saves and dumps
must stay out. The package is local. There is no release workflow or binary
upload in this repository's CI.

For isolated Azahar acquisition and scenario prerequisites, use
`tools/run_integrated_native.py --help` and [Testing](TESTING.md). Never
describe a successful build or emulator run as physical-device validation.
