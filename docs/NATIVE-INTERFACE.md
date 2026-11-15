# Native interface consumer

The experimental native renderer now consumes the maintained port's Options,
pause, diagnostics, build badge and FPS image publications. This connects the
actual port UI; the original DS sound/backlight page is intercepted by
`sm64ds_front_options_begin()` and is not the front-menu Options implementation.
No complete original game scene has been accepted by the native renderer yet.

## Publication and ownership

`enhance_runtime` remains the single producer of two bounded ARGB images.
The native presentation thread acquires each publication even when an unrelated
source component refuses the base scene. A hidden acquisition retires its old
producer lease. A visible acquisition is acknowledged only after a synchronous
copy into renderer-owned texture storage succeeds. This prevents the initial
build-badge publication from blocking all subsequent Options updates.

`SceneRenderer::interface_image` accepts channel, nonzero revision and a bounded
`InterfaceImage`. Channel 0 is 320×240 on the lower LCD; channel 1 is 256×16 on
the upper LCD, at the maintained renderer's actual (140,26) position. Borrowed
pixels never survive the call. UI texture handles remain private to the render
owner; neither `SceneView` nor version-4 component captures contain those handles.
The independent source-interface diagnostic export is explicitly a CPU bitmap,
not a completed LCD capture or RAM/VRAM dump.

The upload fence precedes all texture mutations. Changed revisions scan alpha
once, choose the visible bounding rectangle and a minimum 8×8 power-of-two
texture, convert straight-alpha ARGB into native tiled ABGR, and zero padding.
Equal-sized textures reuse their allocation. Resizes release the old allocation
before reserving a successor under the backend's common budget; failure hides
that successor and allows a later retry. Empty images release their texture.
Unchanged revisions perform no scan, allocation, upload or additional GPU fence.
Hidden images retain a bounded cache until replacement/shutdown; eviction of
hidden interface caches remains a possible memory-pressure improvement.

Composition preserves existing ordered native geometry/images/canvases, then
adds each LCD's UI plane. Both layer counts are checked before drawing. When
there are no image or UI planes, the previous direct 3D composition path remains
allocation-free. The UI cache is destroyed with the scene renderer. GPU readiness
must not enable camera hitboxes merely because a refused scene uploaded a texture.

## Evidence and limits

The host crop/conversion test runs under ASan/UBSan and checks padded source
strides, transparent margins, exact channel/tile orientation, zero padding,
invalid spans, overlapping buffers and unsigned limits. The ARM/Citro3D UI lab
uses the maintained menu painter, tests reuse after hiding, empty publications,
budget refusal/recovery and shutdown, and exports completed targets with full
buffer checksums. Its checker compares every output pixel against the source UI
and explicitly authored backdrop. A privately captured live Options publication
can replace laboratory case 4 through `live-interface.argb`.

The BUILD 24 badge needs a 64×8 RGBA8 texture: **2,048 bytes**, versus the old
fixed 512×256 reservation of **524,288 bytes** (256 times less texture payload).
A full opaque 320×240 Options image still needs 524,288 bytes; the FPS plane can
need another 16,384 bytes. These are bounded UI texture figures, not total RAM,
VRAM or measured frame-rate gains. The two producer buffers and event-driven
CPU menu painter remain. Native glyph/primitive batches are further work.

The native diagnostic game journey proves that the real wrench opens Options
and that new UI publications are consumed. It deliberately continues refused
source frames only with `SM64DS_FRESH_NATIVE_DIAGNOSTIC_CONTINUE=1`; GPU submission
errors are not covered by that opt-in. Uploaded UI over a refused base scene
does not become a successful original scene or a performance result. Physical
Old/New 3DS performance, complete scenes, progression and visual acceptance
remain required.

Checksummed completed-target exports are the accepted diagnostic protocol.
Checksum-free exports reproduced the previously documented corrupted slivers;
their underlying cause remains unresolved. Checksummed exports passing does not
establish a hardware cache fix or repair the old renderer's capture timeout.
