# Build integration

`IntegratedNative.cmake` exposes the integrated native, render60 and ROM-only
custom targets. These call the maintained Python candidate builder and
require private inputs; a normal host build does not invoke them.

`IntegratedHostTests.cmake` registers portable integration helpers alongside
the root CMake project's shared parser/runtime tests. Tests keep assertions
active; optional ASan/UBSan is host-only. Resource-backed GPU/ARM gates remain
in their owning runners and are not mislabeled as portable CTest cases.

`NativeRenderer.cmake` builds the native renderer's portable libraries and
registers its host tests (contracts, assets, lighting, batching, PICA state,
uniform residency, interpolation, producers and the test-mode script). GPU
laboratories are added only when their private catalog headers are passed
(`SM64DS_NATIVE_*_CATALOG_HEADER`); `SM64DS_NATIVE_RENDERER_LAB_ONLY=ON` builds
the laboratory alone. The game itself is built from the generated checkout
described in [the native pipeline](../tools/native_renderer/pipeline/README.md).

See [Building](../docs/BUILDING.md) and [Testing](../docs/TESTING.md).
