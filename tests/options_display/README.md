Display derived overlay verification
====================================

`tools/prepare_integrated_display.py:prepare(output)` runs on the generated
render60 source, never on the pinned vendor/decomp checkout. It owns no settings
parser or UI: it reads `enhance::live().screen_mode` and `.res3d` from the central
settings implementation.

Run host checks:

    clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror tests/options_display/test_plan.cpp -o /tmp/sm64ds-display-plan
    /tmp/sm64ds-display-plan

The 2,711 checks cover 4:3/5:3 viewport geometry, independent circle aspect,
additional horizontal clip coverage, all 400 LCD columns, DS sample mapping,
native distinct pixel counts, and menu/capture fallback. These are mathematical
and host bounds checks, not proof of console output or frame rate.

`compile_probe.py` compiles the changed compositor and viewer with the existing
ARM11/Citro3D build flags against a private source probe. The probe is created by
copying viewer_3ds.cpp, gpu_composite.{h,cpp} from the current generated overlay
then running `prepare(probe_path)`. It uses central options/enhance_settings.h.
Both objects compiled successfully; full link and scene tests belong to root.

Native means actual 320x240 (Original) or 400x240 (Wide/Stretch) upper geometry
and composition targets in 512x256 textures; it is not a final image upscale.
The original front/mask/OBJ input assets remain DS-resolution. The lower panel
and touch input retain 256x192 logical coordinates / 320x240 presentation.
Native mode allocates one physical geometry eye when stereo is off, and leaves
the unused right composed panel at DSsize. Moving the3Dslider above zero triggers
a drained two-eye rebuild. Native mono saves1,310,720 bytes of target storage
compared with allocating both high-resolution eyes unconditionally. Before
enteringNative, previousmenu sharp-filter slices are released after aGPUdrain,
recovering another786,432bytes instead of retaining them behind theNative
presentation bypass. Native mode uses individual eye targets; the original DS mode retains its
stacked stereo path. Target changes drain prior GPU work outside the active
frame before deleting resources. An allocation refusal uses DS targets and
suppresses allocation retry until the settings generation changes.

Intentional DS-resolution cases: menus, no upper 3D geometry, geometry on the
lower panel, DS capture/view/delayed geometry, window cuts, multipass 2D-alpha
scratch and OBJ alpha. These preserve the existing exact DS composition rules.
Native resolution therefore remains an experimental higher-cost mode with
explicit compatibility fallbacks, not a claim that every scene is high-res.

Wide expands clip X by 0.8. `mixed_clip_partition.h` and
`ds_gpu_clip_viewport.h` retain X/Y and clip only Z/W; therefore the resident
mesh path retains geometry beyond the original left/right screen edges.
The derived `display_culling.cpp` linker wrapper now widens only the original
Clipper rebuild input aspect by5/4 inWide, then restores its canonical field.
The original function still computes/normalizes its four planes. Near/farZ,
vertical FOV, actor clipdistance and simulationdistance values are untouched.
`test_culling.cpp` links the actual pinned original plane-rebuild source through
a minimal ARM32-layout shim, exercising both visible edges, rejection beyond
expanded edges,100 repeated rebuilds per mode without compounding, and unchanged
vertical/near/far rejection.329 checks pass underASan/UBSan. The test's vector
cross/normalize helpers are independent host arithmetic, not firmware math.
Nonresident/CPU geometry fallback uses its original projection and is not yet
an independently verified widescreen path. The DS HUD layout is preserved in
source coordinates but final wide/stretch output scales its horizontal pixels.

Native panel and geometry diagnostic readbacks now use real texture dimensions
and downsample to the established DS-sized reference output buffers, with a
512x256 readback scratch allocation. This avoids the former implicit 256-wide
transfer assumption. Full VM memory capture remains root's separate concern.

2026-10-16 root-cause evidence and telemetry
-----------------------------------------
The actual test log from attempt-1792135410058292000 repeatedly reported
"Native resolution unavailable: DS fallback" after requests to toggleNative.
Thus indistinct DS/Native was a confirmed allocation fallback, not a visual
comparison guess. Previously both native eyes/panels were allocated even inmono
and menu sharp slices were retained behind theNative bypass. Both waste cases
are corrected as described above.

The viewer now logs "Display actual" with requested resolution,framing,geometry
viewport,actualupperpanel,LCDdimensions,eyes,projectionX,andfallbackMask. Bits:
1menu,2noUpperGeometry,4capture/feedback,8lowerGeometry,16windowCut,
32multipassAlpha,64allocationRefusal. Allocation refusal also records the exact
stage label (geometry-color,geometry-depth,compositor,panel-color,bottom-target)
and available memory after cleanup. Logging occurs on state changes only.

After successful setup/fallback, the viewer stores actual_render_width,
actual_render_height,and native_fallback into the centralLive atomics for the
root-owned telemetry panel. The generator's changed-path manifest includes the
new culling source/header and port/CMakeLists.txt with the linkage wrapper.

ARM11 object checks passed for viewer_3ds.cpp,gpu_composite.cpp,and the new
culling wrapper. A root full link and a single controlled emulation run are
still needed to demonstrate Native actualdimensions with these allocation
changes, plus actor visibility at the wider edges. Nohardware acceptance is
implied by the host tests orARMobject compilation.

2026-10-21 probe correction
-------------------------
`compile_probe.py` now compiles the exact prepared sources, creates its output
directory and resolves the compiler through DEVKITPRO/DEVKITARM. It no longer
assumes a pre-populated private probe or hardcodes `/opt/devkitpro`. It requires
central CMake configuration and its shader headers; missing inputs fail before
compilation. Example:

    DEVKITPRO=/path/to/devkitpro DEVKITARM=/path/to/devkitpro/devkitARM python3 tests/options_display/compile_probe.py --source /path/to/prepared-overlay --output /tmp/sm64ds-display-objects

The preparers stage all affected sources and copied headers before writing,
revalidate destinations and reject source drift even with `python -O`.
`tests/options_menu/test_prepare_contracts.py` checks failure preservation and
compares two complete independent patch preparations. This provides no atomicity
guarantee for disk failures during the final writes.
