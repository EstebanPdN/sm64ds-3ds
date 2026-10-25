# Original projection retained by each native renderer batch

The first preserved scene-1 stream from
`build/native-runtime-azahar/attempt-1791596592223444000/scene-1-first-gx.bin`
contains 78 triangles using two original projection matrices. Six triangles
use the earlier matrix and 72 use the final matrix. Their 116 unique emitted
vertices form groups of 12 and 104. All three vertices of every triangle use
the same matrix. The six direct `VIEWPORT` writes are identical:
`0xbfff0000`, the full 256x192 viewport.

Previously `NativeFrame` read only the final decoder projection and
`GameRenderer` applied it to all view-space vertices. That moves 12 vertices
and six triangles; the largest coordinate difference corresponds to
71.027755 pixels per axis in the original DS viewport. This is host clip
arithmetic, not a GPU image comparison. For example, vertex 10 at DrawWord
947 has original NDC `(1.000074526, -1.333314480, 0.964855118)` but final-matrix
NDC `(0.445170190, -0.593490382, 0.964974419)`, changing clipping as well as size.

The decoder now copies the original matrix into `GxTriangle::projection[16]`
and sets `native_projection=true` when emitting a triangle. A separate
read-only replay audit confirms these fields match the matrix actually used
at each of the triangle's three vertices for all 78 triangles. No matrix
changes within those triangles were observed. This establishes the contract
for the preserved stream; it does not establish behavior for primitives that
change projection between vertices.

`include/sm64ds/game_projection.hpp` contains the pure logic used by the
production renderer:

- `game_batch_key` selects a triangle's original matrix when
  `GameProjection::native_matrix` is enabled, validates its 16 finite
  components and stores a copy with texture, wrap and translucency. Manual
  triangles with `native_projection=false` use the frame matrix. A failed
  selection leaves its destination unchanged.
- Equal batches require equal matrices as well as equal texture, wrap and
  translucency. Distinct finite float values are not merged using a tolerance.
- `game_projection_for_target` retains the existing DS row-vector to PICA
  column-vector conversion, sideways panel axes, clip-depth conversion and
  stereo offset. The upper panel preserves equal scale on both axes with
  320x240 framing within 400x240. Capture retains 256x192 and horizontal scale
  one. This change does not alter viewport handling.
- With `native_matrix=false`, original triangle matrices are ignored and the
  existing FOV/stereo projection remains in use.

`GameRenderer` owns the selected matrix in each batch and uploads that
batch's converted matrix immediately before its geometry draw. Both panel
and capture draws use this path. The real snapshot has five material batches
with either the previous or new key; this change corrects their matrices
without increasing their count. A fixture with identical materials and the
real 6+72 matrix sequence splits into two batches. The former final-matrix
control incorrectly merges that fixture into one.

Validation commands:

```sh
python3 tools/test_game_projection.py \
  --snapshot-dir build/native-runtime-azahar/attempt-1791596592223444000
python3 tools/audit_native_gx_projection.py \
  --snapshot-dir build/native-runtime-azahar/attempt-1791596592223444000
cmake --build build/3ds \
  --target sm64ds_game_renderer sm64ds-capture-check sm64ds-render-check \
  --parallel 4
```

The host regression passes ASan/UBSan with 9,284 assertions, 2,131 independent
target clip comparisons and 48 component/value cases for each of the
triangle and global fallback matrices (96 nonfinite rejections total). It
checks every vertex instance of the 78 real triangles against DS clip
arithmetic for both panels, capture and three stereo separations. The old
final-matrix control fails for exactly six real triangles. Fixtures also
verify matrix ownership, texture/wrap/translucency separation, equal axis
scale, orthographic capture fallback and the conventional projection path.

The audit compares intact and temporarily instrumented copies of the frozen
production decoder. Their explicitly serialized vertex, material and
projection fields match byte for byte. The audit records source and snapshot
hashes and reconstructs the decoder's stored coordinates to a maximum
difference of `1.50766e-5`, below its `1e-4` assertion threshold. Both decoder
replays accept the complete stream after the separate TEXGEN2 implementation.

ARM11 compilation passes for the renderer and the two existing GPU fixtures.
The zero-initialized manual capture triangles preserve their global-matrix
fallback; the recorded-player fixture preserves its conventional projection.
Runtime and model triangle copies use typed vectors. No full-struct triangle
file format was found in the owning sources. Raw GX snapshots remain pairs
of 32-bit words. Audit evidence uses explicit field serialization, including
the 16 floats and a one-byte flag, instead of host/ARM struct padding.

Results and complete hashes:

- `build/native-runtime/game-projection-test.json`
- `build/native-runtime/gx-projection-audit.json`
- `build/native-runtime/game-projection-implementation.json`

These checks are host and ARM11 compilation evidence. This block did not run
Azahar, compare new GPU pixels, test a physical console, start gameplay or
measure target frame rate. Those results remain separate from this regression.
