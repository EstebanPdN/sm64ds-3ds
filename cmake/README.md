# Build integration

`IntegratedNative.cmake` exposes the integrated native, render60 and ROM-only
custom targets. These call the maintained Python candidate builder and
require private inputs; a normal host build does not invoke them.

`IntegratedHostTests.cmake` registers portable integration helpers alongside
the root CMake project's shared parser/runtime tests. Tests keep assertions
active; optional ASan/UBSan is host-only. Resource-backed GPU/ARM gates remain
in their owning runners and are not mislabeled as portable CTest cases.

See [Building](../docs/BUILDING.md) and [Testing](../docs/TESTING.md).
